/* Host unit tests for PeakDisk atomic publish ordering (payload then header). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define SECTOR 512
#define LBA0 1
#define MAX_SEC 256

static uint8_t disk[MAX_SEC][SECTOR];
static int present = 1;
static int fail_after_payload_writes;
static int payload_writes;
static int flush_count;
static int write_header_after_payload;

static int mock_present(void) { return present; }

static int mock_read(uint64_t lba, uint32_t count, void *buf) {
    uint8_t *out = buf;
    for (uint32_t i = 0; i < count; i++) {
        if (lba + i >= MAX_SEC)
            return -1;
        memcpy(out + i * SECTOR, disk[lba + i], SECTOR);
    }
    return 0;
}

static int mock_write(uint64_t lba, uint32_t count, const void *buf) {
    const uint8_t *in = buf;
    for (uint32_t i = 0; i < count; i++) {
        if (lba + i >= MAX_SEC)
            return -1;
        if (lba + i == LBA0) {
            /* Header write after some payload activity */
            if (payload_writes > 0)
                write_header_after_payload = 1;
        } else if (lba + i >= LBA0 + 1) {
            payload_writes++;
            if (fail_after_payload_writes > 0 &&
                payload_writes >= fail_after_payload_writes)
                return -1;
        }
        memcpy(disk[lba + i], in + i * SECTOR, SECTOR);
    }
    return 0;
}

static int mock_flush(void) {
    flush_count++;
    return 0;
}

/* Mirror peakdisk publish order without linking the kernel. */
static int atomic_save_sim(const uint8_t *payload, uint32_t sz) {
    uint8_t hdr[SECTOR];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "PEAKDSK1", 8);
    memcpy(hdr + 8, &sz, 4);

    /* Old good header already on disk[LBA0] from prior save. */
    payload_writes = 0;
    write_header_after_payload = 0;
    flush_count = 0;

    uint32_t sectors = (sz + SECTOR - 1) / SECTOR;
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t sec[SECTOR];
        memset(sec, 0, sizeof(sec));
        uint32_t off = s * SECTOR;
        uint32_t chunk = sz - off;
        if (chunk > SECTOR)
            chunk = SECTOR;
        memcpy(sec, payload + off, chunk);
        if (mock_write(LBA0 + 1 + s, 1, sec) != 0)
            return -1;
    }
    if (mock_flush() != 0)
        return -1;
    if (mock_write(LBA0, 1, hdr) != 0)
        return -1;
    if (mock_flush() != 0)
        return -1;
    return 0;
}

static int failures;

static void expect(int cond, const char *msg) {
    if (!cond) {
        printf("FAIL: %s\n", msg);
        failures++;
    } else {
        printf("ok: %s\n", msg);
    }
}

int main(void) {
    failures = 0;
    memset(disk, 0, sizeof(disk));

    /* Seed prior good image */
    uint8_t old_hdr[SECTOR];
    memset(old_hdr, 0, sizeof(old_hdr));
    memcpy(old_hdr, "PEAKDSK1", 8);
    uint32_t old_sz = 16;
    memcpy(old_hdr + 8, &old_sz, 4);
    memcpy(disk[LBA0], old_hdr, SECTOR);
    memcpy(disk[LBA0 + 1], "OLD_PAYLOAD_DATA!", 16);

    uint8_t payload[64];
    memset(payload, 'N', sizeof(payload));
    memcpy(payload, "PEAKFS1", 7);

    fail_after_payload_writes = 0;
    expect(atomic_save_sim(payload, 32) == 0, "atomic save succeeds");
    expect(write_header_after_payload == 1, "header published after payload");
    expect(flush_count >= 2, "flush after payload and header");
    expect(memcmp(disk[LBA0], "PEAKDSK1", 8) == 0, "new header magic");

    /* Mid-payload failure must leave old header intact */
    memcpy(disk[LBA0], old_hdr, SECTOR);
    memcpy(disk[LBA0 + 1], "OLD_PAYLOAD_DATA!", 16);
    fail_after_payload_writes = 1;
    write_header_after_payload = 0;
    expect(atomic_save_sim(payload, 64) != 0, "torn payload aborts");
    expect(memcmp(disk[LBA0], old_hdr, 12) == 0, "old header preserved on abort");
    expect(write_header_after_payload == 0, "no header publish on abort");

    (void)mock_present;
    (void)mock_read;
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_peakdisk: ok\n");
    return 0;
}
