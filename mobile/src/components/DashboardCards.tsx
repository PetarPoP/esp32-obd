import { Pressable, StyleSheet, View, useWindowDimensions } from "react-native";
import Svg, { Circle, Path, Text as SvgText } from "react-native-svg";

import { formatNumber, formatValue, shortLabel, valueRange, type ValueDef } from "../dashboard/dashboard";
import { HISTORY_WINDOW_MS, type HistoryPoint } from "../hooks/useObdMonitor";
import type { TelemetryValues } from "../types/protocol";
import { useColors } from "../theme/theme";
import { Icon, T, type IconName } from "./m3";

type Lookup = (key: string) => ValueDef | undefined;

// Dial geometry from the design: r = 116 in a 300×300 box, 270° sweep starting at 135°.
const R = 116;
const CIRC = 2 * Math.PI * R;
const SWEEP = CIRC * 0.75;

function dialLabels(key: string, min: number, max: number): { text: string; frac: number }[] {
  if (key === "rpm") {
    return Array.from({ length: 8 }, (_, i) => ({ text: String(i), frac: i / 7 }));
  }
  return [0, 0.25, 0.5, 0.75, 1].map((f) => ({ text: formatNumber(Math.round(min + (max - min) * f)), frac: f }));
}

export function DialCard({
  arc,
  center,
  footer,
  telemetry,
  history,
  lookup,
}: {
  arc: string;
  center: string;
  footer: string[];
  telemetry: TelemetryValues;
  history: Map<string, HistoryPoint[]>;
  lookup: Lookup;
}) {
  const c = useColors();
  const { width } = useWindowDimensions();
  const size = Math.min(280, width - 64);
  const arcDef = lookup(arc);
  const centerDef = lookup(center);
  const arcValue = telemetry[arc];
  const seen = history.get(arc)?.map((p) => p.v) ?? [];
  const range = valueRange(arc) ?? {
    min: Math.min(0, ...seen),
    max: Math.max(1, ...seen.map((v) => v * 1.2)),
    red: undefined,
  };
  const frac = arcValue === undefined ? 0 : Math.min(1, Math.max(0, (arcValue - range.min) / (range.max - range.min)));
  const labelR = 84;

  return (
    <View style={[styles.hero, { backgroundColor: c.sc }]}>
      <View style={{ width: size, height: size * 0.9 }}>
        <Svg viewBox="0 0 300 300" width={size} height={size} style={StyleSheet.absoluteFill}>
          <Circle cx={150} cy={150} r={R} fill="none" stroke={c.scHighest} strokeWidth={18} strokeLinecap="round"
            strokeDasharray={`${SWEEP} ${CIRC}`} transform="rotate(135 150 150)" />
          {range.red !== undefined ? (
            <Circle cx={150} cy={150} r={R} fill="none" stroke={c.error} strokeWidth={18} strokeLinecap="round" opacity={0.28}
              strokeDasharray={`${SWEEP * (1 - range.red)} ${CIRC}`} strokeDashoffset={-SWEEP * range.red}
              transform="rotate(135 150 150)" />
          ) : null}
          {arcValue !== undefined ? (
            <Circle cx={150} cy={150} r={R} fill="none" stroke={c.primary} strokeWidth={18} strokeLinecap="round"
              strokeDasharray={`${Math.max(0.1, SWEEP * frac)} ${CIRC}`} transform="rotate(135 150 150)" />
          ) : null}
          {dialLabels(arc, range.min, range.max).map((l) => {
            const a = ((135 + l.frac * 270) * Math.PI) / 180;
            return (
              <SvgText key={l.text + l.frac} x={150 + labelR * Math.cos(a)} y={155 + labelR * Math.sin(a)}
                textAnchor="middle" fontSize={13} fontWeight="500" fontFamily="sans-serif" fill={c.onSurfaceVariant}>
                {l.text}
              </SvgText>
            );
          })}
        </Svg>
        <View style={styles.heroCenter}>
          <T style={{ fontSize: Math.round(size * 0.27), lineHeight: Math.round(size * 0.29), fontVariant: ["tabular-nums"], letterSpacing: -1 }}
            numberOfLines={1}>
            {formatValue(centerDef, telemetry[center])}
          </T>
          <T variant="label" color={c.onSurfaceVariant} style={{ letterSpacing: 0.5 }}>
            {centerDef?.unit || centerDef?.label || center}
          </T>
          {arc !== center ? (
            <View style={[styles.chip, { backgroundColor: c.secondaryContainer }]}>
              <T variant="titleSmall" color={c.onSecondaryContainer} style={{ fontVariant: ["tabular-nums"] }}>
                {arcValue === undefined
                  ? `— ${arcDef?.unit ?? ""}`
                  : `${arc === "rpm" ? (Math.round(arcValue / 10) * 10).toLocaleString("en-US") : formatValue(arcDef, arcValue)} ${arcDef?.unit ?? ""}`}
              </T>
            </View>
          ) : null}
        </View>
      </View>
      {footer.length > 0 ? (
        <View style={styles.footerRow}>
          {footer.map((k) => {
            const d = lookup(k);
            return (
              <View key={k} style={styles.footerCell}>
                <T variant="label" color={c.onSurfaceVariant} numberOfLines={1}>{shortLabel(d, k)}</T>
                <T variant="titleLarge" numberOfLines={1}>
                  {formatValue(d, telemetry[k])}
                  <T variant="bodySmall" color={c.onSurfaceVariant}>{d?.unit ? ` ${d.unit}` : ""}</T>
                </T>
              </View>
            );
          })}
        </View>
      ) : null}
    </View>
  );
}

function TonalButton({ icon, title, subtitle, bg, fg, onPress }: { icon: IconName; title: string; subtitle: string; bg: string; fg: string; onPress: () => void }) {
  return (
    <Pressable onPress={onPress} style={({ pressed }) => [styles.tonal, { backgroundColor: bg, opacity: pressed ? 0.9 : 1 }]}>
      <Icon name={icon} size={22} color={fg} />
      <T variant="titleMedium" color={fg} numberOfLines={1}>{title}</T>
      <T variant="labelSmall" color={fg} style={{ opacity: 0.8 }} numberOfLines={1}>{subtitle}</T>
    </Pressable>
  );
}

export function StatusRow({
  linkIcon,
  linkTitle,
  linkSubtitle,
  linkActive,
  onLink,
  codeCount,
  codesRead,
  onCodes,
  busTitle,
  busSubtitle,
  onBus,
}: {
  linkIcon: IconName;
  linkTitle: string;
  linkSubtitle: string;
  linkActive: boolean;
  onLink: () => void;
  codeCount: number | null;
  codesRead: boolean;
  onCodes: () => void;
  busTitle: string;
  busSubtitle: string;
  onBus: () => void;
}) {
  const c = useColors();
  const hasCodes = (codeCount ?? 0) > 0;
  return (
    <View style={styles.statusRow}>
      <TonalButton icon={linkIcon} title={linkTitle} subtitle={linkSubtitle}
        bg={linkActive ? c.primaryContainer : c.scLow} fg={linkActive ? c.onPrimaryContainer : c.onSurface} onPress={onLink} />
      <TonalButton icon={hasCodes ? "warning" : codesRead ? "check-circle" : "troubleshoot"}
        title={codeCount === null ? "Codes" : `${codeCount} code${codeCount === 1 ? "" : "s"}`}
        subtitle={codesRead ? "Details" : "Scan"}
        bg={hasCodes ? c.errorContainer : c.scLow} fg={hasCodes ? c.onErrorContainer : c.onSurface} onPress={onCodes} />
      <TonalButton icon="lan" title={busTitle} subtitle={busSubtitle} bg={c.scLow} fg={c.onSurface} onPress={onBus} />
    </View>
  );
}

export function ChartCard({ def, metric, telemetry, history }: { def?: ValueDef; metric: string; telemetry: TelemetryValues; history: Map<string, HistoryPoint[]> }) {
  const c = useColors();
  const points = history.get(metric) ?? [];
  let line = "";
  let area = "";
  if (points.length > 1) {
    const now = points[points.length - 1].t;
    const vs = points.map((p) => p.v);
    const range = valueRange(metric);
    let min = range ? range.min : Math.min(...vs);
    let max = range ? range.max : Math.max(...vs);
    if (max - min < 1e-6) {
      min -= 1;
      max += 1;
    }
    const xy = points.map((p) => {
      const x = 150 - ((now - p.t) / HISTORY_WINDOW_MS) * 150;
      const y = 29 - ((Math.min(max, Math.max(min, p.v)) - min) / (max - min)) * 28;
      return [x, y] as const;
    });
    line = xy.map(([x, y], i) => `${i === 0 ? "M" : "L"}${x.toFixed(1)} ${y.toFixed(1)}`).join(" ");
    area = `${line} L${xy[xy.length - 1][0].toFixed(1)} 30 L${xy[0][0].toFixed(1)} 30 Z`;
  }
  return (
    <View style={[styles.chart, { backgroundColor: c.scLow }]}>
      <View style={styles.chartHead}>
        <T variant="titleSmall" style={{ flex: 1 }} numberOfLines={1}>{def?.label ?? metric}</T>
        <T variant="titleLarge">
          {formatValue(def, telemetry[metric])}
          <T variant="bodySmall" color={c.onSurfaceVariant}>{def?.unit ? ` ${def.unit}` : ""}</T>
        </T>
      </View>
      <Svg viewBox="0 0 150 30" preserveAspectRatio="none" width="100%" height={56}>
        {area ? <Path d={area} fill={c.primary} fillOpacity={0.16} /> : null}
        {line ? <Path d={line} fill="none" stroke={c.primary} strokeWidth={2.5} strokeLinecap="round" strokeLinejoin="round" vectorEffect="non-scaling-stroke" /> : null}
      </Svg>
      <T variant="labelSmall" color={c.onSurfaceVariant}>Last {HISTORY_WINDOW_MS / 1000} seconds</T>
    </View>
  );
}

export function TilesCard({ metrics, telemetry, lookup }: { metrics: string[]; telemetry: TelemetryValues; lookup: Lookup }) {
  const c = useColors();
  return (
    <View style={styles.tiles}>
      {metrics.map((k) => {
        const d = lookup(k);
        return (
          <View key={k} style={[styles.tile, { backgroundColor: c.scLow }]}>
            <T variant="label" color={c.onSurfaceVariant} numberOfLines={2}>{d?.label ?? k}</T>
            <T variant="titleLarge" numberOfLines={1} style={{ fontVariant: ["tabular-nums"] }}>
              {formatValue(d, telemetry[k])}
              <T variant="bodySmall" color={c.onSurfaceVariant}>{d?.unit && !d.map ? ` ${d.unit}` : ""}</T>
            </T>
          </View>
        );
      })}
    </View>
  );
}

const styles = StyleSheet.create({
  hero: { borderRadius: 28, paddingTop: 12, alignItems: "center" },
  heroCenter: { ...StyleSheet.absoluteFillObject, alignItems: "center", justifyContent: "center", paddingTop: 6 },
  chip: { marginTop: 10, height: 32, paddingHorizontal: 14, borderRadius: 16, justifyContent: "center" },
  footerRow: { alignSelf: "stretch", flexDirection: "row", paddingHorizontal: 8, paddingTop: 4, paddingBottom: 16 },
  footerCell: { flex: 1, alignItems: "center", gap: 2 },
  statusRow: { flexDirection: "row", gap: 8 },
  tonal: { flex: 1, minWidth: 0, padding: 12, borderRadius: 16, gap: 6 },
  chart: { borderRadius: 16, paddingHorizontal: 16, paddingTop: 14, paddingBottom: 12, gap: 8 },
  chartHead: { flexDirection: "row", alignItems: "baseline", gap: 8 },
  tiles: { flexDirection: "row", flexWrap: "wrap", gap: 8 },
  tile: { flexBasis: "48%", flexGrow: 1, borderRadius: 16, padding: 12, gap: 4 },
});
