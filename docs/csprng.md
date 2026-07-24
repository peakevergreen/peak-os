# Peak OS CSPRNG

Kernel random service: `kernel/random.c` / `kernel/include/random.h`.

## Construction

1. **Entropy pool** — SHA-256 accumulation of boot seed + continuous mix events
2. **DRBG** — ChaCha20 keyed from extracted pool material; reseed on counter / time threshold
3. **Domains** — separate derivation labels for `crypto`, `aslr`, `canary`, `alloc`

`crypto_random()` is a thin wrapper that fails closed when the crypto domain is not ready (unless `PEAK_DEV_INSECURE_RNG=1` build).

## Boot seed sources (priority)

1. UEFI `EFI_RNG_PROTOCOL` (when present)
2. `RDSEED` (CPUID leaf 7)
3. Health-checked `RDRAND`
4. **virtio-rng-pci** (QEMU: `-device virtio-rng-pci` in `run-qemu.sh` / smoke) — calls `random_absorb_trusted`
5. Timing jitter + RTC + mmap salt (supplemental; marks entropy weak)

BootInfo v4 carries `entropy[64]`, `entropy_len`, and quality flags (`PEAK_BOOT_FLAG_ENTROPY_OK` / `PEAK_BOOT_FLAG_ENTROPY_WEAK`).

## Continuous mix

Timer, keyboard, and mouse IRQs mix low TSC bits through `random_mix_*` only — never treat those alone as cryptographic.

## Safety gate

TLS handshake / X25519 keygen / PeakDisk key derivation call `random_get(RANDOM_DOMAIN_CRYPTO, ...)`. On failure in release mode: refuse the operation and audit/log a non-secret status line.

## Tests

- Host known-answer vectors for DRBG extract/expand
- Forced weak-entropy path
- Health-test rejection of constant RDRAND streams (simulated)

Do not log seed bytes.
