#pragma once

#include <Arduino.h>

namespace obd {
namespace drivers {

// Byte-level K-line (ISO 9141-2 / ISO 14230-2) driver on a UART at 10400 baud.
// Handles the two wake-up procedures, inter-byte timing on transmit, echo
// removal and gap-based message framing on receive. Protocol headers and
// checksums are built by the caller (ObdTransport).
class KLineDriver {
 public:
  static constexpr uint32_t kBaud = 10400;

  // True when a K-line transceiver is configured in pin_config.h.
  static bool IsAvailable();

  // ISO 9141-2 / KWP2000 slow init: sends `address` at 5 baud and completes
  // the key-byte handshake. Takes about 2.5 s. On success returns the two key
  // bytes (ISO 9141-2: 08 08 or 94 94, KWP2000: 8F xx).
  bool SlowInit(uint8_t address, uint8_t& out_kb1, uint8_t& out_kb2);

  // KWP2000 fast init: 25 ms low / 25 ms high wake-up pattern followed by the
  // caller's StartCommunication request. The pattern is driven here; the
  // request and response go through WriteMessage / ReadMessage.
  bool FastInitPattern();

  // Writes the bytes with the tester inter-byte gap (P4) and swallows the
  // transceiver echo. Waits for the minimum inter-message gap (P3) first.
  bool WriteMessage(const uint8_t* data, uint8_t len);

  // Reads one message: waits up to first_byte_timeout_ms for the first byte,
  // then reads until a gap longer than the inter-byte limit (P1max) or
  // max_len bytes. Returns the number of bytes read (0 on timeout).
  uint8_t ReadMessage(uint8_t* out, uint8_t max_len, uint32_t first_byte_timeout_ms);

  // Releases the UART and lets the line idle high.
  void End();

  bool IsOpen() const;

 private:
  void OpenUart();
  void DriveTxPin(bool high);
  bool ReadByte(uint8_t& out, uint32_t timeout_ms);
  void DrainRx();

  bool uart_open_ = false;
  uint32_t last_activity_ms_ = 0;
};

}  // namespace drivers
}  // namespace obd
