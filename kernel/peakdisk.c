#include "peakdisk.h"
#include "blockdev.h"
#include "blobstore.h"
#include "heap.h"
#include "pmm.h"
#include "vfs.h"
#include "util.h"
#include "serial.h"
#include "sched.h"
#include "cap.h"
#include "privacy.h"
#include "crypto.h"
#include "random.h"
#include "timer.h"

#define PEAKDISK_LBA0 1
#define PEAKDISK_KDF_ITERS 4096u
#define PEAKDISK_AUTOSAVE_DEFAULT_SEC 120u
/* Export blob + optional AEAD buffer must leave headroom for wallpaper/surfaces. */
#define PEAKDISK_SAVE_HEAP_MARGIN (2u * 1024u * 1024u)
#define PEAKDISK_PAGE_SIZE 4096ULL

static volatile int save_busy;
static volatile int save_queued;
static char peakdisk_err[96];
static uint32_t peakdisk_saved_bytes;
static uint32_t peakdisk_progress_pct;
static char g_pass[128];
static size_t g_pass_len;
static volatile int workspace_dirty;
static uint32_t autosave_interval_sec = PEAKDISK_AUTOSAVE_DEFAULT_SEC;
static uint64_t last_save_uptime_sec;
static uint64_t dirty_since_uptime_sec;

static void peakdisk_save_worker(void);

/* True if heap+PMM can hold dual PeakFS buffers (image + AEAD) plus GUI margin. */
static int peakdisk_heap_ok_for_image(size_t image_bytes) {
    uint64_t used = 0, freeb = 0, blocks = 0;
    heap_get_stats(&used, &freeb, &blocks);
    (void)used;
    (void)blocks;
    uint64_t avail = freeb + pmm_free_pages() * PEAKDISK_PAGE_SIZE;
    uint64_t need = (uint64_t)image_bytes * 2u + (uint64_t)PEAKDISK_SAVE_HEAP_MARGIN;
    return avail >= need;
}

void peakdisk_set_passphrase(const char *pass) {
    memzero_explicit(g_pass, sizeof(g_pass));
    g_pass_len = 0;
    if (!pass || !pass[0])
        return;
    size_t n = strlen(pass);
    if (n >= sizeof(g_pass))
        n = sizeof(g_pass) - 1;
    memcpy(g_pass, pass, n);
    g_pass_len = n;
}

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
        if ((s & 15u) == 15u)
            sched_maybe_preempt();
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
    last_save_uptime_sec = timer_uptime_secs();
}

void peakdisk_mark_dirty(void) {
    if (!workspace_dirty) {
        workspace_dirty = 1;
        dirty_since_uptime_sec = timer_uptime_secs();
    }
}

int peakdisk_is_dirty(void) {
    return workspace_dirty;
}

void peakdisk_set_autosave_interval_sec(uint32_t sec) {
    autosave_interval_sec = sec;
}

uint32_t peakdisk_autosave_interval_sec(void) {
    return autosave_interval_sec;
}

void peakdisk_autosave_tick(void) {
    /* Retry worker spawn if queued but never started (task table full). */
    if (save_queued && !save_busy) {
        if (sched_spawn_kthread("disksave", peakdisk_save_worker) == 0)
            return;
        return;
    }
    if (!workspace_dirty || !autosave_interval_sec || !blockdev_present())
        return;
    if (save_busy || save_queued)
        return;
    if (privacy_persist_profile() <= 0)
        return;
    if (!cap_check(CAP_DISK_PERSIST))
        return;
    uint64_t now = timer_uptime_secs();
    if (now - dirty_since_uptime_sec < autosave_interval_sec)
        return;
    /* Defer under heap pressure so dual PeakFS alloc cannot OOM the GUI. */
    int need = vfs_export_ramdisk_size();
    if (need >= 12 && !peakdisk_heap_ok_for_image((size_t)need)) {
        serial_log(SERIAL_LOG_INFO, "peakdisk: autosave deferred (heap pressure)\n");
        return;
    }
    if (peakdisk_save_async() == 0)
        serial_log(SERIAL_LOG_INFO, "peakdisk: autosave queued\n");
}

int peakdisk_available(void) {
    return blockdev_present();
}

int peakdisk_busy(void) {
    return save_busy || save_queued;
}

static void peakdisk_set_err(const char *msg) {
    size_t i = 0;
    if (!msg)
        msg = "unknown error";
    for (; msg[i] && i + 1 < sizeof(peakdisk_err); i++)
        peakdisk_err[i] = msg[i];
    peakdisk_err[i] = '\0';
}

const char *peakdisk_last_error(void) {
    return peakdisk_err;
}

uint32_t peakdisk_last_save_bytes(void) {
    return peakdisk_saved_bytes;
}

int peakdisk_save(void) {
    peakdisk_set_err("");
    if (!blockdev_present()) {
        peakdisk_set_err("no block device (ATA/SD not present)");
        return -1;
    }
    if (!cap_check(CAP_DISK_PERSIST)) {
        peakdisk_set_err("CAP_DISK_PERSIST not granted");
        return -1;
    }
    if (privacy_persist_profile() <= 0) {
        peakdisk_set_err("private profile — enable workspace persist first");
        serial_log(SERIAL_LOG_INFO, "peakdisk: private mode — persist skipped\n");
        return -1;
    }
    save_busy = 1;
    peakdisk_progress_pct = 10;
    sched_maybe_preempt();

    int need = vfs_export_ramdisk_size();
    if (need < 12) {
        peakdisk_set_err(vfs_export_last_error()[0] ? vfs_export_last_error() : "export size invalid (empty workspace?)");
        serial_log(SERIAL_LOG_WARN, "peakdisk: export size invalid\n");
        save_busy = 0;
        return -1;
    }
    /* Soft safety: refuse absurd sizes that would exhaust heap for AEAD. */
    if ((size_t)need > 32u * 1024u * 1024u) {
        peakdisk_set_err("workspace image too large (>32 MiB)");
        serial_log(SERIAL_LOG_WARN, "peakdisk: image too large\n");
        save_busy = 0;
        return -1;
    }
    if (!peakdisk_heap_ok_for_image((size_t)need)) {
        peakdisk_set_err("insufficient heap for save (export+AEAD)");
        serial_log(SERIAL_LOG_WARN, "peakdisk: save refused (heap pressure)\n");
        save_busy = 0;
        return -1;
    }

    uint8_t *blob = kmalloc((size_t)need);
    if (!blob) {
        peakdisk_set_err("out of memory for export buffer");
        serial_log(SERIAL_LOG_WARN, "peakdisk: export alloc failed\n");
        save_busy = 0;
        return -1;
    }
    peakdisk_progress_pct = 25;
    sched_maybe_preempt();
    int n = vfs_export_ramdisk(blob, (size_t)need);
    if (n < 12) {
        peakdisk_set_err(vfs_export_last_error()[0] ? vfs_export_last_error() : "PeakFS export failed");
        serial_log(SERIAL_LOG_WARN, "peakdisk: export failed\n");
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

    peakdisk_progress_pct = 45;
    sched_maybe_preempt();
    /* PEAKDSK3: passphrase PBKDF2 → volume key. Key never stored in header.
     * Without a passphrase, fall back to clear PEAKDSK1 (no PEAKDSK2 header key). */
    if (random_ready(RANDOM_DOMAIN_CRYPTO) && g_pass_len > 0) {
        enc = kmalloc(sz);
        if (enc) {
            uint8_t key[32], nonce[12], tag[16], salt[16];
            memset(key, 0, sizeof(key));
            if (crypto_random(salt, 16) == 0 && crypto_random(nonce, 12) == 0 &&
                pbkdf2_hmac_sha256((const uint8_t *)g_pass, g_pass_len, salt, 16,
                                   PEAKDISK_KDF_ITERS, key, 32) == 0 &&
                chacha20_poly1305_encrypt(key, nonce, salt, 16, blob, (size_t)n,
                                          enc, tag) == 0) {
                memcpy(hdr, "PEAKDSK3", 8);
                memcpy(hdr + 8, &sz, 4);
                memcpy(hdr + 12, salt, 16);
                memcpy(hdr + 28, nonce, 12);
                memcpy(hdr + 40, tag, 16);
                /* hdr+56 left zero — no clear key */
                uint32_t iters = PEAKDISK_KDF_ITERS;
                memcpy(hdr + 56, &iters, 4);
                payload = enc;
                encrypted = 1;
                memzero_explicit(key, sizeof(key));
            } else {
                memzero_explicit(key, sizeof(key));
                kfree(enc);
                enc = NULL;
            }
        }
    }
    peakdisk_progress_pct = 70;
    sched_maybe_preempt();
    if (!encrypted) {
        memcpy(hdr, "PEAKDSK1", 8);
        memcpy(hdr + 8, &sz, 4);
    }

    /* Atomic publish: write payload first while the old header (if any) remains
     * authoritative; only then flip LBA1 to the new envelope and flush. */
    if (write_payload_streamed(payload, sz) != 0) {
        peakdisk_set_err("payload write to disk failed");
        serial_log(SERIAL_LOG_WARN, "peakdisk: payload write failed\n");
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
        peakdisk_set_err("disk flush failed after payload");
        serial_log(SERIAL_LOG_WARN, "peakdisk: payload flush failed\n");
        return -1;
    }
    if (blockdev_write(PEAKDISK_LBA0, 1, hdr) != 0) {
        peakdisk_set_err("header write failed");
        serial_log(SERIAL_LOG_WARN, "peakdisk: header write failed\n");
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
        peakdisk_set_err("disk flush failed after header");
        serial_log(SERIAL_LOG_WARN, "peakdisk: header flush failed\n");
        return -1;
    }
    if (enc)
        kfree_sensitive(enc, sz);
    kfree(blob);

    /* Sync blobstore metadata + dirty cache pages (independent of PeakFS size). */
    (void)blobstore_sync();

    peakdisk_saved_bytes = sz;
    peakdisk_progress_pct = 100;
    workspace_dirty = 0;
    last_save_uptime_sec = timer_uptime_secs();
    serial_log(SERIAL_LOG_INFO,
               encrypted ? "peakdisk: saved (encrypted)\n" : "peakdisk: saved\n");
    save_busy = 0;
    return 0;
}

static void peakdisk_save_worker(void) {
    save_queued = 0;
    (void)peakdisk_save();
    sched_exit();
}

uint32_t peakdisk_save_progress_pct(void) {
    if (!peakdisk_busy()) return peakdisk_progress_pct;
    return peakdisk_progress_pct < 95 ? peakdisk_progress_pct + 5 : 95;
}

int peakdisk_save_async(void) {
    if (!blockdev_present()) {
        peakdisk_set_err("no block device (ATA/SD not present)");
        return -1;
    }
    if (save_busy || save_queued) {
        peakdisk_progress_pct = 5;
        peakdisk_set_err("save already in progress");
        return -1;
    }
    save_queued = 1;
    if (sched_spawn_kthread("disksave", peakdisk_save_worker) < 0) {
        /* Keep queued — never sync-save on the GUI thread; retry from tick. */
        peakdisk_set_err("save queued (waiting for worker slot)");
        return 0;
    }
    return 0;
}

int peakdisk_load(void) {
    if (!blockdev_present())
        return -1;
    uint8_t hdr[BLOCKDEV_SECTOR_SIZE];
    if (blockdev_read(PEAKDISK_LBA0, 1, hdr) != 0)
        return -1;
    int v3 = (memcmp(hdr, "PEAKDSK3", 8) == 0);
    int v2 = (memcmp(hdr, "PEAKDSK2", 8) == 0);
    if (v2) {
        serial_log(SERIAL_LOG_WARN,
                   "peakdisk: PEAKDSK2 header-key retired — re-save as PEAKDSK3\n");
        return -1;
    }
    if (!v3 && memcmp(hdr, "PEAKDSK1", 8) != 0)
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
    if (v3) {
        if (g_pass_len == 0) {
            kfree(blob);
            serial_log(SERIAL_LOG_WARN, "peakdisk: passphrase required\n");
            return -1;
        }
        uint8_t salt[16], nonce[12], tag[16], key[32];
        uint32_t iters = PEAKDISK_KDF_ITERS;
        memcpy(salt, hdr + 12, 16);
        memcpy(nonce, hdr + 28, 12);
        memcpy(tag, hdr + 40, 16);
        memcpy(&iters, hdr + 56, 4);
        if (iters < 1000 || iters > 1000000)
            iters = PEAKDISK_KDF_ITERS;
        dec = kmalloc(sz);
        if (!dec) {
            kfree(blob);
            return -1;
        }
        if (pbkdf2_hmac_sha256((const uint8_t *)g_pass, g_pass_len, salt, 16, iters,
                               key, 32) != 0 ||
            chacha20_poly1305_decrypt(key, nonce, salt, 16, blob, sz, tag, dec) != 0) {
            memzero_explicit(key, sizeof(key));
            kfree_sensitive(dec, sz);
            kfree(blob);
            serial_log(SERIAL_LOG_WARN, "peakdisk: wrong passphrase or corrupt\n");
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
        serial_log(SERIAL_LOG_INFO,
                   v3 ? "peakdisk: loaded (encrypted)\n" : "peakdisk: loaded\n");
    return r;
}
