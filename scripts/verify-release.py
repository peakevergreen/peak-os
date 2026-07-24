#!/usr/bin/env python3
"""Verify build/SHA256SUMS against build/SHA256SUMS.sig."""
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


def main() -> int:
    if not SUMS.is_file() or not SIG.is_file():
        print("FAIL: need SHA256SUMS and SHA256SUMS.sig", file=sys.stderr)
        return 1
    kind = META.read_text().strip() if META.is_file() else "ed25519-openssl"
    if kind == "hmac-sha256":
        key = Path(os.environ.get("PEAK_RELEASE_KEY", BUILD / "peak-release.hmac-key"))
        if not key.is_file():
            print(f"FAIL: missing HMAC key {key}", file=sys.stderr)
            return 1
        expect = hmac.new(key.read_bytes(), SUMS.read_bytes(), hashlib.sha256).digest()
        if not hmac.compare_digest(expect, SIG.read_bytes()):
            print("FAIL: HMAC verify", file=sys.stderr)
            return 1
        print("ok: SHA256SUMS HMAC seal valid")
        return 0

    pub = Path(os.environ.get("PEAK_RELEASE_PUB", BUILD / "peak-release.pub"))
    if not pub.is_file():
        print(f"FAIL: missing public key {pub}", file=sys.stderr)
        return 1
    try:
        subprocess.check_call(
            [
                "openssl",
                "pkeyutl",
                "-verify",
                "-pubin",
                "-inkey",
                str(pub),
                "-rawin",
                "-in",
                str(SUMS),
                "-sigfile",
                str(SIG),
            ]
        )
    except subprocess.CalledProcessError:
        print("FAIL: signature verify", file=sys.stderr)
        return 1
    print("ok: SHA256SUMS signature valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
