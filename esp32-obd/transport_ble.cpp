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

    if (payload.indexOf("start_stream") >= 0) {
      command = "start_stream";
    } else if (payload.indexOf("stop_stream") >= 0) {
      command = "stop_stream";
    } else if (payload.indexOf("read_dtc") >= 0) {
      command = "read_dtc";
    } else if (payload.indexOf("clear_dtc") >= 0) {
      command = "clear_dtc";
      status = "locked";
      reason = "safety_lock";
    } else if (payload.indexOf("probe_capabilities") >= 0) {
      command = "probe_capabilities";
    } else if (payload.indexOf("set_link") >= 0) {
      command = "set_link";
    } else if (payload.indexOf("bus_diag") >= 0) {
      command = "bus_diag";
    } else if (payload.indexOf("bus_scan") >= 0) {
      command = "bus_scan";
    } else if (payload.indexOf("detect_protocol") >= 0) {
      command = "detect_protocol";
    } else {
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

  NotifyResponse("{\"type\":\"status\",\"connected\":true,\"mode\":\"listen_only\","
                 "\"signal\":-55,\"stream_active\":true,\"ts\":" +
                 String(millis()) + "}");
  NotifyResponse("{\"type\":\"capability\",\"supported_metrics\":[\"rpm\","
                 "\"coolant_c\",\"throttle_pct\",\"speed_kmh\"],\"missing_metrics\":[],"
                 "\"confirmed_from_vehicle\":false,"
                 "\"ts\":" +
                 String(millis()) + "}");
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
