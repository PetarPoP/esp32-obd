#pragma once

#include "twai_driver.h"

namespace obd {
namespace core {

// Maximum data bytes returned by RequestPid (single-frame ISO-TP holds up to 5 PID payload bytes).
constexpr uint8_t kPidMaxResponseBytes = 5;

enum class DiagnosticProfile : uint8_t {
  kObdMode01 = 0,
  kSmartEd = 1,  // Smart electric drive (gen. 3); passive broadcast sniffing à la ED_BMSdiag.
};

class ObdTransport {
 public:
  explicit ObdTransport(drivers::TwaiDriver& twai_driver);
  bool Begin();

  void SetDiagnosticProfile(DiagnosticProfile profile);
  DiagnosticProfile GetDiagnosticProfile() const;
  const char* DiagnosticProfileIdString() const;

  // Forwards the next raw frame from the bus, if any. Non-blocking.
  bool PollRawFrame(drivers::CanFrame& out_frame);

  // Sends an OBD-II Mode 01 PID request to functional address 0x7DF and waits
  // for a matching response from any 0x7E8..0x7EF ECU within the timeout.
  // Fills out_payload with the data bytes (A, B, ...) and out_payload_len with
  // their count. Returns false on transmit error or timeout.
  bool RequestPid(uint8_t pid,
                  uint8_t* out_payload,
                  uint8_t& out_payload_len,
                  uint32_t timeout_ms = 120);

 private:
  drivers::TwaiDriver& twai_driver_;
  DiagnosticProfile diagnostic_profile_ = DiagnosticProfile::kObdMode01;
};

}  // namespace core
}  // namespace obd
