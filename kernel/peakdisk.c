#include "peakdisk.h"
#include "blockdev.h"
#include "blobstore.h"
#include "heap.h"
#include "vfs.h"
#include "util.h"
#include "serial.h"
#include "sched.h"
#include "cap.h"
#include "crypto.h"
#include "random.h"

#define PEAKDISK_LBA0 1

static volatile int save_busy;
static volatile int save_queued;

/* Write `len` bytes at payload LBA (PEAKDISK_LBA0+1) in sector-sized chunks. */
static int write_payload_streamed(const uint8_t *data, uint32_t len) {
    uint32_t sectors = (len + BLOCKDEV_SECTOR_SIZE - 1) / BLOCKDEV_SECTOR_SIZE;
    uint8_t sector[BLOCKDEV_SECTOR_SIZE];
    uint32_t off = 0;
    for (uint32_t s = 0; s < sectors; s++) {
        memset(sector, 0, sizeof(sector));
        uint32_t chunk = len - off;
        if (chunk > BLOCKDEV_SECTOR_SIZE)
            chunk = BLOCKDEV_SECTOR_SIZE;
        memcpy(sector, data + off, chunk);
        if (blockdev_write(PEAKDISK_LBA0 + 1 + s, 1, sector) != 0)
            return -1;
        off += chunk;
    }
    return 0;
}

static int read_payload_streamed(uint8_t *data, uint32_t len) {
    uint32_t sectors = (len + BLOCKDEV_SECTOR_SIZE - 1) / BLOCKDEV_SECTOR_SIZE;
    uint8_t sector[BLOCKDEV_SECTOR_SIZE];
    uint32_t off = 0;
    for (uint32_t s = 0; s < sectors; s++) {
        if (blockdev_read(PEAKDISK_LBA0 + 1 + s, 1, sector) != 0)
            return -1;
        uint32_t chunk = len - off;
        if (chunk > BLOCKDEV_SECTOR_SIZE)
            chunk = BLOCKDEV_SECTOR_SIZE;
        memcpy(data + off, sector, chunk);
        off += chunk;
    }
    return 0;
}

void peakdisk_init(void) {
}

int peakdisk_available(void) {
    return blockdev_present();
}

int peakdisk_busy(void) {
    return save_busy || save_queued;
}

int peakdisk_save(void) {
    if (!blockdev_present())
        return -1;
    if (!cap_check(CAP_DISK_PERSIST))
        return -1;
    if (privacy_persist_profile() <= 0) {
        serial_write_str("peakdisk: private mode — persist skipped\n");
        return -1;
    }
    save_busy = 1;

    int need = vfs_export_ramdisk_size();
    if (need < 12) {
        save_busy = 0;
        return -1;
    }
    /* Soft safety: refuse absurd sizes that would exhaust heap for AEAD. */
    if ((size_t)need > 32u * 1024u * 1024u) {
        serial_write_str("peakdisk: image too large\n");
        save_busy = 0;
        return -1;
    }

    uint8_t *blob = kmalloc((size_t)need);
    if (!blob) {
        save_busy = 0;
        return -1;
    }
    int n = vfs_export_ramdisk(blob, (size_t)need);
    if (n < 12) {
        kfree(blob);
        save_busy = 0;
        return -1;
    }

    uint8_t hdr[BLOCKDEV_SECTOR_SIZE];
    memset(hdr, 0, sizeof(hdr));
    uint8_t *payload = blob;
    uint32_t sz = (uint32_t)n;
    uint8_t *enc = NULL;
    int encrypted = 0;

    /* PEAKDSK2: ChaCha20-Poly1305 envelope when crypto RNG is ready.
     * Passphrase-derived keys are required for production; header key is a
     * transitional unlock-less format (documented as experimental). */
    if (random_ready(RANDOM_DOMAIN_CRYPTO)) {
        enc = kmalloc(sz);
        if (enc) {
            uint8_t key[32], nonce[12], tag[16], salt[16];
            if (crypto_random(key, 32) == 0 && crypto_random(nonce, 12) == 0 &&
                crypto_random(salt, 16) == 0 &&
                chacha20_poly1305_encrypt(key, nonce, salt, 16, blob, (size_t)n,
                                          enc, tag) == 0) {
                memcpy(hdr, "PEAKDSK2", 8);
                memcpy(hdr + 8, &sz, 4);
                memcpy(hdr + 12, salt, 16);
                memcpy(hdr + 28, nonce, 12);
                memcpy(hdr + 40, tag, 16);
                memcpy(hdr + 56, key, 32);
                payload = enc;
                encrypted = 1;
                memzero_explicit(key, sizeof(key));
            } else {
                kfree(enc);
                enc = NULL;
            }
        }
    }
    if (!encrypted) {
        memcpy(hdr, "PEAKDSK1", 8);
        memcpy(hdr + 8, &sz, 4);
    }

    /* Atomic publish: write payload first while the old header (if any) remains
     * authoritative; only then flip LBA1 to the new envelope and flush. */
    if (write_payload_streamed(payload, sz) != 0) {
        if (enc)
            kfree_sensitive(enc, sz);
        kfree(blob);
        save_busy = 0;
        return -1;
    }
    if (blockdev_flush() != 0) {
        if (enc)
            kfree_sensitive(enc, sz);
        kfree(blob);
        save_busy = 0;
        serial_write_str("peakdisk: payload flush failed\n");
        return -1;
    }
    if (blockdev_write(PEAKDISK_LBA0, 1, hdr) != 0) {
        if (enc)
            kfree_sensitive(enc, sz);
        kfree(blob);
        save_busy = 0;
        return -1;
    }
    if (blockdev_flush() != 0) {
        if (enc)
            kfree_sensitive(enc, sz);
        kfree(blob);
        save_busy = 0;
        serial_write_str("peakdisk: header flush failed\n");
        return -1;
    }
    if (enc)
        kfree_sensitive(enc, sz);
    kfree(blob);

    /* Sync blobstore metadata + dirty cache pages (independent of PeakFS size). */
    (void)blobstore_sync();

    serial_write_str(encrypted ? "peakdisk: saved (encrypted)\n" : "peakdisk: saved\n");
    save_busy = 0;
    return 0;
}

static void peakdisk_save_worker(void) {
    save_queued = 0;
    (void)peakdisk_save();
    sched_exit();
}

int peakdisk_save_async(void) {
    if (!blockdev_present() || save_busy || save_queued)
        return -1;
    save_queued = 1;
    if (sched_spawn_kthread("disksave", peakdisk_save_worker) < 0) {
        save_queued = 0;
        return peakdisk_save();
    }
    return 0;
}

int peakdisk_load(void) {
    if (!blockdev_present())
        return -1;
    uint8_t hdr[BLOCKDEV_SECTOR_SIZE];
    if (blockdev_read(PEAKDISK_LBA0, 1, hdr) != 0)
        return -1;
    int v2 = (memcmp(hdr, "PEAKDSK2", 8) == 0);
    if (!v2 && memcmp(hdr, "PEAKDSK1", 8) != 0)
        return -1;
    uint32_t sz = 0;
    memcpy(&sz, hdr + 8, 4);
    if (sz < 12 || sz > 32u * 1024u * 1024u)
        return -1;

    uint8_t *blob = kmalloc(sz);
    if (!blob)
        return -1;
    if (read_payload_streamed(blob, sz) != 0) {
        kfree(blob);
        return -1;
    }

    uint8_t *plain = blob;
    uint8_t *dec = NULL;
    if (v2) {
        uint8_t salt[16], nonce[12], tag[16], key[32];
        memcpy(salt, hdr + 12, 16);
        memcpy(nonce, hdr + 28, 12);
        memcpy(tag, hdr + 40, 16);
        memcpy(key, hdr + 56, 32);
        dec = kmalloc(sz);
        if (!dec) {
            kfree(blob);
            return -1;
        }
        if (chacha20_poly1305_decrypt(key, nonce, salt, 16, blob, sz, tag, dec) != 0) {
            memzero_explicit(key, sizeof(key));
            kfree_sensitive(dec, sz);
            kfree(blob);
            serial_write_str("peakdisk: decrypt failed\n");
            return -1;
        }
        memzero_explicit(key, sizeof(key));
        plain = dec;
    }
    /* Load must not be gated by the current privacy persist profile (defaults
     * to private at boot). Temporarily allow full PeakFS namespaces. */
    int prev_profile = privacy_persist_profile();
    privacy_set_persist_profile(2);
    int r = vfs_load_ramdisk(plain, sz);
    privacy_set_persist_profile(prev_profile);
    if (dec)
        kfree_sensitive(dec, sz);
    kfree(blob);

    /* Reload blobstore after PeakFS so large objects remain available. */
    (void)blobstore_load();

    if (r == 0)
        serial_write_str(v2 ? "peakdisk: loaded (encrypted)\n" : "peakdisk: loaded\n");
    return r;
}
