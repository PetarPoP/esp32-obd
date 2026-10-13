#pragma once

#include <Arduino.h>

#include "transport_ble.h"
#include "twai_driver.h"

namespace obd {
namespace services {

// Protects the car battery: when the CAN bus has been silent and no phone has
// been connected for `sleep_after_s`, the ESP32 goes into deep sleep and wakes
// on the first CAN activity (a dominant bit pulls the transceiver RX pin low).
// The setting is kept in flash; 0 disables sleeping.
class PowerManager {
 public:
  static constexpr uint32_t kDefaultSleepAfterS = 600;

  PowerManager(drivers::TwaiDriver& twai, app_link::TransportBle& app_link);

  // Loads the setting and reports why the chip woke up.
  void Begin();
  void SetSleepAfterSeconds(uint32_t seconds);
  uint32_t SleepAfterSeconds() const;
  // "power_on", "can_activity", "timer", ...
  const char* WakeReason() const;

  // `busy` keeps the device awake (e.g. an OTA update in progress).
  void Tick(bool busy);

 private:
  void EnterDeepSleep();

  drivers::TwaiDriver& twai_;
  app_link::TransportBle& app_link_;
  uint32_t sleep_after_s_ = kDefaultSleepAfterS;
  const char* wake_reason_ = "power_on";
};

}  // namespace services
}  // namespace obd
