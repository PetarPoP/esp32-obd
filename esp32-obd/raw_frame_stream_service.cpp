#include "raw_frame_stream_service.h"

namespace obd {
namespace services {

RawFrameStreamService::RawFrameStreamService(app_link::TransportBle& app_link)
    : app_link_(app_link) {}

void RawFrameStreamService::Publish(const drivers::CanFrame& frame) {
  String payload = "{\"type\":\"raw_frame\",\"id\":";
  payload += String(frame.id);
  payload += ",\"dlc\":";
  payload += String(frame.dlc);
  payload += ",\"data\":\"";
  for (uint8_t i = 0; i < frame.dlc && i < 8; ++i) {
    if (frame.data[i] < 16) {
      payload += "0";
    }
    payload += String(frame.data[i], HEX);
  }
  payload += "\",\"ts\":";
  payload += String(frame.timestamp_ms);
  payload += "}";
  app_link_.NotifyRawFrame(payload);
}

}  // namespace services
}  // namespace obd
