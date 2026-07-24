# HACL* NIST P-384 (ECDSA verify)

Vendored from Mozilla NSS `lib/freebl/verified` (HACL* / MIT).

Used only for ECDSA signature verification over secp384r1 (WebPKI path
building and TLS 1.2 ServerKeyExchange). Peak wrapper: `../crypto_p384.c`.

`freestanding/` holds libc header shims for kernel `-ffreestanding` builds
only (not on the host-test include path).

Do not edit generated HACL/Karamel files by hand; refresh from NSS when needed.
