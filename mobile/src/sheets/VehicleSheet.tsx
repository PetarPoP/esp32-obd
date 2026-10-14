import { useMemo, useState, type ReactNode } from "react";
import { StyleSheet, View } from "react-native";

import { Button, Field, Icon, ListRow, Radio, SectionLabel, Sheet, T } from "../components/m3";
import type { ObdMonitor } from "../hooks/useObdMonitor";
import { listMakes, listModels } from "../services/profileRepository";
import { useColors } from "../theme/theme";
import { vehicleName, type VehicleSelection } from "../types/vehicles";

type Step = "kind" | "make" | "model";

export function VehicleSheet({
  visible,
  onClose,
  vehicle,
  onPick,
  onOpenValues,
  valueCount,
  m,
}: {
  visible: boolean;
  onClose: () => void;
  vehicle: VehicleSelection;
  /** Called with the new vehicle; the caller then asks which values to read. */
  onPick: (v: VehicleSelection) => void;
  onOpenValues: () => void;
  valueCount: number;
  m: ObdMonitor;
}) {
  const c = useColors();
  const [step, setStep] = useState<Step>("kind");
  const [make, setMake] = useState("");
  const [query, setQuery] = useState("");
  const makes = useMemo(() => listMakes(), []);
  const models = useMemo(() => (make ? listModels(make) : []), [make]);

  const close = () => {
    setStep("kind");
    setQuery("");
    onClose();
  };
  const pick = (v: VehicleSelection) => {
    setStep("kind");
    setQuery("");
    onPick(v);
  };

  const q = query.trim().toLowerCase();
  const protocol = m.protocolInfo?.ok ? m.protocolInfo.protocol : m.status?.protocol;
  const spec: [string, string][] = [
    ["Protocol", m.probeStage ? `detecting (${m.probeStage})` : protocol && protocol !== "none" ? protocol : "—"],
    ["VIN", m.protocolInfo?.vin || m.vehicleInfo?.vin || "—"],
    ["Supported values", m.capabilitiesConfirmed ? `${m.supportedMetrics.length} confirmed by the car` : "not read yet"],
    ["Values to read", String(valueCount)],
  ];
  if (vehicle.kind === "profile") {
    spec.push(["Manufacturer values on adapter", m.extLoad.running ? `loading ${m.extLoad.sent}/${m.extLoad.total}` : String(m.extOnDevice.length)]);
  }

  const title = step === "kind" ? "Vehicle" : step === "make" ? "Choose make" : make;

  return (
    <Sheet
      visible={visible}
      title={title}
      onClose={close}
      footer={
        step === "kind" ? (
          <>
            {vehicle.kind !== "smart_ed3" ? (
              <Button kind="text" label="Detect protocol" onPress={() => void m.sendCommand("detect_protocol", { protocol: "auto" })} disabled={!m.ready} />
            ) : null}
            <Button icon="tune" label="Values to read" onPress={onOpenValues} />
          </>
        ) : (
          <Button kind="text" label="Back" onPress={() => setStep(step === "model" ? "make" : "kind")} />
        )
      }
    >
      {step === "kind" ? (
        <>
          <View style={{ gap: 2 }}>
            <ChoiceRow label="Generic OBD-II" sub="Any car with OBD-II over CAN (EU/US from 2008)" selected={vehicle.kind === "generic"}
              onPress={() => pick({ kind: "generic" })} />
            <ChoiceRow label={vehicle.kind === "profile" ? vehicleName(vehicle) : "Make & model"}
              sub="Standard values plus manufacturer values (OBDb, 60 makes)" selected={vehicle.kind === "profile"}
              onPress={() => setStep("make")} trailing={<Icon name="chevron-right" />} />
            <ChoiceRow label="Smart ED (451)" sub="Battery, cells, charger and cooling of the 3rd gen Smart electric drive"
              selected={vehicle.kind === "smart_ed3"} onPress={() => pick({ kind: "smart_ed3" })} />
          </View>
          <View style={[styles.spec, { backgroundColor: c.sc }]}>
            {spec.map(([k, v]) => (
              <View key={k} style={styles.specRow}>
                <T variant="bodySmall" color={c.onSurfaceVariant}>{k}</T>
                <T variant="titleSmall" style={{ flexShrink: 1, textAlign: "right" }} numberOfLines={1}>{v}</T>
              </View>
            ))}
          </View>
        </>
      ) : null}

      {step === "make" ? (
        <>
          <Field value={query} onChangeText={setQuery} placeholder={`Search ${makes.length} makes`} />
          <View>
            {makes
              .filter((x) => !q || x.toLowerCase().includes(q))
              .map((x) => (
                <ListRow key={x} title={x} trailing={<Icon name="chevron-right" />}
                  onPress={() => {
                    setMake(x);
                    setQuery("");
                    setStep("model");
                  }} />
              ))}
          </View>
        </>
      ) : null}

      {step === "model" ? (
        <>
          <SectionLabel>{models.length} models</SectionLabel>
          <View>
            {models.map((x) => (
              <ListRow key={x.file} title={x.model} subtitle={`${x.signals} manufacturer values`}
                leading={<Radio selected={vehicle.kind === "profile" && vehicle.file === x.file} />}
                onPress={() => pick({ kind: "profile", make: x.make, model: x.model, file: x.file })} />
            ))}
          </View>
        </>
      ) : null}
    </Sheet>
  );
}

function ChoiceRow({ label, sub, selected, onPress, trailing }: { label: string; sub: string; selected: boolean; onPress: () => void; trailing?: ReactNode }) {
  const c = useColors();
  return (
    <View style={{ borderRadius: 28, overflow: "hidden", backgroundColor: selected ? c.secondaryContainer : "transparent" }}>
      <ListRow title={label} subtitle={sub} leading={<Radio selected={selected} />} trailing={trailing} onPress={onPress} />
    </View>
  );
}

const styles = StyleSheet.create({
  spec: { borderRadius: 16, paddingHorizontal: 16, paddingVertical: 4 },
  specRow: { flexDirection: "row", justifyContent: "space-between", gap: 12, paddingVertical: 12 },
});
