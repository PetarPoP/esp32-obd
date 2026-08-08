#include "smart_bms_service.h"

#include <math.h>

namespace obd {
namespace services {

namespace {

// CELLCOUNT comes from ED_BMSdiag _BMS_dfs.h.
constexpr uint8_t kCellCount = 93;

String FormatFloat(float v, uint8_t decimals) {
  char buf[24];
  if (decimals == 0) {
    snprintf(buf, sizeof(buf), "%ld", (long)v);
  } else if (decimals == 1) {
    snprintf(buf, sizeof(buf), "%.1f", v);
  } else {
    snprintf(buf, sizeof(buf), "%.2f", v);
  }
  return String(buf);
}

uint16_t ReadDiagWord(const uint8_t* data, size_t cap, size_t offset) {
  if (offset + 1 >= cap) {
    return 0;
  }
  return (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
}

}  // namespace

SmartBmsService::SmartBmsService(app_link::TransportBle& app_link, drivers::TwaiDriver& twai)
    : app_link_(app_link), twai_(twai) {}

void SmartBmsService::OnFrame(const drivers::CanFrame& frame) {
  if (frame.id == kBmsResponseId) {
    DispatchUdsFrame(frame);
    return;
  }
  DecodeBroadcast(frame);
}

void SmartBmsService::DecodeBroadcast(const drivers::CanFrame& frame) {
  if (frame.dlc < 1 || frame.dlc > 8) {
    return;
  }
  switch (frame.id) {
    case 0x518: {
      if (frame.dlc < 8) {
        return;
      }
      soc_pct_ = static_cast<float>(frame.data[7]) / 2.0f;
      have_soc_pct_ = true;
      any_fresh_since_last_emit_ = true;
      return;
    }
    case 0x2D5: {
      if (frame.dlc < 6) {
        return;
      }
      const uint16_t raw = (static_cast<uint16_t>(frame.data[4] & 0x03) << 8) | frame.data[5];
      soc_internal_pct_ = static_cast<float>(raw) / 10.0f;
      have_soc_internal_pct_ = true;
      any_fresh_since_last_emit_ = true;
      return;
    }
    case 0x508: {
      if (frame.dlc < 4) {
        return;
      }
      const int32_t raw = (static_cast<int32_t>(frame.data[2] & 0x3F) << 8) | frame.data[3];
      hv_amps_ = static_cast<float>(raw - 0x2000) / 10.0f;
      have_hv_amps_ = true;
      any_fresh_since_last_emit_ = true;
      return;
    }
    case 0x448: {
      if (frame.dlc < 8) {
        return;
      }
      const uint16_t raw = (static_cast<uint16_t>(frame.data[6]) << 8) | frame.data[7];
      hv_v_ = static_cast<float>(raw) / 10.0f;
      have_hv_v_ = true;
      any_fresh_since_last_emit_ = true;
      return;
    }
    case 0x3D5: {
      if (frame.dlc < 4) {
        return;
      }
      lv_v_ = static_cast<float>(frame.data[3]) / 10.0f;
      have_lv_v_ = true;
      any_fresh_since_last_emit_ = true;
      return;
    }
    case 0x412: {
      if (frame.dlc < 5) {
        return;
      }
      odo_km_ = static_cast<uint32_t>(frame.data[2]) * 65535u +
                static_cast<uint32_t>(frame.data[3]) * 256u +
                static_cast<uint32_t>(frame.data[4]);
      have_odo_km_ = true;
      any_fresh_since_last_emit_ = true;
      return;
    }
    case 0x512: {
      if (frame.dlc < 2) {
        return;
      }
      time_hh_ = frame.data[0];
      time_mm_ = frame.data[1];
      have_time_ = true;
      any_fresh_since_last_emit_ = true;
      return;
    }
    default:
      return;
  }
}

void SmartBmsService::DispatchUdsFrame(const drivers::CanFrame& frame) {
  if (frame.dlc == 0) {
    return;
  }
  const uint8_t pci = frame.data[0];
  if ((pci & 0xF0) == 0x00 && pci > 0) {
    HandleSingleFrame(frame);
    return;
  }
  if ((pci & 0xF0) == 0x10) {
    if (uds_state_ != UdsState::kAwaitingFirstFrame) {
      // Out-of-band FF (e.g., we already moved on). Ignore.
      return;
    }
    HandleFirstFrame(frame);
    return;
  }
  if ((pci & 0xF0) == 0x20) {
    if (uds_state_ != UdsState::kAwaitingConsecutiveFrames) {
      return;
    }
    HandleConsecutiveFrame(frame);
    return;
  }
  // Flow-control (0x30) or NRC are not expected on the response side.
}

void SmartBmsService::HandleSingleFrame(const drivers::CanFrame& frame) {
  if (uds_state_ != UdsState::kAwaitingFirstFrame) {
    return;
  }
  if (frame.dlc < 2) {
    AbortUdsExchange();
    return;
  }
  // Negative response (0x7F): bail and rotate to next query.
  if (frame.data[1] == 0x7F) {
    AbortUdsExchange();
    return;
  }
  // Mirror ED_BMSdiag layout: data[i] = rxBuf[i+1] starting at i=0.
  const uint8_t to_copy = frame.dlc - 1;
  for (uint8_t i = 0; i < to_copy && i < kUdsBufferLen; ++i) {
    uds_buffer_[i] = frame.data[1 + i];
  }
  uds_write_pos_ = to_copy;
  uds_items_remaining_ = 0;
  FinishUdsExchange();
}

void SmartBmsService::HandleFirstFrame(const drivers::CanFrame& frame) {
  if (frame.dlc < 2) {
    AbortUdsExchange();
    return;
  }
  const uint16_t total_len = (static_cast<uint16_t>(frame.data[0] & 0x0F) << 8) | frame.data[1];
  // ED_BMSdiag: items := total_len; six ISO-TP payload bytes are conveyed in
  // this FF, then CFs follow. Tracked as `items - 6` decreasing by (len-1)
  // for each CF.
  uds_items_remaining_ = static_cast<int32_t>(total_len) - 6;
  // ED_BMSdiag copies data[i] = rxBuf[i+1] for i=0..len-1; with FF dlc=8 that
  // writes 7 bytes (data[0]=length_lo, data[1]=0x62, data[2..3]=DID, data[4..6]=first three payload bytes).
  const uint8_t to_copy = frame.dlc - 1;
  for (uint8_t i = 0; i < to_copy && i < kUdsBufferLen; ++i) {
    uds_buffer_[i] = frame.data[1 + i];
  }
  if (!SendFlowControl()) {
    AbortUdsExchange();
    return;
  }
  // Mirror ED_BMSdiag write pointer init.
  uds_write_pos_ = 7;
  uds_state_ = UdsState::kAwaitingConsecutiveFrames;
}

void SmartBmsService::HandleConsecutiveFrame(const drivers::CanFrame& frame) {
  // ED_BMSdiag: items := items - len + 1; for i in 0..len-1 if (n<DATALENGTH-6 && i<7) data[n+i] = rxBuf[i+1].
  const uint8_t cf_payload_len = frame.dlc > 1 ? (frame.dlc - 1) : 0;
  for (uint8_t i = 0; i < cf_payload_len && i < 7; ++i) {
    const size_t pos = uds_write_pos_ + i;
    if (pos < kUdsBufferLen) {
      uds_buffer_[pos] = frame.data[1 + i];
    }
  }
  uds_items_remaining_ -= cf_payload_len;
  uds_write_pos_ += 7;
  if (uds_items_remaining_ <= 0 || uds_write_pos_ >= kUdsBufferLen) {
    FinishUdsExchange();
  }
}

bool SmartBmsService::SendUdsRequest(uint16_t did) {
  uint8_t req[8] = {
      0x03,
      0x22,
      static_cast<uint8_t>((did >> 8) & 0xFF),
      static_cast<uint8_t>(did & 0xFF),
      0xFF,
      0xFF,
      0xFF,
      0xFF,
  };
  return twai_.SendFrame(kBmsRequestId, req, sizeof(req));
}

bool SmartBmsService::SendFlowControl() {
  // FC: CTS, BS=0 (no further FC needed), ST=20ms.
  uint8_t fc[8] = {0x30, 0x00, 0x14, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return twai_.SendFrame(kBmsRequestId, fc, sizeof(fc));
}

void SmartBmsService::MaybeStartNextUdsQuery(uint32_t now_ms) {
  if (!active_mode_) {
    return;
  }
  if (uds_state_ != UdsState::kIdle) {
    if ((now_ms - uds_started_at_ms_) > kUdsResponseTimeoutMs) {
      AbortUdsExchange();
    }
    return;
  }
  if ((now_ms - uds_last_rotation_ms_) < kUdsRotationIntervalMs) {
    return;
  }
  const UdsQuery q = static_cast<UdsQuery>(uds_rotation_idx_ % static_cast<uint8_t>(UdsQuery::kCount));
  uds_active_query_ = q;
  uds_rotation_idx_ = (uds_rotation_idx_ + 1) % static_cast<uint8_t>(UdsQuery::kCount);
  uds_last_rotation_ms_ = now_ms;
  uds_started_at_ms_ = now_ms;
  uds_write_pos_ = 0;
  uds_items_remaining_ = 0;
  uds_state_ = UdsState::kAwaitingFirstFrame;
  uint16_t did = 0;
  switch (q) {
    case UdsQuery::kCapacity:
      did = 0x0310;
      break;
    case UdsQuery::kTemperatures:
      did = 0x0201;
      break;
    case UdsQuery::kCellVoltages:
      did = 0x0208;
      break;
    default:
      did = 0;
      break;
  }
  if (did == 0 || !SendUdsRequest(did)) {
    AbortUdsExchange();
  }
}

void SmartBmsService::FinishUdsExchange() {
  switch (uds_active_query_) {
    case UdsQuery::kCapacity:
      DecodeCapacityResponse();
      break;
    case UdsQuery::kTemperatures:
      DecodeTemperaturesResponse();
      break;
    case UdsQuery::kCellVoltages:
      DecodeCellVoltagesResponse();
      break;
    default:
      break;
  }
  uds_state_ = UdsState::kIdle;
}

void SmartBmsService::AbortUdsExchange() {
  uds_state_ = UdsState::kIdle;
}

void SmartBmsService::DecodeCapacityResponse() {
  // ED_BMSdiag getBatteryCapacity():
  //   data[14]            = SOH (byte, %)
  //   ReadDiagWord(data,17,1) = Cap_As.max
  //   ReadDiagWord(data,21,1) = Cap_As.min
  //   ReadDiagWord(data,23,1) = Cap_As.mean
  if (uds_write_pos_ < 25) {
    return;
  }
  soh_pct_ = static_cast<float>(uds_buffer_[14]);
  have_soh_pct_ = true;
  cap_as_max_ = ReadDiagWord(uds_buffer_, kUdsBufferLen, 17);
  cap_as_min_ = ReadDiagWord(uds_buffer_, kUdsBufferLen, 21);
  cap_as_mean_ = ReadDiagWord(uds_buffer_, kUdsBufferLen, 23);
  have_cap_as_ = true;
  any_fresh_since_last_emit_ = true;
}

void SmartBmsService::DecodeTemperaturesResponse() {
  // ED_BMSdiag getBatteryTemperature() / ReadBatteryTemperatures(data, 4, 7):
  //   for n in 0..6: Temps[n] = data[4+2n]*256 + data[4+2n+1] (signed) / 64 → °C
  if (uds_write_pos_ < 4 + 2 * 7) {
    return;
  }
  float t_sum = 0.0f;
  float t_min = 0.0f;
  float t_max = 0.0f;
  uint8_t valid_count = 0;
  for (uint8_t i = 0; i < 7; ++i) {
    const size_t off = 4 + 2 * i;
    const uint16_t raw_u = (static_cast<uint16_t>(uds_buffer_[off]) << 8) | uds_buffer_[off + 1];
    const int16_t raw = static_cast<int16_t>(raw_u);
    const float t_c = static_cast<float>(raw) / 64.0f;
    // Sanity range: reject clearly out-of-spec sensors so they don't poison min/max.
    if (t_c < -50.0f || t_c > 150.0f) {
      continue;
    }
    if (valid_count == 0) {
      t_min = t_max = t_c;
    } else {
      if (t_c < t_min) t_min = t_c;
      if (t_c > t_max) t_max = t_c;
    }
    t_sum += t_c;
    valid_count++;
  }
  if (valid_count == 0) {
    return;
  }
  temp_min_c_ = t_min;
  temp_max_c_ = t_max;
  temp_mean_c_ = t_sum / static_cast<float>(valid_count);
  have_temps_ = true;
  any_fresh_since_last_emit_ = true;
}

void SmartBmsService::DecodeCellVoltagesResponse() {
  // ED_BMSdiag getBatteryVoltage() / ReadCellVoltage(data, 4, CELLCOUNT):
  //   for n in 0..CELLCOUNT-1: Cell[n] = data[4+2n]*256 + data[4+2n+1] (mV raw)
  const size_t need = 4 + 2 * static_cast<size_t>(kCellCount);
  if (uds_write_pos_ < need) {
    return;
  }
  uint32_t sum = 0;
  uint32_t sum_sq = 0;
  uint16_t v_min = 0xFFFF;
  uint16_t v_max = 0;
  uint8_t valid_count = 0;
  for (uint8_t i = 0; i < kCellCount; ++i) {
    const size_t off = 4 + 2 * static_cast<size_t>(i);
    const uint16_t mv =
        (static_cast<uint16_t>(uds_buffer_[off]) << 8) | uds_buffer_[off + 1];
    // Reject impossible values (typical Smart ED cell: ~2500–4200 mV).
    if (mv < 1000 || mv > 5000) {
      continue;
    }
    if (mv < v_min) v_min = mv;
    if (mv > v_max) v_max = mv;
    sum += mv;
    sum_sq += static_cast<uint32_t>(mv) * mv;
    valid_count++;
  }
  if (valid_count == 0) {
    return;
  }
  const float mean = static_cast<float>(sum) / static_cast<float>(valid_count);
  const float mean_sq = static_cast<float>(sum_sq) / static_cast<float>(valid_count);
  const float variance = mean_sq - mean * mean;
  cell_mv_min_ = v_min;
  cell_mv_max_ = v_max;
  cell_mv_mean_ = static_cast<uint16_t>(mean + 0.5f);
  cell_mv_stddev_ = variance > 0.0f ? sqrtf(variance) : 0.0f;
  cell_count_ = valid_count;
  have_cells_ = true;
  any_fresh_since_last_emit_ = true;
}

void SmartBmsService::SetActiveMode(bool active) {
  active_mode_ = active;
  if (!active) {
    AbortUdsExchange();
  }
}

void SmartBmsService::Tick(uint32_t now_ms) {
  MaybeStartNextUdsQuery(now_ms);
  MaybePublish(now_ms);
}

void SmartBmsService::MaybePublish(uint32_t now_ms) {
  if ((now_ms - last_emit_ms_) < kEmitIntervalMs) {
    return;
  }
  last_emit_ms_ = now_ms;

  String payload;
  // We emit even when no broadcast was decoded this window so the app sees a
  // degraded status (helps diagnose "bus is silent" situations).
  payload = any_fresh_since_last_emit_
                ? String("{\"type\":\"telemetry\",\"status\":\"ok\"")
                : String("{\"type\":\"telemetry\",\"status\":\"degraded\"");

  if (have_soc_pct_) {
    payload += ",\"soc_pct\":";
    payload += FormatFloat(soc_pct_, 1);
  }
  if (have_soc_internal_pct_) {
    payload += ",\"soc_internal_pct\":";
    payload += FormatFloat(soc_internal_pct_, 1);
  }
  if (have_hv_v_) {
    payload += ",\"hv_v\":";
    payload += FormatFloat(hv_v_, 1);
  }
  if (have_hv_amps_) {
    payload += ",\"hv_amps\":";
    payload += FormatFloat(hv_amps_, 1);
  }
  if (have_lv_v_) {
    payload += ",\"lv_v\":";
    payload += FormatFloat(lv_v_, 1);
  }
  if (have_odo_km_) {
    payload += ",\"odo_km\":";
    payload += String(odo_km_);
  }
  if (have_time_) {
    payload += ",\"time_hh\":";
    payload += String(time_hh_);
    payload += ",\"time_mm\":";
    payload += String(time_mm_);
  }
  if (have_soh_pct_) {
    payload += ",\"soh_pct\":";
    payload += FormatFloat(soh_pct_, 0);
  }
  if (have_cap_as_) {
    // Convert As → Ah (×1/3600) for friendlier units.
    payload += ",\"cap_ah_min\":";
    payload += FormatFloat(static_cast<float>(cap_as_min_) / 3600.0f, 2);
    payload += ",\"cap_ah_mean\":";
    payload += FormatFloat(static_cast<float>(cap_as_mean_) / 3600.0f, 2);
    payload += ",\"cap_ah_max\":";
    payload += FormatFloat(static_cast<float>(cap_as_max_) / 3600.0f, 2);
  }
  if (have_temps_) {
    payload += ",\"temp_min_c\":";
    payload += FormatFloat(temp_min_c_, 1);
    payload += ",\"temp_mean_c\":";
    payload += FormatFloat(temp_mean_c_, 1);
    payload += ",\"temp_max_c\":";
    payload += FormatFloat(temp_max_c_, 1);
  }
  if (have_cells_) {
    payload += ",\"cell_mv_min\":";
    payload += String(cell_mv_min_);
    payload += ",\"cell_mv_mean\":";
    payload += String(cell_mv_mean_);
    payload += ",\"cell_mv_max\":";
    payload += String(cell_mv_max_);
    payload += ",\"cell_mv_stddev\":";
    payload += FormatFloat(cell_mv_stddev_, 1);
    payload += ",\"cell_count\":";
    payload += String(cell_count_);
  }
  payload += ",\"ts\":";
  payload += String(now_ms);
  payload += "}";
  app_link_.NotifyTelemetry(payload);
  any_fresh_since_last_emit_ = false;
}

void SmartBmsService::Reset() {
  have_soc_pct_ = false;
  have_soc_internal_pct_ = false;
  have_hv_v_ = false;
  have_hv_amps_ = false;
  have_lv_v_ = false;
  have_odo_km_ = false;
  have_time_ = false;
  have_soh_pct_ = false;
  have_cap_as_ = false;
  have_temps_ = false;
  have_cells_ = false;
  any_fresh_since_last_emit_ = false;
  last_emit_ms_ = 0;
  uds_state_ = UdsState::kIdle;
  uds_write_pos_ = 0;
  uds_items_remaining_ = 0;
  uds_started_at_ms_ = 0;
  uds_last_rotation_ms_ = 0;
  uds_rotation_idx_ = 0;
}

}  // namespace services
}  // namespace obd
