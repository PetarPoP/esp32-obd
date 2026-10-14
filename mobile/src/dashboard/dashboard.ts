import type { IconName } from "../components/m3";
import { booleanMap, displayUnit, firmwareKey } from "../services/profileRepository";
import { catalogueFor, type VehicleSelection, type VehicleValues } from "../types/vehicles";

/** One value the dashboard can show: a standard metric or a manufacturer signal. */
export type ValueDef = {
  /** Key in telemetry messages. */
  key: string;
  label: string;
  unit: string;
  /** Text for enumerated values (manufacturer signals). */
  map?: Record<string, string>;
  manufacturer: boolean;
};

export type DashboardCard =
  | { id: string; type: "dial"; arc: string; center: string; footer: string[] }
  | { id: string; type: "status" }
  | { id: string; type: "chart"; metric: string }
  | { id: string; type: "tiles"; metrics: string[] };

export type CardType = DashboardCard["type"];

export const CARD_TYPES: { type: CardType; label: string; description: string; icon: IconName }[] = [
  { type: "dial", label: "Dial", description: "Gauge with a big number in the middle and three values below", icon: "speed" },
  { type: "status", label: "Link · Codes · Bus", description: "Connection, trouble codes and protocol", icon: "dashboard" },
  { type: "chart", label: "Chart", description: "One value as a trace over the last 30 seconds", icon: "show-chart" },
  { type: "tiles", label: "Tiles", description: "Several values as numbers, two per row", icon: "grid-view" },
];

/** Gauge ranges for common values; anything else scales to what it has seen. */
const RANGES: Record<string, [number, number]> = {
  rpm: [0, 7000],
  speed_kmh: [0, 240],
  coolant_c: [-40, 130],
  oil_temp_c: [-40, 150],
  intake_air_c: [-40, 80],
  throttle_pct: [0, 100],
  throttle_valve_pct: [0, 100],
  engine_load_pct: [0, 100],
  abs_load_pct: [0, 100],
  fuel_level_pct: [0, 100],
  map_kpa: [0, 255],
  boost_kpa: [0, 300],
  maf_gs: [0, 300],
  module_voltage_v: [10, 16],
  timing_advance_deg: [-20, 60],
  soc_pct: [0, 100],
  power_kw: [-30, 60],
  hv_amps: [-150, 300],
  lv_v: [10, 16],
};

/** Red zone start as a fraction of the range, where it makes sense. */
const REDLINE: Record<string, number> = { rpm: 6000 / 7000, coolant_c: 115 / 170, oil_temp_c: 130 / 190 };

export function valueRange(key: string): { min: number; max: number; red?: number } | null {
  const r = RANGES[key];
  return r ? { min: r[0], max: r[1], red: REDLINE[key] } : null;
}

/** All values the user selected for this vehicle, in catalogue order. */
export function selectedValueDefs(vehicle: VehicleSelection, values: VehicleValues): ValueDef[] {
  const catalogue = catalogueFor(vehicle);
  const standard = catalogue
    .filter((m) => values.metrics.includes(m.id))
    .map((m) => ({ key: m.id, label: m.label, unit: m.unit, manufacturer: false }));
  const ext = values.ext.map((s) => ({
    key: firmwareKey(s.key),
    label: s.name,
    unit: displayUnit(s.signal.u),
    map: s.signal.map ?? booleanMap(s.signal.u),
    manufacturer: true,
  }));
  return [...standard, ...ext];
}

let idCounter = 0;
export function newCardId(): string {
  idCounter += 1;
  return `c${Date.now().toString(36)}${idCounter}`;
}

/** The dashboard a vehicle starts with, built from the values the user picked. */
export function defaultDashboard(defs: ValueDef[]): DashboardCard[] {
  const keys = defs.map((d) => d.key);
  const has = (k: string) => keys.includes(k);
  const take = (...candidates: string[]) => candidates.find(has);
  const cards: DashboardCard[] = [];
  const used = new Set<string>();

  const arc = take("rpm", "soc_pct", "power_kw", "engine_load_pct") ?? keys[0];
  const center = take("speed_kmh", "soc_pct", "range_km") ?? keys.find((k) => k !== arc) ?? arc;
  if (arc) {
    const footer = ["coolant_c", "throttle_pct", "module_voltage_v", "hv_v", "temp_max_c", "lv_v", "intake_air_c"]
      .filter((k) => has(k) && k !== arc && k !== center)
      .slice(0, 3);
    cards.push({ id: newCardId(), type: "dial", arc, center, footer });
    [arc, center, ...footer].forEach((k) => used.add(k));
  }
  cards.push({ id: newCardId(), type: "status" });
  const chart = take("engine_load_pct", "power_kw", "maf_gs", "hv_amps");
  if (chart) {
    cards.push({ id: newCardId(), type: "chart", metric: chart });
    used.add(chart);
  }
  const rest = keys.filter((k) => !used.has(k));
  if (rest.length > 0) cards.push({ id: newCardId(), type: "tiles", metrics: rest });
  return cards;
}

/** Drops values that are no longer selected; removes cards left empty. */
export function pruneDashboard(cards: DashboardCard[], defs: ValueDef[]): DashboardCard[] {
  const keys = new Set(defs.map((d) => d.key));
  const out: DashboardCard[] = [];
  for (const card of cards) {
    switch (card.type) {
      case "status":
        out.push(card);
        break;
      case "chart":
        if (keys.has(card.metric)) out.push(card);
        break;
      case "tiles": {
        const metrics = card.metrics.filter((k) => keys.has(k));
        if (metrics.length > 0) out.push({ ...card, metrics });
        break;
      }
      case "dial":
        if (keys.has(card.arc) || keys.has(card.center)) {
          out.push({ ...card, footer: card.footer.filter((k) => keys.has(k)) });
        }
        break;
    }
  }
  return out;
}

/** Short names for the three values under the dial. */
const SHORT_LABELS: Record<string, string> = {
  coolant_c: "Coolant",
  throttle_pct: "Throttle",
  module_voltage_v: "Battery",
  engine_load_pct: "Load",
  intake_air_c: "Intake air",
  oil_temp_c: "Oil",
  fuel_level_pct: "Fuel",
  speed_kmh: "Speed",
  rpm: "RPM",
  maf_gs: "MAF",
  map_kpa: "MAP",
  hv_v: "HV battery",
  lv_v: "12 V",
  temp_max_c: "Battery max",
  soc_pct: "SoC",
};

export function shortLabel(def: ValueDef | undefined, key: string): string {
  return SHORT_LABELS[key] ?? def?.label ?? key;
}

export function formatNumber(value: number): string {
  if (Number.isInteger(value)) return String(value);
  const abs = Math.abs(value);
  return value.toFixed(abs >= 100 ? 0 : abs >= 10 ? 1 : 2);
}

export function formatValue(def: ValueDef | undefined, value: number | undefined): string {
  if (value === undefined) return "—";
  const mapped = def?.map?.[String(value)];
  return mapped ?? formatNumber(value);
}
