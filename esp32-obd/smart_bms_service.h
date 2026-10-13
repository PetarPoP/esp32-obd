#pragma once

#include <Arduino.h>

#include "twai_driver.h"
#include "transport_ble.h"

namespace obd {
namespace services {

// Smart ED 3rd-gen (Smart 451 ED, ~2012–2015) battery, drivetrain, cooling and
// charger readouts, ported from ED_BMSdiag by MyLab-odyssey (MIT license,
// https://github.com/MyLab-odyssey/ED_BMSdiag).
//
// 1) Passive broadcast frames (no TX):
//      BMS: 0x518 SOC, 0x2D5 internal SOC, 0x508 current, 0x448 HV, 0x3D5 12 V,
//           0x412 odometer, 0x512 dashboard clock
//      DRV: 0x200 speed, 0x318 range / usable power, 0x3CE consumption,
//           0x3D7 HV active, 0x3F2 ECO score, 0x504 trip counters
// 2) Active UDS 0x22 readDataByIdentifier (needs `active` TWAI mode), one
//    request at a time in a rotation:
//      BMS 0x7E7/0x7EF, cooling & subsystems 0x7E5/0x7ED, charger 0x61A/0x483.
//    Responses are reassembled exactly like ED_BMSdiag (data[i] = frame[i+1],
//    7 bytes per consecutive frame, optional skipped lines), so its byte
//    offsets apply unchanged.
class SmartBmsService {
 public:
  SmartBmsService(app_link::TransportBle& app_link, drivers::TwaiDriver& twai);

  // Hand every CAN frame seen on the bus. Service distinguishes broadcast
  // frames from UDS responses and updates state.
  void OnFrame(const drivers::CanFrame& frame);

  // Drives the UDS query rotation and periodic telemetry emission. Call from
  // the main loop while streaming is enabled.
  void Tick(uint32_t now_ms);

  // Enables UDS rotation (requires TWAI active mode). When false the service
  // only decodes broadcast frames.
  void SetActiveMode(bool active);

  void Reset();

  // Telemetry keys this service can publish (for capability reports).
  static constexpr size_t kMaxMetricKeys = 128;
  static size_t MetricCount();
  static const char* MetricKey(size_t index);

  struct UdsQuery {
    uint16_t request_id;
    uint16_t response_id;
    uint16_t did;
    uint8_t skip_start;  // consecutive-frame lines not stored (1-based), 0 = none
    uint8_t skip_end;
    uint8_t decoder;
  };

 private:
  // ED_BMSdiag DATALENGTH.
  static constexpr size_t kUdsBufferLen = 238;
  static constexpr uint32_t kEmitIntervalMs = 1000;
  static constexpr uint32_t kQueryGapMs = 150;
  static constexpr uint32_t kUdsResponseTimeoutMs = 4000;
  static constexpr uint32_t kResponsePendingTimeoutMs = 5000;
  // Flow control as in ED_BMSdiag: block size 8, separation time 20 ms.
  static constexpr uint8_t kFlowControlBlockSize = 8;

  enum class UdsState : uint8_t { kIdle, kAwaitingFirstFrame, kAwaitingConsecutiveFrames };

  void DecodeBroadcast(const drivers::CanFrame& frame);
  void HandleUdsFrame(const drivers::CanFrame& frame);
  void StartNextQuery(uint32_t now_ms);
  bool SendFlowControl();
  void FinishQuery();
  void Decode(uint8_t decoder);
  void Set(uint8_t metric, float value);
  uint16_t Word(size_t offset) const;
  void MaybePublish(uint32_t now_ms);

  app_link::TransportBle& app_link_;
  drivers::TwaiDriver& twai_;
  bool active_mode_ = false;

  float values_[128] = {0};
  bool have_[128] = {false};
  bool any_fresh_since_last_emit_ = false;
  uint32_t last_emit_ms_ = 0;
  bool nlg6_present_ = false;
  bool charger_checked_ = false;
  float adc_cell_mean_mv_ = 0.0f;

  // UDS state machine.
  UdsState uds_state_ = UdsState::kIdle;
  const UdsQuery* query_ = nullptr;
  uint8_t data_[kUdsBufferLen] = {0};
  size_t write_pos_ = 0;
  int32_t items_remaining_ = 0;
  uint16_t cf_count_ = 0;
  uint16_t cf_line_ = 0;
  uint32_t query_deadline_ms_ = 0;
  uint32_t last_query_end_ms_ = 0;
  uint8_t rotation_idx_ = 0;
};

}  // namespace services
}  // namespace obd
