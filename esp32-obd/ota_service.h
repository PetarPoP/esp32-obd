#pragma once

#include <Arduino.h>
#include <mbedtls/sha256.h>

#include "transport_ble.h"

namespace obd {
namespace services {

// Firmware update over BLE:
//   1. app sends {"cmd":"ota_begin","size":<bytes>,"md5":"<hex>","sig":"<hex r||s>"}
//      -> "ota" status "ready"
//   2. app writes the image in chunks to the OTA data characteristic
//      (6f0a0005-…, write without response, up to MTU-3 bytes per write)
//   3. app sends {"cmd":"ota_end"} -> signature and MD5 checked, "ota" status
//      "done", reboot
// "sig" is an ECDSA P-256 signature of the image's SHA-256 made with the key
// whose public half is in ota_signing_key.h (tools/ota_sign.py). Images
// without a valid signature are rejected. Progress is reported as "ota"
// messages every 5 %. Any error aborts the update and the running firmware
// stays active.
class OtaService {
 public:
  explicit OtaService(app_link::TransportBle& app_link);

  void Begin(size_t size, const String& md5, const String& signature_hex);
  // Called from the BLE task with each chunk.
  void OnData(const uint8_t* data, size_t len);
  void End();
  void Abort(const char* reason);

  // Reports progress and performs the reboot after a successful update.
  void Tick();

  bool IsActive() const;

 private:
  void Publish(const char* status, const char* reason = nullptr);
  bool SignatureValid(const uint8_t digest[32]) const;

  app_link::TransportBle& app_link_;
  volatile bool active_ = false;
  volatile bool failed_ = false;
  volatile size_t received_ = 0;
  size_t expected_ = 0;
  uint8_t last_reported_pct_ = 0;
  uint32_t reboot_at_ms_ = 0;
  uint8_t signature_[64] = {};
  mbedtls_sha256_context sha_;
};

}  // namespace services
}  // namespace obd
