#pragma once

#include <Arduino.h>

namespace obd {
namespace config {

constexpr gpio_num_t kTwaiRxPin = GPIO_NUM_4;
constexpr gpio_num_t kTwaiTxPin = GPIO_NUM_5;

}  // namespace config
}  // namespace obd
