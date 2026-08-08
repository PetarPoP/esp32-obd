#include "connection_manager.h"

namespace obd {
namespace services {

void ConnectionManager::Tick() {
  const uint32_t now = millis();
  if (now - last_heartbeat_ms_ >= 1000) {
    last_heartbeat_ms_ = now;
    Serial.println("{\"type\":\"status\",\"heartbeat\":true}");
  }
}

}  // namespace services
}  // namespace obd
