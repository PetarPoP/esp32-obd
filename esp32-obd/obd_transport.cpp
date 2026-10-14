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

constexpr uint8_t kNegativeResponse = 0x7F;
constexpr uint8_t kNrcResponsePending = 0x78;
constexpr uint8_t kPositiveResponseOffset = 0x40;

// Response windows (ms). ISO 15765-4 allows ECUs 50 ms (P2) to answer.
constexpr uint32_t kCanDefaultTimeoutMs = 150;
constexpr uint32_t kCanMultiFrameTimeoutMs = 1000;
constexpr uint32_t kCanConsecutiveFrameTimeoutMs = 200;
constexpr uint32_t kResponsePendingTimeoutMs = 5000;  // P2* max
constexpr uint32_t kCanListenWindowMs = 250;
constexpr uint32_t kCanFirstRequestTimeoutMs = 300;

bool IsExtendedCan(ObdProtocol p) {
  return p == ObdProtocol::kCan29_500 || p == ObdProtocol::kCan29_250;
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

bool IsCanProtocol(ObdProtocol p) {
  return p == ObdProtocol::kCan11_500 || p == ObdProtocol::kCan29_500 ||
         p == ObdProtocol::kCan11_250 || p == ObdProtocol::kCan29_250 ||
         p == ObdProtocol::kCan11Custom;
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

ObdProtocol ObdTransport::Protocol() const { return protocol_; }

const char* ObdTransport::ProtocolIdString() const { return ObdProtocolId(protocol_); }

bool ObdTransport::IsConnected() const { return protocol_ != ObdProtocol::kNone; }

bool ObdTransport::IsCan() const { return IsCanProtocol(protocol_); }

uint16_t ObdTransport::ConsecutiveFailures() const { return consecutive_failures_; }

uint8_t ObdTransport::MessageCount() const { return message_count_; }

const ObdMessage& ObdTransport::Message(uint8_t index) const {
  return messages_[index < kObdMaxMessages ? index : 0];
}

uint8_t ObdTransport::LastNegativeResponseCode() const { return last_nrc_; }

void ObdTransport::Disconnect() {
  protocol_ = ObdProtocol::kNone;
  ClearSupportedPids();
}

void ObdTransport::UseCanUnverified() {
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
  probe_counters_ = drivers::TwaiDriver::Counters();

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
  //    Many cars keep the OBD port silent until asked, so a baud without
  //    traffic is still probed when no baud had any.
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
      if (Connect(candidate, probe.frames > 0)) {
        return protocol_;
      }
    }
  }

  // Nothing answered. If CAN frames were heard at a baud, that baud is right:
  // stay active there and send requests without verification (the behaviour of
  // set_link without "protocol"), so a car that answers real PID requests but
  // not our probe still works. Otherwise leave the controller passive.
  uint32_t best_baud = 0;
  uint32_t best_frames = 0;
  for (const BaudProbe& probe : bauds) {
    if (probe.frames > best_frames) {
      best_frames = probe.frames;
      best_baud = probe.baud_kbps;
    }
  }
  if (best_frames > 0 && twai_driver_.BeginAt(best_baud, /*listen_only=*/false)) {
    Begin();
    UseCanUnverified();
    return ObdProtocol::kNone;  // not verified, but IsConnected() is true
  }
  twai_driver_.BeginAt(500, /*listen_only=*/true);
  return ObdProtocol::kNone;
}

bool ObdTransport::Connect(ObdProtocol protocol, bool baud_has_traffic) {
  Disconnect();
  consecutive_failures_ = 0;
  if (diagnostic_profile_ != DiagnosticProfile::kObdMode01 || !IsCanProtocol(protocol) ||
      protocol == ObdProtocol::kCan11Custom) {
    return false;
  }
  const bool is_500 = protocol == ObdProtocol::kCan11_500 || protocol == ObdProtocol::kCan29_500;
  if (!twai_driver_.BeginAt(is_500 ? 500 : 250, /*listen_only=*/false)) {
    return false;
  }
  Begin();
  protocol_ = protocol;
  probing_ = !baud_has_traffic;
  // Some ECUs need well over 150 ms for the first request after ignition on,
  // and a busy bus can make a single try lose arbitration: ask a few times.
  const bool ok = VerifyWithPid00(baud_has_traffic ? 3 : 2, kCanFirstRequestTimeoutMs);
  probing_ = false;
  if (!ok) {
    drivers::TwaiDriver::Counters c;
    if (twai_driver_.ReadCounters(c)) {
      probe_counters_.tx_error = c.tx_error > probe_counters_.tx_error ? c.tx_error : probe_counters_.tx_error;
      probe_counters_.tx_failed = c.tx_failed > probe_counters_.tx_failed ? c.tx_failed : probe_counters_.tx_failed;
      probe_counters_.bus_error = c.bus_error > probe_counters_.bus_error ? c.bus_error : probe_counters_.bus_error;
      probe_counters_.arb_lost = c.arb_lost > probe_counters_.arb_lost ? c.arb_lost : probe_counters_.arb_lost;
      probe_counters_.bus_off = probe_counters_.bus_off || c.bus_off;
    }
    twai_driver_.RecoverIfBusOff();
    protocol_ = ObdProtocol::kNone;
    return false;
  }
  DiscoverSupportedPids();
  return true;
}

bool ObdTransport::VerifyWithPid00(uint8_t attempts, uint32_t timeout_ms) {
  const uint8_t request[2] = {0x01, 0x00};
  for (uint8_t i = 0; i < attempts; ++i) {
    if (Request(request, sizeof(request), /*wait_for_all=*/true, timeout_ms) > 0) {
      return true;
    }
    delay(40);
  }
  return false;
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
  // Modes 01, 02 and 09 echo the PID; make sure the answer is for one of ours.
  // A multi-PID Mode 01 request may be answered in any order or partially.
  const uint8_t mode = request[0];
  // Services 21 (1-byte local identifier) and 22 (2-byte DID) echo the identifier.
  if (mode == 0x22 && request_len >= 3) {
    return len >= 3 && data[1] == request[1] && data[2] == request[2];
  }
  if (mode == 0x21 && request_len >= 2) {
    return len >= 2 && data[1] == request[1];
  }
  if ((mode == 0x01 || mode == 0x02 || mode == 0x09) && request_len >= 2) {
    if (len < 2) {
      return false;
    }
    const uint8_t pid_count = (mode == 0x01) ? request_len - 1 : 1;
    for (uint8_t i = 0; i < pid_count; ++i) {
      if (data[1] == request[1 + i]) {
        return true;
      }
    }
    return false;
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

uint8_t ObdTransport::Request(const uint8_t* request, uint8_t request_len, bool wait_for_all,
                              uint32_t timeout_ms, CompletionCallback done, void* done_context) {
  Addressing addressing;
  addressing.extended = IsExtendedCan(protocol_);
  addressing.functional = true;
  addressing.tx = addressing.extended ? kCan29FunctionalId : kCan11FunctionalId;
  return RequestImpl(addressing, request, request_len, wait_for_all, timeout_ms, done, done_context);
}

uint8_t ObdTransport::RequestPhysical(const PhysicalAddress& address, const uint8_t* request,
                                      uint8_t request_len, uint32_t timeout_ms) {
  Addressing addressing;
  addressing.extended = address.extended;
  addressing.functional = false;
  addressing.tx = address.tx;
  addressing.rx = address.rx;
  addressing.ext_addr = address.ext_addr;
  addressing.tester = address.tester;
  return RequestImpl(addressing, request, request_len, /*wait_for_all=*/false, timeout_ms, nullptr,
                     nullptr);
}

uint8_t ObdTransport::RequestImpl(const Addressing& addressing, const uint8_t* request,
                                  uint8_t request_len, bool wait_for_all, uint32_t timeout_ms,
                                  CompletionCallback done, void* done_context) {
  message_count_ = 0;
  last_nrc_ = 0;
  // With an extended address byte in front, a single frame carries 6 bytes.
  const uint8_t o = addressing.ext_addr >= 0 ? 1 : 0;
  if (request == nullptr || request_len == 0 || request_len > 7 - o ||
      protocol_ == ObdProtocol::kNone || diagnostic_profile_ != DiagnosticProfile::kObdMode01) {
    return 0;  // multi-frame requests are never needed for diagnostic reads
  }
  if (timeout_ms == 0) {
    timeout_ms = kCanDefaultTimeoutMs;
  }
  const bool extended = addressing.extended;

  // Drop frames left over from earlier traffic so they cannot be mistaken for answers.
  drivers::CanFrame frame;
  for (uint8_t i = 0; i < 32 && twai_driver_.ReadFrame(frame, 0); ++i) {
  }

  uint8_t sf[8];
  memset(sf, kCanPadding, sizeof(sf));
  if (o) {
    sf[0] = static_cast<uint8_t>(addressing.ext_addr);
  }
  sf[o] = request_len;
  memcpy(&sf[o + 1], request, request_len);
  if (!twai_driver_.SendFrame(addressing.tx, sf, sizeof(sf), 50, extended, probing_)) {
    if (addressing.functional && consecutive_failures_ < 0xFFFF) {
      ++consecutive_failures_;
    }
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

  auto finished = [&]() {
    return message_count_ >= kObdMaxMessages || !wait_for_all ||
           (done != nullptr && done(*this, done_context));
  };

  uint32_t deadline = millis() + timeout_ms;
  bool complete = false;
  while (!complete && IsTimeBefore(millis(), deadline)) {
    if (!twai_driver_.ReadFrame(frame, 5)) {
      continue;
    }
    if (frame.extended != extended || frame.dlc < 2 + o) {
      continue;
    }
    uint8_t ecu = 0;
    uint32_t flow_control_id = 0;
    if (!addressing.functional) {
      if (frame.id != addressing.rx) {
        continue;
      }
      if (o && frame.data[0] != addressing.tester) {
        continue;
      }
      ecu = frame.id & 0xFF;
      flow_control_id = addressing.tx;
    } else if (extended) {
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

    const uint8_t* d = &frame.data[o];
    const uint8_t dlc = frame.dlc - o;
    const uint8_t pci_type = d[0] >> 4;
    if (pci_type == kPciSingle) {
      const uint8_t len = d[0] & 0x0F;
      if (len == 0 || len > dlc - 1) {
        continue;
      }
      if (AcceptResponse(request, request_len, &d[1], len, deadline)) {
        StoreMessage(ecu, &d[1], len);
        complete = finished();
      }
    } else if (pci_type == kPciFirst) {
      if (dlc < 3) {
        continue;
      }
      RxSlot* slot = nullptr;
      for (RxSlot& candidate : slots) {
        if (candidate.active && candidate.id == frame.id) {
          slot = &candidate;  // the ECU restarted its answer
          break;
        }
      }
      for (RxSlot& candidate : slots) {
        if (slot == nullptr && !candidate.active) {
          slot = &candidate;
        }
      }
      if (slot == nullptr) {
        continue;
      }
      slot->id = frame.id;
      slot->expected = (static_cast<uint16_t>(d[0] & 0x0F) << 8) | d[1];
      slot->received = 0;
      slot->next_sn = 1;
      slot->active = true;
      for (uint8_t i = 2; i < dlc; ++i) {
        if (slot->received < kObdMessageMaxLen) {
          slot->buf[slot->received] = d[i];
        }
        ++slot->received;
      }
      // Flow control: continue to send, no block limit, no separation time.
      uint8_t fc[8];
      memset(fc, kCanPadding, sizeof(fc));
      if (o) {
        fc[0] = static_cast<uint8_t>(addressing.ext_addr);
      }
      fc[o] = 0x30;
      fc[o + 1] = 0x00;
      fc[o + 2] = 0x00;
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
        if ((d[0] & 0x0F) != slot.next_sn) {
          slot.active = false;  // lost a frame; drop this ECU's answer
          break;
        }
        slot.next_sn = (slot.next_sn + 1) & 0x0F;
        for (uint8_t i = 1; i < dlc && slot.received < slot.expected; ++i) {
          if (slot.received < kObdMessageMaxLen) {
            slot.buf[slot.received] = d[i];
          }
          ++slot.received;
        }
        if (slot.received >= slot.expected) {
          slot.active = false;
          const uint8_t len =
              slot.expected > kObdMessageMaxLen ? kObdMessageMaxLen : static_cast<uint8_t>(slot.expected);
          if (AcceptResponse(request, request_len, slot.buf, len, deadline)) {
            StoreMessage(ecu, slot.buf, len);
            complete = finished();
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

  // Only standard OBD requests feed link health; an unsupported
  // manufacturer DID must not look like a lost vehicle.
  if (addressing.functional) {
    if (message_count_ == 0) {
      if (consecutive_failures_ < 0xFFFF) {
        ++consecutive_failures_;
      }
    } else {
      consecutive_failures_ = 0;
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

void ObdTransport::Tick() {
  if (IsCan()) {
    twai_driver_.RecoverIfBusOff();
  }
}

}  // namespace core
}  // namespace obd
