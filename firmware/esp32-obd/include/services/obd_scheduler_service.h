#pragma once

#include "../obd/obd_transport.h"
#include "raw_frame_stream_service.h"
#include "smart_bms_service.h"
#include "telemetry_service.h"

namespace obd {
namespace services {

enum class StreamMode { kTelemetry, kRawFrames };

class ObdSchedulerService {
 public:
  ObdSchedulerService(core::ObdTransport& transport,
                      TelemetryService& telemetry_service,
                      RawFrameStreamService& raw_frame_stream,
                      SmartBmsService& smart_bms_service);
  void Tick();
  void SetStreamingEnabled(bool enabled);
  bool IsStreamingEnabled() const;
  void SetStreamMode(StreamMode mode);
  StreamMode GetStreamMode() const;

 private:
  core::ObdTransport& transport_;
  TelemetryService& telemetry_service_;
  RawFrameStreamService& raw_frame_stream_;
  SmartBmsService& smart_bms_service_;
  bool streaming_enabled_ = false;
  StreamMode stream_mode_ = StreamMode::kTelemetry;
};

}  // namespace services
}  // namespace obd
