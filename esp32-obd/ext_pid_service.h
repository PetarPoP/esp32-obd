#pragma once

#include <Arduino.h>

#include "json_lite.h"
#include "obd_transport.h"
#include "transport_ble.h"

namespace obd {
namespace services {

// Manufacturer-specific parameters (UDS service 22 DIDs and KWP service 21
// local identifiers) that the app loads per vehicle, e.g. from the OBDb
// profiles in profiles/. Signals are kept in flash (NVS) so they survive a
// reboot, grouped by request, and polled after the standard Mode 01 PIDs.
//
// Bit extraction follows OBDb: `bix` counts bits from the first payload byte
// after the service and identifier echo, MSB first; little-endian values
// have their bytes reversed first.
class ExtPidService {
 public:
  static constexpr size_t kMaxSignals = 96;
  // Longer keys are shortened to 30 chars + "_" + 8 hex digits of a hash.
  static constexpr size_t kMaxKeyChars = 39;

  struct Signal {
    char key[kMaxKeyChars + 1];
    uint32_t tx;
    uint32_t rx;
    uint16_t did;
    uint16_t bix;
    uint8_t svc;
    uint8_t len;
    uint8_t flags;
    uint8_t ext_addr;
    uint8_t tester;
    uint8_t session;  // diagnostic session to enter first (0 = none)
    uint8_t decimals;
    uint8_t reserved;
    float mul;
    float div;
    float add;
    float min;
    float max;
    float null_min;
    float null_max;
  };

  enum SignalFlags : uint8_t {
    kExtended29 = 1 << 0,
    kSigned = 1 << 1,
    kLittleEndian = 1 << 2,
    kHasExtAddr = 1 << 3,
    kClamp = 1 << 4,
    kHasNullMin = 1 << 5,
    kHasNullMax = 1 << 6,
  };

  using FieldSink = void (*)(void* context, const char* key, const String& value);

  ExtPidService(core::ObdTransport& transport, app_link::TransportBle& app_link);

  // Loads the stored signals from flash.
  void Begin();

  // Parses one "ext_add" command. Returns false and fills `error` if invalid.
  bool Add(const util::JsonObject& json, String& error);
  void Clear();
  size_t Count() const;
  // Publishes the loaded keys as "ext_list" messages (split for the MTU).
  void PublishList();

  // Saves pending changes to flash once additions have settled.
  void Tick();

  // Requests the next signal group and passes every decoded value to `sink`.
  // Returns true when the rotation over all groups has completed.
  bool PollNext(FieldSink sink, void* context);

  // Decodes one signal from the payload after the service/identifier echo.
  // Exposed for tests. Returns false when there is no valid value.
  static bool DecodeSignal(const Signal& signal, const uint8_t* payload, size_t payload_len,
                           float& out_value);

 private:
  bool SameRequest(const Signal& a, const Signal& b) const;
  void Save();

  core::ObdTransport& transport_;
  app_link::TransportBle& app_link_;
  Signal signals_[kMaxSignals];
  size_t count_ = 0;
  bool polled_[kMaxSignals] = {false};
  size_t cursor_ = 0;
  bool dirty_ = false;
  uint32_t last_change_ms_ = 0;
};

}  // namespace services
}  // namespace obd
