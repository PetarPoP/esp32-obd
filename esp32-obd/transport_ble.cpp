#include "transport_ble.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>

namespace obd {
namespace app_link {

namespace {
constexpr char kDeviceName[] = "OBD-ESP32";
constexpr char kServiceUuid[] = "6f0a0001-8f7f-4c0c-9c2c-44d4d818a001";
constexpr char kTelemetryUuid[] = "6f0a0002-8f7f-4c0c-9c2c-44d4d818a001";
constexpr char kCommandUuid[] = "6f0a0003-8f7f-4c0c-9c2c-44d4d818a001";
constexpr char kResponseUuid[] = "6f0a0004-8f7f-4c0c-9c2c-44d4d818a001";

bool g_ble_connected = false;
BLECharacteristic* g_telemetry_char = nullptr;
BLECharacteristic* g_response_char = nullptr;
BLECharacteristic* g_command_char = nullptr;
TransportBle::CommandHandler g_command_handler = nullptr;

// Every command the firmware understands. Order matters only for the
// substring fallback: names that contain another name come first.
const char* const kKnownCommands[] = {
    "start_stream",      "stop_stream",  "read_dtc",     "clear_dtc",
    "read_freeze_frame", "vehicle_info", "probe_capabilities", "set_link",
    "detect_protocol",   "bus_diag",     "bus_scan",
};

// Value of a "key":"value" string field, or empty.
String JsonStringField(const String& payload, const char* key) {
  const int key_pos = payload.indexOf(key);
  if (key_pos < 0) {
    return String();
  }
  const int colon = payload.indexOf(':', key_pos);
  const int open_quote = colon < 0 ? -1 : payload.indexOf('"', colon);
  const int close_quote = open_quote < 0 ? -1 : payload.indexOf('"', open_quote + 1);
  if (close_quote < 0) {
    return String();
  }
  return payload.substring(open_quote + 1, close_quote);
}

// Finds the command: an explicit "cmd"/"command" field wins; otherwise the
// first known command name that appears anywhere in the payload (the
// original wire format, e.g. a bare "start_stream").
String DetectCommand(const String& payload) {
  const char* const kCommandKeys[] = {"\"cmd\"", "\"command\"", "\"action\"", "\"type\""};
  for (const char* key : kCommandKeys) {
    const String value = JsonStringField(payload, key);
    for (const char* known : kKnownCommands) {
      if (value == known) {
        return value;
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

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* p_server) override {
    g_ble_connected = true;
    p_server->startAdvertising();
  }

  void onDisconnect(BLEServer* p_server) override {
    g_ble_connected = false;
    p_server->startAdvertising();
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    const String cmd = characteristic->getValue();
    if (cmd.length() == 0) {
      return;
    }

    String payload = cmd;
    String command = "unknown";
    String status = "ok";
    String reason = "";

    const String detected = DetectCommand(payload);
    if (detected.length() > 0) {
      command = detected;
    } else {
      status = "error";
      reason = "unsupported_command";
    }
    if (command == "clear_dtc") {
      status = "locked";
      reason = "safety_lock";
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

    if (g_response_char != nullptr) {
      g_response_char->setValue(ack.c_str());
      g_response_char->notify();
    }
  }
};
}  // namespace

bool TransportBle::Begin() {
  BLEDevice::init(kDeviceName);
  // Allow multi-field telemetry JSON in a single notify (default ATT MTU is 23 bytes).
  BLEDevice::setMTU(247);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(kServiceUuid);

  g_telemetry_char = service->createCharacteristic(
      kTelemetryUuid, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  g_telemetry_char->addDescriptor(new BLE2902());

  g_command_char = service->createCharacteristic(
      kCommandUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  g_command_char->setCallbacks(new CommandCallbacks());

  g_response_char = service->createCharacteristic(
      kResponseUuid, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  g_response_char->addDescriptor(new BLE2902());

  service->start();
  server->getAdvertising()->start();
  // The real status and capability snapshots are published by setup().
  return true;
}

void TransportBle::NotifyTelemetry(const String& json_payload) {
  if (g_telemetry_char != nullptr) {
    g_telemetry_char->setValue(json_payload.c_str());
    if (g_ble_connected) {
      g_telemetry_char->notify();
    }
  }
  Serial.println(json_payload);
}

void TransportBle::NotifyResponse(const String& json_payload) {
  if (g_response_char != nullptr) {
    g_response_char->setValue(json_payload.c_str());
    if (g_ble_connected) {
      g_response_char->notify();
    }
  }
  Serial.println(json_payload);
}

bool TransportBle::IsConnected() const { return g_ble_connected; }

void TransportBle::SetCommandHandler(CommandHandler handler) { g_command_handler = handler; }

}  // namespace app_link
}  // namespace obd
