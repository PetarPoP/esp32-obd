import { APP_CONFIG } from "../config";

/** firmware/version.json, written by tools/publish_firmware.py. */
export type FirmwareInfo = {
  version: string;
  file: string;
  size: number;
  md5: string;
};

export async function fetchFirmwareInfo(): Promise<FirmwareInfo> {
  const response = await fetch(`${APP_CONFIG.firmwareBaseUrl}version.json`, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`version.json not found (${response.status})`);
  }
  const info = (await response.json()) as FirmwareInfo;
  if (!info.version || !info.file || !info.size || !/^[0-9a-f]{32}$/.test(info.md5)) {
    throw new Error("version.json is malformed");
  }
  return info;
}

export async function downloadFirmware(info: FirmwareInfo): Promise<Uint8Array> {
  const response = await fetch(`${APP_CONFIG.firmwareBaseUrl}${encodeURIComponent(info.file)}`, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`firmware download failed (${response.status})`);
  }
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (bytes.length !== info.size) {
    throw new Error(`firmware size mismatch (${bytes.length} of ${info.size} bytes)`);
  }
  // 0xE9 is the magic byte of every ESP32 application image.
  if (bytes[0] !== 0xe9) {
    throw new Error("downloaded file is not an ESP32 firmware image");
  }
  return bytes;
}

/** True when `latest` is a higher dotted version than `installed` (e.g. 2.1.0 > 2.0.0). */
export function isNewerVersion(latest: string, installed: string | undefined): boolean {
  if (!installed) return true;
  const a = latest.split(".").map((n) => parseInt(n, 10) || 0);
  const b = installed.split(".").map((n) => parseInt(n, 10) || 0);
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    const x = a[i] ?? 0;
    const y = b[i] ?? 0;
    if (x !== y) return x > y;
  }
  return false;
}
