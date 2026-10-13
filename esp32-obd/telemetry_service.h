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

  // Should be called periodically from the main loop. Each invocation sends
  // one request (up to six PIDs at once on CAN), accumulates the JSON, and
  // once the rotation completes publishes the telemetry payload over BLE.
  void Tick();

  // Selects which catalogue fields the service reads. Fields whose PID the
  // vehicle does not support are skipped.
  void SetTelemetryFields(const core::BitSet256& fields);

  // Drops accumulated state so the next rotation starts clean.
  void Reset();

 private:
  struct BatchContext {
    const uint8_t* pids;
    uint8_t count;
  };

  static bool BatchComplete(const core::ObdTransport& transport, void* context);
  void DecodePid(uint8_t pid, const uint8_t* data, uint8_t len);
  void AppendField(const char* key, const String& value);
  void PublishRotation();

  core::ObdTransport& transport_;
  core::PidDecoder& pid_decoder_;
  app_link::TransportBle& app_link_;
  core::BitSet256 fields_;
  core::BitSet256 wanted_pids_;
  uint16_t next_pid_ = 1;
  bool batching_enabled_ = true;
  uint8_t empty_batches_ = 0;
  String accumulated_json_;
  bool any_field_obtained_ = false;
  uint32_t last_degraded_ms_ = 0;

  // ISO 15765-4 allows up to six PIDs in one Mode 01 request.
  static constexpr uint8_t kMaxPidsPerRequest = 6;
  // Empty multi-PID answers in a row before falling back to one PID per request.
  static constexpr uint8_t kMaxEmptyBatches = 3;
  // Telemetry JSON body length before "ts" at which a message is split; keeps
  // each notify under the 244-byte payload of a 247-byte ATT MTU.
  static constexpr size_t kMaxPayloadBody = 190;
  static constexpr uint32_t kDegradedIntervalMs = 1000;
};

}  // namespace services
}  // namespace obd
