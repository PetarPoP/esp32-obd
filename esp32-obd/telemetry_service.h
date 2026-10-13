#pragma once

#include "transport_ble.h"
#include "obd_transport.h"
#include "pid_decoder.h"
#include "telemetry_field_mask.h"

namespace obd {
namespace services {

class TelemetryService {
 public:
  TelemetryService(core::ObdTransport& transport,
                   core::PidDecoder& pid_decoder,
                   app_link::TransportBle& app_link);

  // Should be called periodically from the main loop. Each invocation polls
  // a single PID from the configured mask, accumulates the JSON, and once the
  // rotation completes publishes the telemetry payload over BLE.
  void Tick();

  // Restricts which fields the service tries to read. Bits set here are taken
  // from the catalogue; unsupported bits are ignored gracefully.
  void SetTelemetryFieldMask(uint32_t mask);

  // Drops accumulated state so the next rotation starts clean.
  void Reset();

 private:
  core::ObdTransport& transport_;
  core::PidDecoder& pid_decoder_;
  app_link::TransportBle& app_link_;
  uint32_t telemetry_field_mask_ = core::kTelemetryFieldDefault;
  size_t rotation_index_ = 0;
  String accumulated_json_;
  bool any_field_obtained_ = false;
  uint32_t last_degraded_ms_ = 0;

  // Telemetry JSON body length before "ts" at which a message is split; keeps
  // each notify under the 244-byte payload of a 247-byte ATT MTU.
  static constexpr size_t kMaxPayloadBody = 190;
  static constexpr uint32_t kDegradedIntervalMs = 1000;
};

}  // namespace services
}  // namespace obd
