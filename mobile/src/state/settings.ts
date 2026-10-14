import AsyncStorage from "@react-native-async-storage/async-storage";
import { useCallback, useEffect, useRef, useState } from "react";

import type { DashboardCard } from "../dashboard/dashboard";
import type { DtcLang } from "../data/dtcDescriptions";
import {
  defaultValues,
  vehicleKey,
  type VehicleSelection,
  type VehicleValues,
} from "../types/vehicles";

export type ThemePreference = "system" | "light" | "dark";

export type AppSettings = {
  theme: ThemePreference;
  developer: boolean;
  dtcLang: DtcLang;
  vehicle: VehicleSelection;
  /** Values chosen per vehicle (vehicleKey -> values). */
  values: Record<string, VehicleValues>;
  /** Dashboard per vehicle; missing means the generated default. */
  dashboards: Record<string, DashboardCard[]>;
  /** False until the user has chosen values once for the current vehicle. */
  valuesChosen: Record<string, boolean>;
};

const STORAGE_KEY = "obd-monitor/settings/v1";

const DEFAULTS: AppSettings = {
  theme: "system",
  developer: false,
  dtcLang: "hr",
  vehicle: { kind: "generic" },
  values: {},
  dashboards: {},
  valuesChosen: {},
};

/** Persistent app settings (AsyncStorage), loaded once at start. */
export function useSettings() {
  const [settings, setSettings] = useState<AppSettings>(DEFAULTS);
  const [loaded, setLoaded] = useState(false);
  const latest = useRef(settings);

  useEffect(() => {
    AsyncStorage.getItem(STORAGE_KEY)
      .then((raw) => {
        if (raw) {
          const parsed = { ...DEFAULTS, ...(JSON.parse(raw) as Partial<AppSettings>) };
          latest.current = parsed;
          setSettings(parsed);
        }
      })
      .catch(() => undefined)
      .finally(() => setLoaded(true));
  }, []);

  const update = useCallback((patch: Partial<AppSettings> | ((prev: AppSettings) => Partial<AppSettings>)) => {
    const next = { ...latest.current, ...(typeof patch === "function" ? patch(latest.current) : patch) };
    latest.current = next;
    setSettings(next);
    AsyncStorage.setItem(STORAGE_KEY, JSON.stringify(next)).catch(() => undefined);
  }, []);

  const key = vehicleKey(settings.vehicle);
  const values = settings.values[key] ?? defaultValues(settings.vehicle);

  const setValues = useCallback(
    (v: VehicleValues) =>
      update((prev) => {
        const k = vehicleKey(prev.vehicle);
        return { values: { ...prev.values, [k]: v }, valuesChosen: { ...prev.valuesChosen, [k]: true } };
      }),
    [update]
  );

  const setDashboard = useCallback(
    (cards: DashboardCard[] | null) =>
      update((prev) => {
        const k = vehicleKey(prev.vehicle);
        const dashboards = { ...prev.dashboards };
        if (cards) dashboards[k] = cards;
        else delete dashboards[k];
        return { dashboards };
      }),
    [update]
  );

  return {
    settings,
    loaded,
    update,
    vehicleKey: key,
    values,
    valuesChosen: settings.valuesChosen[key] ?? false,
    dashboard: settings.dashboards[key] ?? null,
    setValues,
    setDashboard,
  };
}

export type SettingsStore = ReturnType<typeof useSettings>;
