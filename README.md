# ESP32 OBD

Firmware za ESP32 s MrDIY OBD/CAN shieldom koji čita podatke iz auta preko OBD-II
porta i šalje ih mobilnoj aplikaciji preko Bluetooth Low Energy (BLE).

## Hardver i podržana vozila

- ESP32 + MrDIY ESP32 OBD shield (CAN transceiver na OBD pinovima 6 i 14).
- TWAI (CAN) pinovi: RX = GPIO4, TX = GPIO5 (`esp32-obd/pin_config.h`).
- Podržani protokoli: ISO 15765-4 CAN, 11-bit i 29-bit ID, 500 i 250 kbps.
  To pokriva sve aute prodane u EU i SAD-u od 2008. (i mnoge ranije).
- K-line (ISO 9141-2, KWP2000) i J1850 nisu podržani jer pločica nema te
  sklopove; zahtjev za njima vraća `"reason":"unsupported_hardware"`.

## Prevođenje

Arduino IDE ili `arduino-cli` s ESP32 jezgrom 3.x:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 esp32-obd
arduino-cli upload  --fqbn esp32:esp32:esp32 -p COM3 esp32-obd
```

## Sigurnost

- Nakon paljenja ESP32 je u `listen_only` načinu (500 kbps) i ništa ne šalje na
  sabirnicu dok aplikacija ne pošalje `set_link` ili `detect_protocol`.
- Auto-detekcija prvo pasivno sluša i nikad ne šalje na brzini za koju zna da
  je kriva; probni okviri se šalju bez ponavljanja (single-shot).
- Brisanje DTC-a (`clear_dtc`) je zaključano (`safety_guard`).
- Automatsko ponovno spajanje radi samo dok je mobitel spojen, pa parkirani
  auto ne ostaje budan zbog sonde.

## BLE sučelje

Uređaj se oglašava kao `OBD-ESP32`.

| Karakteristika | UUID | Smjer |
| --- | --- | --- |
| Servis | `6f0a0001-8f7f-4c0c-9c2c-44d4d818a001` | |
| Telemetrija | `6f0a0002-…` | notify (telemetrija, sirovi okviri) |
| Naredbe | `6f0a0003-…` | write (JSON naredba) |
| Odgovori | `6f0a0004-…` | notify (ack, status, dtc, ...) |

Naredba je JSON s poljem `"cmd"` (ili samo ime naredbe u tekstu, kao prije), npr.
`{"cmd":"start_stream","metrics":["rpm","speed_kmh"]}`.
Svaka naredba odmah dobije `{"type":"ack",...}`, a rezultat stiže kao zasebna poruka.

| Naredba | Polja | Rezultat |
| --- | --- | --- |
| `detect_protocol` | `protocol`: `auto` (zadano) ili `can11_500`, `can29_500`, `can11_250`, `can29_250` | `protocol_probe` tijekom traženja, zatim `protocol` (VIN, podržani PID-ovi), `status`, `capability` |
| `set_link` | `baud_kbps`, `mode` (`active`/`listen_only`), `stream_mode` (`telemetry`/`raw_frames`), `diagnostic_profile` (`obd_mode01`/`smart_ed`), opcionalno `protocol` | `link`, `status`, `capability` |
| `start_stream` | `metrics`: popis ključeva ili `["all"]`; bez njega četiri osnovna | `telemetry` poruke |
| `stop_stream` | | `status` |
| `probe_capabilities` | | `capability` (u dijelovima, `part`/`parts`) |
| `read_dtc` | | `dtc`: `codes` (spremljeni), `pending`, `permanent` |
| `read_freeze_frame` | | `freeze_frame`: DTC i vrijednosti u trenutku kvara |
| `vehicle_info` | | `vehicle_info`: `vin`, `cal_ids`, `ecu_names` |
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

## Smart ED profil

`set_link` s `"diagnostic_profile":"smart_ed"` pasivno čita broadcast poruke
Smart ED (3. generacija) i, u `active` načinu, UDS upite prema BMS-u
(SOC, napon i struja baterije, temperature, naponi ćelija, SOH).

## Struktura koda (`esp32-obd/`)

- `twai_driver` – CAN kontroler (11/29-bit, single-shot, oporavak od bus-off).
- `obd_transport` – ISO 15765-4 / ISO-TP, auto-detekcija, podržani PID-ovi.
- `pid_decoder` – tablica svih Mode 01 vrijednosti i dekoder.
- `telemetry_service` – rotacija PID-ova i slanje telemetrije.
- `diagnostics_service` – DTC, freeze frame, VIN i podaci o vozilu.
- `transport_ble` – BLE servis i prepoznavanje naredbi.
- `esp32-obd.ino` – povezivanje servisa, red naredbi, praćenje veze.
