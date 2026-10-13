export const BLE_CONFIG = {
  deviceNamePrefix: "OBD-ESP32",
  serviceUuid: "6f0a0001-8f7f-4c0c-9c2c-44d4d818a001",
  telemetryNotifyUuid: "6f0a0002-8f7f-4c0c-9c2c-44d4d818a001",
  commandWriteUuid: "6f0a0003-8f7f-4c0c-9c2c-44d4d818a001",
  responseNotifyUuid: "6f0a0004-8f7f-4c0c-9c2c-44d4d818a001",
  otaDataUuid: "6f0a0005-8f7f-4c0c-9c2c-44d4d818a001",
} as const;
