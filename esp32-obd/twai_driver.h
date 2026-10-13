#pragma once

#include <Arduino.h>

namespace obd {
namespace drivers {

struct CanFrame {
  uint32_t id = 0;
  uint8_t dlc = 0;
  uint8_t data[8] = {0};
  uint32_t timestamp_ms = 0;
  bool extended = false;  // 29-bit identifier
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

  // Transmits one classic CAN frame (11-bit, or 29-bit when extended is set).
  // single_shot disables automatic retransmission, which keeps protocol probing
  // at a possibly wrong baud from flooding the bus. Returns false in listen-only mode.
  bool SendFrame(uint32_t id, const uint8_t* data, uint8_t dlc, uint32_t timeout_ms = 50,
                 bool extended = false, bool single_shot = false);

  // Recovers the controller from bus-off and restarts it. Returns true if the
  // controller is (again) running.
  bool RecoverIfBusOff();

  // Bus error counter since the driver was installed (0 if not installed).
  uint32_t BusErrorCount();

  // millis() of the last received frame (0 if none since boot).
  uint32_t LastRxMs() const;

  bool IsInstalled() const;
  bool IsListenOnly() const;
  uint32_t BaudKbps() const;

  // Returns a JSON snapshot of the TWAI controller state (bus state, TX/RX queue,
  // error counters, ...) for diagnostics. Empty string if the driver is not installed.
  String GetStatusJson();

 private:
  bool installed_ = false;
  uint32_t last_rx_ms_ = 0;
  bool listen_only_ = false;
  uint32_t baud_kbps_ = 500;
};

}  // namespace drivers
}  // namespace obd
