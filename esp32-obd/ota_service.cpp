#include "ota_service.h"

#include <Update.h>
#include <mbedtls/ecdsa.h>

#include "ota_signing_key.h"

namespace obd {
namespace services {

namespace {
bool ParseHex(const String& hex, uint8_t* out, size_t len) {
  if (hex.length() != len * 2) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const char pair[3] = {hex.charAt(i * 2), hex.charAt(i * 2 + 1), 0};
    if (!isHexadecimalDigit(pair[0]) || !isHexadecimalDigit(pair[1])) {
      return false;
    }
    out[i] = static_cast<uint8_t>(strtoul(pair, nullptr, 16));
  }
  return true;
}
}  // namespace

OtaService::OtaService(app_link::TransportBle& app_link) : app_link_(app_link) {
  mbedtls_sha256_init(&sha_);
}

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

bool OtaService::SignatureValid(const uint8_t digest[32]) const {
  mbedtls_ecp_group group;
  mbedtls_ecp_point key;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&key);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);
  const bool ok =
      mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
      mbedtls_ecp_point_read_binary(&group, &key, config::kOtaPublicKey, sizeof(config::kOtaPublicKey)) == 0 &&
      mbedtls_mpi_read_binary(&r, signature_, 32) == 0 &&
      mbedtls_mpi_read_binary(&s, signature_ + 32, 32) == 0 &&
      mbedtls_ecdsa_verify(&group, digest, 32, &key, &r, &s) == 0;
  mbedtls_mpi_free(&s);
  mbedtls_mpi_free(&r);
  mbedtls_ecp_point_free(&key);
  mbedtls_ecp_group_free(&group);
  return ok;
}

void OtaService::Begin(size_t size, const String& md5, const String& signature_hex) {
  if (active_) {
    Update.abort();
    active_ = false;
  }
  received_ = 0;
  expected_ = size;
  failed_ = false;
  last_reported_pct_ = 0;
  if (size == 0) {
    Publish("error", "missing_size");
    return;
  }
  if (!ParseHex(signature_hex, signature_, sizeof(signature_))) {
    Publish("error", "signature_required");
    return;
  }
  if (!Update.begin(size, U_FLASH)) {
    Publish("error", "no_space");
    return;
  }
  if (md5.length() == 32) {
    Update.setMD5(md5.c_str());
  }
  mbedtls_sha256_free(&sha_);
  mbedtls_sha256_init(&sha_);
  mbedtls_sha256_starts(&sha_, /*is224=*/0);
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
  mbedtls_sha256_update(&sha_, data, len);
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
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha_, digest);
  if (!SignatureValid(digest)) {
    // Not signed with our key: the new image is never marked bootable.
    Abort("bad_signature");
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
