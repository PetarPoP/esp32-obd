#pragma once

#include <Arduino.h>

namespace obd {
namespace config {

// MrDIY ESP32 OBD shield: CAN transceiver on OBD pins 6 (CAN-H) and 14 (CAN-L).
// The board has no K-line or J1850 driver, so only ISO 15765-4 CAN vehicles
// (all cars sold in the EU from 2008 and in the US from 2008) can be read.
constexpr gpio_num_t kTwaiRxPin = GPIO_NUM_4;
constexpr gpio_num_t kTwaiTxPin = GPIO_NUM_5;

}  // namespace config
}  // namespace obd
