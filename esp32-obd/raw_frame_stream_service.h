#pragma once

#include <Arduino.h>

#include "transport_ble.h"
#include "twai_driver.h"

namespace obd {
namespace services {

class RawFrameStreamService {
 public:
  explicit RawFrameStreamService(app_link::TransportBle& app_link);
  void Publish(const drivers::CanFrame& frame);

 private:
  app_link::TransportBle& app_link_;
};

}  // namespace services
}  // namespace obd
