#include "power_manager.h"

#include <Preferences.h>
#include <esp_sleep.h>

#include "pin_config.h"

namespace obd {
namespace services {

namespace {
constexpr char kNvsNamespace[] = "power";
}  // namespace

PowerManager::PowerManager(drivers::TwaiDriver& twai, app_link::TransportBle& app_link)
    : twai_(twai), app_link_(app_link) {}

void PowerManager::Begin() {
  Preferences prefs;
  if (prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
    sleep_after_s_ = prefs.getUInt("sleep_s", kDefaultSleepAfterS);
    prefs.end();
  }
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0:
      wake_reason_ = "can_activity";
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      wake_reason_ = "timer";
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      wake_reason_ = "power_on";
      break;
    default:
      wake_reason_ = "other";
      break;
  }
}

void PowerManager::SetSleepAfterSeconds(uint32_t seconds) {
  // Never sleep sooner than a minute after the last activity.
  sleep_after_s_ = (seconds != 0 && seconds < 60) ? 60 : seconds;
  Preferences prefs;
  if (prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    prefs.putUInt("sleep_s", sleep_after_s_);
    prefs.end();
  }
}

uint32_t PowerManager::SleepAfterSeconds() const { return sleep_after_s_; }

const char* PowerManager::WakeReason() const { return wake_reason_; }

void PowerManager::Tick(bool busy) {
  if (sleep_after_s_ == 0 || busy || app_link_.IsConnected()) {
    return;
  }
  const uint32_t now = millis();
  const uint32_t idle_ms = sleep_after_s_ * 1000u;
  const uint32_t last_can = twai_.LastRxMs();
  const uint32_t last_ble = app_link_.LastDisconnectMs();
  if (now < idle_ms || now - last_can < idle_ms || (last_ble != 0 && now - last_ble < idle_ms)) {
    return;
  }
  EnterDeepSleep();
}

void PowerManager::EnterDeepSleep() {
  Serial.println("{\"type\":\"log\",\"level\":\"info\",\"event\":\"deep_sleep\"}");
  Serial.flush();
  twai_.End();
  app_link_.Shutdown();
  // CAN idle (recessive) keeps the transceiver RX pin high; any frame pulls it low.
  esp_sleep_enable_ext0_wakeup(config::kTwaiRxPin, 0);
  esp_deep_sleep_start();
}

}  // namespace services
}  // namespace obd
