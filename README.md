# ESP32 OBD

Firmware za ESP32 s MrDIY OBD/CAN shieldom koji čita podatke iz auta preko OBD-II
porta i šalje ih mobilnoj aplikaciji preko Bluetooth Low Energy (BLE).

## Repozitorij

| Mapa | Sadržaj |
| --- | --- |
| [`esp32-obd/`](esp32-obd/) | firmware (Arduino, ESP32) |
| [`mobile/`](mobile/) | mobilna aplikacija (Expo / React Native), vidi [mobile/README.md](mobile/README.md) |
| [`profiles/`](profiles/) | proizvođački PID-ovi za 60 marki / 608 modela (OBDb, CC BY-SA 4.0) |
| [`tools/`](tools/) | generiranje profila i kataloga metrika, provjere |
| [`firmware/`](firmware/) | gotova slika firmwarea i `version.json` za OTA iz aplikacije |
| [`docs/`](docs/) | protokol, plan seminara, generator PDF-a seminara |

## Hardver i podržana vozila

- ESP32 + MrDIY ESP32 OBD shield (CAN transceiver na OBD pinovima 6 i 14).
- TWAI (CAN) pinovi: RX = GPIO4, TX = GPIO5 (`esp32-obd/pin_config.h`).
- Podržani protokoli: ISO 15765-4 CAN, 11-bit i 29-bit ID, 500 i 250 kbps.
  To pokriva sve aute prodane u EU i SAD-u od 2008. (i mnoge ranije).
- K-line (ISO 9141-2, KWP2000) i J1850 nisu podržani jer pločica nema te
  sklopove; zahtjev za njima vraća `"reason":"unsupported_hardware"`.

## Prevođenje

Arduino IDE ili `arduino-cli` s ESP32 jezgrom 3.x i bibliotekom
**NimBLE-Arduino 2.x** (Library Manager → "NimBLE-Arduino"):

```sh
arduino-cli lib install NimBLE-Arduino
arduino-cli compile --fqbn esp32:esp32:esp32 esp32-obd
arduino-cli upload  --fqbn esp32:esp32:esp32 -p COM3 esp32-obd
```

Zadana particijska shema ("Default 4MB with spiffs") ima dvije aplikacijske
particije, što treba za OTA nadogradnju. Firmware zauzima oko 53% jedne.

## Sigurnost

- Nakon paljenja ESP32 je u `listen_only` načinu (500 kbps) i ništa ne šalje na
  sabirnicu dok aplikacija ne pošalje `set_link` ili `detect_protocol`.
- Auto-detekcija prvo pasivno sluša i nikad ne šalje na brzini za koju zna da
  je kriva; probni okviri se šalju bez ponavljanja (single-shot).
- Brisanje DTC-a radi samo u dva koraka (token potvrde) i samo kad auto stoji
  s ugašenim motorom. Potpuno se zaključava s `kAllowClearDtcDefault = false`
  u `safety_guard.h`.
- Automatsko ponovno spajanje radi samo dok je mobitel spojen, pa parkirani
  auto ne ostaje budan zbog sonde.

## Štednja baterije auta

Kad CAN sabirnica šuti i mobitel nije spojen 10 minuta (`sleep_after_s`, pamti
se u flashu), ESP32 ide u deep sleep i budi se na prvi CAN okvir (CAN RX pin,
GPIO4). Dok spava, mobitel se ne može spojiti: dovoljno je otključati auto ili
upaliti kontakt. `{"cmd":"set_power","sleep_after_s":0}` isključuje spavanje.
Poruka `device` javlja razlog buđenja (`power_on`, `can_activity`).

## BLE sučelje

Uređaj se oglašava kao `OBD-ESP32`.

| Karakteristika | UUID | Smjer |
| --- | --- | --- |
| Servis | `6f0a0001-8f7f-4c0c-9c2c-44d4d818a001` | |
| Telemetrija | `6f0a0002-…` | notify (telemetrija, sirovi okviri) |
| Naredbe | `6f0a0003-…` | write (JSON naredba) |
| Odgovori | `6f0a0004-…` | notify (ack, status, dtc, ...) |
| OTA podaci | `6f0a0005-…` | write without response (dijelovi firmwarea) |

Naredba je JSON s poljem `"cmd"` (ili samo ime naredbe u tekstu, kao prije), npr.
`{"cmd":"start_stream","metrics":["rpm","speed_kmh"]}`.
Svaka naredba odmah dobije `{"type":"ack",...}`, a rezultat stiže kao zasebna poruka.
Naredbe se izvršavaju redom (red od 16 mjesta); kad je red pun stiže
`{"type":"log","event":"command_dropped"}`.

| Naredba | Polja | Rezultat |
| --- | --- | --- |
| `detect_protocol` | `protocol`: `auto` (zadano) ili `can11_500`, `can29_500`, `can11_250`, `can29_250` | `protocol_probe` tijekom traženja, zatim `protocol` (VIN, podržani PID-ovi), `status`, `capability` |
| `set_link` | `baud_kbps`, `mode` (`active`/`listen_only`), `stream_mode` (`telemetry`/`raw_frames`), `diagnostic_profile` (`obd_mode01`/`smart_ed`), opcionalno `protocol` | `link`, `status`, `capability` |
| `start_stream` | `metrics`: popis ključeva ili `["all"]`; bez njega četiri osnovna | `telemetry` poruke |
| `stop_stream` | | `status` |
| `probe_capabilities` | | `capability` (u dijelovima, `part`/`parts`) |
| `read_dtc` | | `dtc`: `codes` (spremljeni), `pending`, `permanent` |
| `clear_dtc` | `confirm`: token iz prvog odgovora | prvo `clear_dtc` `confirm_required` s `token` (30 s), zatim `cleared` / `refused` / `failed` |
| `read_freeze_frame` | | `freeze_frame`: DTC i vrijednosti u trenutku kvara |
| `read_readiness` | | `readiness`: `mil`, `dtc_count`, `ignition`, `complete`, `incomplete` |
| `read_monitor_tests` | | `monitor_test` po testu (Mode 06: `mid`, `tid`, `value`, `min`, `max`, `pass`), zatim `monitor_tests_done` |
| `vehicle_info` | | `vehicle_info`: `vin`, `cal_ids`, `ecu_names` |
| `ext_add` | jedan proizvođački signal (vidi dolje) | `ext_add` s `ok`, `count` |
| `ext_list` / `ext_clear` | | `ext_list` s ključevima učitanih signala |
| `ota_begin` | `size`, `md5` | `ota` `ready`, zatim `progress` |
| `ota_end` / `ota_abort` | | `ota` `done` (ponovno pokretanje) ili `aborted` |
| `set_power` | `sleep_after_s` (0 = nikad) | `device` |
| `bus_scan` | | `bus_scan_result` za 100/125/250/500/1000 kbps |
| `bus_diag` | | `bus_diag` (stanje CAN kontrolera) |

Poruke se dijele da stanu u jedan BLE paket (MTU 247): telemetrija s puno polja
dolazi u više poruka s `"partial":true` (zadnja je bez njega), a `capability`
s `"part"` i `"parts"`.

## Metrike (Mode 01)

Firmware čita samo PID-ove koje auto prijavi kao podržane (PID 00/20/40/...),
a do šest PID-ova šalje u jednom upitu. Ključevi za `metrics`:

- Motor: `rpm`, `engine_load_pct`, `abs_load_pct`, `coolant_c`, `coolant_1_c`, `coolant_2_c`,
  `oil_temp_c`, `intake_air_c`, `intake_air_1_c`, `ambient_air_c`, `charge_air_c`,
  `timing_advance_deg`, `injection_timing_deg`, `runtime_s`, `engine_run_total_s`,
  `demand_torque_pct`, `actual_torque_pct`, `friction_torque_pct`, `torque_idle_pct`, `ref_torque_nm`
- Zrak i tlak: `maf_gs`, `maf_a_gs`, `maf_max_gs`, `map_kpa`, `baro_kpa`, `boost_cmd_kpa`,
  `boost_kpa`, `turbo_rpm`, `exhaust_pressure_kpa`, `exhaust_flow_kgh`
- Gas: `throttle_pct` (papučica D), `throttle_valve_pct`, `rel_throttle_pct`,
  `abs_throttle_b_pct`, `abs_throttle_c_pct`, `pedal_e_pct`, `pedal_f_pct`,
  `rel_pedal_pct`, `cmd_throttle_pct`
- Gorivo: `fuel_level_pct`, `fuel_rate_lh`, `fuel_rate_gs`, `cyl_fuel_rate_mg`,
  `fuel_pressure_kpa`, `fuel_rail_kpa`, `fuel_rail_rel_kpa`, `fuel_rail_abs_kpa`,
  `fuel_system_status`, `fuel_type`, `ethanol_pct`, `lambda`,
  `stft_b1_pct`, `ltft_b1_pct`, `stft_b2_pct`, `ltft_b2_pct`,
  `st_o2_trim_b1_pct`, `lt_o2_trim_b1_pct`, `st_o2_trim_b2_pct`, `lt_o2_trim_b2_pct`
- Lambda sonde 1–8 (`N` = 1..8): `o2_sN_v`, `o2_sN_trim_pct`, `o2_sN_lambda`, `o2_sN_wide_v`,
  `o2_sN_lambda_c`, `o2_sN_current_ma`, te `o2_sensors_present`
- Ispuh i emisije: `catalyst_b1s1_c`, `catalyst_b2s1_c`, `catalyst_b1s2_c`, `catalyst_b2s2_c`,
  `egt_b1s1_c`, `egt_b2s1_c`, `dpf_delta_kpa`, `nox_ppm`, `egr_cmd_pct`, `egr_error_pct`,
  `evap_purge_pct`, `evap_vapor_pa`, `evap_vapor2_pa`, `evap_abs_kpa`, `sec_air_status`,
  `manifold_surface_c`
- Vozilo: `speed_kmh`, `odometer_km`, `module_voltage_v`, `hybrid_battery_pct`,
  `trans_gear`, `trans_ratio`, `pto_active`, `obd_standard`
- Dijagnostika: `mil_on`, `dtc_count`, `dist_mil_km`, `dist_since_clear_km`,
  `time_mil_min`, `time_since_clear_min`, `warmups_since_clear`

## Proizvođački PID-ovi i profili vozila

Standardni OBD ne daje stvari kao SOC baterije električnog auta, temperaturu
ulja mjenjača ili napunjenost DPF-a. To se čita proizvođačkim upitima (UDS
servis 22 ili KWP servis 21) prema pojedinom ECU-u.

U mapi [`profiles/`](profiles/) su profili za **60 marki i 608 modela**
(224 710 signala) iz zajedničke baze [OBDb](https://github.com/OBDb)
(CC BY-SA 4.0): `profiles/index.json` popisuje sve modele, a
`profiles/<Marka>/<Model>.json.gz` je profil jednog modela (gzip JSON, format
je opisan u `tools/obdb_to_profiles.py`). Mobilna aplikacija ih preuzima
odavde i nudi odabir marke, modela i signala.

Aplikacija korisniku ponudi marku i model, zatim svaki odabrani signal pošalje
naredbom `ext_add` (najviše 96 signala, pamte se u flashu i nakon gašenja):

```json
{"cmd":"ext_add","k":"KONAEV_HVBAT_SOC","h":"7E4","r":"7EC","s":"22","d":"0101","b":32,"l":8,"v":2,"mx":100}
```

| Polje | Značenje |
| --- | --- |
| `k` | ključ (u telemetriji kao mala slova; dulji od 39 znakova se skraćuje uz hash) |
| `h` / `r` | CAN ID upita / odgovora (3 znamenke = 11-bit, 8 = 29-bit) |
| `s` / `d` | servis (`22` ili `21`) i identifikator (DID) |
| `e` / `t` | ISO-TP prošireno adresiranje (BMW) i adresa testera |
| `din` | dijagnostička sesija prije upita (npr. `03`) |
| `b` / `l` | početni bit i duljina u bitovima, od prvog bajta nakon DID-a |
| `m` / `v` / `a` | vrijednost = sirovo × `m` / `v` + `a` |
| `sg` / `le` | s predznakom / little-endian |
| `mn` / `mx` / `nmin` / `nmax` / `x` | raspon, granice za "nema podatka", broj decimala |

Signali s istim upitom dijele jedan zahtjev, a vrijednosti stižu u istim
`telemetry` porukama kao standardne metrike. Skripta
`tools/obdb_to_profiles.py` ponovno generira profile iz novije OBDb baze, a
`tools/check_profiles.py` provjerava da svaka naredba stane u jedan BLE paket
i da se dekoder poklapa s OBDb referencom.

## OTA nadogradnja preko BLE-a

Najjednostavnije iz aplikacije: Device → Firmware update → Check for update →
Install. Aplikacija preuzima `firmware/esp32-obd.bin` i `firmware/version.json`
s GitHuba (grana `master`) i šalje sliku na ESP. Nova verzija se objavljuje
ovako: podigni `kFirmwareVersion` u `esp32-obd.ino`, pokreni
`python tools/publish_firmware.py` i commitaj `firmware/`. Prvo flashanje
firmwarea s OTA podrškom mora biti USB-om.

Isto ručno:

1. `{"cmd":"ota_begin","size":704512,"md5":"<md5 .bin datoteke>"}` → `ota` `ready`
2. aplikacija piše `.bin` u dijelovima (do MTU−3 bajta) na karakteristiku OTA podataka
3. `{"cmd":"ota_end"}` → provjera MD5, `ota` `done` i ponovno pokretanje

Napredak stiže kao `ota` `progress` svakih 5%. Ako bilo što pođe krivo, staro
firmware ostaje aktivno.

## Smart ED profil

`set_link` s `"diagnostic_profile":"smart_ed"` čita Smart fortwo electric drive
(451, 3. generacija). Port je napravljen prema
[ED_BMSdiag](https://github.com/MyLab-odyssey/ED_BMSdiag) (MyLab-odyssey, MIT).

- Pasivno (broadcast): `soc_pct`, `soc_internal_pct`, `hv_v`, `hv_amps`, `power_kw`,
  `lv_v`, `odo_km`, `time_hh`, `time_mm`, `speed_kmh`, `range_km`, `usable_power_pct`,
  `energy_start_kwh100`, `energy_reset_kwh100`, `hv_active`, `eco_*_pct`, `trip_*_km`.
- U `active` načinu, upitima prema BMS-u (0x7E7), hlađenju (0x7E5) i punjaču
  (0x61A): temperature baterije (max/min/prosjek/ulaz rashladne tekućine i 9
  senzora modula), naponi 93 ćelije (min/max/prosjek/devijacija, broj ćelije),
  kapacitet (BMS i po ćelijama, Ah), SOH procjena, izolacija, ciklusi HV
  sklopnika, pumpa i ventilator hlađenja, grijač baterije, vakuum pumpa, te
  OBL ili NLG6 punjač (AC naponi i struje po fazi, DC napon i struja,
  temperature, struja kabela i punionice). Ukupno 96 vrijednosti.

## Struktura koda (`esp32-obd/`)

- `twai_driver` – CAN kontroler (11/29-bit, single-shot, oporavak od bus-off).
- `obd_transport` – ISO 15765-4 / ISO-TP (funkcionalno i fizičko adresiranje), auto-detekcija.
- `pid_decoder` – tablica svih Mode 01 vrijednosti i dekoder.
- `telemetry_service` – rotacija PID-ova (do 6 po upitu) i proizvođačkih signala.
- `ext_pid_service` – proizvođački signali (Mode 21/22), spremanje u NVS.
- `diagnostics_service` – DTC, brisanje uz potvrdu, freeze frame, readiness, Mode 06, VIN.
- `smart_bms_service` – Smart ED (broadcast + UDS).
- `ota_service` – nadogradnja firmwarea preko BLE-a.
- `power_manager` – deep sleep i buđenje na CAN promet.
- `transport_ble` – NimBLE servis i prepoznavanje naredbi; `json_lite` – JSON parser.
- `esp32-obd.ino` – povezivanje servisa, red naredbi, praćenje veze.
