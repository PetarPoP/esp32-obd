import { APP_CONFIG } from "../config";
import profileIndex from "../data/profile-index.json";
import type { StoredExtSignal } from "../types/vehicles";
import { gunzip, utf8Decode } from "../utils/gunzip";

/** One entry of profiles/index.json. */
export type ProfileIndexEntry = {
  make: string;
  model: string;
  file: string;
  commands: number;
  signals: number;
};

/** A manufacturer signal inside a profile command (see tools/obdb_to_profiles.py). */
export type ProfileSignal = {
  k: string;
  n: string;
  u: string;
  path?: string;
  b: number;
  l: number;
  m?: number;
  v?: number;
  a?: number;
  sg?: number;
  le?: number;
  mn?: number;
  mx?: number;
  nmin?: number;
  nmax?: number;
  map?: Record<string, string>;
  x?: number;
};

export type ProfileCommand = {
  tx: string;
  rx: string;
  svc: string;
  did: string;
  eax?: string;
  tst?: string;
  din?: string;
  years?: { from?: number; to?: number; years?: number[] };
  freq: number;
  signals: ProfileSignal[];
};

export type VehicleProfileData = {
  make: string;
  model: string;
  source: string;
  license: string;
  commands: ProfileCommand[];
};

/** A signal flattened with the command it belongs to, ready for ext_add. */
export type ExtSignalSelection = {
  key: string;
  name: string;
  unit: string;
  path: string;
  command: ProfileCommand;
  signal: ProfileSignal;
};

const INDEX = profileIndex as ProfileIndexEntry[];
const cache = new Map<string, VehicleProfileData>();

export function listMakes(): string[] {
  return Array.from(new Set(INDEX.map((e) => e.make))).sort((a, b) => a.localeCompare(b));
}

export function listModels(make: string): ProfileIndexEntry[] {
  return INDEX.filter((e) => e.make === make).sort((a, b) => a.model.localeCompare(b.model));
}

export async function loadProfile(entry: ProfileIndexEntry): Promise<VehicleProfileData> {
  const cached = cache.get(entry.file);
  if (cached) return cached;
  const url = APP_CONFIG.profileBaseUrl + entry.file.split("/").map(encodeURIComponent).join("/");
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`profile download failed (${response.status})`);
  }
  const bytes = new Uint8Array(await response.arrayBuffer());
  const profile = JSON.parse(utf8Decode(gunzip(bytes))) as VehicleProfileData;
  cache.set(entry.file, profile);
  return profile;
}

// OBDb unit names -> what the app shows.
const UNITS: Record<string, string> = {
  scalar: "", unknown: "", hex: "", volts: "V", millivolts: "mV", celsius: "°C", fahrenheit: "°F", percent: "%",
  degrees: "°", rpm: "rpm", kilometers: "km", miles: "mi", kilometersPerHour: "km/h", milesPerHour: "mph",
  newtonMeters: "Nm", milliseconds: "ms", seconds: "s", minutes: "min", hours: "h", bars: "bar", kilopascal: "kPa",
  psi: "psi", amps: "A", milliamps: "mA", millimeters: "mm", centimeters: "cm", meters: "m", liters: "L",
  gallons: "gal", ampereHours: "Ah", metersPerSecondSquared: "m/s²", metersPerSecond: "m/s", kilowattHours: "kWh",
  kilowatts: "kW", watts: "W", gramsPerSecond: "g/s", gravity: "g", ohms: "Ω", milliohms: "mΩ", kiloohms: "kΩ",
  megaohms: "MΩ", hertz: "Hz", milligramsPerStroke: "mg/stroke", gramsPerLiter: "g/L", kilogramsPerHour: "kg/h",
  kilowattHoursPer100Kilometers: "kWh/100km", kilowattHoursPer100Miles: "kWh/100mi", radians: "rad",
  litersPerHour: "L/h",
};

// Two-state units get readable values when the profile has no map of its own.
const BOOLEAN_MAPS: Record<string, Record<string, string>> = {
  offon: { "0": "Off", "1": "On" },
  onoff: { "0": "On", "1": "Off" },
  noyes: { "0": "No", "1": "Yes" },
  yesno: { "0": "Yes", "1": "No" },
};

export function booleanMap(unit: string | undefined): Record<string, string> | undefined {
  return unit ? BOOLEAN_MAPS[unit] : undefined;
}

export function displayUnit(unit: string | undefined): string {
  if (!unit) return "";
  if (unit in BOOLEAN_MAPS) return "";
  return UNITS[unit] ?? unit;
}

export function flattenSignals(profile: VehicleProfileData): ExtSignalSelection[] {
  const out: ExtSignalSelection[] = [];
  for (const command of profile.commands) {
    for (const signal of command.signals) {
      const map = signal.map ?? BOOLEAN_MAPS[signal.u];
      out.push({
        key: signal.k,
        name: signal.n || signal.k,
        unit: displayUnit(signal.u),
        path: signal.path ?? "",
        command,
        signal: map && !signal.map ? { ...signal, map } : signal,
      });
    }
  }
  return out;
}

/** Same shortening as ExtPidService::Add so telemetry keys can be matched. */
export function firmwareKey(key: string): string {
  const lower = key.toLowerCase();
  if (lower.length <= 39) return lower;
  let hash = 2166136261;
  for (let i = 0; i < lower.length; i++) {
    hash = Math.imul(hash ^ (lower.charCodeAt(i) & 0xff), 16777619) >>> 0;
  }
  return `${lower.slice(0, 30)}_${hash.toString(16).padStart(8, "0")}`;
}

/** A selection without the command's other signals, small enough to keep in storage. */
export function toStoredSignal(selection: ExtSignalSelection): StoredExtSignal {
  const { signals: _signals, ...command } = selection.command;
  return { key: selection.key, name: selection.name, unit: selection.unit, path: selection.path, command, signal: selection.signal };
}

/** Builds the ext_add command body for one signal. */
export function extAddBody(selection: { command: Omit<ProfileCommand, "signals">; signal: ProfileSignal }): Record<string, unknown> {
  const { command, signal } = selection;
  const body: Record<string, unknown> = {
    k: signal.k,
    h: command.tx,
    r: command.rx,
    s: command.svc,
    d: command.did,
    b: signal.b,
    l: signal.l,
  };
  if (command.eax) body.e = command.eax;
  if (command.tst) body.t = command.tst;
  if (command.din) body.din = command.din;
  for (const key of ["m", "v", "a", "sg", "le", "mn", "mx", "nmin", "nmax", "x"] as const) {
    if (signal[key] !== undefined) body[key] = signal[key];
  }
  return body;
}

/** Human readable model-year filter, e.g. "2019–2022". */
export function yearsLabel(command: Pick<ProfileCommand, "years">): string {
  const y = command.years;
  if (!y) return "";
  if (y.years && y.years.length) return y.years.join(", ");
  if (y.from && y.to) return `${y.from}–${y.to}`;
  if (y.from) return `${y.from}+`;
  if (y.to) return `≤${y.to}`;
  return "";
}
