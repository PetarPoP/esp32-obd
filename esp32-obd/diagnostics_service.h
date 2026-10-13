#pragma once

#include <Arduino.h>

#include "transport_ble.h"
#include "dtc_decoder.h"
#include "obd_transport.h"
#include "safety_guard.h"

namespace obd {
namespace services {

class DiagnosticsService {
 public:
  DiagnosticsService(core::ObdTransport& transport,
                     core::DtcDecoder& dtc_decoder,
                     app_link::TransportBle& app_link,
                     safety::SafetyGuard& safety_guard);

  // Reads stored (Mode 03), pending (Mode 07) and permanent (Mode 0A) DTCs
  // from every ECU and publishes them in one "dtc" message.
  void ReadDtc();
  void ClearDtc();

 private:
  // Appends the codes of one mode as JSON strings to `list`, skipping
  // duplicates. Returns false if no ECU answered.
  bool ReadMode(uint8_t mode, String& list, bool& truncated);

  core::ObdTransport& transport_;
  core::DtcDecoder& dtc_decoder_;
  app_link::TransportBle& app_link_;
  safety::SafetyGuard& safety_guard_;
  size_t code_list_chars_ = 0;
};

}  // namespace services
}  // namespace obd
