#include "telemetry_service.h"

namespace obd {
namespace services {

namespace {

// Walks a (possibly multi-PID) Mode 01 answer "41 p1 A.. p2 A.." and calls
// fn(pid, data, len) for every PID whose length is known.
template <typename Fn>
void ForEachPidInAnswer(const core::ObdMessage& msg, Fn fn) {
  uint8_t i = 1;
  while (i < msg.len) {
    const uint8_t pid = msg.data[i];
    const uint8_t len = core::PidDataLength(pid);
    if (len == 0 || i + 1 + len > msg.len) {
      return;
    }
    fn(pid, &msg.data[i + 1], len);
    i += 1 + len;
  }
}

}  // namespace

TelemetryService::TelemetryService(core::ObdTransport& transport,
                                   core::PidDecoder& pid_decoder,
                                   app_link::TransportBle& app_link)
    : transport_(transport), pid_decoder_(pid_decoder), app_link_(app_link) {
  core::BitSet256 defaults;
  core::DefaultTelemetryFields(defaults);
  SetTelemetryFields(defaults);
}

void TelemetryService::SetTelemetryFields(const core::BitSet256& fields) {
  fields_ = fields;
  wanted_pids_.Clear();
  for (size_t i = 0; i < core::kPidCatalogueLen; ++i) {
    if (fields_.Has(static_cast<uint8_t>(i))) {
      wanted_pids_.Set(core::kPidCatalogue[i].pid);
    }
  }
  Reset();
}

void TelemetryService::SetExtPidService(ExtPidService* ext_pids) { ext_pids_ = ext_pids; }

void TelemetryService::AppendExtField(void* context, const char* key, const String& value) {
  static_cast<TelemetryService*>(context)->AppendField(key, value);
}

void TelemetryService::Reset() {
  next_pid_ = 1;
  ext_rotation_done_ = false;
  accumulated_json_ = "";
  any_field_obtained_ = false;
  batching_enabled_ = true;
  empty_batches_ = 0;
}

bool TelemetryService::BatchComplete(const core::ObdTransport& transport, void* context) {
  const BatchContext& batch = *static_cast<const BatchContext*>(context);
  core::BitSet256 seen;
  for (uint8_t m = 0; m < transport.MessageCount(); ++m) {
    ForEachPidInAnswer(transport.Message(m),
                       [&](uint8_t pid, const uint8_t*, uint8_t) { seen.Set(pid); });
  }
  for (uint8_t i = 0; i < batch.count; ++i) {
    if (!seen.Has(batch.pids[i])) {
      return false;
    }
  }
  return true;
}

void TelemetryService::AppendField(const char* key, const String& value) {
  if (any_field_obtained_ &&
      accumulated_json_.length() + strlen(key) + value.length() > kMaxPayloadBody) {
    // Keep every notify inside the BLE MTU: send what we have and continue
    // the rotation in a new telemetry message.
    app_link_.NotifyTelemetry(accumulated_json_ + ",\"partial\":true,\"ts\":" + String(millis()) +
                              "}");
    any_field_obtained_ = false;
  }
  if (!any_field_obtained_) {
    accumulated_json_ = "{\"type\":\"telemetry\",\"status\":\"ok\"";
    any_field_obtained_ = true;
  }
  accumulated_json_ += ",\"";
  accumulated_json_ += key;
  accumulated_json_ += "\":";
  accumulated_json_ += value;
}

void TelemetryService::DecodePid(uint8_t pid, const uint8_t* data, uint8_t len) {
  for (size_t i = 0; i < core::kPidCatalogueLen; ++i) {
    const core::PidField& field = core::kPidCatalogue[i];
    if (field.pid != pid || !fields_.Has(static_cast<uint8_t>(i))) {
      continue;
    }
    const String value = pid_decoder_.FormatField(field, data, len);
    if (value.length() > 0) {
      AppendField(field.json_key, value);
    }
  }
}

void TelemetryService::PublishRotation() {
  String payload;
  if (any_field_obtained_) {
    payload = accumulated_json_;
  } else {
    // Nothing answered (or nothing requested is supported): do not spin out
    // degraded messages on every loop pass.
    if (millis() - last_degraded_ms_ < kDegradedIntervalMs) {
      return;
    }
    last_degraded_ms_ = millis();
    payload = "{\"type\":\"telemetry\",\"status\":\"degraded\"";
  }
  payload += ",\"ts\":";
  payload += String(millis());
  payload += "}";
  app_link_.NotifyTelemetry(payload);

  next_pid_ = 1;
  accumulated_json_ = "";
  any_field_obtained_ = false;
}

void TelemetryService::Tick() {
  // Collect the next PIDs of the rotation: several fixed-length PIDs share one
  // request, a PID of unknown length goes alone.
  uint8_t batch[kMaxPidsPerRequest];
  uint8_t count = 0;
  while (next_pid_ <= 0xFF && count < kMaxPidsPerRequest) {
    const uint8_t pid = static_cast<uint8_t>(next_pid_);
    if (!wanted_pids_.Has(pid) || !transport_.IsPidSupported(pid)) {
      ++next_pid_;
      continue;
    }
    const bool batchable = batching_enabled_ && core::PidDataLength(pid) > 0;
    if (!batchable && count > 0) {
      break;
    }
    batch[count++] = pid;
    ++next_pid_;
    if (!batchable) {
      break;
    }
  }

  if (count == 0) {
    // Standard PIDs done: one manufacturer request per pass, then publish.
    if (ext_pids_ != nullptr && ext_pids_->Count() > 0 && !ext_rotation_done_) {
      ext_rotation_done_ = ext_pids_->PollNext(AppendExtField, this);
      if (!ext_rotation_done_) {
        return;
      }
    }
    ext_rotation_done_ = false;
    PublishRotation();
    return;
  }

  uint8_t request[1 + kMaxPidsPerRequest];
  request[0] = 0x01;
  memcpy(&request[1], batch, count);

  if (count == 1) {
    if (transport_.Request(request, 2, /*wait_for_all=*/false) > 0) {
      const core::ObdMessage& msg = transport_.Message(0);
      if (msg.len > 2) {
        DecodePid(batch[0], &msg.data[2], msg.len - 2);
      }
    }
    return;
  }

  // Several ECUs may each answer part of the batch; wait until every PID was
  // seen or the response window ends.
  BatchContext context = {batch, count};
  const uint8_t answers =
      transport_.Request(request, 1 + count, /*wait_for_all=*/true, 0, BatchComplete, &context);
  if (answers == 0) {
    if (++empty_batches_ >= kMaxEmptyBatches) {
      // Some ECUs ignore multi-PID requests: ask one PID at a time from now on.
      batching_enabled_ = false;
      next_pid_ = batch[0];
    }
    return;
  }
  empty_batches_ = 0;
  core::BitSet256 decoded;
  for (uint8_t m = 0; m < answers; ++m) {
    ForEachPidInAnswer(transport_.Message(m), [&](uint8_t pid, const uint8_t* data, uint8_t len) {
      bool requested = false;
      for (uint8_t i = 0; i < count; ++i) {
        requested = requested || batch[i] == pid;
      }
      if (requested && !decoded.Has(pid)) {
        decoded.Set(pid);
        DecodePid(pid, data, len);
      }
    });
  }
}

}  // namespace services
}  // namespace obd
