#pragma once

#include <Arduino.h>

namespace obd {
namespace services {

class ConnectionManager {
 public:
  void Tick();

 private:
  uint32_t last_heartbeat_ms_ = 0;
};

}  // namespace services
}  // namespace obd
