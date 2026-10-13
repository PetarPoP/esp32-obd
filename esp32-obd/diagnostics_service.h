#pragma once

#include <Arduino.h>

#include "transport_ble.h"
#include "dtc_decoder.h"
#include "obd_transport.h"
#include "pid_decoder.h"
#include "safety_guard.h"

namespace obd {
namespace services {

class DiagnosticsService {
 public:
  DiagnosticsService(core::ObdTransport& transport,
                     core::DtcDecoder& dtc_decoder,
                     core::PidDecoder& pid_decoder,
                     app_link::TransportBle& app_link,
                     safety::SafetyGuard& safety_guard);

  // Reads stored (Mode 03), pending (Mode 07) and permanent (Mode 0A) DTCs
  // from every ECU and publishes them in one "dtc" message.
  void ReadDtc();
  void ClearDtc();

  // Reads freeze frame 0 (Mode 02): the DTC that stored it and the engine
  // values at that moment. Publishes one or more "freeze_frame" messages.
  void ReadFreezeFrame();

  // Reads Mode 09 vehicle information (VIN, calibration IDs, ECU names) and
  // publishes a "vehicle_info" message.
  void ReadVehicleInfo();

  // VIN from Mode 09 PID 02, or an empty string.
  String ReadVin();

 private:
  // Appends the codes of one mode as JSON strings to `list`, skipping
  // duplicates. Returns false if no ECU answered.
  bool ReadMode(uint8_t mode, String& list, bool& truncated);
  bool IsObdReady() const;

  core::ObdTransport& transport_;
  core::DtcDecoder& dtc_decoder_;
  core::PidDecoder& pid_decoder_;
  app_link::TransportBle& app_link_;
  safety::SafetyGuard& safety_guard_;
  size_t code_list_chars_ = 0;
};

}  // namespace services
}  // namespace obd
