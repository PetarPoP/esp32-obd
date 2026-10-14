import { PermissionsAndroid, Platform } from "react-native";
import { BleManager, type Characteristic, type Device, type Subscription } from "react-native-ble-plx";
import { decode as b64Decode, encode as b64Encode } from "base-64";

import { BLE_CONFIG } from "./bleConfig";
import type { ObdCommand } from "../types/protocol";

type PayloadListener = (payload: string) => void;
type ConnectionListener = (connected: boolean) => void;

class BleObdClient {
  private manager = new BleManager();
  private device: Device | null = null;
  private subscriptions: Subscription[] = [];
  private disconnectSubscription: Subscription | null = null;
  private payloadListener: PayloadListener | null = null;

  /**
   * Subscribes to the notify characteristics again. Firmware 2.1+ only accepts
   * subscriptions over a paired link, so the first attempt can fail while the
   * user is still typing the PIN.
   */
  resubscribe(): void {
    if (!this.device || !this.payloadListener) return;
    this.subscriptions.forEach((sub) => sub.remove());
    this.subscriptions = [];
    this.subscribeNotifications(this.payloadListener);
  }

  /** Signal strength of the current connection in dBm, or null. */
  async readRssi(): Promise<number | null> {
    if (!this.device) return null;
    try {
      const d = await this.manager.readRSSIForDevice(this.device.id);
      return d.rssi ?? null;
    } catch {
      return null;
    }
  }

  async connect(payloadListener: PayloadListener, connectionListener: ConnectionListener): Promise<void> {
    await this.requestAndroidPermissions();
    connectionListener(false);

    await new Promise<void>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.manager.stopDeviceScan();
        reject(new Error("BLE scan timeout"));
      }, 15000);

      this.manager.startDeviceScan(null, { allowDuplicates: false }, async (error, device) => {
        if (error) {
          clearTimeout(timeout);
          this.manager.stopDeviceScan();
          reject(error);
          return;
        }

        if (!device?.name?.startsWith(BLE_CONFIG.deviceNamePrefix)) {
          return;
        }

        clearTimeout(timeout);
        this.manager.stopDeviceScan();

        try {
          this.device = await device.connect();
          this.device = await this.device.requestMTU(247);
          await this.device.discoverAllServicesAndCharacteristics();
          this.payloadListener = payloadListener;
          this.subscribeNotifications(payloadListener);
          this.disconnectSubscription?.remove();
          this.disconnectSubscription = this.manager.onDeviceDisconnected(this.device.id, () => {
            this.subscriptions.forEach((sub) => sub.remove());
            this.subscriptions = [];
            this.device = null;
            connectionListener(false);
          });
          connectionListener(true);
          resolve();
        } catch (connectError) {
          reject(connectError);
        }
      });
    });
  }

  isConnected(): boolean {
    return this.device !== null;
  }

  /** Negotiated ATT MTU (23 until the MTU request has completed). */
  mtu(): number {
    return this.device?.mtu ?? 23;
  }

  /** Writes one firmware chunk (write without response) to the OTA data characteristic. */
  async writeOtaChunk(bytes: Uint8Array): Promise<void> {
    if (!this.device) {
      throw new Error("not connected");
    }
    let binary = "";
    for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    await this.manager.writeCharacteristicWithoutResponseForDevice(
      this.device.id,
      BLE_CONFIG.serviceUuid,
      BLE_CONFIG.otaDataUuid,
      b64Encode(binary)
    );
  }

  /** Sends one JSON command; `fields` are merged into {"command": ...}. */
  async sendCommand(command: ObdCommand, fields?: Record<string, unknown>): Promise<void> {
    if (!this.device) {
      throw new Error("not connected");
    }
    const payload = b64Encode(JSON.stringify({ command, ...(fields ?? {}) }));
    await this.manager.writeCharacteristicWithResponseForDevice(
      this.device.id,
      BLE_CONFIG.serviceUuid,
      BLE_CONFIG.commandWriteUuid,
      payload
    );
  }

  disconnect(): void {
    this.subscriptions.forEach((sub) => sub.remove());
    this.subscriptions = [];
    this.disconnectSubscription?.remove();
    this.disconnectSubscription = null;
    if (this.device) {
      this.manager.cancelDeviceConnection(this.device.id).catch(() => undefined);
    }
    this.device = null;
  }

  private subscribeNotifications(payloadListener: PayloadListener): void {
    if (!this.device) {
      return;
    }

    const onNotify = (error: Error | null, characteristic: Characteristic | null) => {
      if (error || !characteristic?.value) {
        return;
      }
      try {
        payloadListener(b64Decode(characteristic.value));
      } catch {
        // Ignore malformed payload and keep stream alive.
      }
    };

    this.subscriptions.push(
      this.manager.monitorCharacteristicForDevice(
        this.device.id,
        BLE_CONFIG.serviceUuid,
        BLE_CONFIG.telemetryNotifyUuid,
        onNotify
      )
    );
    this.subscriptions.push(
      this.manager.monitorCharacteristicForDevice(
        this.device.id,
        BLE_CONFIG.serviceUuid,
        BLE_CONFIG.responseNotifyUuid,
        onNotify
      )
    );
  }

  private async requestAndroidPermissions(): Promise<void> {
    if (Platform.OS !== "android") {
      return;
    }
    await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
    ]);
  }
}

export const bleObdClient = new BleObdClient();
