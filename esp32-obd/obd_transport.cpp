#include "obd_transport.h"

namespace obd {
namespace core {

namespace {

constexpr uint32_t kFunctionalRequestId = 0x7DF;
constexpr uint32_t kEcuResponseIdLow = 0x7E8;
constexpr uint32_t kEcuResponseIdHigh = 0x7EF;
constexpr uint8_t kModeShowCurrentData = 0x01;
constexpr uint8_t kModeResponseOffset = 0x40;

}  // namespace

ObdTransport::ObdTransport(drivers::TwaiDriver& twai_driver)
    : twai_driver_(twai_driver), diagnostic_profile_(DiagnosticProfile::kObdMode01) {}

void ObdTransport::SetDiagnosticProfile(DiagnosticProfile profile) {
  diagnostic_profile_ = profile;
}

DiagnosticProfile ObdTransport::GetDiagnosticProfile() const {
  return diagnostic_profile_;
}

const char* ObdTransport::DiagnosticProfileIdString() const {
  return diagnostic_profile_ == DiagnosticProfile::kSmartEd ? "smart_ed" : "obd_mode01";
}

bool ObdTransport::Begin() {
  drivers::CanFrame drained;
  while (twai_driver_.ReadFrame(drained, 0)) {
    // Drop any stale frames buffered before streaming starts.
  }
  return true;
}

bool ObdTransport::PollRawFrame(drivers::CanFrame& out_frame) {
  return twai_driver_.ReadFrame(out_frame, 0);
}

bool ObdTransport::RequestPid(uint8_t pid,
                              uint8_t* out_payload,
                              uint8_t& out_payload_len,
                              uint32_t timeout_ms) {
  if (out_payload == nullptr) {
    return false;
  }
  out_payload_len = 0;

  if (diagnostic_profile_ != DiagnosticProfile::kObdMode01) {
    // smart_ed profile: SmartBmsService listens passively to broadcast frames; we
    // intentionally do not transmit any Mode 01 query in this profile.
    return false;
  }

  // ISO 15765-2 single frame: PCI=0x02 (length 2), then [mode, pid], 0xCC padding.
  uint8_t request[8] = {0x02, kModeShowCurrentData, pid, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
  if (!twai_driver_.SendFrame(kFunctionalRequestId, request, sizeof(request))) {
    return false;
  }

  const uint32_t deadline = millis() + timeout_ms;
  drivers::CanFrame frame;
  while ((int32_t)(millis() - deadline) < 0) {
    const uint32_t remaining = deadline - millis();
    const uint32_t wait_ms = remaining > 10 ? 10 : remaining;
    if (!twai_driver_.ReadFrame(frame, wait_ms)) {
      continue;
    }
    if (frame.id < kEcuResponseIdLow || frame.id > kEcuResponseIdHigh) {
      continue;
    }
    if (frame.dlc < 3) {
      continue;
    }
    const uint8_t pci_high_nibble = frame.data[0] & 0xF0;
    if (pci_high_nibble != 0x00) {
      // Multi-frame ISO-TP responses are not handled in this iteration.
      continue;
    }
    const uint8_t reported_len = frame.data[0] & 0x0F;
    if (reported_len < 2) {
      continue;
    }
    if (frame.data[1] != (kModeShowCurrentData + kModeResponseOffset)) {
      continue;
    }
    if (frame.data[2] != pid) {
      continue;
    }
    const uint8_t payload_len = reported_len - 2;
    const uint8_t copy_len = payload_len > kPidMaxResponseBytes ? kPidMaxResponseBytes : payload_len;
    for (uint8_t i = 0; i < copy_len; ++i) {
      out_payload[i] = frame.data[3 + i];
    }
    out_payload_len = copy_len;
    return true;
  }
  return false;
}

}  // namespace core
}  // namespace obd
