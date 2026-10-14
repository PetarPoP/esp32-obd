import { View } from "react-native";

import { Button, Icon, ListRow, SectionLabel, Segmented, Sheet, Switch } from "../components/m3";
import { DTC_TABLE_SIZE } from "../data/dtcDescriptions";
import type { SettingsStore, ThemePreference } from "../state/settings";

export function SettingsSheet({
  visible,
  onClose,
  store,
  onOpen,
}: {
  visible: boolean;
  onClose: () => void;
  store: SettingsStore;
  onOpen: (sheet: "dashboard" | "values" | "adapter" | "tools") => void;
}) {
  const { settings, update } = store;
  return (
    <Sheet visible={visible} title="Settings" onClose={onClose}>
      <SectionLabel>Dashboard</SectionLabel>
      <View>
        <ListRow title="Dashboard cards" subtitle="Choose, order and configure the cards" leading={<Icon name="dashboard" />}
          trailing={<Icon name="chevron-right" />} onPress={() => onOpen("dashboard")} />
        <ListRow title="Values to read" subtitle="Which PIDs the adapter polls for this car" leading={<Icon name="tune" />}
          trailing={<Icon name="chevron-right" />} onPress={() => onOpen("values")} />
        <ListRow title="Adapter" subtitle="Connection, PIN, sleep, firmware update" leading={<Icon name="memory" />}
          trailing={<Icon name="chevron-right" />} onPress={() => onOpen("adapter")} />
      </View>

      <SectionLabel>Appearance</SectionLabel>
      <View style={{ paddingHorizontal: 8, gap: 12 }}>
        <Segmented<ThemePreference>
          options={[
            { value: "system", label: "System" },
            { value: "light", label: "Light" },
            { value: "dark", label: "Dark" },
          ]}
          value={settings.theme}
          onChange={(theme) => update({ theme })}
        />
      </View>
      <ListRow
        title="Trouble code language"
        subtitle={`Descriptions for ${DTC_TABLE_SIZE} generic codes`}
        leading={<Icon name="translate" />}
        trailing={
          <View style={{ width: 150 }}>
            <Segmented
              options={[
                { value: "hr", label: "HR" },
                { value: "en", label: "EN" },
              ]}
              value={settings.dtcLang}
              onChange={(dtcLang) => update({ dtcLang })}
            />
          </View>
        }
      />

      <SectionLabel>Developer</SectionLabel>
      <ListRow
        title="Developer mode"
        subtitle="PID probe, bus check, baud scan, BLE log"
        leading={<Icon name="code" />}
        trailing={<Switch value={settings.developer} onChange={(developer) => update({ developer })} />}
      />
      {settings.developer ? <Button kind="tonal" icon="build" label="Open developer tools" onPress={() => onOpen("tools")} /> : null}
    </Sheet>
  );
}
