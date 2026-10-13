#include "k_line_driver.h"

#include "pin_config.h"

namespace obd {
namespace drivers {

namespace {

// ISO 9141-2 / ISO 14230-2 timing (ms).
constexpr uint32_t kIdleBeforeInitMs = 300;   // W5
constexpr uint32_t kFiveBaudBitMs = 200;      // 1 bit at 5 baud
constexpr uint32_t kSyncTimeoutMs = 400;      // W1 (300 ms max) + margin
constexpr uint32_t kKeyByteTimeoutMs = 100;   // W2 / W3 (20 ms max) + margin
constexpr uint32_t kKeyByteAckDelayMs = 30;   // W4 (25..50 ms)
constexpr uint32_t kFastInitLowMs = 25;       // TiniL
constexpr uint32_t kFastInitHighMs = 25;      // TiniH
constexpr uint32_t kTesterInterByteMs = 5;    // P4
constexpr uint32_t kMinRequestGapMs = 55;     // P3 min
constexpr uint32_t kEcuInterByteGapMs = 20;   // P1 max
constexpr uint32_t kEchoTimeoutMs = 5;

HardwareSerial g_kline_uart(2);

}  // namespace

bool KLineDriver::IsAvailable() { return config::kKLineEnabled; }

bool KLineDriver::IsOpen() const { return uart_open_; }

void KLineDriver::OpenUart() {
  g_kline_uart.begin(kBaud, SERIAL_8N1, config::kKLineRxPin, config::kKLineTxPin);
  uart_open_ = true;
}

void KLineDriver::End() {
  if (uart_open_) {
    g_kline_uart.end();
    uart_open_ = false;
  }
  if (IsAvailable()) {
    DriveTxPin(true);
  }
}

void KLineDriver::DriveTxPin(bool high) {
  if (uart_open_) {
    g_kline_uart.end();
    uart_open_ = false;
  }
  pinMode(config::kKLineTxPin, OUTPUT);
  digitalWrite(config::kKLineTxPin, high ? HIGH : LOW);
}

bool KLineDriver::ReadByte(uint8_t& out, uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (true) {
    if (g_kline_uart.available() > 0) {
      out = static_cast<uint8_t>(g_kline_uart.read());
      last_activity_ms_ = millis();
      return true;
    }
    if (millis() - start >= timeout_ms) {
      return false;
    }
    delay(1);
  }
}

void KLineDriver::DrainRx() {
  while (g_kline_uart.available() > 0) {
    g_kline_uart.read();
  }
}

bool KLineDriver::SlowInit(uint8_t address, uint8_t& out_kb1, uint8_t& out_kb2) {
  if (!IsAvailable()) {
    return false;
  }
  DriveTxPin(true);
  delay(kIdleBeforeInitMs);

  // Address byte at 5 baud: start bit, 8 data bits LSB first, stop bit.
  DriveTxPin(false);
  delay(kFiveBaudBitMs);
  for (uint8_t bit = 0; bit < 8; ++bit) {
    DriveTxPin(((address >> bit) & 0x01) != 0);
    delay(kFiveBaudBitMs);
  }
  DriveTxPin(true);
  delay(kFiveBaudBitMs);

  OpenUart();
  DrainRx();

  uint8_t sync = 0;
  if (!ReadByte(sync, kSyncTimeoutMs) || sync != 0x55) {
    End();
    return false;
  }
  if (!ReadByte(out_kb1, kKeyByteTimeoutMs) || !ReadByte(out_kb2, kKeyByteTimeoutMs)) {
    End();
    return false;
  }

  delay(kKeyByteAckDelayMs);
  const uint8_t kb2_inverted = static_cast<uint8_t>(~out_kb2);
  g_kline_uart.write(kb2_inverted);
  g_kline_uart.flush();
  if (config::kKLineEchoesTx) {
    uint8_t echo = 0;
    ReadByte(echo, kEchoTimeoutMs);
  }

  uint8_t address_inverted = 0;
  if (!ReadByte(address_inverted, kKeyByteTimeoutMs) ||
      address_inverted != static_cast<uint8_t>(~address)) {
    End();
    return false;
  }
  last_activity_ms_ = millis();
  return true;
}

bool KLineDriver::FastInitPattern() {
  if (!IsAvailable()) {
    return false;
  }
  DriveTxPin(true);
  delay(kIdleBeforeInitMs);
  DriveTxPin(false);
  delay(kFastInitLowMs);
  DriveTxPin(true);
  const uint32_t high_start = millis();
  OpenUart();
  DrainRx();
  while (millis() - high_start < kFastInitHighMs) {
    delay(1);
  }
  // The StartCommunication request follows right away, so no P3 wait.
  last_activity_ms_ = millis() - kMinRequestGapMs;
  return true;
}

bool KLineDriver::WriteMessage(const uint8_t* data, uint8_t len) {
  if (!uart_open_ || data == nullptr || len == 0) {
    return false;
  }
  while (millis() - last_activity_ms_ < kMinRequestGapMs) {
    delay(1);
  }
  DrainRx();
  for (uint8_t i = 0; i < len; ++i) {
    g_kline_uart.write(data[i]);
    g_kline_uart.flush();
    if (config::kKLineEchoesTx) {
      uint8_t echo = 0;
      if (!ReadByte(echo, kEchoTimeoutMs) || echo != data[i]) {
        // A missing or corrupted echo means a bus collision or no transceiver.
        DrainRx();
        last_activity_ms_ = millis();
        return false;
      }
    }
    if (i + 1 < len) {
      delay(kTesterInterByteMs);
    }
  }
  last_activity_ms_ = millis();
  return true;
}

uint8_t KLineDriver::ReadMessage(uint8_t* out, uint8_t max_len, uint32_t first_byte_timeout_ms) {
  if (!uart_open_ || out == nullptr || max_len == 0) {
    return 0;
  }
  uint8_t count = 0;
  if (!ReadByte(out[count], first_byte_timeout_ms)) {
    return 0;
  }
  ++count;
  while (count < max_len && ReadByte(out[count], kEcuInterByteGapMs)) {
    ++count;
  }
  return count;
}

}  // namespace drivers
}  // namespace obd
