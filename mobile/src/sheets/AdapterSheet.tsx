import { useState } from "react";
import { View } from "react-native";

import { Button, Card, Dialog, Field, Icon, ListRow, ProgressBar, SectionLabel, Segmented, Sheet, T } from "../components/m3";
import type { ObdMonitor } from "../hooks/useObdMonitor";
import { useColors } from "../theme/theme";

const SLEEP_OPTIONS = [
  { value: "0", label: "Never" },
  { value: "300", label: "5 min" },
  { value: "600", label: "10 min" },
  { value: "1800", label: "30 min" },
];

export function AdapterSheet({ visible, onClose, m }: { visible: boolean; onClose: () => void; m: ObdMonitor }) {
  const c = useColors();
  const [pinDialog, setPinDialog] = useState(false);
  const [pin, setPin] = useState("");
  const [pin2, setPin2] = useState("");
  const [forgetDialog, setForgetDialog] = useState(false);
  const d = m.device;
  const busy = ["downloading", "uploading", "installing"].includes(m.firmware.phase);
  const pinValid = /^\d{6}$/.test(pin) && pin === pin2;

  const linkText =
    m.link === "ready"
      ? `Connected${m.rssi !== null ? ` · ${m.rssi} dBm` : ""}`
      : m.link === "pairing"
        ? "Pairing: enter the adapter PIN when the phone asks (factory PIN 123456)"
        : m.link === "connecting"
          ? "Searching for OBD-ESP32…"
          : "Not connected";

  return (
    <Sheet visible={visible} title="Adapter" onClose={onClose}>
      <Card level="mid" style={{ gap: 8 }}>
        <View style={{ flexDirection: "row", alignItems: "center", gap: 12 }}>
          <Icon name={m.ready ? "bluetooth-connected" : m.link === "disconnected" ? "bluetooth-disabled" : "bluetooth-searching"} />
          <T style={{ flex: 1 }}>{linkText}</T>
        </View>
        {d ? (
          <T variant="bodySmall" color={c.onSurfaceVariant}>
            Firmware {d.fw} · MTU {d.mtu} · woke by {d.wake} · manufacturer values {d.ext_pids}/{d.ext_pids_max}
          </T>
        ) : null}
        <View style={{ flexDirection: "row", justifyContent: "flex-end" }}>
          {m.link === "disconnected" ? (
            <Button icon="bluetooth-searching" label="Connect" onPress={() => void m.connect()} />
          ) : (
            <Button kind="outlined" label="Disconnect" onPress={m.disconnect} />
          )}
        </View>
      </Card>

      <SectionLabel>Security</SectionLabel>
      {d?.pairing === false || (d && d.pairing === undefined) ? (
        <T variant="bodySmall" color={c.error} style={{ paddingHorizontal: 8 }}>
          This firmware does not require pairing. Update the firmware below to protect the adapter with a PIN.
        </T>
      ) : null}
      <ListRow
        title="Bluetooth PIN"
        subtitle={d?.pin_default ? "Still the factory PIN 123456. Change it so nobody nearby can pair." : "Phones need this PIN once to pair"}
        leading={<Icon name="password" color={d?.pin_default ? c.error : undefined} />}
        trailing={<Icon name="chevron-right" />}
        onPress={() => {
          setPin("");
          setPin2("");
          setPinDialog(true);
        }}
        disabled={!m.ready || !d?.pairing}
      />
      <ListRow
        title="Forget paired phones"
        subtitle={d?.bonds !== undefined ? `${d.bonds} paired` : undefined}
        leading={<Icon name="link-off" />}
        onPress={() => setForgetDialog(true)}
        disabled={!m.ready || !d?.pairing}
      />

      <SectionLabel>Sleep when the car is off</SectionLabel>
      <View style={{ paddingHorizontal: 8 }}>
        <Segmented
          options={SLEEP_OPTIONS}
          value={String(d?.sleep_after_s ?? 600)}
          onChange={(v) => m.ready && m.setSleepAfter(Number(v))}
        />
      </View>

      <SectionLabel>Firmware update</SectionLabel>
      <Card level="mid" style={{ gap: 10 }}>
        <T variant="bodySmall" color={c.onSurfaceVariant}>
          {m.firmware.message ||
            `Installed: ${d?.fw ?? "unknown"}. Updates come from GitHub, are signed, and go to the adapter over Bluetooth.`}
        </T>
        {busy ? <ProgressBar progress={m.firmware.progress} /> : null}
        <View style={{ flexDirection: "row", justifyContent: "flex-end", gap: 8 }}>
          <Button kind="tonal" icon="refresh" label="Check" onPress={() => !busy && void m.checkFirmware()} disabled={busy} />
          {m.firmware.phase === "available" ? (
            <Button icon="system-update" label={`Install ${m.firmware.latest?.version ?? ""}`} onPress={() => void m.installFirmware()}
              disabled={!m.ready} />
          ) : null}
        </View>
        <T variant="labelSmall" color={c.onSurfaceVariant}>Keep the phone close and the app open until the adapter restarts.</T>
      </Card>

      <Dialog
        visible={pinDialog}
        icon="password"
        title="Change PIN"
        text="Six digits. Phones that are already paired keep working; new phones need the new PIN."
        onDismiss={() => setPinDialog(false)}
        actions={[
          { label: "Cancel", onPress: () => setPinDialog(false) },
          {
            label: "Save",
            onPress: () => {
              if (!pinValid) return;
              m.setPin(pin);
              setPinDialog(false);
            },
          },
        ]}
      >
        <Field value={pin} onChangeText={(v) => setPin(v.replace(/\D/g, ""))} placeholder="New PIN" keyboardType="number-pad" secure maxLength={6} />
        <Field value={pin2} onChangeText={(v) => setPin2(v.replace(/\D/g, ""))} placeholder="Repeat PIN" keyboardType="number-pad" secure maxLength={6} />
        {pin.length === 6 && pin2.length === 6 && pin !== pin2 ? <T variant="bodySmall" color={c.error}>The PINs differ.</T> : null}
      </Dialog>

      <Dialog
        visible={forgetDialog}
        icon="link-off"
        title="Forget paired phones?"
        text="Every phone, this one included, has to pair again with the PIN. On this phone also remove OBD-ESP32 under Bluetooth settings first."
        onDismiss={() => setForgetDialog(false)}
        actions={[
          { label: "Cancel", onPress: () => setForgetDialog(false) },
          {
            label: "Forget",
            onPress: () => {
              m.forgetBonds();
              setForgetDialog(false);
            },
          },
        ]}
      />
    </Sheet>
  );
}
