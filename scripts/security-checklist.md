# Privacy / security checklist

Manual + automated gates for Phase S.

## Automated (CI / smoke-cli)

- [x] `make test-host` (includes `test_random`, `test_agent_policy`) — CI `host-tests` job
- [x] `./scripts/purity-check.sh` — CI `host-tests` + `smoke-cli.sh`
- [x] `./scripts/smoke-cli.sh` — CI `host-tests` (Pass 19–49 + Wave 2 subsystem markers)
- [x] `./scripts/fuzz-elf-smoke.sh` (ELF loader + TLS ClientHello fuzz) — CI `host-tests`
- [x] Docs present: `docs/security-model.md`, `privacy.md`, `csprng.md`, `verified-boot.md` — CI `host-tests`
- [x] `python3 scripts/mkmanifest.py` after `make iso` — CI `host-tests`
- [x] No `RWE` LOAD PHDRs: `llvm-readelf -lW build/x86_64/kernel.elf` — CI `host-tests`
- [x] HTTP User-Agent is `PeakBrowser/1` (no version/OS detail) — `smoke-cli.sh`
- [x] Agent cannot write `/var/peak/audit.log` (host: `test_agent_policy` deny-audit) — `test-host`

## Boot / entropy

- [ ] Serial shows `Entropy (crypto ready)` or `Entropy (degraded)` status (no seed material)
- [ ] TLS handshake fails with `crypto RNG not ready` when degraded (release)

## Isolation

- [ ] User ELF W+X rejected
- [ ] Stack guard page unmapped below user stack
- [ ] Bad user pointer does not hang whole OS (process kill path)

## Privacy UX

- [ ] `privacy` shows persist=0 by default
- [ ] `privacy persist workspace` then Save disk works
- [ ] Private mode: Save disk skipped
- [ ] `wget` / Browser Go grants outbound for session only
- [ ] `ctr run` listens with localhost policy by default
- [ ] Kill switch blocks outbound

## Persist

- [ ] PEAKDSK2 encrypted save when crypto RNG ready (experimental header key)
- [ ] Agent audit not wiped across PeakFS restore (PeakFS clear preserves `audit.log`; host covers append/tail truncate)

## Negative smoke

- [ ] `PEAK_FIRMWARE=bios ./scripts/smoke-qemu.sh`
- [ ] `PEAK_FIRMWARE=uefi ./scripts/smoke-qemu.sh` (when OVMF present)


## CI automation (Wave 3 / Pass 78)

- [ ] `PEAK_SKIP_ISO=1 PEAK_SKIP_HOST_TESTS=0 ./scripts/smoke-cli.sh` passes Wave 3 Pass 59–77 marker blocks
- [ ] Host tests job (`make test-host`) green on main

## CI automation (Wave 5 / Pass 118)

- [ ] `PEAK_SKIP_ISO=1 PEAK_SKIP_HOST_TESTS=0 ./scripts/smoke-cli.sh` passes Wave 5 Pass 99–117 marker blocks
- [ ] Host tests job (`make test-host`) green on main

## CI automation (Wave 4 / Pass 98)

- [ ] `PEAK_SKIP_ISO=1 PEAK_SKIP_HOST_TESTS=0 ./scripts/smoke-cli.sh` passes Wave 4 Pass 79–97 marker blocks
- [ ] Host tests job (`make test-host`) green on main
