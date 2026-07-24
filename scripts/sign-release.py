#!/usr/bin/env python3
"""Sign build/SHA256SUMS for Peak S8 release ceremony.

Prefers Ed25519 via cryptography or OpenSSL 3; falls back to HMAC-SHA256
seal (build/peak-release.hmac-key) on older OpenSSL (macOS stock).

Usage:
  python3 scripts/sign-release.py
  PEAK_RELEASE_KEY=path python3 scripts/sign-release.py
"""
from __future__ import annotations

import hashlib
import hmac
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
SUMS = BUILD / "SHA256SUMS"
SIG = BUILD / "SHA256SUMS.sig"
META = BUILD / "SHA256SUMS.sig.type"


def _have_ed25519_openssl() -> bool:
    try:
        out = subprocess.check_output(["openssl", "list", "-public-key-algorithms"], text=True)
        return "ED25519" in out.upper() or "Ed25519" in out
    except Exception:
        return False


def sign_ed25519_openssl(key: Path) -> None:
    if not key.is_file():
        BUILD.mkdir(parents=True, exist_ok=True)
        print(f"==> generating Ed25519 key at {key}")
        subprocess.check_call(["openssl", "genpkey", "-algorithm", "Ed25519", "-out", str(key)])
        pub = key.with_suffix(".pub")
        subprocess.check_call(["openssl", "pkey", "-in", str(key), "-pubout", "-out", str(pub)])
        print(f"==> public key: {pub}")
    subprocess.check_call(
        ["openssl", "pkeyutl", "-sign", "-inkey", str(key), "-rawin", "-in", str(SUMS), "-out", str(SIG)]
    )
    META.write_text("ed25519-openssl\n")


def sign_hmac(key: Path) -> None:
    if not key.is_file():
        BUILD.mkdir(parents=True, exist_ok=True)
        key.write_bytes(os.urandom(32))
        print(f"==> generated HMAC key at {key} (keep private)")
    secret = key.read_bytes()
    dig = hmac.new(secret, SUMS.read_bytes(), hashlib.sha256).digest()
    SIG.write_bytes(dig)
    META.write_text("hmac-sha256\n")
    # Publishable verifier material: SHA256 of key (not the key)
    (BUILD / "peak-release.hmac-id").write_text(hashlib.sha256(secret).hexdigest() + "\n")


def main() -> int:
    if not SUMS.is_file():
        print("FAIL: missing build/SHA256SUMS", file=sys.stderr)
        return 1
    if _have_ed25519_openssl():
        key = Path(os.environ.get("PEAK_RELEASE_KEY", BUILD / "peak-release.key"))
        sign_ed25519_openssl(key)
    else:
        key = Path(os.environ.get("PEAK_RELEASE_KEY", BUILD / "peak-release.hmac-key"))
        print("note: OpenSSL lacks Ed25519 — using HMAC-SHA256 seal")
        sign_hmac(key)
    print(f"ok: wrote {SIG} ({META.read_text().strip()})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
