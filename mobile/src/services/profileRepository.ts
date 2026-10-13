import { APP_CONFIG } from "../config";
import profileIndex from "../data/profile-index.json";
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

export function flattenSignals(profile: VehicleProfileData): ExtSignalSelection[] {
  const out: ExtSignalSelection[] = [];
  for (const command of profile.commands) {
    for (const signal of command.signals) {
      out.push({
        key: signal.k,
        name: signal.n || signal.k,
        unit: signal.u ?? "",
        path: signal.path ?? "",
        command,
        signal,
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

/** Builds the ext_add command body for one signal. */
export function extAddBody(selection: ExtSignalSelection): Record<string, unknown> {
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
export function yearsLabel(command: ProfileCommand): string {
  const y = command.years;
  if (!y) return "";
  if (y.years && y.years.length) return y.years.join(", ");
  if (y.from && y.to) return `${y.from}–${y.to}`;
  if (y.from) return `${y.from}+`;
  if (y.to) return `≤${y.to}`;
  return "";
}
