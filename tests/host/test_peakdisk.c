/* Host unit tests for PeakDisk atomic publish ordering (payload then header). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define SECTOR 512
#define LBA0 1
#define MAX_SEC 256
#define MAX_WRITES 512

static uint8_t disk[MAX_SEC][SECTOR];
static int present = 1;
static int fail_after_payload_writes;
static int fail_on_header_write;
static int fail_on_flush_num;
static int payload_writes;
static int flush_count;
static int write_header_after_payload;
static uint64_t write_order[MAX_WRITES];
static int write_order_n;

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

static void record_write_lba(uint64_t lba) {
    if (write_order_n < MAX_WRITES)
        write_order[write_order_n++] = lba;
}

static int mock_write(uint64_t lba, uint32_t count, const void *buf) {
    const uint8_t *in = buf;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t cur = lba + i;
        if (cur >= MAX_SEC)
            return -1;
        record_write_lba(cur);
        if (cur == LBA0) {
            if (payload_writes > 0)
                write_header_after_payload = 1;
            if (fail_on_header_write)
                return -1;
        } else if (cur >= LBA0 + 1) {
            payload_writes++;
            if (fail_after_payload_writes > 0 &&
                payload_writes >= fail_after_payload_writes)
                return -1;
        }
        memcpy(disk[cur], in + i * SECTOR, SECTOR);
    }
    return 0;
}

static int mock_flush(void) {
    flush_count++;
    if (fail_on_flush_num > 0 && flush_count >= fail_on_flush_num)
        return -1;
    return 0;
}

/* Mirror peakdisk publish order without linking the kernel (PEAKDSK1 only). */
static int atomic_save_sim(const uint8_t *payload, uint32_t sz) {
    uint8_t hdr[SECTOR];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, "PEAKDSK1", 8);
    memcpy(hdr + 8, &sz, 4);

    payload_writes = 0;
    write_header_after_payload = 0;
    flush_count = 0;
    write_order_n = 0;

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

static int payload_on_disk(const uint8_t *payload, uint32_t sz) {
    for (uint32_t off = 0; off < sz; off++) {
        uint64_t lba = LBA0 + 1 + off / SECTOR;
        uint32_t in_sec = off % SECTOR;
        if (disk[lba][in_sec] != payload[off])
            return 0;
    }
    return 1;
}

static int payload_before_header_writes(void) {
    int seen_header = 0;
    for (int i = 0; i < write_order_n; i++) {
        if (write_order[i] == LBA0)
            seen_header = 1;
        else if (seen_header && write_order[i] >= LBA0 + 1)
            return 0;
    }
    return 1;
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

static void seed_old_image(const uint8_t *old_hdr) {
    memcpy(disk[LBA0], old_hdr, SECTOR);
    memcpy(disk[LBA0 + 1], "OLD_PAYLOAD_DATA!", 16);
}

int main(void) {
    failures = 0;
    memset(disk, 0, sizeof(disk));

    uint8_t old_hdr[SECTOR];
    memset(old_hdr, 0, sizeof(old_hdr));
    memcpy(old_hdr, "PEAKDSK1", 8);
    uint32_t old_sz = 16;
    memcpy(old_hdr + 8, &old_sz, 4);
    seed_old_image(old_hdr);

    uint8_t payload[64];
    memset(payload, 'N', sizeof(payload));
    memcpy(payload, "PEAKFS1", 7);

    fail_after_payload_writes = 0;
    fail_on_header_write = 0;
    fail_on_flush_num = 0;
    expect(atomic_save_sim(payload, 32) == 0, "atomic save succeeds");
    expect(write_header_after_payload == 1, "header published after payload");
    expect(flush_count == 2, "flush after payload and header");
    expect(memcmp(disk[LBA0], "PEAKDSK1", 8) == 0, "new header magic");
    {
        uint32_t on_disk = 0;
        memcpy(&on_disk, disk[LBA0] + 8, 4);
        expect(on_disk == 32, "header size matches payload");
    }
    expect(payload_on_disk(payload, 32), "payload bytes on disk");

    /* Multi-sector payload: ordering and content */
    seed_old_image(old_hdr);
    uint8_t big[600];
    for (uint32_t i = 0; i < sizeof(big); i++)
        big[i] = (uint8_t)(0xA0 + (i & 0x3F));
    memcpy(big, "PEAKFS1", 7);
    fail_after_payload_writes = 0;
    expect(atomic_save_sim(big, (uint32_t)sizeof(big)) == 0, "multi-sector save");
    expect(payload_writes == 2, "two payload sectors written");
    expect(payload_on_disk(big, (uint32_t)sizeof(big)), "multi-sector payload intact");
    expect(payload_before_header_writes(), "payload LBAs before header LBA");

    /* Mid-payload failure must leave old header intact */
    seed_old_image(old_hdr);
    fail_after_payload_writes = 1;
    write_header_after_payload = 0;
    expect(atomic_save_sim(payload, 64) != 0, "torn payload aborts");
    expect(memcmp(disk[LBA0], old_hdr, 12) == 0, "old header preserved on abort");
    expect(write_header_after_payload == 0, "no header publish on abort");

    /* Payload flush failure: header must stay old */
    seed_old_image(old_hdr);
    fail_after_payload_writes = 0;
    fail_on_flush_num = 1;
    expect(atomic_save_sim(payload, 32) != 0, "payload flush failure aborts");
    expect(memcmp(disk[LBA0], old_hdr, SECTOR) == 0, "old header on payload flush fail");
    expect(write_header_after_payload == 0, "no header on payload flush fail");

    /* Header write failure after payload committed */
    seed_old_image(old_hdr);
    fail_on_flush_num = 0;
    fail_on_header_write = 1;
    expect(atomic_save_sim(payload, 32) != 0, "header write failure aborts");
    expect(memcmp(disk[LBA0], old_hdr, SECTOR) == 0, "old header on header write fail");
    expect(payload_on_disk(payload, 32), "payload staged despite header fail");

    /* Final header flush failure still stages header + payload in block layer */
    seed_old_image(old_hdr);
    fail_on_header_write = 0;
    fail_on_flush_num = 2;
    expect(atomic_save_sim(payload, 32) != 0, "header flush failure aborts");
    expect(flush_count == 2, "both flushes attempted");
    expect(memcmp(disk[LBA0], "PEAKDSK1", 8) == 0, "header staged on flush fail");
    expect(payload_on_disk(payload, 32), "payload staged on header flush fail");

    (void)mock_present;
    (void)mock_read;
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_peakdisk: ok\n");
    return 0;
}
