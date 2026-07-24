#ifndef PEAK_VIRTIO_RNG_H
#define PEAK_VIRTIO_RNG_H

/* Probe virtio-rng-pci and absorb host entropy into the CSPRNG.
 * No-op success if crypto domain already ready. Returns 0 on seed/skip, -1 if
 * device missing or timed out. */
int virtio_rng_init(void);
int virtio_rng_seeded(void);

#endif
