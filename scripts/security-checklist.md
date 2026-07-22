# Privacy / security checklist

Manual + automated gates for Phase S.

## Automated

- [ ] `make test-host` (includes `test_random`)
- [ ] `./scripts/purity-check.sh`
- [ ] Docs present: `docs/security-model.md`, `privacy.md`, `csprng.md`, `verified-boot.md`
- [ ] `python3 scripts/mkmanifest.py` after `make iso`
- [ ] No `RWE` LOAD PHDRs: `llvm-readelf -lW build/x86_64/kernel.elf`

## Boot / entropy

- [ ] Serial shows `cpu-sec:` feature line (no seeds)
- [ ] Serial shows `Entropy (crypto ready)` or clear degraded status
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

## Network identity

- [ ] HTTP User-Agent is `PeakBrowser/1` (no version/OS detail)

## Persist

- [ ] PEAKDSK2 encrypted save when crypto RNG ready (experimental header key)
- [ ] Agent audit not wiped across PeakFS restore
- [ ] Agent cannot write `/var/peak/audit.log`

## Negative smoke

- [ ] `PEAK_FIRMWARE=bios ./scripts/smoke-qemu.sh`
- [ ] `PEAK_FIRMWARE=uefi ./scripts/smoke-qemu.sh` (when OVMF present)
