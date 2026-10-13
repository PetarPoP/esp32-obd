#pragma once

#include <Arduino.h>

namespace obd {
namespace app_link {

// BLE link to the app over NimBLE: telemetry and response notifications, a
// command characteristic for JSON commands and a write-without-response
// characteristic that carries firmware image chunks during OTA.
class TransportBle {
 public:
  using CommandHandler = void (*)(const String& command, const String& raw_payload);
  using DataHandler = void (*)(const uint8_t* data, size_t len);

  bool Begin();
  void NotifyTelemetry(const String& json_payload);
  void NotifyResponse(const String& json_payload);
  bool IsConnected() const;
  void SetCommandHandler(CommandHandler handler);
  void SetOtaDataHandler(DataHandler handler);

  // Negotiated ATT MTU of the current connection (23 when unknown).
  uint16_t Mtu() const;
  // millis() of the last disconnect (0 if none yet).
  uint32_t LastDisconnectMs() const;

  // Stops advertising and the BLE stack before deep sleep.
  void Shutdown();
};

}  // namespace app_link
}  // namespace obd
