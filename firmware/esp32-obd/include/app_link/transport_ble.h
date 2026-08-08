#pragma once

#include <Arduino.h>

namespace obd {
namespace app_link {

class TransportBle {
 public:
  using CommandHandler = void (*)(const String& command, const String& raw_payload);

  bool Begin();
  void NotifyTelemetry(const String& json_payload);
  void NotifyResponse(const String& json_payload);
  bool IsConnected() const;
  void SetCommandHandler(CommandHandler handler);
};

}  // namespace app_link
}  // namespace obd
