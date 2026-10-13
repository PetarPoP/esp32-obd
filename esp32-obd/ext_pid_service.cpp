#include "ext_pid_service.h"

#include <Preferences.h>
#include <math.h>

namespace obd {
namespace services {

namespace {

constexpr char kNvsNamespace[] = "extpid";
constexpr uint32_t kStoreVersion = 2;
constexpr uint32_t kSaveDelayMs = 2000;
constexpr uint32_t kRequestTimeoutMs = 300;
constexpr size_t kMaxListChars = 180;

uint32_t ParseHex(const String& text, bool& ok) {
  ok = text.length() > 0 && text.length() <= 8;
  uint32_t value = 0;
  for (size_t i = 0; ok && i < text.length(); ++i) {
    const char c = text.charAt(i);
    value <<= 4;
    if (c >= '0' && c <= '9') {
      value |= c - '0';
    } else if (c >= 'a' && c <= 'f') {
      value |= c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      value |= c - 'A' + 10;
    } else {
      ok = false;
    }
  }
  return value;
}

}  // namespace

ExtPidService::ExtPidService(core::ObdTransport& transport, app_link::TransportBle& app_link)
    : transport_(transport), app_link_(app_link) {}

void ExtPidService::Begin() {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
    return;
  }
  const uint32_t version = prefs.getUInt("ver", 0);
  const size_t stored = prefs.getUInt("n", 0);
  if (version == kStoreVersion && stored > 0 && stored <= kMaxSignals &&
      prefs.getBytesLength("sigs") == stored * sizeof(Signal)) {
    prefs.getBytes("sigs", signals_, stored * sizeof(Signal));
    count_ = stored;
  }
  prefs.end();
}

void ExtPidService::Save() {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    return;
  }
  prefs.putUInt("ver", kStoreVersion);
  prefs.putUInt("n", count_);
  if (count_ > 0) {
    prefs.putBytes("sigs", signals_, count_ * sizeof(Signal));
  } else {
    prefs.remove("sigs");
  }
  prefs.end();
  dirty_ = false;
}

void ExtPidService::Tick() {
  if (dirty_ && millis() - last_change_ms_ > kSaveDelayMs) {
    Save();
  }
}

size_t ExtPidService::Count() const { return count_; }

void ExtPidService::Clear() {
  count_ = 0;
  cursor_ = 0;
  memset(polled_, 0, sizeof(polled_));
  Save();
}

bool ExtPidService::Add(const util::JsonObject& json, String& error) {
  const String key = json.GetString("k");
  const String tx_text = json.GetString("h");
  const String svc_text = json.GetString("s", "22");
  const String did_text = json.GetString("d");
  bool ok_tx = false, ok_rx = true, ok_svc = false, ok_did = false;
  Signal s = {};
  s.tx = ParseHex(tx_text, ok_tx);
  s.svc = static_cast<uint8_t>(ParseHex(svc_text, ok_svc));
  s.did = static_cast<uint16_t>(ParseHex(did_text, ok_did));
  if (key.length() == 0 || !ok_tx || !ok_svc || !ok_did || (s.svc != 0x21 && s.svc != 0x22)) {
    error = "bad_fields";
    return false;
  }
  if (tx_text.length() > 3) {
    s.flags |= kExtended29;
  }
  const String rx_text = json.GetString("r");
  if (rx_text.length() > 0) {
    s.rx = ParseHex(rx_text, ok_rx);
  } else {
    s.rx = (s.flags & kExtended29) ? ((s.tx & 0xFFFF0000u) | ((s.tx & 0xFF) << 8) | ((s.tx >> 8) & 0xFF))
                                   : s.tx + 8;
  }
  if (!ok_rx) {
    error = "bad_rx";
    return false;
  }
  bool ok = true;
  const String eax = json.GetString("e");
  if (eax.length() > 0) {
    s.ext_addr = static_cast<uint8_t>(ParseHex(eax, ok));
    s.flags |= kHasExtAddr;
  }
  s.tester = static_cast<uint8_t>(ParseHex(json.GetString("t", "F1"), ok));
  const String session = json.GetString("din");
  if (session.length() > 0) {
    s.session = static_cast<uint8_t>(ParseHex(session, ok));
  }
  const double bix = json.GetNumber("b", 0);
  const double len = json.GetNumber("l", 8);
  if (!ok || bix < 0 || bix > 0xFFFF || len < 1 || len > 32) {
    error = "bad_format";
    return false;
  }
  s.bix = static_cast<uint16_t>(bix);
  s.len = static_cast<uint8_t>(len);
  s.mul = static_cast<float>(json.GetNumber("m", 1));
  s.div = static_cast<float>(json.GetNumber("v", 1));
  s.add = static_cast<float>(json.GetNumber("a", 0));
  if (s.div == 0.0f) {
    s.div = 1.0f;
  }
  if (json.GetBool("sg", false)) s.flags |= kSigned;
  if (json.GetBool("le", false)) s.flags |= kLittleEndian;
  if (json.Has("mn") && json.Has("mx")) {
    s.min = static_cast<float>(json.GetNumber("mn", 0));
    s.max = static_cast<float>(json.GetNumber("mx", 0));
    if (s.max > s.min) s.flags |= kClamp;
  }
  if (json.Has("nmin")) {
    s.null_min = static_cast<float>(json.GetNumber("nmin", 0));
    s.flags |= kHasNullMin;
  }
  if (json.Has("nmax")) {
    s.null_max = static_cast<float>(json.GetNumber("nmax", 0));
    s.flags |= kHasNullMax;
  }
  s.decimals = static_cast<uint8_t>(constrain(json.GetNumber("x", 0), 0, 4));

  String lower = key;
  lower.toLowerCase();
  if (lower.length() > kMaxKeyChars) {
    // FNV-1a keeps shortened keys unique (OBDb ids reach 80 characters).
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < lower.length(); ++i) {
      hash = (hash ^ static_cast<uint8_t>(lower.charAt(i))) * 16777619u;
    }
    char suffix[10];
    snprintf(suffix, sizeof(suffix), "_%08lx", static_cast<unsigned long>(hash));
    lower = lower.substring(0, kMaxKeyChars - 9) + suffix;
  }
  strncpy(s.key, lower.c_str(), sizeof(s.key) - 1);

  // Replace a signal with the same key, otherwise append.
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(signals_[i].key, s.key) == 0) {
      signals_[i] = s;
      dirty_ = true;
      last_change_ms_ = millis();
      return true;
    }
  }
  if (count_ >= kMaxSignals) {
    error = "full";
    return false;
  }
  signals_[count_++] = s;
  dirty_ = true;
  last_change_ms_ = millis();
  return true;
}

void ExtPidService::PublishList() {
  String list;
  uint8_t part = 0;
  auto flush = [&](bool last) {
    String payload = "{\"type\":\"ext_list\",\"count\":";
    payload += String(count_);
    payload += ",\"max\":";
    payload += String(kMaxSignals);
    payload += ",\"part\":";
    payload += String(++part);
    payload += ",\"last\":";
    payload += last ? "true" : "false";
    payload += ",\"keys\":[";
    payload += list;
    payload += "],\"ts\":";
    payload += String(millis());
    payload += "}";
    app_link_.NotifyResponse(payload);
    list = "";
  };
  for (size_t i = 0; i < count_; ++i) {
    if (list.length() + strlen(signals_[i].key) + 3 > kMaxListChars) {
      flush(false);
    }
    list += (list.length() > 0 ? ",\"" : "\"");
    list += signals_[i].key;
    list += "\"";
  }
  flush(true);
}

bool ExtPidService::SameRequest(const Signal& a, const Signal& b) const {
  return a.tx == b.tx && a.rx == b.rx && a.svc == b.svc && a.did == b.did &&
         a.session == b.session && ((a.flags ^ b.flags) & (kExtended29 | kHasExtAddr)) == 0 &&
         a.ext_addr == b.ext_addr;
}

bool ExtPidService::DecodeSignal(const Signal& s, const uint8_t* payload, size_t payload_len,
                                 float& out_value) {
  const uint32_t start_bit = s.bix;
  const uint32_t end_bit = start_bit + s.len;
  if (s.len == 0 || s.len > 32 || end_bit > payload_len * 8) {
    return false;
  }
  // Work on a copy of the covering bytes so little-endian values can be reversed.
  const size_t start_byte = start_bit / 8;
  const size_t end_byte = (end_bit + 7) / 8;
  uint8_t bytes[6] = {0};
  const size_t span = end_byte - start_byte;
  if (span > sizeof(bytes)) {
    return false;
  }
  memcpy(bytes, payload + start_byte, span);
  if ((s.flags & kLittleEndian) && s.len > 8) {
    const size_t count = (s.len + 7) / 8;
    for (size_t i = 0; i < count / 2; ++i) {
      const uint8_t t = bytes[i];
      bytes[i] = bytes[count - 1 - i];
      bytes[count - 1 - i] = t;
    }
  }
  uint32_t raw = 0;
  for (uint32_t bit = start_bit; bit < end_bit; ++bit) {
    const uint8_t byte = bytes[bit / 8 - start_byte];
    raw = (raw << 1) | ((byte >> (7 - (bit % 8))) & 0x01);
  }
  double value;
  if ((s.flags & kSigned) && s.len < 32 && (raw & (1u << (s.len - 1)))) {
    value = static_cast<double>(static_cast<int64_t>(raw) - (static_cast<int64_t>(1) << s.len));
  } else if ((s.flags & kSigned) && s.len == 32) {
    value = static_cast<double>(static_cast<int32_t>(raw));
  } else {
    value = static_cast<double>(raw);
  }
  value = value * s.mul / s.div + s.add;
  if (s.flags & kClamp) {
    value = value < s.min ? s.min : (value > s.max ? s.max : value);
  }
  const double epsilon = 1e-6;
  if ((s.flags & kHasNullMin) && value <= s.null_min + epsilon) {
    return false;
  }
  if ((s.flags & kHasNullMax) && value >= s.null_max - epsilon) {
    return false;
  }
  out_value = static_cast<float>(value);
  return true;
}

bool ExtPidService::PollNext(FieldSink sink, void* context) {
  while (cursor_ < count_ && polled_[cursor_]) {
    ++cursor_;
  }
  if (cursor_ >= count_) {
    cursor_ = 0;
    memset(polled_, 0, sizeof(polled_));
    return true;
  }
  const Signal& head = signals_[cursor_];
  core::PhysicalAddress address;
  address.tx = head.tx;
  address.rx = head.rx;
  address.extended = (head.flags & kExtended29) != 0;
  address.ext_addr = (head.flags & kHasExtAddr) ? head.ext_addr : -1;
  address.tester = head.tester;

  if (head.session != 0) {
    const uint8_t session[2] = {0x10, head.session};
    transport_.RequestPhysical(address, session, sizeof(session), kRequestTimeoutMs);
  }
  uint8_t request[3] = {head.svc, 0, 0};
  uint8_t request_len = 0;
  if (head.svc == 0x22) {
    request[1] = static_cast<uint8_t>(head.did >> 8);
    request[2] = static_cast<uint8_t>(head.did & 0xFF);
    request_len = 3;
  } else {
    request[1] = static_cast<uint8_t>(head.did & 0xFF);
    request_len = 2;
  }
  const bool answered = transport_.RequestPhysical(address, request, request_len, kRequestTimeoutMs) > 0;
  const core::ObdMessage* msg = answered ? &transport_.Message(0) : nullptr;

  for (size_t i = cursor_; i < count_; ++i) {
    if (polled_[i] || !SameRequest(head, signals_[i])) {
      continue;
    }
    polled_[i] = true;
    if (msg == nullptr || msg->len <= request_len) {
      continue;
    }
    float value = 0.0f;
    if (!DecodeSignal(signals_[i], &msg->data[request_len], msg->len - request_len, value)) {
      continue;
    }
    String text;
    if (signals_[i].decimals == 0) {
      text = String(static_cast<long>(lroundf(value)));
    } else {
      text = String(value, static_cast<unsigned int>(signals_[i].decimals));
    }
    sink(context, signals_[i].key, text);
  }
  return false;
}

}  // namespace services
}  // namespace obd
