#include "ota_service.h"

#include <Update.h>

namespace obd {
namespace services {

OtaService::OtaService(app_link::TransportBle& app_link) : app_link_(app_link) {}

bool OtaService::IsActive() const { return active_; }

void OtaService::Publish(const char* status, const char* reason) {
  String payload = "{\"type\":\"ota\",\"status\":\"";
  payload += status;
  payload += "\",\"received\":";
  payload += String(static_cast<uint32_t>(received_));
  payload += ",\"size\":";
  payload += String(static_cast<uint32_t>(expected_));
  if (reason != nullptr) {
    payload += ",\"reason\":\"";
    payload += reason;
    payload += "\"";
  }
  payload += ",\"mtu\":";
  payload += String(app_link_.Mtu());
  payload += ",\"ts\":";
  payload += String(millis());
  payload += "}";
  app_link_.NotifyResponse(payload);
}

void OtaService::Begin(size_t size, const String& md5) {
  if (active_) {
    Update.abort();
  }
  received_ = 0;
  expected_ = size;
  failed_ = false;
  last_reported_pct_ = 0;
  if (size == 0) {
    Publish("error", "missing_size");
    return;
  }
  if (!Update.begin(size, U_FLASH)) {
    Publish("error", "no_space");
    return;
  }
  if (md5.length() == 32) {
    Update.setMD5(md5.c_str());
  }
  active_ = true;
  Publish("ready");
}

void OtaService::OnData(const uint8_t* data, size_t len) {
  if (!active_ || failed_ || data == nullptr || len == 0) {
    return;
  }
  if (received_ + len > expected_ || Update.write(const_cast<uint8_t*>(data), len) != len) {
    failed_ = true;
    return;
  }
  received_ += len;
}

void OtaService::End() {
  if (!active_) {
    Publish("error", "not_started");
    return;
  }
  if (failed_ || received_ != expected_) {
    Abort(failed_ ? "write_failed" : "size_mismatch");
    return;
  }
  active_ = false;
  if (!Update.end(/*evenIfRemaining=*/false)) {
    Publish("error", Update.errorString());
    return;
  }
  Publish("done");
  reboot_at_ms_ = millis() + 1000;  // let the notification go out first
}

void OtaService::Abort(const char* reason) {
  if (active_) {
    Update.abort();
  }
  active_ = false;
  failed_ = false;
  Publish("aborted", reason);
}

void OtaService::Tick() {
  if (reboot_at_ms_ != 0 && (int32_t)(millis() - reboot_at_ms_) >= 0) {
    ESP.restart();
  }
  if (!active_) {
    return;
  }
  if (failed_) {
    Abort("write_failed");
    return;
  }
  const uint8_t pct = expected_ > 0 ? static_cast<uint8_t>((received_ * 100) / expected_) : 0;
  if (pct >= last_reported_pct_ + 5) {
    last_reported_pct_ = pct - (pct % 5);
    Publish("progress");
  }
}

}  // namespace services
}  // namespace obd
