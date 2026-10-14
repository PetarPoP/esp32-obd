# ESP32 <-> aplikacija: BLE protokol

Potpun i ažuran opis (karakteristike, sve naredbe, poruke i primjeri) nalazi se u
glavnom [README.md](../README.md#ble-sučelje). Ovdje je sažetak.

## BLE

- Servis `6f0a0001-8f7f-4c0c-9c2c-44d4d818a001`
- `6f0a0002-…` telemetrija (notify), `6f0a0003-…` naredbe (write),
  `6f0a0004-…` odgovori (notify), `6f0a0005-…` OTA podaci (write without response)

## Naredbe (JSON, ključ `command` ili `cmd`)

```json
{ "command": "set_link", "baud_kbps": 500, "mode": "active", "stream_mode": "telemetry",
  "diagnostic_profile": "obd_mode01", "protocol": "auto" }
{ "command": "start_stream", "metrics": ["rpm", "speed_kmh", "coolant_c"] }
{ "command": "read_dtc" }
{ "command": "clear_dtc", "confirm": "4821" }
{ "command": "ext_add", "k": "KONAEV_HVBAT_SOC", "h": "7E4", "r": "7EC", "s": "22", "d": "0101", "b": 32, "l": 8, "v": 2 }
```

Ostale: `stop_stream`, `detect_protocol`, `probe_capabilities`, `read_freeze_frame`,
`read_readiness`, `read_monitor_tests`, `vehicle_info`, `ext_list`, `ext_clear`,
`set_power`, `ota_begin` (s `sig`), `ota_end`, `ota_abort`, `bus_scan`, `bus_diag`,
`device_info`, `set_pin`, `forget_bonds`. Od firmwarea 2.1.0 veza mora biti uparena (PIN).

## Poruke s ESP-a (polje `type`)

`ack`, `status`, `link`, `protocol_probe`, `protocol`, `device`, `capability`
(u dijelovima), `telemetry` (dugačke dolaze u dijelovima s `"partial":true`),
`security`, `dtc`, `clear_dtc`, `freeze_frame`, `readiness`, `monitor_test`,
`monitor_tests_done`, `vehicle_info`, `ext_add`, `ext_list`, `ota`, `log`,
`raw_frame`, `bus_diag`, `bus_scan_start`, `bus_scan_result`, `bus_scan_done`.

Primjer telemetrije:

```json
{ "type": "telemetry", "status": "ok", "rpm": 1840, "coolant_c": 86, "speed_kmh": 42,
  "konaev_hvbat_soc": 78.5, "ts": 1713000000 }
```
