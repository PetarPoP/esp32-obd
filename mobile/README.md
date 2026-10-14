# OBD BLE Monitor (Expo / React Native)

Mobilna aplikacija za ESP32 OBD adapter (firmware u `../esp32-obd`), u
Material 3 dizajnu "3a" (instrument ploča s brojčanikom, tonalne kartice,
bottom sheetovi, svijetla i tamna tema):

- spajanje na `OBD-ESP32` preko BLE-a, uparivanje s PIN-om (firmware 2.1+),
  automatsko ponovno spajanje
- odabir auta (Generic OBD-II, marka i model iz OBDb profila, Smart ED), nakon
  čega korisnik bira **koje vrijednosti čitati**: adapter čita i aplikacija
  prikazuje samo njih (standardni PID-ovi filtrirani na one koje auto podržava,
  plus proizvođački signali za odabrani model)
- **dashboard od kartica koje korisnik bira**: brojčanik (luk + veliki broj +
  tri vrijednosti), Link/Codes/Bus, graf zadnjih 30 s, pločice s vrijednostima;
  dodavanje, brisanje, redoslijed i izbor vrijednosti po kartici, posebno za
  svaki auto
- DTC (spremljeni, pending, permanent) s **opisima na hrvatskom ili engleskom**
  (`src/data/dtcDescriptions.ts`: generički kodovi, nepoznati dobiju opis grupe,
  kodovi proizvođača su označeni), readiness, freeze frame, brisanje uz potvrdu
- adapter: PIN, zaboravljanje uparenih mobitela, deep sleep, nadogradnja
  firmwarea s GitHuba (potpisana slika)
- razvojni alati (Settings → Developer mode): PID probe, otkrivanje protokola,
  bus check, baud scan, Mode 06, vehicle info, BLE log s izvozom

## Pokretanje

BLE ne radi u Expo Go, potreban je development build:

```sh
npm install
npx expo prebuild          # generira android/ i ios/ (nisu u gitu)
npx expo run:android       # ili: eas build --profile development
npm run typecheck
```

Nove nativne ovisnosti (react-native-svg, AsyncStorage, expo-system-ui) traže
novi build nakon `npm install`. Gotov APK se gradi na GitHubu (vidi "CI i
izdanja" u glavnom README-u).

Pri prvom spajanju s firmwareom 2.1+ Android traži PIN adaptera (tvornički
123456); promijeni ga u Settings → Adapter → Bluetooth PIN.

## Struktura

- `App.tsx` – tema (System/Light/Dark) i učitavanje postavki
- `src/screens/HomeScreen.tsx` – glavni ekran i sheetovi
- `src/components/m3.tsx` – Material 3 komponente (gumbi, kartice, sheet, dijalog, switch, snackbar)
- `src/components/DashboardCards.tsx` – brojčanik, Link/Codes/Bus, graf, pločice
- `src/dashboard/dashboard.ts` – model kartica, zadani dashboard, rasponi brojčanika
- `src/sheets/` – Codes, Vehicle, Values to read, Dashboard cards, Settings, Adapter, Developer tools
- `src/state/settings.ts` – postavke u AsyncStorageu (auto, vrijednosti i dashboard po autu)
- `src/hooks/useObdMonitor.ts` – BLE sesija (uparivanje), obrada poruka, povijest za grafove, OTA
- `src/services/bleObdClient.ts` – BLE (react-native-ble-plx)
- `src/services/profileRepository.ts` – OBDb profili, jedinice, `ext_add`
- `src/services/firmwareRepository.ts` – `version.json` (s potpisom) i slika firmwarea s GitHuba
- `src/data/metrics.generated.ts` – katalog metrika, generira ga `tools/gen_app_metrics.py`
- `src/data/dtcDescriptions.ts` – opisi DTC kodova (HR/EN)
- `plugins/withReleaseSigning.js` – potpisivanje release APK-a iz CI tajni

Cijeli BLE protokol opisan je u glavnom `README.md`.
