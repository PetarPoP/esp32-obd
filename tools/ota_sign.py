#!/usr/bin/env python3
"""ECDSA P-256 signing of firmware images for OTA updates (no extra packages).

The ESP accepts an OTA image only when its SHA-256 carries a valid signature
from the key whose public half is compiled into the firmware
(esp32-obd/ota_signing_key.h). The private key never goes into the repo.

    python tools/ota_sign.py keygen [--key PATH]   # new key + ota_signing_key.h
    python tools/ota_sign.py sign FILE [--key PATH] # prints the signature (hex r||s)
    python tools/ota_sign.py verify FILE SIG        # checks against ota_signing_key.h

The key file holds the private scalar as 64 hex characters. Default location:
$OTA_SIGNING_KEY, else ~/.esp32-obd/ota_signing_key.hex. In CI the key can be
passed as the hex string itself in $OTA_SIGNING_KEY_HEX.
"""

import argparse
import hashlib
import hmac
import os
import re
import secrets
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "esp32-obd", "ota_signing_key.h")

# NIST P-256 (secp256r1)
P = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
A = P - 3
B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
N = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
G = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
     0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)


def _add(p, q):
    if p is None:
        return q
    if q is None:
        return p
    if p[0] == q[0] and (p[1] + q[1]) % P == 0:
        return None
    if p == q:
        lam = (3 * p[0] * p[0] + A) * pow(2 * p[1], -1, P) % P
    else:
        lam = (q[1] - p[1]) * pow(q[0] - p[0], -1, P) % P
    x = (lam * lam - p[0] - q[0]) % P
    return x, (lam * (p[0] - x) - p[1]) % P


def _mul(k, point):
    result = None
    while k:
        if k & 1:
            result = _add(result, point)
        point = _add(point, point)
        k >>= 1
    return result


def _rfc6979_k(d, digest):
    """Deterministic nonce (RFC 6979, HMAC-SHA256), so signing needs no RNG."""
    x = d.to_bytes(32, "big")
    h = (int.from_bytes(digest, "big") % N).to_bytes(32, "big")
    v, k = b"\x01" * 32, b"\x00" * 32
    k = hmac.new(k, v + b"\x00" + x + h, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    k = hmac.new(k, v + b"\x01" + x + h, hashlib.sha256).digest()
    v = hmac.new(k, v, hashlib.sha256).digest()
    while True:
        v = hmac.new(k, v, hashlib.sha256).digest()
        candidate = int.from_bytes(v, "big")
        if 1 <= candidate < N:
            return candidate
        k = hmac.new(k, v + b"\x00", hashlib.sha256).digest()
        v = hmac.new(k, v, hashlib.sha256).digest()


def sign_digest(d, digest):
    z = int.from_bytes(digest, "big")
    k = _rfc6979_k(d, digest)
    r = _mul(k, G)[0] % N
    s = pow(k, -1, N) * (z + r * d) % N
    if s > N // 2:  # low-S form
        s = N - s
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify_digest(pub, digest, sig):
    r, s = int.from_bytes(sig[:32], "big"), int.from_bytes(sig[32:], "big")
    if not (1 <= r < N and 1 <= s < N):
        return False
    w = pow(s, -1, N)
    point = _add(_mul(int.from_bytes(digest, "big") * w % N, G), _mul(r * w % N, pub))
    return point is not None and point[0] % N == r


def default_key_path():
    return os.environ.get("OTA_SIGNING_KEY") or os.path.join(os.path.expanduser("~"), ".esp32-obd", "ota_signing_key.hex")


def load_private_key(path=None):
    text = os.environ.get("OTA_SIGNING_KEY_HEX") if path is None else None
    if not text:
        path = path or default_key_path()
        if not os.path.exists(path):
            sys.exit(f"signing key not found: {path} (run: python tools/ota_sign.py keygen)")
        with open(path, encoding="ascii") as f:
            text = f.read()
    text = text.strip()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", text):
        sys.exit("signing key must be 64 hex characters")
    d = int(text, 16)
    if not 1 <= d < N:
        sys.exit("signing key out of range")
    return d


def header_public_key():
    with open(HEADER, encoding="utf-8") as f:
        body = f.read().split("kOtaPublicKey", 1)[1]
    raw = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", body)[:65])
    return int.from_bytes(raw[1:33], "big"), int.from_bytes(raw[33:65], "big")


def sign_file(path, key_path=None):
    with open(path, "rb") as f:
        digest = hashlib.sha256(f.read()).digest()
    sig = sign_digest(load_private_key(key_path), digest)
    if not verify_digest(header_public_key(), digest, sig):
        sys.exit("the signing key does not match esp32-obd/ota_signing_key.h")
    return sig.hex()


def write_header(pub):
    raw = b"\x04" + pub[0].to_bytes(32, "big") + pub[1].to_bytes(32, "big")
    rows = []
    for i in range(0, len(raw), 13):
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in raw[i:i + 13]) + ",")
    with open(HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write(
            "#pragma once\n\n#include <stdint.h>\n\n"
            "// Public half of the OTA signing key (uncompressed P-256 point), written by\n"
            "// `python tools/ota_sign.py keygen`. The ESP installs an OTA image only when\n"
            "// its SHA-256 is signed with the matching private key.\n"
            "namespace obd {\nnamespace config {\n\n"
            "constexpr uint8_t kOtaPublicKey[65] = {\n" + "\n".join(rows) + "\n};\n\n"
            "}  // namespace config\n}  // namespace obd\n"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("keygen")
    p.add_argument("--key")
    p.add_argument("--force", action="store_true")
    p = sub.add_parser("sign")
    p.add_argument("file")
    p.add_argument("--key")
    p = sub.add_parser("verify")
    p.add_argument("file")
    p.add_argument("sig")
    args = parser.parse_args()

    if args.cmd == "keygen":
        path = args.key or default_key_path()
        if os.path.exists(path) and not args.force:
            sys.exit(f"{path} already exists (use --force to replace it; old firmware then rejects new images)")
        d = secrets.randbelow(N - 1) + 1
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w", encoding="ascii") as f:
            f.write(f"{d:064x}\n")
        write_header(_mul(d, G))
        print(f"private key: {path} (back it up, keep it out of git)")
        print(f"public key:  {HEADER}")
    elif args.cmd == "sign":
        print(sign_file(args.file, args.key))
    else:
        with open(args.file, "rb") as f:
            digest = hashlib.sha256(f.read()).digest()
        ok = verify_digest(header_public_key(), digest, bytes.fromhex(args.sig))
        print("signature OK" if ok else "signature INVALID")
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
