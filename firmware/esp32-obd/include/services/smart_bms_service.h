#pragma once

#include <Arduino.h>

#include "../drivers/twai_driver.h"
#include "../app_link/transport_ble.h"

namespace obd {
namespace services {

// Smart ED 3rd-gen (Smart 451 ED, ~2012–2015) battery / drivetrain readouts.
//
// Two data paths, both ported from ED_BMSdiag (MyLab-odyssey, MIT license):
//
// 1) Passive broadcast sniffing — canDiag::ReadCAN(...) listens to frames the
//    car emits continuously when the bus is awake:
//        0x518  byte[7]/2                                              → SOC display %
//        0x2D5  ((byte[4] & 0x03) << 8 | byte[5]) / 10                 → internal SOC %
//        0x508  (((byte[2] & 0x3F) << 8 | byte[3]) - 0x2000) / 10.0    → HV current (A)
//        0x448  (byte[6] << 8 | byte[7]) / 10.0                        → HV voltage (V)
//        0x3D5  byte[3] / 10.0                                         → 12V LV bus (V)
//        0x412  byte[2]*65535 + byte[3]*256 + byte[4]                  → odometer (km)
//        0x512  byte[0], byte[1]                                       → dashboard hh:mm
//
// 2) Active UDS 0x22 readDataByIdentifier on the BMS diagnostic address
//    (request 0x7E7, response 0x7EF), with ISO 15765-2 single + multi-frame
//    transport. Slow rotation every few seconds:
//        DID 0x0310 (rqBattCapacity)      → SOH%, cell capacity As stats
//        DID 0x0201 (rqBattTemperatures)  → 7 module temperatures (°C)
//        DID 0x0208 (rqBattVolts)         → 93 cell voltages → min/max/mean/stddev (mV)
//
// Active UDS path requires `active` TWAI mode (set via set_link). In
// listen-only mode only the broadcast path is used (no TX on the bus).
class SmartBmsService {
 public:
  SmartBmsService(app_link::TransportBle& app_link, drivers::TwaiDriver& twai);

  // Hand every CAN frame seen on the bus. Service distinguishes broadcast
  // frames (0x518/0x448/...) from UDS responses (0x7EF) and updates state.
  void OnFrame(const drivers::CanFrame& frame);

  // Drives the UDS query rotation and periodic telemetry emission. Call from
  // the main loop while streaming is enabled.
  void Tick(uint32_t now_ms);

  // Enables UDS rotation (requires TWAI active mode). When false the service
  // only decodes broadcast frames.
  void SetActiveMode(bool active);

  void Reset();

 private:
  // ED_BMSdiag uses DATALENGTH 238 for its reassembly buffer.
  static constexpr size_t kUdsBufferLen = 240;
  static constexpr uint32_t kEmitIntervalMs = 1000;
  static constexpr uint32_t kUdsRotationIntervalMs = 4000;
  static constexpr uint32_t kUdsResponseTimeoutMs = 800;
  static constexpr uint32_t kBmsRequestId = 0x7E7;
  static constexpr uint32_t kBmsResponseId = 0x7EF;

  enum class UdsState : uint8_t { kIdle, kAwaitingFirstFrame, kAwaitingConsecutiveFrames };

  enum class UdsQuery : uint8_t {
    kCapacity = 0,      // DID 0x0310, gives SOH + cap stats
    kTemperatures = 1,  // DID 0x0201, 7 module temps
    kCellVoltages = 2,  // DID 0x0208, 93 cell voltages
    kCount = 3,
  };

  app_link::TransportBle& app_link_;
  drivers::TwaiDriver& twai_;
  bool active_mode_ = false;

  // Broadcast state.
  bool have_soc_pct_ = false;
  float soc_pct_ = 0.0f;
  bool have_soc_internal_pct_ = false;
  float soc_internal_pct_ = 0.0f;
  bool have_hv_v_ = false;
  float hv_v_ = 0.0f;
  bool have_hv_amps_ = false;
  float hv_amps_ = 0.0f;
  bool have_lv_v_ = false;
  float lv_v_ = 0.0f;
  bool have_odo_km_ = false;
  uint32_t odo_km_ = 0;
  bool have_time_ = false;
  uint8_t time_hh_ = 0;
  uint8_t time_mm_ = 0;

  // UDS-derived state (latest snapshot; persists across emit cycles).
  bool have_soh_pct_ = false;
  float soh_pct_ = 0.0f;
  bool have_cap_as_ = false;
  uint16_t cap_as_min_ = 0;
  uint16_t cap_as_mean_ = 0;
  uint16_t cap_as_max_ = 0;
  bool have_temps_ = false;
  float temp_min_c_ = 0.0f;
  float temp_max_c_ = 0.0f;
  float temp_mean_c_ = 0.0f;
  bool have_cells_ = false;
  uint16_t cell_mv_min_ = 0;
  uint16_t cell_mv_max_ = 0;
  uint16_t cell_mv_mean_ = 0;
  float cell_mv_stddev_ = 0.0f;
  uint8_t cell_count_ = 0;

  bool any_fresh_since_last_emit_ = false;
  uint32_t last_emit_ms_ = 0;

  // UDS state machine.
  UdsState uds_state_ = UdsState::kIdle;
  UdsQuery uds_active_query_ = UdsQuery::kCapacity;
  uint8_t uds_buffer_[kUdsBufferLen] = {0};
  size_t uds_write_pos_ = 0;
  int32_t uds_items_remaining_ = 0;
  uint32_t uds_started_at_ms_ = 0;
  uint32_t uds_last_rotation_ms_ = 0;
  uint8_t uds_rotation_idx_ = 0;

  void DecodeBroadcast(const drivers::CanFrame& frame);
  void DispatchUdsFrame(const drivers::CanFrame& frame);
  void HandleFirstFrame(const drivers::CanFrame& frame);
  void HandleConsecutiveFrame(const drivers::CanFrame& frame);
  void HandleSingleFrame(const drivers::CanFrame& frame);
  void DecodeCapacityResponse();
  void DecodeTemperaturesResponse();
  void DecodeCellVoltagesResponse();
  void MaybeStartNextUdsQuery(uint32_t now_ms);
  bool SendUdsRequest(uint16_t did);
  bool SendFlowControl();
  void FinishUdsExchange();
  void AbortUdsExchange();
  void MaybePublish(uint32_t now_ms);
};

}  // namespace services
}  // namespace obd
