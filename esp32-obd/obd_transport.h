#pragma once

#include "twai_driver.h"

namespace obd {
namespace core {

// Maximum data bytes returned by RequestPid. Most Mode 01 PIDs carry up to 4
// data bytes; a few newer ones (e.g. 0x7F engine run time) carry more.
constexpr uint8_t kPidMaxResponseBytes = 16;

// Largest reassembled response kept per ECU message (VIN needs 20, DTC lists more).
constexpr uint8_t kObdMessageMaxLen = 96;
// Most ECU messages collected for one request.
constexpr uint8_t kObdMaxMessages = 8;

enum class DiagnosticProfile : uint8_t {
  kObdMode01 = 0,
  kSmartEd = 1,  // Smart electric drive (gen. 3); passive broadcast sniffing à la ED_BMSdiag.
};

// OBD-II physical protocols. Values 1..9 follow the ELM327 "ATSP" numbering.
// Only the CAN ones work with this hardware; the others are listed so that a
// request for them gets a clear "unsupported_hardware" answer.
enum class ObdProtocol : uint8_t {
  kNone = 0,
  kJ1850Pwm = 1,
  kJ1850Vpw = 2,
  kIso9141 = 3,
  kKwpSlow = 4,
  kKwpFast = 5,
  kCan11_500 = 6,  // ISO 15765-4 CAN, 11-bit ID, 500 kbps
  kCan29_500 = 7,  // ISO 15765-4 CAN, 29-bit ID, 500 kbps
  kCan11_250 = 8,  // ISO 15765-4 CAN, 11-bit ID, 250 kbps
  kCan29_250 = 9,  // ISO 15765-4 CAN, 29-bit ID, 250 kbps
  kCan11Custom = 10,  // 11-bit OBD addressing at a non-standard baud from set_link
};

const char* ObdProtocolId(ObdProtocol protocol);
bool IsCanProtocol(ObdProtocol protocol);
// Parses "auto", "can11_500", "6", ... Returns false if unknown.
bool ParseObdProtocol(const String& text, ObdProtocol& out_protocol, bool& out_auto);

// One response message from one ECU: the service bytes starting with the
// positive response SID (e.g. 41 0C 1A F8), ISO-TP framing stripped.
struct ObdMessage {
  uint8_t ecu = 0;  // low byte of the response ID (0xE8.. for 11-bit, source address for 29-bit)
  uint8_t len = 0;
  uint8_t data[kObdMessageMaxLen] = {0};
};

// Physical (one ECU) addressing for manufacturer-specific requests.
struct PhysicalAddress {
  uint32_t tx = 0;
  uint32_t rx = 0;
  bool extended = false;   // 29-bit identifiers
  int16_t ext_addr = -1;   // ISO-TP extended address byte (BMW), -1 = none
  uint8_t tester = 0xF1;   // first byte of answers when ext_addr is used
};

class ObdTransport {
 public:
  using ProgressCallback = void (*)(const char* stage, ObdProtocol candidate);
  // Called after each stored response; returning true ends the request early.
  using CompletionCallback = bool (*)(const ObdTransport& transport, void* context);

  explicit ObdTransport(drivers::TwaiDriver& twai_driver);
  bool Begin();

  void SetDiagnosticProfile(DiagnosticProfile profile);
  DiagnosticProfile GetDiagnosticProfile() const;
  const char* DiagnosticProfileIdString() const;

  // Forwards the next raw frame from the bus, if any. Non-blocking.
  bool PollRawFrame(drivers::CanFrame& out_frame);

  // Tries CAN 500/250 kbps with 11/29-bit IDs (ELM327 order) and stays
  // connected to the first one where an ECU answers 01 00. A baud that is
  // silent in a short listen-only window is skipped when another baud carries
  // traffic, so we never transmit at a baud that is known to be wrong.
  ObdProtocol AutoDetect(ProgressCallback progress = nullptr);

  // Sets up the controller for one CAN protocol and verifies it with 01 00.
  bool Connect(ObdProtocol protocol);

  // Legacy set_link path: 11-bit OBD addressing on the already configured
  // TWAI baud, without verification (no protocol in listen-only mode).
  void UseCanUnverified();

  void Disconnect();

  ObdProtocol Protocol() const;
  const char* ProtocolIdString() const;
  bool IsConnected() const;
  bool IsCan() const;

  // Sends a diagnostic request (service bytes, e.g. {0x01, 0x0C}) and
  // collects positive responses. With wait_for_all the full response window
  // is used so every ECU can answer (unless `done` ends it early); otherwise
  // the call returns on the first matching response. timeout_ms == 0 picks
  // the default. Returns the number of messages available through Message().
  uint8_t Request(const uint8_t* request, uint8_t request_len, bool wait_for_all,
                  uint32_t timeout_ms = 0, CompletionCallback done = nullptr,
                  void* done_context = nullptr);
  // Sends a request to one ECU (Mode 21/22 manufacturer PIDs, session
  // control) and returns on its first positive answer. Does not affect
  // ConsecutiveFailures().
  uint8_t RequestPhysical(const PhysicalAddress& address, const uint8_t* request,
                          uint8_t request_len, uint32_t timeout_ms = 0);
  uint8_t MessageCount() const;
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

  // Walks PIDs 00, 20, 40, ... E0 and merges the answers of every ECU.
  bool DiscoverSupportedPids();
  bool SupportedPidsKnown() const;
  bool IsPidSupported(uint8_t pid) const;
  // Hex string of the 8 bitmap words (PIDs 01..FF), e.g. "BE3EB813...".
  String SupportedPidsHex() const;

  // Housekeeping from the main loop: CAN bus-off recovery.
  void Tick();

  // Consecutive requests that got no answer; the caller can use it to decide
  // that the link is gone and a new detection is needed.
  uint16_t ConsecutiveFailures() const;

  // Worst TWAI counters seen while the last AutoDetect probed the bus actively.
  const drivers::TwaiDriver::Counters& ProbeCounters() const { return probe_counters_; }

 private:
  struct Addressing {
    uint32_t tx = 0;
    uint32_t rx = 0;
    bool extended = false;
    bool functional = true;
    int16_t ext_addr = -1;
    uint8_t tester = 0xF1;
  };

  uint8_t RequestImpl(const Addressing& addressing, const uint8_t* request, uint8_t request_len,
                      bool wait_for_all, uint32_t timeout_ms, CompletionCallback done,
                      void* done_context);
  bool AcceptResponse(const uint8_t* request, uint8_t request_len, const uint8_t* data,
                      uint8_t len, uint32_t& deadline);
  void StoreMessage(uint8_t ecu, const uint8_t* data, uint8_t len);
  bool VerifyWithPid00();
  void ClearSupportedPids();
  void MergeSupportedBlock(uint8_t base_pid);

  drivers::TwaiDriver& twai_driver_;
  DiagnosticProfile diagnostic_profile_ = DiagnosticProfile::kObdMode01;
  ObdProtocol protocol_ = ObdProtocol::kNone;
  bool probing_ = false;  // single-shot CAN transmissions while detecting

  ObdMessage messages_[kObdMaxMessages];
  uint8_t message_count_ = 0;
  uint8_t last_nrc_ = 0;

  uint32_t supported_pids_[8] = {0};
  bool supported_pids_known_ = false;

  uint16_t consecutive_failures_ = 0;
  drivers::TwaiDriver::Counters probe_counters_;
};

}  // namespace core
}  // namespace obd
