#include "obd_transport.h"

namespace obd {
namespace core {

namespace {

// ISO 15765-4 addressing.
constexpr uint32_t kCan11FunctionalId = 0x7DF;
constexpr uint32_t kCan11ResponseLow = 0x7E8;
constexpr uint32_t kCan11ResponseHigh = 0x7EF;
constexpr uint32_t kCan11PhysicalOffset = 8;  // 0x7E8 answers requests sent to 0x7E0
constexpr uint32_t kCan29FunctionalId = 0x18DB33F1;
constexpr uint32_t kCan29ResponseBase = 0x18DAF100;  // 0x18DAF1xx, xx = ECU address
constexpr uint32_t kCan29PhysicalBase = 0x18DA00F1;  // 0x18DAxxF1
constexpr uint8_t kCanPadding = 0xCC;

// ISO 15765-2 PCI types.
constexpr uint8_t kPciSingle = 0x0;
constexpr uint8_t kPciFirst = 0x1;
constexpr uint8_t kPciConsecutive = 0x2;

// K-line addressing (tester 0xF1, functional OBD target 0x33 / 0x6A).
constexpr uint8_t kTesterAddress = 0xF1;
constexpr uint8_t kKwpTarget = 0x33;
constexpr uint8_t kIso9141Header1 = 0x68;
constexpr uint8_t kIso9141Header2 = 0x6A;
constexpr uint8_t kIso9141ResponseHeader1 = 0x48;
constexpr uint8_t kIso9141ResponseHeader2 = 0x6B;
constexpr uint8_t kSlowInitAddress = 0x33;
constexpr uint8_t kKwpStartCommunication = 0x81;
constexpr uint8_t kKwpTesterPresent = 0x3E;

constexpr uint8_t kNegativeResponse = 0x7F;
constexpr uint8_t kNrcResponsePending = 0x78;
constexpr uint8_t kPositiveResponseOffset = 0x40;

// Response windows (ms).
constexpr uint32_t kCanDefaultTimeoutMs = 150;
constexpr uint32_t kCanMultiFrameTimeoutMs = 1000;
constexpr uint32_t kCanConsecutiveFrameTimeoutMs = 200;
constexpr uint32_t kKLineDefaultTimeoutMs = 300;
constexpr uint32_t kKLineNextMessageTimeoutMs = 100;  // P2 max 50 ms + margin
constexpr uint32_t kResponsePendingTimeoutMs = 5000;  // P2* max
constexpr uint32_t kKLineKeepAliveMs = 2000;          // well under P3 max (5 s)
constexpr uint32_t kCanListenWindowMs = 250;

enum class ResponseVerdict : uint8_t { kIgnore, kPositive, kPending, kNegative };

bool IsCanProtocol(ObdProtocol p) {
  return p == ObdProtocol::kCan11_500 || p == ObdProtocol::kCan29_500 ||
         p == ObdProtocol::kCan11_250 || p == ObdProtocol::kCan29_250 ||
         p == ObdProtocol::kCan11Custom;
}

bool IsExtendedCan(ObdProtocol p) {
  return p == ObdProtocol::kCan29_500 || p == ObdProtocol::kCan29_250;
}

bool IsKLineProtocol(ObdProtocol p) {
  return p == ObdProtocol::kIso9141 || p == ObdProtocol::kKwpSlow || p == ObdProtocol::kKwpFast;
}

uint8_t Checksum(const uint8_t* data, uint8_t len) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < len; ++i) {
    sum = static_cast<uint8_t>(sum + data[i]);
  }
  return sum;
}

bool IsTimeBefore(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) < 0; }

}  // namespace

const char* ObdProtocolId(ObdProtocol protocol) {
  switch (protocol) {
    case ObdProtocol::kJ1850Pwm:
      return "j1850_pwm";
    case ObdProtocol::kJ1850Vpw:
      return "j1850_vpw";
    case ObdProtocol::kIso9141:
      return "iso9141";
    case ObdProtocol::kKwpSlow:
      return "kwp_slow";
    case ObdProtocol::kKwpFast:
      return "kwp_fast";
    case ObdProtocol::kCan11_500:
      return "can11_500";
    case ObdProtocol::kCan29_500:
      return "can29_500";
    case ObdProtocol::kCan11_250:
      return "can11_250";
    case ObdProtocol::kCan29_250:
      return "can29_250";
    case ObdProtocol::kCan11Custom:
      return "can11_custom";
    default:
      return "none";
  }
}

bool ParseObdProtocol(const String& text, ObdProtocol& out_protocol, bool& out_auto) {
  out_auto = false;
  if (text.length() == 0 || text == "auto" || text == "0") {
    out_auto = true;
    out_protocol = ObdProtocol::kNone;
    return true;
  }
  for (uint8_t i = 1; i <= static_cast<uint8_t>(ObdProtocol::kCan29_250); ++i) {
    const ObdProtocol candidate = static_cast<ObdProtocol>(i);
    if (text == ObdProtocolId(candidate) || text == String(i)) {
      out_protocol = candidate;
      return true;
    }
  }
  return false;
}

ObdTransport::ObdTransport(drivers::TwaiDriver& twai_driver, drivers::KLineDriver& k_line_driver)
    : twai_driver_(twai_driver),
      k_line_driver_(k_line_driver),
      diagnostic_profile_(DiagnosticProfile::kObdMode01) {}

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

ObdProtocol ObdTransport::Protocol() const { return protocol_; }

const char* ObdTransport::ProtocolIdString() const { return ObdProtocolId(protocol_); }

bool ObdTransport::IsConnected() const { return protocol_ != ObdProtocol::kNone; }

bool ObdTransport::IsCan() const { return IsCanProtocol(protocol_); }

bool ObdTransport::IsKLine() const { return IsKLineProtocol(protocol_); }

uint16_t ObdTransport::ConsecutiveFailures() const { return consecutive_failures_; }

const ObdMessage& ObdTransport::Message(uint8_t index) const {
  return messages_[index < kObdMaxMessages ? index : 0];
}

uint8_t ObdTransport::LastNegativeResponseCode() const { return last_nrc_; }

void ObdTransport::Disconnect() {
  k_line_driver_.End();
  protocol_ = ObdProtocol::kNone;
  ClearSupportedPids();
}

void ObdTransport::UseCanUnverified() {
  k_line_driver_.End();
  ClearSupportedPids();
  consecutive_failures_ = 0;
  if (twai_driver_.IsListenOnly()) {
    protocol_ = ObdProtocol::kNone;  // cannot transmit requests
    return;
  }
  switch (twai_driver_.BaudKbps()) {
    case 500:
      protocol_ = ObdProtocol::kCan11_500;
      break;
    case 250:
      protocol_ = ObdProtocol::kCan11_250;
      break;
    default:
      protocol_ = ObdProtocol::kCan11Custom;
      break;
  }
}

ObdProtocol ObdTransport::AutoDetect(ProgressCallback progress) {
  Disconnect();

  // 1) Passive listen at each OBD CAN baud. A baud that receives clean frames
  //    is certainly right; one that only produces bus errors is certainly wrong.
  struct BaudProbe {
    uint32_t baud_kbps;
    uint32_t frames;
  };
  BaudProbe bauds[] = {{500, 0}, {250, 0}};
  bool any_traffic = false;
  for (BaudProbe& probe : bauds) {
    if (progress != nullptr) {
      progress("listen", probe.baud_kbps == 500 ? ObdProtocol::kCan11_500 : ObdProtocol::kCan11_250);
    }
    if (!twai_driver_.BeginAt(probe.baud_kbps, /*listen_only=*/true)) {
      continue;
    }
    const uint32_t deadline = millis() + kCanListenWindowMs;
    drivers::CanFrame frame;
    while (IsTimeBefore(millis(), deadline)) {
      if (twai_driver_.ReadFrame(frame, 10)) {
        ++probe.frames;
      }
    }
    any_traffic = any_traffic || probe.frames > 0;
  }

  // 2) Active probing with 01 00, in ELM327 order (11-bit before 29-bit).
  for (const BaudProbe& probe : bauds) {
    if (any_traffic && probe.frames == 0) {
      continue;  // never transmit at a baud that is known to be wrong
    }
    const ObdProtocol candidates[2] = {
        probe.baud_kbps == 500 ? ObdProtocol::kCan11_500 : ObdProtocol::kCan11_250,
        probe.baud_kbps == 500 ? ObdProtocol::kCan29_500 : ObdProtocol::kCan29_250,
    };
    for (ObdProtocol candidate : candidates) {
      if (progress != nullptr) {
        progress("probe", candidate);
      }
      if (Connect(candidate)) {
        return protocol_;
      }
    }
  }

  // 3) K-line, when a transceiver is fitted. Some pre-2008 cars carry
  //    non-OBD CAN traffic on pins 6/14 but answer OBD only on the K-line.
  if (drivers::KLineDriver::IsAvailable()) {
    const ObdProtocol candidates[2] = {ObdProtocol::kKwpFast, ObdProtocol::kIso9141};
    for (ObdProtocol candidate : candidates) {
      if (progress != nullptr) {
        progress("probe", candidate);
      }
      if (Connect(candidate)) {
        return protocol_;
      }
    }
  }

  // Nothing answered: leave the controller passive.
  twai_driver_.BeginAt(500, /*listen_only=*/true);
  return ObdProtocol::kNone;
}

bool ObdTransport::Connect(ObdProtocol protocol) {
  Disconnect();
  consecutive_failures_ = 0;
  if (diagnostic_profile_ != DiagnosticProfile::kObdMode01) {
    return false;
  }
  if (IsCanProtocol(protocol) && protocol != ObdProtocol::kCan11Custom) {
    return ConnectCan(protocol);
  }
  if (IsKLineProtocol(protocol)) {
    return ConnectKLine(protocol);
  }
  // J1850 PWM/VPW need a bus driver this board does not have.
  return false;
}

bool ObdTransport::ConnectCan(ObdProtocol protocol) {
  const bool is_500 = protocol == ObdProtocol::kCan11_500 || protocol == ObdProtocol::kCan29_500;
  if (!twai_driver_.BeginAt(is_500 ? 500 : 250, /*listen_only=*/false)) {
    return false;
  }
  Begin();
  protocol_ = protocol;
  probing_ = true;
  const bool ok = VerifyWithPid00();
  probing_ = false;
  if (!ok) {
    twai_driver_.RecoverIfBusOff();
    protocol_ = ObdProtocol::kNone;
    return false;
  }
  DiscoverSupportedPids();
  return true;
}

bool ObdTransport::ConnectKLine(ObdProtocol protocol) {
  if (!drivers::KLineDriver::IsAvailable()) {
    return false;
  }
  // Keep the CAN controller passive while the K-line is in use.
  twai_driver_.BeginAt(twai_driver_.BaudKbps(), /*listen_only=*/true);

  if (protocol == ObdProtocol::kKwpFast) {
    if (!k_line_driver_.FastInitPattern()) {
      return false;
    }
    protocol_ = ObdProtocol::kKwpFast;
    const uint8_t start_communication = kKwpStartCommunication;
    if (Request(&start_communication, 1, /*wait_for_all=*/true) == 0) {
      Disconnect();
      return false;
    }
  } else {
    // One 5 baud init serves both ISO 9141-2 and KWP2000 slow init; the key
    // bytes tell them apart (KWP2000 uses KB1 = 0x8F).
    uint8_t kb1 = 0;
    uint8_t kb2 = 0;
    if (!k_line_driver_.SlowInit(kSlowInitAddress, kb1, kb2)) {
      return false;
    }
    protocol_ = (kb1 == 0x8F) ? ObdProtocol::kKwpSlow : ObdProtocol::kIso9141;
  }

  if (!VerifyWithPid00()) {
    Disconnect();
    return false;
  }
  DiscoverSupportedPids();
  return true;
}

bool ObdTransport::VerifyWithPid00() {
  const uint8_t request[2] = {0x01, 0x00};
  return Request(request, sizeof(request), /*wait_for_all=*/true) > 0;
}

uint8_t ObdTransport::Request(const uint8_t* request, uint8_t request_len, bool wait_for_all,
                              uint32_t timeout_ms) {
  message_count_ = 0;
  last_nrc_ = 0;
  if (request == nullptr || request_len == 0 || protocol_ == ObdProtocol::kNone ||
      diagnostic_profile_ != DiagnosticProfile::kObdMode01) {
    return 0;
  }
  uint8_t count = 0;
  if (IsCan()) {
    count = RequestCan(request, request_len, wait_for_all,
                       timeout_ms == 0 ? kCanDefaultTimeoutMs : timeout_ms);
  } else if (IsKLine()) {
    // K-line responses are always read to the end so the next request never
    // collides with a slower ECU that is still answering.
    count = RequestKLine(request, request_len,
                         timeout_ms == 0 ? kKLineDefaultTimeoutMs : timeout_ms);
  }
  last_request_ms_ = millis();
  if (count == 0) {
    if (consecutive_failures_ < 0xFFFF) {
      ++consecutive_failures_;
    }
  } else {
    consecutive_failures_ = 0;
  }
  return count;
}

bool ObdTransport::AcceptResponse(const uint8_t* request, uint8_t request_len,
                                  const uint8_t* data, uint8_t len, uint32_t& deadline) {
  if (len == 0) {
    return false;
  }
  if (data[0] == kNegativeResponse) {
    if (len >= 3 && data[1] == request[0]) {
      if (data[2] == kNrcResponsePending) {
        deadline = millis() + kResponsePendingTimeoutMs;
      } else {
        last_nrc_ = data[2];
      }
    }
    return false;
  }
  if (data[0] != static_cast<uint8_t>(request[0] + kPositiveResponseOffset)) {
    return false;
  }
  // Modes 01, 02 and 09 echo the PID; make sure the answer is for our PID.
  const uint8_t mode = request[0];
  if ((mode == 0x01 || mode == 0x02 || mode == 0x09) && request_len >= 2) {
    if (len < 2 || data[1] != request[1]) {
      return false;
    }
  }
  return true;
}

void ObdTransport::StoreMessage(uint8_t ecu, const uint8_t* data, uint8_t len) {
  if (message_count_ >= kObdMaxMessages) {
    return;
  }
  ObdMessage& msg = messages_[message_count_++];
  msg.ecu = ecu;
  msg.len = len > kObdMessageMaxLen ? kObdMessageMaxLen : len;
  memcpy(msg.data, data, msg.len);
}

uint8_t ObdTransport::RequestCan(const uint8_t* request, uint8_t request_len, bool wait_for_all,
                                 uint32_t timeout_ms) {
  if (request_len > 7) {
    return 0;  // multi-frame requests are never needed for OBD-II services
  }
  const bool extended = IsExtendedCan(protocol_);

  // Drop frames left over from earlier traffic so they cannot be mistaken for answers.
  drivers::CanFrame frame;
  for (uint8_t i = 0; i < 32 && twai_driver_.ReadFrame(frame, 0); ++i) {
  }

  uint8_t sf[8];
  memset(sf, kCanPadding, sizeof(sf));
  sf[0] = request_len;
  memcpy(&sf[1], request, request_len);
  if (!twai_driver_.SendFrame(extended ? kCan29FunctionalId : kCan11FunctionalId, sf, sizeof(sf),
                              50, extended, probing_)) {
    return 0;
  }

  // Multi-frame reassembly state, one slot per responding ECU.
  struct RxSlot {
    uint32_t id;
    uint16_t expected;
    uint16_t received;
    uint8_t next_sn;
    bool active;
    uint8_t buf[kObdMessageMaxLen];
  };
  static RxSlot slots[4];
  for (RxSlot& slot : slots) {
    slot.active = false;
  }

  uint32_t deadline = millis() + timeout_ms;
  while (IsTimeBefore(millis(), deadline)) {
    if (!twai_driver_.ReadFrame(frame, 5)) {
      continue;
    }
    if (frame.extended != extended || frame.dlc < 2) {
      continue;
    }
    uint8_t ecu = 0;
    uint32_t flow_control_id = 0;
    if (extended) {
      if ((frame.id & 0xFFFFFF00u) != kCan29ResponseBase) {
        continue;
      }
      ecu = frame.id & 0xFF;
      flow_control_id = kCan29PhysicalBase | (static_cast<uint32_t>(ecu) << 8);
    } else {
      if (frame.id < kCan11ResponseLow || frame.id > kCan11ResponseHigh) {
        continue;
      }
      ecu = frame.id & 0xFF;
      flow_control_id = frame.id - kCan11PhysicalOffset;
    }

    const uint8_t pci_type = frame.data[0] >> 4;
    if (pci_type == kPciSingle) {
      const uint8_t len = frame.data[0] & 0x0F;
      if (len == 0 || len > frame.dlc - 1) {
        continue;
      }
      if (AcceptResponse(request, request_len, &frame.data[1], len, deadline)) {
        StoreMessage(ecu, &frame.data[1], len);
        if (!wait_for_all || message_count_ >= kObdMaxMessages) {
          return message_count_;
        }
      }
    } else if (pci_type == kPciFirst) {
      if (frame.dlc < 8) {
        continue;
      }
      RxSlot* slot = nullptr;
      for (RxSlot& candidate : slots) {
        if (!candidate.active || candidate.id == frame.id) {
          slot = &candidate;
          break;
        }
      }
      if (slot == nullptr) {
        continue;
      }
      slot->id = frame.id;
      slot->expected = (static_cast<uint16_t>(frame.data[0] & 0x0F) << 8) | frame.data[1];
      slot->received = 0;
      slot->next_sn = 1;
      slot->active = true;
      for (uint8_t i = 2; i < 8; ++i) {
        if (slot->received < kObdMessageMaxLen) {
          slot->buf[slot->received] = frame.data[i];
        }
        ++slot->received;
      }
      // Flow control: continue to send, no block limit, no separation time.
      uint8_t fc[8];
      memset(fc, kCanPadding, sizeof(fc));
      fc[0] = 0x30;
      fc[1] = 0x00;
      fc[2] = 0x00;
      twai_driver_.SendFrame(flow_control_id, fc, sizeof(fc), 50, extended, false);
      const uint32_t extended_deadline = millis() + kCanMultiFrameTimeoutMs;
      if (!IsTimeBefore(extended_deadline, deadline)) {
        deadline = extended_deadline;
      }
    } else if (pci_type == kPciConsecutive) {
      for (RxSlot& slot : slots) {
        if (!slot.active || slot.id != frame.id) {
          continue;
        }
        if ((frame.data[0] & 0x0F) != slot.next_sn) {
          slot.active = false;  // lost a frame; drop this ECU's answer
          break;
        }
        slot.next_sn = (slot.next_sn + 1) & 0x0F;
        for (uint8_t i = 1; i < frame.dlc && slot.received < slot.expected; ++i) {
          if (slot.received < kObdMessageMaxLen) {
            slot.buf[slot.received] = frame.data[i];
          }
          ++slot.received;
        }
        if (slot.received >= slot.expected) {
          slot.active = false;
          const uint8_t len =
              slot.expected > kObdMessageMaxLen ? kObdMessageMaxLen : static_cast<uint8_t>(slot.expected);
          if (AcceptResponse(request, request_len, slot.buf, len, deadline)) {
            StoreMessage(ecu, slot.buf, len);
            if (!wait_for_all || message_count_ >= kObdMaxMessages) {
              return message_count_;
            }
          }
        } else {
          const uint32_t cf_deadline = millis() + kCanConsecutiveFrameTimeoutMs;
          if (!IsTimeBefore(cf_deadline, deadline)) {
            deadline = cf_deadline;
          }
        }
        break;
      }
    }
  }
  return message_count_;
}

bool ObdTransport::ParseKLineFrame(const uint8_t* raw, uint8_t raw_len, uint8_t& consumed,
                                   uint8_t& ecu, const uint8_t*& data, uint8_t& data_len) const {
  consumed = raw_len;
  if (protocol_ == ObdProtocol::kIso9141) {
    // 48 6B <src> <data...> <cs>; framing comes from the inter-message gap.
    if (raw_len < 5 || raw[0] != kIso9141ResponseHeader1 || raw[1] != kIso9141ResponseHeader2) {
      return false;
    }
    if (Checksum(raw, raw_len - 1) != raw[raw_len - 1]) {
      return false;
    }
    ecu = raw[2];
    data = &raw[3];
    data_len = raw_len - 4;
    return true;
  }

  // KWP2000: <fmt> [<tgt> <src>] [<len>] <data...> <cs>
  if (raw_len < 3) {
    return false;
  }
  const uint8_t fmt = raw[0];
  const bool has_address = (fmt & 0xC0) != 0;
  uint8_t header = 1 + (has_address ? 2 : 0);
  uint8_t len = fmt & 0x3F;
  if (len == 0) {
    if (raw_len <= header) {
      return false;
    }
    len = raw[header];
    ++header;
  }
  const uint16_t total = static_cast<uint16_t>(header) + len + 1;
  if (len == 0 || total > raw_len) {
    return false;
  }
  consumed = static_cast<uint8_t>(total);
  if (Checksum(raw, total - 1) != raw[total - 1]) {
    return false;
  }
  if (has_address && raw[1] != kTesterAddress) {
    return false;
  }
  ecu = has_address ? raw[2] : 0;
  data = &raw[header];
  data_len = len;
  return true;
}

uint8_t ObdTransport::RequestKLine(const uint8_t* request, uint8_t request_len,
                                   uint32_t timeout_ms) {
  if (request_len > 7) {
    return 0;
  }
  uint8_t out[3 + 7 + 1];
  uint8_t n = 0;
  if (protocol_ == ObdProtocol::kIso9141) {
    out[n++] = kIso9141Header1;
    out[n++] = kIso9141Header2;
  } else {
    out[n++] = 0xC0 | request_len;
    out[n++] = kKwpTarget;
  }
  out[n++] = kTesterAddress;
  memcpy(&out[n], request, request_len);
  n += request_len;
  out[n] = Checksum(out, n);
  ++n;
  if (!k_line_driver_.WriteMessage(out, n)) {
    return 0;
  }

  uint32_t wait_ms = timeout_ms;
  while (message_count_ < kObdMaxMessages) {
    uint8_t raw[80];
    const uint8_t raw_len = k_line_driver_.ReadMessage(raw, sizeof(raw), wait_ms);
    if (raw_len == 0) {
      break;
    }
    wait_ms = kKLineNextMessageTimeoutMs;
    uint8_t offset = 0;
    while (offset < raw_len) {
      uint8_t consumed = 0;
      uint8_t ecu = 0;
      const uint8_t* data = nullptr;
      uint8_t data_len = 0;
      const bool ok = ParseKLineFrame(&raw[offset], raw_len - offset, consumed, ecu, data, data_len);
      offset += consumed == 0 ? raw_len : consumed;
      if (!ok) {
        continue;
      }
      uint32_t deadline = 0;
      if (AcceptResponse(request, request_len, data, data_len, deadline)) {
        StoreMessage(ecu, data, data_len);
      } else if (deadline != 0) {
        wait_ms = kResponsePendingTimeoutMs;  // ECU asked for more time (NRC 0x78)
      }
    }
  }
  return message_count_;
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

  const uint8_t request[2] = {0x01, pid};
  if (Request(request, sizeof(request), /*wait_for_all=*/false, timeout_ms) == 0) {
    return false;
  }
  const ObdMessage& msg = messages_[0];
  const uint8_t payload_len = msg.len > 2 ? msg.len - 2 : 0;
  const uint8_t copy_len = payload_len > kPidMaxResponseBytes ? kPidMaxResponseBytes : payload_len;
  memcpy(out_payload, &msg.data[2], copy_len);
  out_payload_len = copy_len;
  return copy_len > 0;
}

void ObdTransport::ClearSupportedPids() {
  memset(supported_pids_, 0, sizeof(supported_pids_));
  supported_pids_known_ = false;
}

void ObdTransport::MergeSupportedBlock(uint8_t base_pid) {
  uint32_t& block = supported_pids_[base_pid / 0x20];
  for (uint8_t i = 0; i < message_count_; ++i) {
    const ObdMessage& msg = messages_[i];
    if (msg.len < 6) {
      continue;
    }
    block |= (static_cast<uint32_t>(msg.data[2]) << 24) | (static_cast<uint32_t>(msg.data[3]) << 16) |
             (static_cast<uint32_t>(msg.data[4]) << 8) | msg.data[5];
  }
}

bool ObdTransport::DiscoverSupportedPids() {
  ClearSupportedPids();
  for (uint16_t base = 0x00; base <= 0xE0; base += 0x20) {
    const uint8_t request[2] = {0x01, static_cast<uint8_t>(base)};
    // Every ECU answers for itself, so use the full window and merge.
    if (Request(request, sizeof(request), /*wait_for_all=*/true) == 0) {
      if (base == 0x00) {
        return false;
      }
      break;
    }
    MergeSupportedBlock(static_cast<uint8_t>(base));
    // Bit 0 of each block says whether the next block (base + 0x20) exists.
    if ((supported_pids_[base / 0x20] & 0x01) == 0) {
      break;
    }
  }
  supported_pids_known_ = true;
  return true;
}

bool ObdTransport::SupportedPidsKnown() const { return supported_pids_known_; }

bool ObdTransport::IsPidSupported(uint8_t pid) const {
  if (pid == 0x00) {
    return true;
  }
  if (!supported_pids_known_) {
    return true;  // unknown: let the request decide
  }
  const uint8_t index = (pid - 1) / 32;
  const uint8_t bit = 31 - ((pid - 1) % 32);
  return ((supported_pids_[index] >> bit) & 0x01) != 0;
}

String ObdTransport::SupportedPidsHex() const {
  String hex;
  char buf[9];
  for (uint32_t block : supported_pids_) {
    snprintf(buf, sizeof(buf), "%08lX", static_cast<unsigned long>(block));
    hex += buf;
  }
  return hex;
}

String ObdTransport::ReadVin() {
  const uint8_t request[2] = {0x09, 0x02};
  const uint8_t count = Request(request, sizeof(request), /*wait_for_all=*/true, IsCan() ? 300 : 0);
  if (count == 0) {
    return String();
  }
  String vin;
  if (IsCan()) {
    // 49 02 <count> <17 ASCII bytes>
    const ObdMessage& msg = messages_[0];
    for (uint8_t i = 3; i < msg.len; ++i) {
      if (isAlphaNumeric(msg.data[i])) {
        vin += static_cast<char>(msg.data[i]);
      }
    }
  } else {
    // K-line: 49 02 <seq> <4 bytes>, several messages, sequence 1..5.
    for (uint8_t seq = 1; seq <= 5; ++seq) {
      for (uint8_t m = 0; m < count; ++m) {
        const ObdMessage& msg = messages_[m];
        if (msg.len < 7 || msg.data[2] != seq) {
          continue;
        }
        for (uint8_t i = 3; i < 7; ++i) {
          if (isAlphaNumeric(msg.data[i])) {
            vin += static_cast<char>(msg.data[i]);
          }
        }
        break;
      }
    }
  }
  if (vin.length() > 17) {
    vin = vin.substring(vin.length() - 17);
  }
  return vin;
}

void ObdTransport::Tick() {
  if (protocol_ == ObdProtocol::kNone) {
    return;
  }
  if (IsCan()) {
    twai_driver_.RecoverIfBusOff();
    return;
  }
  if (IsKLine() && millis() - last_request_ms_ > kKLineKeepAliveMs) {
    if (protocol_ == ObdProtocol::kIso9141) {
      const uint8_t keep_alive[2] = {0x01, 0x00};
      Request(keep_alive, sizeof(keep_alive), /*wait_for_all=*/true);
    } else {
      const uint8_t keep_alive = kKwpTesterPresent;
      Request(&keep_alive, 1, /*wait_for_all=*/true);
    }
  }
}

}  // namespace core
}  // namespace obd
