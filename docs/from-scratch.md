# From-scratch inventory

Peak OS is intentionally **not** a Linux userspace on a hobby kernel. Runtime guest features are Peak C.

## Boot

| Piece | Role |
|-------|------|
| `boot/bios/` | Peak BIOS El Torito loader (E820, VBE, ISO9660, long mode) |
| `boot/uefi/` | Peak UEFI application (GOP, simple FS, ExitBootServices) |
| `boot/common/` | Shared ELF load + paging + serial helpers |
| `boot/include/peak_boot.h` | Peak BootInfo ABI consumed by `kernel_entry` |

Developer tools (clang, lld, xorriso, QEMU, OVMF) stay on the host and are not shipped as guest packages.

## In-guest only

| Area | Implementation |
|------|----------------|
| Agent | Local intent planner + policy + audit + GUI write approval + PeakVec recall |
| Network | e1000 + IPv4/TCP/DNS/HTTP/HTTPS (TLS 1.2) |
| Browser | Lazy tabs; fetch only on explicit Enter/Go |
| Containers | `ctr` / Peak Container Runtime |

## Serial

COM1 only — local diagnostics and smoke tests. No COM2/COM3 host bridges.
