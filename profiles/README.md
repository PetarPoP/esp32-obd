# Profili vozila

Proizvođački dijagnostički signali (UDS servis 22, KWP servis 21) za 60 marki i
608 modela, pretvoreni iz [OBDb](https://github.com/OBDb) skriptom
`tools/obdb_to_profiles.py`.

- `index.json` – popis svih modela: marka, model, datoteka, broj upita i signala.
- `<Marka>.json.gz` – gzip JSON `{"make": ..., "models": [profil, ...]}`.

Format profila i značenje polja opisani su na vrhu `tools/obdb_to_profiles.py`,
a naredba `ext_add` kojom aplikacija šalje signal uređaju u glavnom README-u.

Mnogi OBDb signali su prijavljeni od zajednice i nisu provjereni na svakom
modelu i godištu (polje `years` ograničava neke na godišta), pa vrijednosti
treba provjeriti prije oslanjanja na njih.

## Licenca

Podaci su izvedeni iz OBDb projekta i objavljeni pod licencom
[Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
(CC BY-SA 4.0), kao i izvorni podaci. Izvor svakog modela naveden je u polju
`source` (npr. `OBDb https://github.com/OBDb/Hyundai-Kona-Electric`).
Licenca vrijedi samo za ove podatke, ne za firmware.
