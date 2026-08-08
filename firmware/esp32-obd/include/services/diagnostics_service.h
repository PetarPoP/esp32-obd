#pragma once

#include <Arduino.h>

#include "../app_link/transport_ble.h"
#include "../obd/dtc_decoder.h"
#include "../safety/safety_guard.h"

namespace obd {
namespace services {

class DiagnosticsService {
 public:
  DiagnosticsService(core::DtcDecoder& dtc_decoder,
                     app_link::TransportBle& app_link,
                     safety::SafetyGuard& safety_guard);
  void ReadDtc();
  void ClearDtc();

 private:
  core::DtcDecoder& dtc_decoder_;
  app_link::TransportBle& app_link_;
  safety::SafetyGuard& safety_guard_;
};

}  // namespace services
}  // namespace obd
