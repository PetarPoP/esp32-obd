#pragma once

#include <Arduino.h>

namespace obd {
namespace drivers {

struct CanFrame {
  uint32_t id = 0;
  uint8_t dlc = 0;
  uint8_t data[8] = {0};
  uint32_t timestamp_ms = 0;
};

class TwaiDriver {
 public:
  // Installs and starts the TWAI driver in normal mode at 500 kbps (standard OBD-II).
  // Required for OBD-II PID queries since they need to transmit on the bus.
  bool Begin();

  // Installs the driver in listen-only mode (no TX, no ACK). Keeps the bus passive
  // for raw frame sniffing without disturbing other ECUs.
  bool BeginListenOnly();

  // Re-installs the driver at a runtime-selectable baud and mode. Supported baud
  // values (kbps): 100, 125, 250, 500, 1000. Other values fall back to 500 kbps.
  bool BeginAt(uint32_t baud_kbps, bool listen_only);

  // Stops and uninstalls the driver. Safe to call multiple times.
  void End();

  // Reads one frame. timeout_ms == 0 returns immediately; >0 blocks up to that long.
  bool ReadFrame(CanFrame& out_frame, uint32_t timeout_ms = 0);

  // Transmits one classic 11-bit CAN frame. Returns false in listen-only mode.
  bool SendFrame(uint32_t id, const uint8_t* data, uint8_t dlc, uint32_t timeout_ms = 50);

  bool IsInstalled() const;
  bool IsListenOnly() const;
  uint32_t BaudKbps() const;

  // Returns a JSON snapshot of the TWAI controller state (bus state, TX/RX queue,
  // error counters, ...) for diagnostics. Empty string if the driver is not installed.
  String GetStatusJson();

 private:
  bool installed_ = false;
  bool listen_only_ = false;
  uint32_t baud_kbps_ = 500;
};

}  // namespace drivers
}  // namespace obd
