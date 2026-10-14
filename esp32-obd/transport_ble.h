#pragma once

#include <Arduino.h>

namespace obd {
namespace app_link {

// BLE link to the app over NimBLE: telemetry and response notifications, a
// command characteristic for JSON commands and a write-without-response
// characteristic that carries firmware image chunks during OTA.
//
// With kRequirePairing every characteristic needs an encrypted, MITM
// authenticated link: the phone pairs once with a 6-digit PIN (passkey entry,
// LE Secure Connections) and stays bonded. Nothing is notified to, or accepted
// from, a connection that has not finished pairing.
class TransportBle {
 public:
  using CommandHandler = void (*)(const String& command, const String& raw_payload);
  using DataHandler = void (*)(const uint8_t* data, size_t len);

  static constexpr bool kRequirePairing = true;
  // PIN used until the app sets another one with set_pin (kept in NVS).
  static constexpr uint32_t kDefaultPasskey = 123456;

  bool Begin();
  void NotifyTelemetry(const String& json_payload);
  void NotifyResponse(const String& json_payload);
  // True while a paired (or, without kRequirePairing, any) phone is connected.
  bool IsConnected() const;
  void SetCommandHandler(CommandHandler handler);
  void SetOtaDataHandler(DataHandler handler);

  // Negotiated ATT MTU of the current connection (23 when unknown).
  uint16_t Mtu() const;
  // millis() of the last disconnect (0 if none yet).
  uint32_t LastDisconnectMs() const;

  // Stores a new PIN (0..999999) for future pairings; bonded phones keep working.
  bool SetPasskey(uint32_t passkey);
  bool PasskeyIsDefault() const;
  // Forgets every bonded phone; each one has to pair again with the PIN.
  void ForgetBonds();
  int BondCount() const;

  // Stops advertising and the BLE stack before deep sleep.
  void Shutdown();
};

}  // namespace app_link
}  // namespace obd
