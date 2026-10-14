import { createContext, useContext } from "react";

// Material 3 color roles from the "3a" design (tonal palette from a steel-blue
// seed). Names follow the M3 spec; sc* are the surface-container levels.
export type ColorScheme = {
  dark: boolean;
  primary: string;
  onPrimary: string;
  primaryContainer: string;
  onPrimaryContainer: string;
  secondaryContainer: string;
  onSecondaryContainer: string;
  error: string;
  errorContainer: string;
  onErrorContainer: string;
  surface: string;
  onSurface: string;
  onSurfaceVariant: string;
  outline: string;
  outlineVariant: string;
  scLow: string;
  sc: string;
  scHigh: string;
  scHighest: string;
  inverseSurface: string;
  inverseOnSurface: string;
  scrim: string;
  success: string;
};

export const lightScheme: ColorScheme = {
  dark: false,
  primary: "#36618e",
  onPrimary: "#ffffff",
  primaryContainer: "#d1e4ff",
  onPrimaryContainer: "#001d36",
  secondaryContainer: "#d7e3f7",
  onSecondaryContainer: "#101c2b",
  error: "#ba1a1a",
  errorContainer: "#ffdad6",
  onErrorContainer: "#410002",
  surface: "#f8f9ff",
  onSurface: "#191c20",
  onSurfaceVariant: "#43474e",
  outline: "#73777f",
  outlineVariant: "#c3c7cf",
  scLow: "#f2f3fa",
  sc: "#eceef4",
  scHigh: "#e6e8ee",
  scHighest: "#e0e2e8",
  inverseSurface: "#2e3135",
  inverseOnSurface: "#eff0f7",
  scrim: "rgba(0,0,0,0.32)",
  success: "#2e6b30",
};

export const darkScheme: ColorScheme = {
  dark: true,
  primary: "#a0cafd",
  onPrimary: "#003258",
  primaryContainer: "#194975",
  onPrimaryContainer: "#d1e4ff",
  secondaryContainer: "#3b4858",
  onSecondaryContainer: "#d7e3f7",
  error: "#ffb4ab",
  errorContainer: "#93000a",
  onErrorContainer: "#ffdad6",
  surface: "#111418",
  onSurface: "#e1e2e8",
  onSurfaceVariant: "#c3c7cf",
  outline: "#8d9199",
  outlineVariant: "#43474e",
  scLow: "#191c20",
  sc: "#1d2024",
  scHigh: "#282a2f",
  scHighest: "#32353a",
  inverseSurface: "#e1e2e8",
  inverseOnSurface: "#2e3135",
  scrim: "rgba(0,0,0,0.5)",
  success: "#93d692",
};

/** M3 type scale (system Roboto on Android). */
export const type = {
  headline: { fontSize: 24, lineHeight: 32, fontWeight: "400" as const },
  titleLarge: { fontSize: 22, lineHeight: 28, fontWeight: "400" as const },
  titleMedium: { fontSize: 16, lineHeight: 22, fontWeight: "500" as const },
  titleSmall: { fontSize: 14, lineHeight: 20, fontWeight: "500" as const },
  body: { fontSize: 16, lineHeight: 24, fontWeight: "400" as const },
  bodySmall: { fontSize: 14, lineHeight: 20, fontWeight: "400" as const },
  label: { fontSize: 12, lineHeight: 16, fontWeight: "500" as const },
  labelSmall: { fontSize: 12, lineHeight: 16, fontWeight: "400" as const },
  mono: { fontSize: 11, lineHeight: 16, fontFamily: "monospace" },
};

export const ThemeContext = createContext<ColorScheme>(darkScheme);

export function useColors(): ColorScheme {
  return useContext(ThemeContext);
}
