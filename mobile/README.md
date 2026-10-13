# OBD BLE Monitor (Expo / React Native)

Mobilna aplikacija za ESP32 OBD adapter (firmware u `../esp32-obd`):

- spajanje na `OBD-ESP32` preko BLE-a i automatsko prepoznavanje protokola auta
- live vrijednosti: 139 standardnih OBD-II metrika, 96 Smart ED vrijednosti i
  proizvođački signali za odabranu marku i model (60 marki, 608 modela iz OBDb)
- DTC (spremljeni, pending, permanent), freeze frame, readiness monitori,
  Mode 06 testovi, VIN / CAL ID / ECU, brisanje kodova uz potvrdu
- postavke uređaja (deep sleep), bus scan i dijagnostika, BLE konzola s izvozom

## Pokretanje

BLE ne radi u Expo Go, potreban je development build:

```sh
npm install
npx expo prebuild          # generira android/ i ios/ (nisu u gitu)
npx expo run:android       # ili: eas build --profile development
npm run typecheck
```

## Vozila

| Način | Što radi |
| --- | --- |
| Generic OBD-II (auto-detect) | `set_link` + `protocol: auto`; ESP sam nađe CAN 11/29-bit 500/250 kbps |
| Make & model | isto + odabir proizvođačkih signala iz profila; šalju se na ESP naredbom `ext_add` (do 96, pamte se) |
| Smart ED (451) | ESP čita BMS, hlađenje, punjač i vožnju; aplikacija bira što prikazati |

Profili se preuzimaju s GitHuba (`profiles/<Marka>/<Model>.json.gz`, adresa u
`src/config.ts`), a popis modela je ugrađen u aplikaciju
(`src/data/profile-index.json`). Za rad bez interneta dovoljno je jednom
učitati signale na ESP: oni ostaju u njegovoj memoriji.

## Struktura

- `src/hooks/useObdMonitor.ts` – stanje aplikacije i obrada svih poruka s ESP-a
- `src/services/bleObdClient.ts` – BLE (react-native-ble-plx)
- `src/services/profileRepository.ts` – popis i preuzimanje profila vozila
- `src/utils/gunzip.ts` – gzip dekoder (bez dodatnih paketa)
- `src/data/metrics.generated.ts` – katalog metrika, generira ga
  `tools/gen_app_metrics.py` iz izvornog koda firmwarea
- `src/screens/DashboardScreen.tsx`, `src/components/` – sučelje

Cijeli BLE protokol opisan je u glavnom `README.md`.
