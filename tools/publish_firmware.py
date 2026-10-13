#!/usr/bin/env python3
"""Builds the firmware and writes firmware/esp32-obd.bin + firmware/version.json.

The mobile app reads firmware/version.json from GitHub, compares `version`
with the version the ESP reports and, on request, downloads the .bin and sends
it to the ESP over BLE (OTA). The ESP checks the MD5 before it switches over.

The version is kFirmwareVersion in esp32-obd/esp32-obd.ino: raise it before
publishing, otherwise the app sees no update.

Usage:
    python tools/publish_firmware.py [--arduino-cli PATH] [--fqbn esp32:esp32:esp32]
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKETCH = os.path.join(ROOT, "esp32-obd")
OUT = os.path.join(ROOT, "firmware")
BIN_NAME = "esp32-obd.bin"
DEFAULT_CLI = r"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"


def find_cli(explicit):
    for candidate in (explicit, os.environ.get("ARDUINO_CLI"), shutil.which("arduino-cli"), DEFAULT_CLI):
        if candidate and os.path.exists(candidate):
            return candidate
    sys.exit("arduino-cli not found: pass --arduino-cli or set ARDUINO_CLI")


def sketch_version():
    with open(os.path.join(SKETCH, "esp32-obd.ino"), encoding="utf-8") as f:
        match = re.search(r'kFirmwareVersion\[\]\s*=\s*"([0-9]+(?:\.[0-9]+)*)"', f.read())
    if not match:
        sys.exit("kFirmwareVersion not found in esp32-obd.ino")
    return match.group(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--arduino-cli")
    parser.add_argument("--fqbn", default="esp32:esp32:esp32")
    args = parser.parse_args()

    cli = find_cli(args.arduino_cli)
    version = sketch_version()
    with tempfile.TemporaryDirectory() as build:
        subprocess.run([cli, "compile", "--fqbn", args.fqbn, "--build-path", build, SKETCH], check=True)
        built = os.path.join(build, "esp32-obd.ino.bin")
        if not os.path.exists(built):
            sys.exit(f"build output not found: {built}")
        os.makedirs(OUT, exist_ok=True)
        shutil.copyfile(built, os.path.join(OUT, BIN_NAME))

    with open(os.path.join(OUT, BIN_NAME), "rb") as f:
        data = f.read()
    info = {"version": version, "file": BIN_NAME, "size": len(data), "md5": hashlib.md5(data).hexdigest()}
    with open(os.path.join(OUT, "version.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(info, f, indent=1)
        f.write("\n")
    print(f"firmware {version}: {len(data)} bytes, md5 {info['md5']}")


if __name__ == "__main__":
    main()
