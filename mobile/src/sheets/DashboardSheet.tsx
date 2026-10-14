import { useEffect, useState } from "react";
import { View } from "react-native";

import { Button, Checkbox, Icon, IconButton, ListRow, Radio, Segmented, Sheet, T } from "../components/m3";
import { CARD_TYPES, newCardId, type DashboardCard, type ValueDef } from "../dashboard/dashboard";
import { useColors } from "../theme/theme";

type View_ = { kind: "list" } | { kind: "add" } | { kind: "edit"; id: string };
type DialSlot = "arc" | "center" | "footer";

/** Add, remove, reorder and configure dashboard cards. */
export function DashboardSheet({
  visible,
  onClose,
  cards,
  defs,
  onSave,
  onReset,
}: {
  visible: boolean;
  onClose: () => void;
  cards: DashboardCard[];
  defs: ValueDef[];
  onSave: (cards: DashboardCard[]) => void;
  onReset: () => void;
}) {
  const c = useColors();
  const [draft, setDraft] = useState<DashboardCard[]>(cards);
  const [view, setView] = useState<View_>({ kind: "list" });
  const [slot, setSlot] = useState<DialSlot>("arc");

  useEffect(() => {
    if (!visible) return;
    setDraft(cards);
    setView({ kind: "list" });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [visible]);

  const label = (k: string) => defs.find((d) => d.key === k)?.label ?? k;
  const describe = (card: DashboardCard) => {
    switch (card.type) {
      case "dial":
        return [label(card.arc), label(card.center), ...card.footer.map(label)].join(" · ");
      case "chart":
        return label(card.metric);
      case "tiles":
        return `${card.metrics.length} values`;
      default:
        return "Connection, codes, protocol";
    }
  };

  const move = (i: number, d: -1 | 1) =>
    setDraft((prev) => {
      const next = [...prev];
      const j = i + d;
      if (j < 0 || j >= next.length) return prev;
      [next[i], next[j]] = [next[j], next[i]];
      return next;
    });
  const replace = (card: DashboardCard) => setDraft((prev) => prev.map((x) => (x.id === card.id ? card : x)));

  const addCard = (type: DashboardCard["type"]) => {
    const first = defs[0]?.key ?? "rpm";
    const card: DashboardCard =
      type === "dial"
        ? { id: newCardId(), type, arc: first, center: defs[1]?.key ?? first, footer: [] }
        : type === "chart"
          ? { id: newCardId(), type, metric: first }
          : type === "tiles"
            ? { id: newCardId(), type, metrics: defs.slice(0, 4).map((d) => d.key) }
            : { id: newCardId(), type };
    setDraft((prev) => [...prev, card]);
    setSlot("arc");
    setView(type === "status" ? { kind: "list" } : { kind: "edit", id: card.id });
  };

  const editing = view.kind === "edit" ? draft.find((x) => x.id === view.id) : undefined;
  const title = view.kind === "add" ? "Add card" : editing ? CARD_TYPES.find((t) => t.type === editing.type)?.label ?? "Card" : "Dashboard cards";

  const valueList = (isChecked: (k: string) => boolean, onToggle: (k: string) => void, multi: boolean) => (
    <View>
      {defs.map((d) => (
        <ListRow
          key={d.key}
          title={d.label}
          subtitle={d.manufacturer ? `Manufacturer${d.unit ? ` · ${d.unit}` : ""}` : d.unit || undefined}
          leading={multi ? <Checkbox checked={isChecked(d.key)} /> : <Radio selected={isChecked(d.key)} />}
          onPress={() => onToggle(d.key)}
        />
      ))}
    </View>
  );

  return (
    <Sheet
      visible={visible}
      title={title}
      onClose={onClose}
      footer={
        view.kind === "list" ? (
          <>
            <Button kind="text" label="Reset" onPress={onReset} />
            <Button kind="tonal" icon="add" label="Add card" onPress={() => setView({ kind: "add" })} />
            <Button label="Save" onPress={() => onSave(draft)} />
          </>
        ) : (
          <Button label="Done" onPress={() => setView({ kind: "list" })} />
        )
      }
    >
      {view.kind === "list" ? (
        <>
          <T variant="bodySmall" color={c.onSurfaceVariant} style={{ paddingHorizontal: 8 }}>
            Cards show only the values chosen in Values to read. Tap a card to change what it shows.
          </T>
          {draft.length === 0 ? <T variant="bodySmall" color={c.onSurfaceVariant}>No cards. Add one below.</T> : null}
          {draft.map((card, i) => {
            const t = CARD_TYPES.find((x) => x.type === card.type)!;
            return (
              <View key={card.id} style={{ backgroundColor: c.sc, borderRadius: 16 }}>
                <ListRow
                  title={t.label}
                  subtitle={describe(card)}
                  leading={<Icon name={t.icon} />}
                  onPress={card.type === "status" ? undefined : () => {
                    setSlot("arc");
                    setView({ kind: "edit", id: card.id });
                  }}
                  trailing={
                    <View style={{ flexDirection: "row" }}>
                      <IconButton icon="arrow-upward" onPress={() => move(i, -1)} />
                      <IconButton icon="arrow-downward" onPress={() => move(i, 1)} />
                      <IconButton icon="delete" onPress={() => setDraft((prev) => prev.filter((x) => x.id !== card.id))} />
                    </View>
                  }
                />
              </View>
            );
          })}
        </>
      ) : null}

      {view.kind === "add"
        ? CARD_TYPES.map((t) => (
            <ListRow key={t.type} title={t.label} subtitle={t.description} leading={<Icon name={t.icon} />}
              onPress={() => addCard(t.type)} />
          ))
        : null}

      {editing?.type === "dial" ? (
        <>
          <Segmented
            options={[
              { value: "arc", label: "Gauge" },
              { value: "center", label: "Number" },
              { value: "footer", label: `Below (${editing.footer.length}/3)` },
            ]}
            value={slot}
            onChange={setSlot}
          />
          {slot === "arc"
            ? valueList((k) => editing.arc === k, (k) => replace({ ...editing, arc: k }), false)
            : slot === "center"
              ? valueList((k) => editing.center === k, (k) => replace({ ...editing, center: k }), false)
              : valueList(
                  (k) => editing.footer.includes(k),
                  (k) =>
                    replace({
                      ...editing,
                      footer: editing.footer.includes(k)
                        ? editing.footer.filter((x) => x !== k)
                        : editing.footer.length < 3
                          ? [...editing.footer, k]
                          : editing.footer,
                    }),
                  true
                )}
        </>
      ) : null}
      {editing?.type === "chart" ? valueList((k) => editing.metric === k, (k) => replace({ ...editing, metric: k }), false) : null}
      {editing?.type === "tiles"
        ? valueList(
            (k) => editing.metrics.includes(k),
            (k) =>
              replace({
                ...editing,
                metrics: editing.metrics.includes(k) ? editing.metrics.filter((x) => x !== k) : [...editing.metrics, k],
              }),
            true
          )
        : null}
    </Sheet>
  );
}
