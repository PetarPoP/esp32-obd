#!/usr/bin/env python3
"""Sanity checks for the generated profiles and the firmware's signal decoder.

1. Every signal becomes an `ext_add` command that fits one BLE write.
2. Signal keys stay unique per model after the firmware truncates them.
3. A Python mirror of ExtPidService::DecodeSignal agrees with the OBDb
   reference bit extraction on random payloads.

Usage: python tools/check_profiles.py profiles/
"""

import gzip
import json
import os
import random
import sys

KEY_CHARS = 39          # ExtPidService::kMaxKeyChars
MAX_COMMAND_BYTES = 240  # one BLE write at MTU 247, minus headroom


def ext_add_command(cmd, sig):
    out = {"cmd": "ext_add", "k": sig["k"], "h": cmd["tx"], "r": cmd["rx"], "s": cmd["svc"], "d": cmd["did"]}
    for src, dst in (("eax", "e"), ("tst", "t"), ("din", "din")):
        if src in cmd:
            out[dst] = cmd[src]
    for key in ("b", "l", "m", "v", "a", "sg", "le", "mn", "mx", "nmin", "nmax", "x"):
        if key in sig:
            out[key] = sig[key]
    return json.dumps(out, separators=(",", ":"))


def firmware_key(key):
    """Mirror of the key shortening in ExtPidService::Add."""
    lower = key.lower()
    if len(lower) <= KEY_CHARS:
        return lower
    h = 2166136261
    for ch in lower.encode("ascii", "ignore"):
        h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return lower[:KEY_CHARS - 9] + f"_{h:08x}"


def reference_extract(data, bix, length, lsb):
    """OBDb .schemas python/can/signals.py _extract_bits."""
    data = bytearray(data)
    if lsb and length > 8:
        start_byte = bix // 8
        count = (length + 7) // 8
        data[start_byte:start_byte + count] = bytes(reversed(data[start_byte:start_byte + count]))
    result = 0
    for i in range(bix, bix + length):
        if data[i // 8] & (1 << (7 - i % 8)):
            result |= 1 << (bix + length - i - 1)
    return result


def firmware_extract(payload, bix, length, lsb):
    """Mirror of ExtPidService::DecodeSignal's bit extraction."""
    start_byte, end_byte = bix // 8, (bix + length + 7) // 8
    window = bytearray(payload[start_byte:end_byte])
    if lsb and length > 8:
        count = (length + 7) // 8
        window[:count] = bytes(reversed(window[:count]))
    raw = 0
    for bit in range(bix, bix + length):
        raw = (raw << 1) | ((window[bit // 8 - start_byte] >> (7 - bit % 8)) & 1)
    return raw


def main(profiles_dir):
    longest = 0
    commands = 0
    collisions = []
    with open(os.path.join(profiles_dir, "index.json"), encoding="utf-8") as f:
        index = json.load(f)
    for entry in index:
        with gzip.open(os.path.join(profiles_dir, entry["file"]), "rt", encoding="utf-8") as f:
            model = json.load(f)
        seen = {}
        for cmd in model["commands"]:
            for sig in cmd["signals"]:
                line = ext_add_command(cmd, sig)
                longest = max(longest, len(line.encode("utf-8")))
                commands += 1
                short = firmware_key(sig["k"])
                if short in seen and seen[short] != sig["k"]:
                    collisions.append((model["make"], model["model"], sig["k"], seen[short]))
                seen[short] = sig["k"]

    rng = random.Random(1)
    mismatches = 0
    for _ in range(20000):
        payload = bytes(rng.randrange(256) for _ in range(12))
        length = rng.randint(1, 32)
        bix = rng.randint(0, 96 - length)
        lsb = rng.random() < 0.5
        if firmware_extract(payload, bix, length, lsb) != reference_extract(payload, bix, length, lsb):
            mismatches += 1

    print(f"ext_add commands: {commands}, longest: {longest} bytes (limit {MAX_COMMAND_BYTES})")
    print(f"key collisions after truncation: {len(collisions)}")
    for c in collisions[:5]:
        print("  ", c)
    print(f"decoder mismatches vs OBDb reference: {mismatches} / 20000")
    return 0 if longest <= MAX_COMMAND_BYTES and mismatches == 0 else 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
