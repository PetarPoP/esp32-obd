#include "obd_scheduler_service.h"

namespace obd {
namespace services {

namespace {
constexpr uint8_t kMaxRawFramesPerTick = 8;
constexpr uint8_t kMaxPiggybackFramesPerTick = 4;
constexpr uint8_t kMaxSmartFramesPerTick = 16;
}  // namespace

ObdSchedulerService::ObdSchedulerService(core::ObdTransport& transport,
                                         TelemetryService& telemetry_service,
                                         RawFrameStreamService& raw_frame_stream,
                                         SmartBmsService& smart_bms_service)
    : transport_(transport),
      telemetry_service_(telemetry_service),
      raw_frame_stream_(raw_frame_stream),
      smart_bms_service_(smart_bms_service) {}

void ObdSchedulerService::Tick() {
  if (!streaming_enabled_) {
    return;
  }

  // Smart ED profile: passive listener on the broadcast CAN traffic. We do not
  // run the OBD-II Mode 01 PID rotation (ObdTransport::RequestPid bails out
  // when profile != kObdMode01 anyway).
  if (transport_.GetDiagnosticProfile() == core::DiagnosticProfile::kSmartEd) {
    drivers::CanFrame frame;
    for (uint8_t i = 0; i < kMaxSmartFramesPerTick; ++i) {
      if (!transport_.PollRawFrame(frame)) {
        break;
      }
      smart_bms_service_.OnFrame(frame);
      if (stream_mode_ == StreamMode::kRawFrames) {
        raw_frame_stream_.Publish(frame);
      }
    }
    if (stream_mode_ == StreamMode::kTelemetry) {
      smart_bms_service_.Tick(millis());
    }
    return;
  }

  // Default OBD-II Mode 01 profile.
  if (stream_mode_ == StreamMode::kRawFrames) {
    drivers::CanFrame frame;
    for (uint8_t i = 0; i < kMaxRawFramesPerTick; ++i) {
      if (!transport_.PollRawFrame(frame)) {
        break;
      }
      raw_frame_stream_.Publish(frame);
    }
    return;
  }
  drivers::CanFrame frame;
  for (uint8_t i = 0; i < kMaxPiggybackFramesPerTick; ++i) {
    if (!transport_.PollRawFrame(frame)) {
      break;
    }
    raw_frame_stream_.Publish(frame);
  }
  telemetry_service_.Tick();
}

void ObdSchedulerService::SetStreamingEnabled(bool enabled) {
  streaming_enabled_ = enabled;
  if (enabled && stream_mode_ == StreamMode::kTelemetry) {
    telemetry_service_.Reset();
    smart_bms_service_.Reset();
  }
}

bool ObdSchedulerService::IsStreamingEnabled() const { return streaming_enabled_; }

void ObdSchedulerService::SetStreamMode(StreamMode mode) {
  stream_mode_ = mode;
  if (streaming_enabled_ && mode == StreamMode::kTelemetry) {
    telemetry_service_.Reset();
    smart_bms_service_.Reset();
  }
}

StreamMode ObdSchedulerService::GetStreamMode() const { return stream_mode_; }

}  // namespace services
}  // namespace obd
