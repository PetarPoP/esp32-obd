#pragma once

#include <Arduino.h>

namespace obd {
namespace config {

constexpr gpio_num_t kTwaiRxPin = GPIO_NUM_4;
constexpr gpio_num_t kTwaiTxPin = GPIO_NUM_5;

// K-line (ISO 9141-2 / ISO 14230 KWP2000) on OBD pin 7. The MrDIY CAN shield
// has no K-line transceiver, so this is off by default. To enable it, wire an
// L9637D / MC33660 / SI9241 transceiver (or an equivalent transistor circuit)
// to the pins below and set kKLineEnabled to true.
constexpr bool kKLineEnabled = false;
constexpr int kKLineRxPin = 16;
constexpr int kKLineTxPin = 17;
// Most K-line transceivers echo every transmitted byte back on RX.
constexpr bool kKLineEchoesTx = true;

// SAE J1850 PWM/VPW (OBD pins 2 and 10, older Ford/GM) needs a dedicated bus
// driver that this board does not have, so those protocols are reported as
// unsupported by the hardware.
constexpr bool kJ1850Available = false;

}  // namespace config
}  // namespace obd
