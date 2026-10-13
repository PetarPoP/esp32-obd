#pragma once

#include "k_line_driver.h"
#include "twai_driver.h"

namespace obd {
namespace core {

// Maximum data bytes returned by RequestPid (Mode 01 PIDs carry up to 4 data bytes A..D).
constexpr uint8_t kPidMaxResponseBytes = 5;

// Largest reassembled response kept per ECU message (VIN needs 20, DTC lists more).
constexpr uint8_t kObdMessageMaxLen = 96;
// Most ECUs / K-line messages collected for one request.
constexpr uint8_t kObdMaxMessages = 8;

enum class DiagnosticProfile : uint8_t {
  kObdMode01 = 0,
  kSmartEd = 1,  // Smart electric drive (gen. 3); passive broadcast sniffing à la ED_BMSdiag.
};

// OBD-II physical protocols. Values 1..9 follow the ELM327 "ATSP" numbering.
enum class ObdProtocol : uint8_t {
  kNone = 0,
  kJ1850Pwm = 1,   // SAE J1850 PWM 41.6 kbps (needs hardware this board lacks)
  kJ1850Vpw = 2,   // SAE J1850 VPW 10.4 kbps (needs hardware this board lacks)
  kIso9141 = 3,    // ISO 9141-2, 5 baud init, 10.4 kbps
  kKwpSlow = 4,    // ISO 14230-4 KWP2000, 5 baud init, 10.4 kbps
  kKwpFast = 5,    // ISO 14230-4 KWP2000, fast init, 10.4 kbps
  kCan11_500 = 6,  // ISO 15765-4 CAN, 11-bit ID, 500 kbps
  kCan29_500 = 7,  // ISO 15765-4 CAN, 29-bit ID, 500 kbps
  kCan11_250 = 8,  // ISO 15765-4 CAN, 11-bit ID, 250 kbps
  kCan29_250 = 9,  // ISO 15765-4 CAN, 29-bit ID, 250 kbps
  kCan11Custom = 10,  // 11-bit OBD addressing at a non-standard baud from set_link
};

const char* ObdProtocolId(ObdProtocol protocol);
// Parses "auto", "can11_500", "iso9141", "6", ... Returns false if unknown.
bool ParseObdProtocol(const String& text, ObdProtocol& out_protocol, bool& out_auto);

// One response message from one ECU: the service bytes starting with the
// positive response SID (e.g. 41 0C 1A F8), headers and checksums stripped.
struct ObdMessage {
  uint8_t ecu = 0;  // CAN: low byte of the response ID / source address; K-line: source address
  uint8_t len = 0;
  uint8_t data[kObdMessageMaxLen] = {0};
};

class ObdTransport {
 public:
  using ProgressCallback = void (*)(const char* stage, ObdProtocol candidate);

  ObdTransport(drivers::TwaiDriver& twai_driver, drivers::KLineDriver& k_line_driver);
  bool Begin();

  void SetDiagnosticProfile(DiagnosticProfile profile);
  DiagnosticProfile GetDiagnosticProfile() const;
  const char* DiagnosticProfileIdString() const;

  // Forwards the next raw frame from the bus, if any. Non-blocking.
  bool PollRawFrame(drivers::CanFrame& out_frame);

  // Tries every protocol the hardware supports (CAN 500/250 kbps with 11/29-bit
  // IDs, then K-line fast init and 5 baud init) and stays connected to the
  // first one where an ECU answers 01 00. CAN bauds that are silent in a short
  // listen-only window are skipped when another baud carries traffic, so we
  // never transmit at a baud that is known to be wrong. Blocks for up to ~8 s.
  ObdProtocol AutoDetect(ProgressCallback progress = nullptr);

  // Sets up the hardware for one protocol and verifies it with 01 00.
  bool Connect(ObdProtocol protocol);

  // Legacy set_link path: 11-bit OBD addressing on the already configured
  // TWAI baud, without verification (no protocol in listen-only mode).
  void UseCanUnverified();

  void Disconnect();

  ObdProtocol Protocol() const;
  const char* ProtocolIdString() const;
  bool IsConnected() const;
  bool IsCan() const;
  bool IsKLine() const;

  // Sends a diagnostic request (service bytes, e.g. {0x01, 0x0C}) using the
  // current protocol and collects positive responses. With wait_for_all the
  // full response window is used so every ECU can answer; otherwise the call
  // returns on the first matching response. timeout_ms == 0 picks a protocol
  // default. Returns the number of messages available through Message().
  uint8_t Request(const uint8_t* request, uint8_t request_len, bool wait_for_all,
                  uint32_t timeout_ms = 0);
  const ObdMessage& Message(uint8_t index) const;
  // Last negative response code (0x7F xx NRC) seen, or 0.
  uint8_t LastNegativeResponseCode() const;

  // Sends an OBD-II Mode 01 PID request and fills out_payload with the data
  // bytes (A, B, ...) of the first ECU that answers. Returns false on transmit
  // error, negative response or timeout.
  bool RequestPid(uint8_t pid,
                  uint8_t* out_payload,
                  uint8_t& out_payload_len,
                  uint32_t timeout_ms = 0);

  // Walks PIDs 00, 20, 40, ... C0 and merges the answers of every ECU.
  bool DiscoverSupportedPids();
  bool SupportedPidsKnown() const;
  bool IsPidSupported(uint8_t pid) const;
  // Hex string of the 8 bitmap words (PIDs 01..FF), e.g. "BE3EB813...".
  String SupportedPidsHex() const;

  // Reads the VIN (Mode 09 PID 02). Empty string if unavailable.
  String ReadVin();

  // Housekeeping from the main loop: K-line keep-alive and CAN bus-off recovery.
  void Tick();

  // Consecutive requests that got no answer; the caller can use it to decide
  // that the link is gone and a new detection is needed.
  uint16_t ConsecutiveFailures() const;

 private:
  uint8_t RequestCan(const uint8_t* request, uint8_t request_len, bool wait_for_all,
                     uint32_t timeout_ms);
  uint8_t RequestKLine(const uint8_t* request, uint8_t request_len, uint32_t timeout_ms);
  bool AcceptResponse(const uint8_t* request, uint8_t request_len, const uint8_t* data,
                      uint8_t len, uint32_t& deadline);
  void StoreMessage(uint8_t ecu, const uint8_t* data, uint8_t len);
  bool ParseKLineFrame(const uint8_t* raw, uint8_t raw_len, uint8_t& consumed, uint8_t& ecu,
                       const uint8_t*& data, uint8_t& data_len) const;
  bool ConnectCan(ObdProtocol protocol);
  bool ConnectKLine(ObdProtocol protocol);
  bool VerifyWithPid00();
  void ClearSupportedPids();
  void MergeSupportedBlock(uint8_t base_pid);

  drivers::TwaiDriver& twai_driver_;
  drivers::KLineDriver& k_line_driver_;
  DiagnosticProfile diagnostic_profile_ = DiagnosticProfile::kObdMode01;
  ObdProtocol protocol_ = ObdProtocol::kNone;
  bool probing_ = false;  // single-shot CAN transmissions while detecting

  ObdMessage messages_[kObdMaxMessages];
  uint8_t message_count_ = 0;
  uint8_t last_nrc_ = 0;

  uint32_t supported_pids_[8] = {0};
  bool supported_pids_known_ = false;

  uint32_t last_request_ms_ = 0;
  uint16_t consecutive_failures_ = 0;
};

}  // namespace core
}  // namespace obd
