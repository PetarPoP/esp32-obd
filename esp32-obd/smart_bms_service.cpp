#include "smart_bms_service.h"

#include <math.h>

namespace obd {
namespace services {

namespace {

constexpr uint8_t kCellCount = 93;  // ED_BMSdiag CELLCOUNT
constexpr uint8_t kTempOffset = 40;  // charger temperatures
constexpr size_t kMaxPayloadBody = 190;
const char kNlg6PartNumber[] = "4519822221";

// Telemetry metrics. Keys of the original port are kept unchanged.
enum Metric : uint8_t {
  // BMS broadcast
  kSocPct, kSocInternalPct, kHvV, kHvAmps, kPowerKw, kLvV, kOdoKm, kTimeHh, kTimeMm,
  // Drivetrain broadcast
  kSpeedKmh, kRangeKm, kUsablePowerPct, kEnergyStartKwh100, kEnergyResetKwh100, kHvActive,
  kEcoAccelPct, kEcoConstPct, kEcoCoastPct, kEcoTotalPct, kTripStartKm, kTripResetKm,
  // BMS UDS
  kTempMaxC, kTempMinC, kTempMeanC, kCoolantInC,
  kBattT1C, kBattT2C, kBattT3C, kBattT4C, kBattT5C, kBattT6C, kBattT7C, kBattT8C, kBattT9C,
  kBmsAmps, kBmsHvV, kAdcCellMvMax, kAdcCellMvMin, kAdcCellMvMean,
  kCellMvMin, kCellMvMean, kCellMvMax, kCellMvStddev, kCellMinNo, kCellMaxNo, kCellCountMetric,
  kIsolationKohm, kDcFault,
  kCapAhMin, kCapAhMean, kCapAhMax, kCellCapAhMin, kCellCapAhMean, kCellCapAhMax,
  kCapInitAh, kCapLossPct, kSohPct, kBattStatus, kHvOffTimeS, kHvLowCurrentS, kOcvTimerS,
  kLastMeasDays, kCapMeasQuality, kCapCombinedQuality,
  kContactorCyclesLeft, kContactorCyclesMax, kContactorState,
  // Cooling & subsystems
  kCoolingTempC, kPumpTempC, kPumpLvV, kPumpAmps, kPumpPct, kPumpOtrH, kFanPct, kFanOtrH,
  kHeaterOtrH, kHeaterOn, kVacuumOtrH, kVacuumP1Mbar, kVacuumP2Mbar,
  // Charger (OBL or NLG6 fast charger)
  kChargerNlg6, kChargerPlateC, kChargerTempC, kChargerSocketC, kChargerSetAmps, kChargerLvV,
  kChargerDcV, kChargerDcAmps, kAcL1V, kAcL2V, kAcL3V, kAcL1A, kAcL2A, kAcL3A, kCableMaxA,
  kChargepointMaxA,
  kMetricCount
};

struct MetricInfo {
  const char* key;
  uint8_t decimals;
};

const MetricInfo kMetrics[kMetricCount] = {
    {"soc_pct", 1}, {"soc_internal_pct", 1}, {"hv_v", 1}, {"hv_amps", 1}, {"power_kw", 2},
    {"lv_v", 1}, {"odo_km", 0}, {"time_hh", 0}, {"time_mm", 0},
    {"speed_kmh", 0}, {"range_km", 0}, {"usable_power_pct", 0}, {"energy_start_kwh100", 2},
    {"energy_reset_kwh100", 2}, {"hv_active", 0}, {"eco_accel_pct", 0}, {"eco_const_pct", 0},
    {"eco_coast_pct", 0}, {"eco_total_pct", 0}, {"trip_start_km", 2}, {"trip_reset_km", 2},
    {"temp_max_c", 1}, {"temp_min_c", 1}, {"temp_mean_c", 1}, {"coolant_in_c", 1},
    {"batt_t1_c", 1}, {"batt_t2_c", 1}, {"batt_t3_c", 1}, {"batt_t4_c", 1}, {"batt_t5_c", 1},
    {"batt_t6_c", 1}, {"batt_t7_c", 1}, {"batt_t8_c", 1}, {"batt_t9_c", 1},
    {"bms_amps", 2}, {"bms_hv_v", 1}, {"adc_cell_mv_max", 0}, {"adc_cell_mv_min", 0},
    {"adc_cell_mv_mean", 0},
    {"cell_mv_min", 0}, {"cell_mv_mean", 0}, {"cell_mv_max", 0}, {"cell_mv_stddev", 1},
    {"cell_min_no", 0}, {"cell_max_no", 0}, {"cell_count", 0},
    {"isolation_kohm", 0}, {"dc_fault", 0},
    {"cap_ah_min", 2}, {"cap_ah_mean", 2}, {"cap_ah_max", 2}, {"cell_cap_ah_min", 2},
    {"cell_cap_ah_mean", 2}, {"cell_cap_ah_max", 2},
    {"cap_init_ah", 1}, {"cap_loss_pct", 3}, {"soh_pct", 1}, {"batt_status", 0},
    {"hv_off_time_s", 0}, {"hv_lowcurrent_s", 0}, {"ocv_timer_s", 0},
    {"last_meas_days", 0}, {"cap_meas_quality", 3}, {"cap_combined_quality", 3},
    {"contactor_cycles_left", 0}, {"contactor_cycles_max", 0}, {"contactor_state", 0},
    {"cooling_temp_c", 1}, {"pump_temp_c", 0}, {"pump_lv_v", 1}, {"pump_amps", 1},
    {"pump_pct", 1}, {"pump_otr_h", 0}, {"fan_pct", 1}, {"fan_otr_h", 0},
    {"heater_otr_h", 0}, {"heater_on", 0}, {"vacuum_otr_h", 2}, {"vacuum_p1_mbar", 0},
    {"vacuum_p2_mbar", 0},
    {"charger_nlg6", 0}, {"charger_plate_c", 0}, {"charger_temp_c", 0}, {"charger_socket_c", 0},
    {"charger_set_amps", 0}, {"charger_lv_v", 1}, {"charger_dc_v", 1}, {"charger_dc_amps", 1},
    {"ac_l1_v", 1}, {"ac_l2_v", 1}, {"ac_l3_v", 1}, {"ac_l1_a", 1}, {"ac_l2_a", 1},
    {"ac_l3_a", 1}, {"cable_max_a", 0}, {"chargepoint_max_a", 0},
};

// Decoder ids for the UDS rotation.
enum Decoder : uint8_t {
  kDecBattAmps, kDecHvStatus, kDecTemps, kDecModuleTemps, kDecAdcRef, kDecCellVolts,
  kDecIsolation, kDecCapacity, kDecCapInit, kDecCapLoss, kDecCyclesLeft, kDecCyclesMax,
  kDecContactorState,
  kDecCoolingTemp, kDecPumpTemp, kDecPumpLv, kDecPumpAmps, kDecPumpRpm, kDecPumpOtr,
  kDecFanRpm, kDecFanOtr, kDecHeaterOtr, kDecHeaterOn, kDecVacuumOtr, kDecVacuumP1,
  kDecVacuumP2,
  kDecChargerPartNumber, kDecChargerTemps, kDecChargerSelCurrent, kDecChargerVoltages,
  kDecChargerAmps,
};

constexpr uint16_t kBmsTx = 0x7E7, kBmsRx = 0x7EF;
constexpr uint16_t kCoolTx = 0x7E5, kCoolRx = 0x7ED;
constexpr uint16_t kChgTx = 0x61A, kChgRx = 0x483;

}  // namespace

// One rotation; fast-changing BMS values first. The charger part number is
// only queried until it has been read once.
static const SmartBmsService::UdsQuery kQueries[] = {
    {kBmsTx, kBmsRx, 0x0203, 0, 0, kDecBattAmps},
    {kBmsTx, kBmsRx, 0x0204, 0, 0, kDecHvStatus},
    {kBmsTx, kBmsRx, 0x0201, 0, 0, kDecTemps},
    {kBmsTx, kBmsRx, 0x0202, 0, 0, kDecModuleTemps},
    {kBmsTx, kBmsRx, 0x0207, 0, 0, kDecAdcRef},
    {kBmsTx, kBmsRx, 0x0208, 28, 57, kDecCellVolts},
    {kBmsTx, kBmsRx, 0x0209, 0, 0, kDecIsolation},
    {kBmsTx, kBmsRx, 0x0310, 31, 59, kDecCapacity},
    {kBmsTx, kBmsRx, 0x0305, 0, 0, kDecCapInit},
    {kBmsTx, kBmsRx, 0x0309, 0, 0, kDecCapLoss},
    {kBmsTx, kBmsRx, 0x030B, 0, 0, kDecCyclesLeft},
    {kBmsTx, kBmsRx, 0x030C, 0, 0, kDecCyclesMax},
    {kBmsTx, kBmsRx, 0xD000, 0, 0, kDecContactorState},
    {kCoolTx, kCoolRx, 0x2047, 0, 0, kDecCoolingTemp},
    {kCoolTx, kCoolRx, 0x230A, 0, 0, kDecPumpTemp},
    {kCoolTx, kCoolRx, 0x2308, 0, 0, kDecPumpLv},
    {kCoolTx, kCoolRx, 0x2309, 0, 0, kDecPumpAmps},
    {kCoolTx, kCoolRx, 0xD032, 0, 0, kDecPumpRpm},
    {kCoolTx, kCoolRx, 0x6309, 0, 0, kDecPumpOtr},
    {kCoolTx, kCoolRx, 0xD041, 0, 0, kDecFanRpm},
    {kCoolTx, kCoolRx, 0x630A, 0, 0, kDecFanOtr},
    {kCoolTx, kCoolRx, 0x6321, 0, 0, kDecHeaterOtr},
    {kCoolTx, kCoolRx, 0xD302, 0, 0, kDecHeaterOn},
    {kCoolTx, kCoolRx, 0x6303, 0, 0, kDecVacuumOtr},
    {kCoolTx, kCoolRx, 0x2041, 0, 0, kDecVacuumP1},
    {kCoolTx, kCoolRx, 0x2043, 0, 0, kDecVacuumP2},
    {kChgTx, kChgRx, 0xF111, 0, 0, kDecChargerPartNumber},
    {kChgTx, kChgRx, 0x0223, 0, 0, kDecChargerTemps},
    {kChgTx, kChgRx, 0x022A, 0, 0, kDecChargerSelCurrent},
    {kChgTx, kChgRx, 0x0226, 0, 0, kDecChargerVoltages},
    {kChgTx, kChgRx, 0x0225, 0, 0, kDecChargerAmps},
};
static constexpr uint8_t kQueryCount = sizeof(kQueries) / sizeof(kQueries[0]);
static_assert(kMetricCount <= 128, "values_/have_ arrays hold 128 metrics");

SmartBmsService::SmartBmsService(app_link::TransportBle& app_link, drivers::TwaiDriver& twai)
    : app_link_(app_link), twai_(twai) {}

size_t SmartBmsService::MetricCount() { return kMetricCount; }

const char* SmartBmsService::MetricKey(size_t index) {
  return index < kMetricCount ? kMetrics[index].key : "";
}

void SmartBmsService::Set(uint8_t metric, float value) {
  if (metric >= kMetricCount || isnan(value) || isinf(value)) {
    return;
  }
  values_[metric] = value;
  have_[metric] = true;
  any_fresh_since_last_emit_ = true;
}

uint16_t SmartBmsService::Word(size_t offset) const {
  if (offset + 1 >= kUdsBufferLen) {
    return 0;
  }
  return (static_cast<uint16_t>(data_[offset]) << 8) | data_[offset + 1];
}

void SmartBmsService::OnFrame(const drivers::CanFrame& frame) {
  if (frame.extended) {
    return;
  }
  if (query_ != nullptr && uds_state_ != UdsState::kIdle && frame.id == query_->response_id) {
    HandleUdsFrame(frame);
    return;
  }
  DecodeBroadcast(frame);
}

void SmartBmsService::DecodeBroadcast(const drivers::CanFrame& f) {
  const uint8_t* b = f.data;
  switch (f.id) {
    case 0x518:
      if (f.dlc >= 8) Set(kSocPct, b[7] / 2.0f);
      return;
    case 0x2D5:
      if (f.dlc >= 6) Set(kSocInternalPct, (((b[4] & 0x03) << 8) | b[5]) / 10.0f);
      return;
    case 0x508:
      if (f.dlc >= 4) {
        const int32_t raw = ((b[2] & 0x3F) << 8) | b[3];
        Set(kHvAmps, (raw - 0x2000) / 10.0f);
        if (have_[kHvV]) Set(kPowerKw, values_[kHvV] * values_[kHvAmps] / 1000.0f);
      }
      return;
    case 0x448:
      if (f.dlc >= 8) Set(kHvV, ((b[6] << 8) | b[7]) / 10.0f);
      return;
    case 0x3D5:
      if (f.dlc >= 4) Set(kLvV, b[3] / 10.0f);
      return;
    case 0x412:
      if (f.dlc >= 5) Set(kOdoKm, static_cast<float>(b[2]) * 65535.0f + b[3] * 256.0f + b[4]);
      return;
    case 0x512:
      if (f.dlc >= 2) {
        Set(kTimeHh, b[0]);
        Set(kTimeMm, b[1]);
      }
      return;
    case 0x200:
      if (f.dlc >= 4) Set(kSpeedKmh, ((b[2] << 8) | b[3]) / 18.0f);
      return;
    case 0x318:
      if (f.dlc >= 8) {
        Set(kUsablePowerPct, b[5]);
        Set(kRangeKm, b[7]);
      }
      return;
    case 0x3CE:
      if (f.dlc >= 4) {
        Set(kEnergyStartKwh100, ((b[0] << 8) | b[1]) / 100.0f);
        Set(kEnergyResetKwh100, ((b[2] << 8) | b[3]) / 100.0f);
      }
      return;
    case 0x3D7:
      if (f.dlc >= 1) Set(kHvActive, b[0]);
      return;
    case 0x3F2:
      if (f.dlc >= 4) {
        Set(kEcoAccelPct, b[0] >> 1);
        Set(kEcoConstPct, b[1] >> 1);
        Set(kEcoCoastPct, b[2] >> 1);
        Set(kEcoTotalPct, b[3] >> 1);
      }
      return;
    case 0x504:
      if (f.dlc >= 6) {
        const uint16_t start = (b[1] << 8) | b[2];
        const uint16_t reset = (b[4] << 8) | b[5];
        if (start != 254) Set(kTripStartKm, start / 100.0f);
        if (reset != 254) Set(kTripResetKm, reset / 100.0f);
      }
      return;
    default:
      return;
  }
}

void SmartBmsService::HandleUdsFrame(const drivers::CanFrame& frame) {
  if (frame.dlc < 2) {
    return;
  }
  const uint8_t pci = frame.data[0] & 0xF0;
  if (uds_state_ == UdsState::kAwaitingFirstFrame && pci == 0x00) {
    if (frame.data[1] == 0x7F) {
      if (frame.dlc >= 4 && frame.data[3] == 0x78) {
        query_deadline_ms_ = millis() + kResponsePendingTimeoutMs;  // ECU busy, keep waiting
      } else {
        uds_state_ = UdsState::kIdle;  // negative response: skip this DID
        last_query_end_ms_ = millis();
      }
      return;
    }
    // ED_BMSdiag single frame: data[i] = rxBuf[i+1].
    memset(data_, 0, sizeof(data_));
    for (uint8_t i = 0; i + 1 < frame.dlc; ++i) {
      data_[i] = frame.data[i + 1];
    }
    FinishQuery();
    return;
  }
  if (uds_state_ == UdsState::kAwaitingFirstFrame && pci == 0x10) {
    memset(data_, 0, sizeof(data_));
    const uint16_t total = ((frame.data[0] & 0x0F) << 8) | frame.data[1];
    for (uint8_t i = 0; i + 1 < frame.dlc; ++i) {
      data_[i] = frame.data[i + 1];
    }
    items_remaining_ = static_cast<int32_t>(total) - 6;
    write_pos_ = 7;
    cf_count_ = 0;
    cf_line_ = 0;
    uds_state_ = UdsState::kAwaitingConsecutiveFrames;
    SendFlowControl();
    return;
  }
  if (uds_state_ == UdsState::kAwaitingConsecutiveFrames && pci == 0x20) {
    ++cf_count_;
    const uint8_t len = frame.dlc;
    items_remaining_ -= len - 1;
    for (uint8_t i = 0; i < len - 1 && i < 7; ++i) {
      if (write_pos_ + i < kUdsBufferLen - 6) {
        data_[write_pos_ + i] = frame.data[i + 1];
      }
    }
    if (cf_count_ % kFlowControlBlockSize == 0 && items_remaining_ > 0) {
      SendFlowControl();
    }
    ++cf_line_;
    const bool skipped = query_->skip_start != 0 && cf_line_ >= query_->skip_start &&
                         cf_line_ <= query_->skip_end;
    if (!skipped) {
      write_pos_ += 7;
    }
    if (items_remaining_ <= 0) {
      FinishQuery();
    }
  }
}

bool SmartBmsService::SendFlowControl() {
  uint8_t fc[8] = {0x30, kFlowControlBlockSize, 0x14, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  return twai_.SendFrame(query_->request_id, fc, sizeof(fc));
}

void SmartBmsService::StartNextQuery(uint32_t now_ms) {
  // Skip the charger part-number query once it has been answered.
  for (uint8_t tries = 0; tries < kQueryCount; ++tries) {
    query_ = &kQueries[rotation_idx_];
    rotation_idx_ = (rotation_idx_ + 1) % kQueryCount;
    if (!(query_->decoder == kDecChargerPartNumber && charger_checked_)) {
      break;
    }
  }
  uint8_t req[8] = {0x03, 0x22, static_cast<uint8_t>(query_->did >> 8),
                    static_cast<uint8_t>(query_->did & 0xFF), 0xFF, 0xFF, 0xFF, 0xFF};
  uds_state_ = UdsState::kAwaitingFirstFrame;
  query_deadline_ms_ = now_ms + kUdsResponseTimeoutMs;
  if (!twai_.SendFrame(query_->request_id, req, sizeof(req))) {
    uds_state_ = UdsState::kIdle;
    last_query_end_ms_ = now_ms;
  }
}

void SmartBmsService::FinishQuery() {
  uds_state_ = UdsState::kIdle;
  last_query_end_ms_ = millis();
  Decode(query_->decoder);
}

void SmartBmsService::Decode(uint8_t decoder) {
  const uint8_t* d = data_;
  switch (decoder) {
    case kDecBattAmps:
      Set(kBmsAmps, static_cast<int16_t>(Word(3)) / 32.0f);
      return;
    case kDecHvStatus:
      Set(kBmsHvV, Word(12) / 64.0f);
      return;
    case kDecTemps:
      // max, min, mean, coolant inlet (signed, 1/64 degC).
      Set(kTempMaxC, static_cast<int16_t>(Word(4)) / 64.0f);
      Set(kTempMinC, static_cast<int16_t>(Word(6)) / 64.0f);
      Set(kTempMeanC, static_cast<int16_t>(Word(8)) / 64.0f);
      Set(kCoolantInC, static_cast<int16_t>(Word(10)) / 64.0f);
      return;
    case kDecModuleTemps:
      // Three sensors per battery unit, 9 values.
      for (uint8_t i = 0; i < 9; ++i) {
        Set(kBattT1C + i, static_cast<int16_t>(Word(4 + 2 * i)) / 64.0f);
      }
      return;
    case kDecAdcRef:
      Set(kAdcCellMvMax, Word(4) + 1500.0f);
      Set(kAdcCellMvMin, Word(6) + 1500.0f);
      Set(kAdcCellMvMean, Word(8));
      adc_cell_mean_mv_ = Word(8);
      return;
    case kDecCellVolts: {
      uint16_t v_min = 0xFFFF, v_max = 0;
      uint8_t min_at = 0, max_at = 0, count = 0;
      double sum = 0.0, sum_sq = 0.0;
      for (uint8_t i = 0; i < kCellCount; ++i) {
        const uint16_t mv = Word(4 + 2 * i);
        if (mv == 0 || mv == 0xFFFF) continue;
        if (mv < v_min) { v_min = mv; min_at = i; }
        if (mv > v_max) { v_max = mv; max_at = i; }
        sum += mv;
        sum_sq += static_cast<double>(mv) * mv;
        ++count;
      }
      if (count == 0) return;
      const double mean = sum / count;
      const double var = sum_sq / count - mean * mean;
      // ED_BMSdiag corrects raw cell voltages by the offset to the BMS ADC reference.
      const float offset = adc_cell_mean_mv_ > 0.0f ? static_cast<float>(mean) - adc_cell_mean_mv_ : 0.0f;
      Set(kCellMvMin, v_min - offset);
      Set(kCellMvMax, v_max - offset);
      Set(kCellMvMean, static_cast<float>(mean) - offset);
      Set(kCellMvStddev, var > 0.0 ? static_cast<float>(sqrt(var)) : 0.0f);
      Set(kCellMinNo, min_at + 1);
      Set(kCellMaxNo, max_at + 1);
      Set(kCellCountMetric, count);
      return;
    }
    case kDecIsolation:
      Set(kIsolationKohm, static_cast<int16_t>(Word(4)));
      Set(kDcFault, d[6]);
      return;
    case kDecCapacity: {
      // Capacities are in As/10: Ah = value / 360.
      Set(kHvOffTimeS, static_cast<float>(d[5]) * 65535.0f + d[6] * 256.0f + d[7]);
      Set(kHvLowCurrentS, static_cast<float>(d[9]) * 65535.0f + d[10] * 256.0f + d[11]);
      Set(kOcvTimerS, Word(12));
      Set(kBattStatus, d[14]);  // 0xFF = OK, 0 = fault, otherwise degraded-cell bit mask
      Set(kCapAhMax, Word(17) / 360.0f);
      Set(kCapAhMin, Word(21) / 360.0f);
      Set(kCapAhMean, Word(23) / 360.0f);
      Set(kCapCombinedQuality, Word(222) / 65535.0f);
      Set(kLastMeasDays, Word(224));
      Set(kCapMeasQuality, Word(226) / 65535.0f);
      uint16_t c_min = 0xFFFF, c_max = 0;
      double c_sum = 0.0;
      uint8_t count = 0;
      for (uint8_t i = 0; i < kCellCount; ++i) {
        const uint16_t c = Word(25 + 2 * i);
        if (c == 0 || c == 0xFFFF) continue;
        if (c < c_min) c_min = c;
        if (c > c_max) c_max = c;
        c_sum += c;
        ++count;
      }
      if (count > 0) {
        Set(kCellCapAhMin, c_min / 360.0f);
        Set(kCellCapAhMax, c_max / 360.0f);
        Set(kCellCapAhMean, static_cast<float>(c_sum / count) / 360.0f);
      }
      if (have_[kCapInitAh] && values_[kCapInitAh] > 0.0f) {
        // Estimate: BMS mean capacity against the initial capacity.
        Set(kSohPct, values_[kCapAhMean] / values_[kCapInitAh] * 100.0f);
      }
      return;
    }
    case kDecCapInit:
      Set(kCapInitAh, static_cast<int16_t>(Word(3)) / 360.0f);
      return;
    case kDecCapLoss:
      Set(kCapLossPct, static_cast<int16_t>(Word(3)) / 1000.0f);
      return;
    case kDecCyclesLeft:
      Set(kContactorCyclesLeft, static_cast<float>(d[4]) * 65536.0f + d[5] * 256.0f + d[6]);
      return;
    case kDecCyclesMax:
      Set(kContactorCyclesMax, static_cast<float>(d[4]) * 65536.0f + d[5] * 256.0f + d[6]);
      return;
    case kDecContactorState:
      Set(kContactorState, d[3]);  // 0 = off, 2 = on
      return;
    case kDecCoolingTemp:
      Set(kCoolingTempC, static_cast<int16_t>(Word(3)) / 8.0f);
      return;
    case kDecPumpTemp:
      Set(kPumpTempC, static_cast<int>(d[3]) - 50);
      return;
    case kDecPumpLv:
      Set(kPumpLvV, d[3] / 10.0f);
      return;
    case kDecPumpAmps:
      Set(kPumpAmps, d[4] / 5.0f);
      return;
    case kDecPumpRpm:
      Set(kPumpPct, d[3] / 255.0f * 100.0f);
      return;
    case kDecPumpOtr:
      Set(kPumpOtrH, Word(3));
      return;
    case kDecFanRpm:
      Set(kFanPct, d[3] / 255.0f * 100.0f);
      return;
    case kDecFanOtr:
      Set(kFanOtrH, Word(3));
      return;
    case kDecHeaterOtr:
      Set(kHeaterOtrH, Word(3));
      return;
    case kDecHeaterOn:
      Set(kHeaterOn, d[3]);
      return;
    case kDecVacuumOtr: {
      const double otr = d[3] * (16777216 / 10.0) + d[4] * (65535 / 10.0) + d[5] * (256 / 10.0);
      Set(kVacuumOtrH, static_cast<float>(otr / 36000.0));
      return;
    }
    case kDecVacuumP1:
      Set(kVacuumP1Mbar, static_cast<int16_t>(Word(3)));
      return;
    case kDecVacuumP2:
      Set(kVacuumP2Mbar, static_cast<int16_t>(Word(3)));
      return;
    case kDecChargerPartNumber: {
      nlg6_present_ = memcmp(&d[4], kNlg6PartNumber, 10) == 0;
      charger_checked_ = true;
      Set(kChargerNlg6, nlg6_present_ ? 1 : 0);
      return;
    }
    case kDecChargerTemps:
      // Raw 255 (215 degC) means "no reading" on the OBL.
      if (!nlg6_present_ && (d[5] == 0xFF || d[7] == 0xFF || d[9] == 0xFF)) {
        return;
      }
      if (nlg6_present_) {
        Set(kChargerPlateC, static_cast<int>(d[4]) - kTempOffset);
        Set(kChargerTempC, static_cast<int>(d[12]) - kTempOffset);
        Set(kChargerSocketC, static_cast<int>(d[13]) - kTempOffset);
      } else {
        Set(kChargerPlateC, static_cast<int>(d[5]) - kTempOffset);
        Set(kChargerTempC, static_cast<int>(d[7]) - kTempOffset);
        Set(kChargerSocketC, static_cast<int>(d[9]) - kTempOffset);
      }
      return;
    case kDecChargerSelCurrent:
      Set(kChargerSetAmps, nlg6_present_ ? d[8] : d[7]);
      return;
    case kDecChargerVoltages: {
      // The standard OBL reports 8190 when not charging.
      auto valid = [](uint16_t v) { return v != 8190 ? v / 10.0f : 0.0f; };
      if (nlg6_present_) {
        Set(kChargerLvV, d[8] / 10.0f);
        Set(kChargerDcV, Word(9) / 10.0f);
        Set(kAcL1V, Word(11) / 10.0f);
        Set(kAcL2V, Word(13) / 10.0f);
        Set(kAcL3V, Word(15) / 10.0f);
      } else {
        Set(kChargerLvV, d[6] / 10.0f);
        Set(kChargerDcV, valid(Word(9)));
        Set(kAcL1V, valid(Word(11)));
      }
      return;
    }
    case kDecChargerAmps: {
      auto valid = [](uint16_t v) { return v != 2047 ? v / 10.0f : 0.0f; };
      if (nlg6_present_) {
        Set(kChargerDcAmps, Word(4) / 10.0f);
        Set(kAcL1A, Word(6) / 10.0f);
        Set(kAcL2A, Word(8) / 10.0f);
        Set(kAcL3A, Word(10) / 10.0f);
        Set(kChargepointMaxA, Word(16) / 10.0f);
        Set(kCableMaxA, Word(18) / 10.0f);
      } else {
        Set(kAcL1A, valid(Word(6)));
        Set(kChargerDcAmps, valid(Word(18)));
        // ED_BMSdiag reads word 14 here but marks it as unsure; OBDb's smart
        // fortwo signal set places the cable current at word 8.
        Set(kCableMaxA, Word(8) / 10.0f);
      }
      return;
    }
    default:
      return;
  }
}

void SmartBmsService::SetActiveMode(bool active) {
  active_mode_ = active;
  if (!active) {
    uds_state_ = UdsState::kIdle;
  }
}

void SmartBmsService::Tick(uint32_t now_ms) {
  if (active_mode_) {
    if (uds_state_ != UdsState::kIdle && (int32_t)(now_ms - query_deadline_ms_) > 0) {
      uds_state_ = UdsState::kIdle;  // no (complete) answer: move on
      last_query_end_ms_ = now_ms;
    }
    if (uds_state_ == UdsState::kIdle && now_ms - last_query_end_ms_ >= kQueryGapMs) {
      StartNextQuery(now_ms);
    }
  }
  MaybePublish(now_ms);
}

void SmartBmsService::MaybePublish(uint32_t now_ms) {
  if ((now_ms - last_emit_ms_) < kEmitIntervalMs) {
    return;
  }
  last_emit_ms_ = now_ms;

  // Emit even when nothing was decoded this window so the app sees a degraded
  // status (helps diagnose "bus is silent" situations). Long payloads are
  // split so each notify fits the BLE MTU.
  const String header = any_fresh_since_last_emit_
                            ? String("{\"type\":\"telemetry\",\"status\":\"ok\"")
                            : String("{\"type\":\"telemetry\",\"status\":\"degraded\"");
  String payload = header;
  for (uint8_t i = 0; i < kMetricCount; ++i) {
    if (!have_[i]) {
      continue;
    }
    String value;
    if (kMetrics[i].decimals == 0) {
      value = String(static_cast<long>(lroundf(values_[i])));
    } else {
      value = String(values_[i], static_cast<unsigned int>(kMetrics[i].decimals));
    }
    if (payload.length() + strlen(kMetrics[i].key) + value.length() > kMaxPayloadBody) {
      app_link_.NotifyTelemetry(payload + ",\"partial\":true,\"ts\":" + String(now_ms) + "}");
      payload = header;
    }
    payload += ",\"";
    payload += kMetrics[i].key;
    payload += "\":";
    payload += value;
  }
  payload += ",\"ts\":";
  payload += String(now_ms);
  payload += "}";
  app_link_.NotifyTelemetry(payload);
  any_fresh_since_last_emit_ = false;
}

void SmartBmsService::Reset() {
  memset(have_, 0, sizeof(have_));
  any_fresh_since_last_emit_ = false;
  last_emit_ms_ = 0;
  uds_state_ = UdsState::kIdle;
  query_ = nullptr;
  rotation_idx_ = 0;
  last_query_end_ms_ = 0;
  charger_checked_ = false;
  nlg6_present_ = false;
  adc_cell_mean_mv_ = 0.0f;
}

}  // namespace services
}  // namespace obd
