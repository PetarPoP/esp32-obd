import { StyleSheet, View } from "react-native";

import { Button, Card, Dialog, Icon, Sheet, T } from "../components/m3";
import { describeDtc, type DtcLang } from "../data/dtcDescriptions";
import type { ObdMonitor } from "../hooks/useObdMonitor";
import { useColors } from "../theme/theme";
import type { ValueDef } from "../dashboard/dashboard";

type Kind = "stored" | "pending" | "permanent";

const KIND_TEXT: Record<Kind, { en: string; hr: string; note: { en: string; hr: string } }> = {
  stored: {
    en: "Stored",
    hr: "Spremljen",
    note: { en: "Confirmed fault, usually lights the check-engine lamp", hr: "Potvrđen kvar, obično pali lampicu motora" },
  },
  pending: {
    en: "Pending",
    hr: "Na čekanju",
    note: { en: "Seen once, not confirmed yet", hr: "Viđen jednom, još nije potvrđen" },
  },
  permanent: {
    en: "Permanent",
    hr: "Trajni",
    note: { en: "Clears itself only after the car verifies the repair", hr: "Briše se tek kad auto potvrdi popravak" },
  },
};

export function CodesSheet({
  visible,
  onClose,
  m,
  lang,
  lookup,
}: {
  visible: boolean;
  onClose: () => void;
  m: ObdMonitor;
  lang: DtcLang;
  lookup: (key: string) => ValueDef | undefined;
}) {
  const c = useColors();
  const rows: { code: string; kind: Kind }[] = [];
  if (m.dtc) {
    m.dtc.codes.forEach((code) => rows.push({ code, kind: "stored" }));
    m.dtc.pending?.forEach((code) => rows.push({ code, kind: "pending" }));
    m.dtc.permanent?.forEach((code) => rows.push({ code, kind: "permanent" }));
  }
  const hasCodes = rows.length > 0;
  const hr = lang === "hr";

  let note: string | null = null;
  if (!m.ready) note = hr ? "Spoji adapter za čitanje kodova." : "Connect the adapter to read codes.";
  else if (!m.dtc) note = hr ? "Kodovi još nisu pročitani." : "Codes have not been read yet.";
  else if (m.dtc.status && m.dtc.status !== "ok") note = hr ? `Auto ne odgovara (${m.dtc.status}).` : `The car does not answer (${m.dtc.status}).`;
  else if (!hasCodes) note = hr ? "Nema spremljenih kodova kvara." : "No trouble codes stored.";

  const freeze = m.freezeFrame;

  return (
    <Sheet
      visible={visible}
      title={hr ? "Kodovi kvara" : "Trouble codes"}
      onClose={onClose}
      footer={
        <>
          {hasCodes ? <Button kind="outlined" label={hr ? "Obriši kodove" : "Clear codes"} onPress={m.requestClearDtc} disabled={!m.ready} /> : null}
          <Button icon="refresh" label={m.dtcReading ? (hr ? "Čitam…" : "Reading…") : m.dtc ? (hr ? "Čitaj ponovno" : "Scan again") : hr ? "Čitaj kodove" : "Read codes"}
            onPress={m.readDtc} disabled={!m.ready || m.dtcReading} />
        </>
      }
    >
      <View style={{ gap: 2 }}>
        {rows.map(({ code, kind }) => {
          const d = describeDtc(code, lang);
          const severe = kind !== "pending";
          return (
            <View key={`${kind}-${code}`} style={styles.codeRow}>
              <View style={[styles.codeIcon, { backgroundColor: severe ? c.errorContainer : c.secondaryContainer }]}>
                <Icon name={severe ? "error" : "info"} size={22} color={severe ? c.onErrorContainer : c.onSecondaryContainer} />
              </View>
              <View style={{ flex: 1, minWidth: 0, gap: 2 }}>
                <T variant="mono" color={c.onSurfaceVariant} style={{ fontSize: 12, letterSpacing: 0.5 }}>
                  {code} · {hr ? KIND_TEXT[kind].hr : KIND_TEXT[kind].en}
                </T>
                <T>{d.description}</T>
                <T variant="bodySmall" color={c.onSurfaceVariant}>
                  {[d.system, hr ? KIND_TEXT[kind].note.hr : KIND_TEXT[kind].note.en].filter(Boolean).join(" · ")}
                </T>
              </View>
            </View>
          );
        })}
      </View>
      {note ? <T variant="bodySmall" color={c.onSurfaceVariant} style={{ paddingHorizontal: 8 }}>{note}</T> : null}

      <View style={styles.actions}>
        <Button kind="tonal" label={hr ? "Spremnost (readiness)" : "Readiness"} onPress={() => void m.sendCommand("read_readiness")} disabled={!m.ready} />
        <Button kind="tonal" label="Freeze frame" onPress={m.readFreezeFrame} disabled={!m.ready} />
      </View>

      {m.readiness ? (
        <Card level="mid" style={{ gap: 4 }}>
          {m.readiness.status === "ok" ? (
            <>
              <T variant="titleSmall">
                {hr ? "Lampica motora" : "Check-engine lamp"} {m.readiness.mil ? (hr ? "UPALJENA" : "ON") : hr ? "ugašena" : "off"} · {m.readiness.dtc_count} DTC
              </T>
              <T variant="bodySmall" color={c.onSurfaceVariant}>
                {hr ? "Završeno" : "Complete"}: {m.readiness.complete?.join(", ") || "—"}
              </T>
              <T variant="bodySmall" color={m.readiness.incomplete?.length ? c.error : c.onSurfaceVariant}>
                {hr ? "Nije završeno" : "Incomplete"}: {m.readiness.incomplete?.join(", ") || (hr ? "ništa, spreman za tehnički" : "none, ready for inspection")}
              </T>
            </>
          ) : (
            <T variant="bodySmall" color={c.error}>Readiness: {m.readiness.status}</T>
          )}
        </Card>
      ) : null}

      {freeze ? (
        <Card level="mid" style={{ gap: 4 }}>
          <T variant="titleSmall">
            Freeze frame {freeze.dtc ? `· ${String(freeze.dtc)} ${describeDtc(String(freeze.dtc), lang).description}` : `(${String(freeze.status)})`}
          </T>
          {Object.entries(freeze)
            .filter(([k]) => k !== "dtc" && k !== "status")
            .map(([k, v]) => {
              const d = lookup(k);
              return (
                <View key={k} style={styles.kv}>
                  <T variant="bodySmall" color={c.onSurfaceVariant} style={{ flex: 1 }}>{d?.label ?? k}</T>
                  <T variant="titleSmall">{String(v)}{d?.unit ? ` ${d.unit}` : ""}</T>
                </View>
              );
            })}
        </Card>
      ) : null}

      {/* Inside the sheet so it always stacks above it. */}
      <Dialog
        visible={m.clearConfirm !== null}
        icon="delete-sweep"
        title={hr ? "Obrisati sve kodove?" : "Clear all codes?"}
        text={
          hr
            ? `Lampica motora se gasi, a readiness monitori se resetiraju (prije tehničkog treba cijeli ciklus vožnje). Kodovi se vraćaju ako kvar i dalje postoji. Motor ugašen, kontakt uključen. Potvrdi u ${m.clearConfirm?.expires ?? 30} s.`
            : `The check-engine light turns off and readiness monitors reset (a full drive cycle is needed before an inspection). Codes return if the fault is still there. Engine off, ignition on. Confirm within ${m.clearConfirm?.expires ?? 30} s.`
        }
        onDismiss={m.cancelClearDtc}
        actions={[
          { label: hr ? "Odustani" : "Cancel", onPress: m.cancelClearDtc },
          { label: hr ? "Obriši" : "Clear", onPress: m.confirmClearDtc },
        ]}
      />
  </Sheet>
  );
}

const styles = StyleSheet.create({
  codeRow: { flexDirection: "row", gap: 16, paddingVertical: 12, paddingHorizontal: 8, alignItems: "flex-start" },
  codeIcon: { width: 40, height: 40, borderRadius: 20, alignItems: "center", justifyContent: "center" },
  actions: { flexDirection: "row", gap: 8, flexWrap: "wrap" },
  kv: { flexDirection: "row", gap: 12 },
});
