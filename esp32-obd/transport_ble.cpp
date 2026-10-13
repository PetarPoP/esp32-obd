#include "transport_ble.h"

#include <NimBLEDevice.h>

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

volatile bool g_ble_connected = false;
volatile uint16_t g_mtu = 23;
volatile uint32_t g_last_disconnect_ms = 0;
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

void Notify(NimBLECharacteristic* characteristic, const String& json_payload) {
  if (characteristic == nullptr) {
    return;
  }
  characteristic->setValue(reinterpret_cast<const uint8_t*>(json_payload.c_str()),
                           json_payload.length());
  if (g_ble_connected) {
    characteristic->notify();
  }
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    g_ble_connected = true;
    g_mtu = server->getPeerMTU(info.getConnHandle());
    NimBLEDevice::startAdvertising();
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo&, int) override {
    g_ble_connected = server->getConnectedCount() > 0;
    g_last_disconnect_ms = millis();
    g_mtu = 23;
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { g_mtu = mtu; }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
    const NimBLEAttValue value = characteristic->getValue();
    if (value.length() == 0) {
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
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
    if (g_ota_handler == nullptr) {
      return;
    }
    const NimBLEAttValue value = characteristic->getValue();
    g_ota_handler(value.data(), value.length());
  }
};
}  // namespace

bool TransportBle::Begin() {
  NimBLEDevice::init(kDeviceName);
  // Allow multi-field telemetry JSON in a single notify (default ATT MTU is 23 bytes).
  NimBLEDevice::setMTU(247);
  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  NimBLEService* service = g_server->createService(kServiceUuid);
  g_telemetry_char =
      service->createCharacteristic(kTelemetryUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* command_char =
      service->createCharacteristic(kCommandUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  command_char->setCallbacks(new CommandCallbacks());
  g_response_char =
      service->createCharacteristic(kResponseUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* ota_char =
      service->createCharacteristic(kOtaDataUuid, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE);
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

bool TransportBle::IsConnected() const { return g_ble_connected; }

void TransportBle::SetCommandHandler(CommandHandler handler) { g_command_handler = handler; }

void TransportBle::SetOtaDataHandler(DataHandler handler) { g_ota_handler = handler; }

uint16_t TransportBle::Mtu() const { return g_mtu; }

uint32_t TransportBle::LastDisconnectMs() const { return g_last_disconnect_ms; }

void TransportBle::Shutdown() {
  NimBLEDevice::getAdvertising()->stop();
  NimBLEDevice::deinit(true);
}

}  // namespace app_link
}  // namespace obd
