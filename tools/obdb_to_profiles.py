#!/usr/bin/env python3
"""Converts OBDb signal sets into compact vehicle profiles for the app.

OBDb (https://github.com/OBDb, CC BY-SA 4.0) documents manufacturer-specific
diagnostic parameters (Mode 21/22) per make and model. This script turns each
`signalsets/v3/default.json` into a profile whose commands carry full CAN IDs,
so the app can send every signal to the firmware with the `ext_add` command.

Usage:
    python tools/obdb_to_profiles.py <dir with Make-Model.json files> profiles/

Output: profiles/index.json (every make and model) and one gzip-compressed
profile per model, profiles/<Make>/<Model>.json.gz. A copy of the index is
written to mobile/src/data/profile-index.json for the app.

Profile format:
{
  "make": "Hyundai", "model": "Kona Electric", "source": "OBDb ...", "license": "CC BY-SA 4.0",
  "commands": [
    {"tx": "7E4", "rx": "7EC", "svc": "22", "did": "0101",
     "eax": "12",            # optional: ISO-TP extended address byte (BMW)
     "din": "03",            # optional: diagnostic session to enter first
     "years": {...},         # optional: OBDb model year filter
     "freq": 1.0,            # suggested poll period, seconds
     "signals": [
       {"k": "KONAEV_HVBAT_SOC", "n": "HV battery charge", "u": "percent", "path": "Battery",
        "b": 32, "l": 8,     # bit index and bit length in the payload after SID + DID
        "m": 1, "v": 2, "a": 0,    # value = raw * m / v + a
        "sg": 1, "le": 1,    # optional: signed, little-endian bytes
        "mn": 0, "mx": 100,  # optional: clamp range
        "nmin": .., "nmax": .., # optional: values at/below/above these mean "no data"
        "map": {"0": "off", ...}, "x": 1}  # optional enum map; x = decimals
     ]}
  ]
}
"""

import gzip
import json
import math
import os
import sys

NL = chr(10)
MULTI_WORD_MAKES = ["Mercedes-Benz", "Land-Rover", "Alfa-Romeo", "Rolls-Royce", "Aston-Martin"]
# OBDb spells a few makes in more than one way.
CANONICAL_MAKES = {"fiat": "Fiat", "seat": "SEAT", "smart": "smart"}


def split_name(stem):
    make, model = stem, ""
    for candidate in MULTI_WORD_MAKES:
        if stem == candidate or stem.startswith(candidate + "-"):
            make, model = candidate, stem[len(candidate) + 1:]
            break
    else:
        if "-" in stem:
            make, model = stem.split("-", 1)
    make = CANONICAL_MAKES.get(make.lower(), make)
    if model.lower() == make.lower():
        model = ""
    return make, model


def can_ids(cmd):
    """Returns (tx, rx) as hex strings with full CAN identifiers."""
    hdr = cmd["hdr"].upper()
    rax = cmd.get("rax", "").upper()
    if len(hdr) == 3:
        tx = int(hdr, 16)
        rx = int(rax, 16) if rax else tx + 8
        return f"{tx:03X}", f"{rx:03X}"
    # 29-bit: priority, two header bytes, tester address.
    pri = int(cmd.get("pri", "18"), 16)
    tst = int(cmd.get("tst", "F1"), 16)
    tx = (pri << 24) | (int(hdr, 16) << 8) | tst
    if len(rax) == 8:
        rx = int(rax, 16)
    elif len(rax) == 6:
        rx = (pri << 24) | int(rax, 16)
    elif len(rax) in (1, 2):
        rx = (pri << 24) | (0xDA << 16) | (tst << 8) | int(rax, 16)
    else:
        rx = (pri << 24) | (0xDA << 16) | (tst << 8) | (int(hdr, 16) & 0xFF)
    return f"{tx:08X}", f"{rx:08X}"


def decimals_for(fmt):
    step = abs(fmt.get("mul", 1) / fmt.get("div", 1))
    if step == 0 or (step >= 1 and float(step).is_integer() and float(fmt.get("add", 0)).is_integer()):
        return 0
    return max(0, min(4, math.ceil(-math.log10(step)) if step < 1 else 1))


def convert_signal(sig):
    fmt = sig["fmt"]
    out = {"k": sig["id"], "n": sig.get("name", ""), "u": fmt.get("unit", "")}
    if sig.get("path"):
        out["path"] = sig["path"]
    out["b"] = fmt.get("bix", 0)
    out["l"] = fmt["len"]
    for src, dst, default in (("mul", "m", 1), ("div", "v", 1), ("add", "a", 0)):
        if fmt.get(src, default) != default:
            out[dst] = fmt[src]
    if fmt.get("sign"):
        out["sg"] = 1
    if fmt.get("blsb"):
        out["le"] = 1
    if "min" in fmt:
        out["mn"] = fmt["min"]
    if "max" in fmt:
        out["mx"] = fmt["max"]
    if "nullmin" in fmt:
        out["nmin"] = fmt["nullmin"]
    if "nullmax" in fmt:
        out["nmax"] = fmt["nullmax"]
    if "map" in fmt:
        out["map"] = {k: (v.get("value") if isinstance(v, dict) else v) for k, v in fmt["map"].items()}
    else:
        x = decimals_for(fmt)
        if x:
            out["x"] = x
    return out


def convert_command(cmd):
    svc, did = next(iter(cmd["cmd"].items()))
    if svc not in ("21", "22"):
        return None  # Mode 01 is handled by the firmware's standard catalogue
    signals = [convert_signal(s) for s in cmd.get("signals", []) if s["fmt"].get("len", 0) <= 32]
    if not signals:
        return None
    tx, rx = can_ids(cmd)
    out = {"tx": tx, "rx": rx, "svc": svc, "did": did.upper()}
    if "eax" in cmd:
        out["eax"] = cmd["eax"].upper()
        if "tst" in cmd and len(cmd["hdr"]) == 3:
            out["tst"] = cmd["tst"].upper()
    if "din" in cmd:
        out["din"] = cmd["din"].upper()
    if "filter" in cmd:
        out["years"] = cmd["filter"]
    out["freq"] = cmd.get("freq", 1)
    out["signals"] = signals
    return out


def slug(text):
    return "".join(c if c.isalnum() or c in "-_." else "-" for c in text).strip("-") or "default"


def main(src_dir, out_dir):
    index = []
    for name in sorted(os.listdir(src_dir)):
        if not name.endswith(".json"):
            continue
        stem = name[:-5]
        if stem == "SAEJ1979":
            continue
        with open(os.path.join(src_dir, name), encoding="utf-8") as f:
            data = json.load(f)
        commands = [c for c in (convert_command(c) for c in data.get("commands", [])) if c]
        if not commands:
            continue
        make, model = split_name(stem)
        profile = {
            "make": make.replace("-", " "),
            "model": model.replace("-", " ") or make.replace("-", " "),
            "source": f"OBDb https://github.com/OBDb/{stem}",
            "license": "CC BY-SA 4.0",
            "commands": commands,
        }
        rel = f"{slug(make)}/{slug(model or make)}.json.gz"
        os.makedirs(os.path.join(out_dir, slug(make)), exist_ok=True)
        payload = json.dumps(profile, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        # mtime=0 keeps the output byte-identical between runs.
        with open(os.path.join(out_dir, rel), "wb") as f:
            f.write(gzip.compress(payload, compresslevel=9, mtime=0))
        index.append({
            "make": profile["make"],
            "model": profile["model"],
            "file": rel,
            "commands": len(commands),
            "signals": sum(len(c["signals"]) for c in commands),
        })
    with open(os.path.join(out_dir, "index.json"), "w", encoding="utf-8", newline=NL) as f:
        json.dump(index, f, ensure_ascii=False, indent=1)
    app_data = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mobile", "src", "data")
    if os.path.isdir(app_data):
        with open(os.path.join(app_data, "profile-index.json"), "w", encoding="utf-8", newline=NL) as f:
            json.dump(index, f, ensure_ascii=False, separators=(",", ":"))
    makes = len({i["make"] for i in index})
    print(f"{makes} makes, {len(index)} models, {sum(i['signals'] for i in index)} signals")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
