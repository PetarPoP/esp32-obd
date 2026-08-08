#include "twai_driver.h"

#include <driver/twai.h>

#include "pin_config.h"

namespace obd {
namespace drivers {

namespace {

twai_timing_config_t MakeTiming(uint32_t baud_kbps) {
  switch (baud_kbps) {
    case 100:
      return TWAI_TIMING_CONFIG_100KBITS();
    case 125:
      return TWAI_TIMING_CONFIG_125KBITS();
    case 250:
      return TWAI_TIMING_CONFIG_250KBITS();
    case 500:
      return TWAI_TIMING_CONFIG_500KBITS();
    case 1000:
      return TWAI_TIMING_CONFIG_1MBITS();
    default:
      return TWAI_TIMING_CONFIG_500KBITS();
  }
}

uint32_t NormalizeBaud(uint32_t baud_kbps) {
  switch (baud_kbps) {
    case 100:
    case 125:
    case 250:
    case 500:
    case 1000:
      return baud_kbps;
    default:
      return 500;
  }
}

bool InstallAndStart(twai_mode_t mode, uint32_t baud_kbps) {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
      config::kTwaiTxPin, config::kTwaiRxPin, mode);
  // Larger queues smooth out bursty traffic during PID polling and raw sniffing.
  g.tx_queue_len = 8;
  g.rx_queue_len = 64;
  twai_timing_config_t t = MakeTiming(baud_kbps);
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK) {
    return false;
  }
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }
  return true;
}

}  // namespace

bool TwaiDriver::Begin() { return BeginAt(500, /*listen_only=*/false); }

bool TwaiDriver::BeginListenOnly() { return BeginAt(500, /*listen_only=*/true); }

bool TwaiDriver::BeginAt(uint32_t baud_kbps, bool listen_only) {
  if (installed_) {
    End();
  }
  const uint32_t normalized = NormalizeBaud(baud_kbps);
  const twai_mode_t mode = listen_only ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL;
  if (!InstallAndStart(mode, normalized)) {
    return false;
  }
  installed_ = true;
  listen_only_ = listen_only;
  baud_kbps_ = normalized;
  return true;
}

void TwaiDriver::End() {
  if (!installed_) {
    return;
  }
  twai_stop();
  twai_driver_uninstall();
  installed_ = false;
}

bool TwaiDriver::ReadFrame(CanFrame& out_frame, uint32_t timeout_ms) {
  if (!installed_) {
    return false;
  }
  twai_message_t msg;
  const TickType_t ticks =
      timeout_ms == 0 ? 0 : pdMS_TO_TICKS(timeout_ms);
  if (twai_receive(&msg, ticks) != ESP_OK) {
    return false;
  }
  out_frame.id = msg.identifier;
  out_frame.dlc = msg.data_length_code;
  for (uint8_t i = 0; i < out_frame.dlc && i < 8; ++i) {
    out_frame.data[i] = msg.data[i];
  }
  out_frame.timestamp_ms = millis();
  return true;
}

bool TwaiDriver::SendFrame(uint32_t id, const uint8_t* data, uint8_t dlc, uint32_t timeout_ms) {
  if (!installed_ || listen_only_ || data == nullptr || dlc > 8) {
    return false;
  }
  twai_message_t msg = {};
  msg.identifier = id;
  msg.data_length_code = dlc;
  msg.flags = 0;  // standard 11-bit, data frame, no extended
  for (uint8_t i = 0; i < dlc; ++i) {
    msg.data[i] = data[i];
  }
  return twai_transmit(&msg, pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
}

bool TwaiDriver::IsInstalled() const { return installed_; }
bool TwaiDriver::IsListenOnly() const { return listen_only_; }
uint32_t TwaiDriver::BaudKbps() const { return baud_kbps_; }

String TwaiDriver::GetStatusJson() {
  if (!installed_) {
    return String();
  }
  twai_status_info_t info;
  if (twai_get_status_info(&info) != ESP_OK) {
    return String();
  }
  String json = "{\"bus_state\":";
  switch (info.state) {
    case TWAI_STATE_STOPPED:
      json += "\"stopped\"";
      break;
    case TWAI_STATE_RUNNING:
      json += "\"running\"";
      break;
    case TWAI_STATE_BUS_OFF:
      json += "\"bus_off\"";
      break;
    case TWAI_STATE_RECOVERING:
      json += "\"recovering\"";
      break;
    default:
      json += "\"unknown\"";
      break;
  }
  json += ",\"msgs_to_tx\":";
  json += String(info.msgs_to_tx);
  json += ",\"msgs_to_rx\":";
  json += String(info.msgs_to_rx);
  json += ",\"tx_error\":";
  json += String(info.tx_error_counter);
  json += ",\"rx_error\":";
  json += String(info.rx_error_counter);
  json += ",\"tx_failed\":";
  json += String(info.tx_failed_count);
  json += ",\"rx_missed\":";
  json += String(info.rx_missed_count);
  json += ",\"arb_lost\":";
  json += String(info.arb_lost_count);
  json += ",\"bus_error\":";
  json += String(info.bus_error_count);
  json += ",\"baud_kbps\":";
  json += String(baud_kbps_);
  json += ",\"mode\":\"";
  json += listen_only_ ? "listen_only" : "active";
  json += "\"}";
  return json;
}

}  // namespace drivers
}  // namespace obd
