#include "transport_ble.h"

#include <NimBLEDevice.h>
#include <Preferences.h>

#include "json_lite.h"

namespace obd {
namespace app_link {

namespace {
constexpr char kDeviceName[] = "OBD-ESP32";
constexpr char kServiceUuid[] = "6f0a0001-8f7f-4c0c-9c2c-44d4d818a001";
constexpr char kTelemetryUuid[] = "6f0a0002-8f7f-4c0c-9c2c-44d4d818a001";
constexpr char kCommandUuid[] = "6f0a0003-8f7f-4c0c-9c2c-44d4d818a001";
constexpr char kResponseUuid[] = "6f0a0004-8f7f-4c0c-9c2c-44d4d818a001";
constexpr char kOtaDataUuid[] = "6f0a0005-8f7f-4c0c-9c2c-44d4d818a001";
constexpr char kNvsNamespace[] = "ble";

volatile bool g_ble_connected = false;
// True once the current connection is encrypted and authenticated (always
// true when pairing is not required).
volatile bool g_ble_secure = false;
volatile uint16_t g_mtu = 23;
volatile uint32_t g_last_disconnect_ms = 0;
uint32_t g_passkey = TransportBle::kDefaultPasskey;
NimBLEServer* g_server = nullptr;
NimBLECharacteristic* g_telemetry_char = nullptr;
NimBLECharacteristic* g_response_char = nullptr;
TransportBle::CommandHandler g_command_handler = nullptr;
TransportBle::DataHandler g_ota_handler = nullptr;

// Every command the firmware understands. Order matters only for the
// substring fallback: names that contain another name come first.
const char* const kKnownCommands[] = {
    "start_stream",       "stop_stream",  "read_dtc",           "clear_dtc",
    "read_freeze_frame",  "vehicle_info", "probe_capabilities", "set_link",
    "detect_protocol",    "bus_diag",     "bus_scan",           "read_readiness",
    "read_monitor_tests", "ext_add",      "ext_clear",          "ext_list",
    "ota_begin",          "ota_end",      "ota_abort",          "set_power",
    "set_pin",            "forget_bonds", "device_info",
};

// Finds the command: an explicit "cmd"/"command"/"action"/"type" field wins;
// otherwise the first known command name that appears anywhere in the
// payload (the original wire format, e.g. a bare "start_stream").
String DetectCommand(const String& payload) {
  const util::JsonObject json(payload);
  if (json.IsValid()) {
    const char* const kCommandKeys[] = {"cmd", "command", "action", "type"};
    for (const char* key : kCommandKeys) {
      const String value = json.GetString(key);
      for (const char* known : kKnownCommands) {
        if (value == known) {
          return value;
        }
      }
    }
  }
  for (const char* known : kKnownCommands) {
    if (payload.indexOf(known) >= 0) {
      return known;
    }
  }
  return String();
}

bool IsSecure(const NimBLEConnInfo& info) {
  return !TransportBle::kRequirePairing || (info.isEncrypted() && info.isAuthenticated());
}

void Notify(NimBLECharacteristic* characteristic, const String& json_payload) {
  if (characteristic == nullptr) {
    return;
  }
  characteristic->setValue(reinterpret_cast<const uint8_t*>(json_payload.c_str()),
                           json_payload.length());
  if (g_ble_connected && g_ble_secure) {
    characteristic->notify();
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    g_ble_connected = true;
    g_ble_secure = IsSecure(info);
    g_mtu = server->getPeerMTU(info.getConnHandle());
    if (!g_ble_secure) {
      // Ask the phone to pair (or to restore its bond) right away.
      NimBLEDevice::startSecurity(info.getConnHandle());
    }
    NimBLEDevice::startAdvertising();
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo&, int) override {
    g_ble_connected = server->getConnectedCount() > 0;
    if (!g_ble_connected) {
      g_ble_secure = !TransportBle::kRequirePairing;
    }
    g_last_disconnect_ms = millis();
    g_mtu = 23;
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { g_mtu = mtu; }

  uint32_t onPassKeyDisplay() override { return g_passkey; }

  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (IsSecure(info)) {
      g_ble_secure = true;
      return;
    }
    // Wrong PIN, or a phone that tried to pair without MITM protection.
    Serial.println("BLE pairing failed, disconnecting");
    g_server->disconnect(info.getConnHandle());
  }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    const NimBLEAttValue value = characteristic->getValue();
    if (value.length() == 0 || !IsSecure(info)) {
      return;
    }
    String payload;
    payload.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
      payload += static_cast<char>(value.data()[i]);
    }
    String command = DetectCommand(payload);
    String status = "ok";
    String reason;
    if (command.length() == 0) {
      command = "unknown";
      status = "error";
      reason = "unsupported_command";
    }

    if (g_command_handler != nullptr) {
      g_command_handler(command, payload);
    }

    String ack = "{\"type\":\"ack\",\"command\":\"";
    ack += command;
    ack += "\",\"status\":\"";
    ack += status;
    ack += "\"";
    if (reason.length() > 0) {
      ack += ",\"reason\":\"";
      ack += reason;
      ack += "\"";
    }
    ack += ",\"ts\":";
    ack += String(millis());
    ack += "}";
    Notify(g_response_char, ack);
  }
};

class OtaDataCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
    if (g_ota_handler == nullptr || !IsSecure(info)) {
      return;
    }
    const NimBLEAttValue value = characteristic->getValue();
    g_ota_handler(value.data(), value.length());
  }
};
}  // namespace

bool TransportBle::Begin() {
  {
    Preferences prefs;
    if (prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
      g_passkey = prefs.getUInt("pin", kDefaultPasskey);
      prefs.end();
    }
  }
  g_ble_secure = !kRequirePairing;

  NimBLEDevice::init(kDeviceName);
  // Allow multi-field telemetry JSON in a single notify (default ATT MTU is 23 bytes).
  NimBLEDevice::setMTU(247);

  uint32_t notify_props = NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY;
  uint32_t write_props = NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR;
  if (kRequirePairing) {
    // Bonding + MITM + LE Secure Connections. The ESP "displays" the PIN, so
    // the phone has to type it in; there is no Just Works fallback.
    NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/true, /*sc=*/true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(g_passkey);
    notify_props |= NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN;
    write_props |= NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN;
  }

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = g_server->createService(kServiceUuid);
  g_telemetry_char = service->createCharacteristic(kTelemetryUuid, notify_props);
  NimBLECharacteristic* command_char = service->createCharacteristic(kCommandUuid, write_props);
  command_char->setCallbacks(new CommandCallbacks());
  g_response_char = service->createCharacteristic(kResponseUuid, notify_props);
  NimBLECharacteristic* ota_char = service->createCharacteristic(kOtaDataUuid, write_props);
  ota_char->setCallbacks(new OtaDataCallbacks());
  // NimBLE starts the services together with the server when advertising starts.

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(kDeviceName);
  advertising->addServiceUUID(kServiceUuid);
  advertising->enableScanResponse(true);
  advertising->start();
  // The real status and capability snapshots are published by setup().
  return true;
}

void TransportBle::NotifyTelemetry(const String& json_payload) {
  Notify(g_telemetry_char, json_payload);
  Serial.println(json_payload);
}

void TransportBle::NotifyResponse(const String& json_payload) {
  Notify(g_response_char, json_payload);
  Serial.println(json_payload);
}

bool TransportBle::IsConnected() const { return g_ble_connected && g_ble_secure; }

void TransportBle::SetCommandHandler(CommandHandler handler) { g_command_handler = handler; }

void TransportBle::SetOtaDataHandler(DataHandler handler) { g_ota_handler = handler; }

uint16_t TransportBle::Mtu() const { return g_mtu; }

uint32_t TransportBle::LastDisconnectMs() const { return g_last_disconnect_ms; }

bool TransportBle::SetPasskey(uint32_t passkey) {
  if (passkey > 999999) {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    return false;
  }
  const bool ok = prefs.putUInt("pin", passkey) == sizeof(uint32_t);
  prefs.end();
  if (ok) {
    g_passkey = passkey;
    NimBLEDevice::setSecurityPasskey(passkey);
  }
  return ok;
}

bool TransportBle::PasskeyIsDefault() const { return g_passkey == kDefaultPasskey; }

void TransportBle::ForgetBonds() { NimBLEDevice::deleteAllBonds(); }

int TransportBle::BondCount() const { return NimBLEDevice::getNumBonds(); }

void TransportBle::Shutdown() {
  NimBLEDevice::getAdvertising()->stop();
  NimBLEDevice::deinit(true);
}

}  // namespace app_link
}  // namespace obd
