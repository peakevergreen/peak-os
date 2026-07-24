/*
 * Host tests for VFS core + PeakFS load/export path policy.
 * Links kernel/vfs.c, vfs_peakfs.c, vfs_path_util.c (no QEMU).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "vfs.h"
#include "vfs_path_util.h"
#include "peak_errno.h"
#include "privacy.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

/* Build a PEAKFS1 blob with one file entry. */
static int encode_one(uint8_t *blob, size_t cap, const char *path,
                      const void *data, size_t dlen) {
    size_t nlen = strlen(path);
    size_t need = 12 + 2 + nlen + 4 + dlen;
    if (need > cap)
        return -1;
    memset(blob, 0, cap);
    memcpy(blob, "PEAKFS1", 7);
    uint32_t count = 1;
    memcpy(blob + 8, &count, 4);
    size_t off = 12;
    uint16_t nl = (uint16_t)nlen;
    memcpy(blob + off, &nl, 2);
    off += 2;
    memcpy(blob + off, path, nlen);
    off += nlen;
    uint32_t dl = (uint32_t)dlen;
    memcpy(blob + off, &dl, 4);
    off += 4;
    if (dlen)
        memcpy(blob + off, data, dlen);
    return (int)(off + dlen);
}

static void reset_vfs(void) {
    vfs_init();
    privacy_set_persist_profile(2);
}

int main(void) {
    char norm[VFS_PATH_MAX];
    char buf[256];
    size_t got = 0;

    /* --- path normalize --- */
    reset_vfs();
    expect(vfs_normalize("/home/dev/../dev/a", norm, sizeof(norm)) == 0,
           "normalize ..");
    expect(strcmp(norm, "/home/dev/a") == 0, "normalize collapses ..");
    expect(vfs_normalize("relative", norm, sizeof(norm)) == PEAK_EINVAL,
           "reject relative normalize");
    expect(vfs_normalize("/a/./b//c", norm, sizeof(norm)) == 0, "normalize dots");
    expect(strcmp(norm, "/a/b/c") == 0, "normalize ./ and //");

    /* --- mkdir / write / read / lookup --- */
    reset_vfs();
    expect(vfs_mkdir("/home") != NULL, "mkdir /home");
    expect(vfs_mkdir("/home/dev") != NULL, "mkdir /home/dev");
    expect(vfs_write_file("/home/dev/t.txt", "hello", 5) == 0, "write file");
    expect(vfs_exists("/home/dev/t.txt"), "exists after write");
    expect(vfs_is_file("/home/dev/t.txt"), "is file");
    expect(vfs_is_dir("/home/dev"), "is dir");
    got = 0;
    memset(buf, 0, sizeof(buf));
    expect(vfs_read_file("/home/dev/t.txt", buf, sizeof(buf), &got) == 0,
           "read file");
    expect(got == 5 && memcmp(buf, "hello", 5) == 0, "read bytes");

    /* --- rename / unlink --- */
    expect(vfs_rename("/home/dev/t.txt", "/home/dev/u.txt") == 0, "rename");
    expect(!vfs_exists("/home/dev/t.txt") && vfs_exists("/home/dev/u.txt"),
           "rename moves");
    expect(vfs_unlink("/home/dev/u.txt") == 0, "unlink");
    expect(!vfs_exists("/home/dev/u.txt"), "gone after unlink");

    /* --- seed defaults --- */
    reset_vfs();
    vfs_seed_defaults();
    expect(vfs_is_dir("/home/dev/workspace"), "seed workspace");
    expect(vfs_is_file("/etc/peak/agent.policy"), "seed policy");
    expect(vfs_is_file("/home/dev/workspace/hello.c"), "seed hello.c");

    /* --- PeakFS export/load roundtrip (full profile) --- */
    {
        uint8_t blob[65536];
        int need = vfs_export_ramdisk_size();
        expect(need > 12, "export size > header");
        expect(need < (int)sizeof(blob), "export fits test buffer");
        int n = vfs_export_ramdisk(blob, sizeof(blob));
        expect(n == need, "export bytes match size");
        expect(memcmp(blob, "PEAKFS1", 7) == 0, "export magic");

        /* Mutate then reload from export. */
        expect(vfs_write_file("/home/dev/workspace/extra.txt", "x", 1) == 0,
               "write extra before reload");
        expect(vfs_load_ramdisk(blob, (size_t)n) == 0, "load export");
        expect(!vfs_exists("/home/dev/workspace/extra.txt"),
               "reload replaces persist ns");
        expect(vfs_is_file("/home/dev/workspace/hello.c"),
               "hello.c restored from blob");
    }

    /* --- path policy: reject unsafe PeakFS load --- */
    {
        uint8_t bad[256];
        const char *evil = "/etc/shadow";
        const char *payload = "x";
        expect(encode_one(bad, sizeof(bad), evil, payload, 1) > 0, "encode evil");
        reset_vfs();
        vfs_seed_defaults();
        expect(vfs_load_ramdisk(bad, sizeof(bad)) == PEAK_EACCES,
               "load rejects /etc/shadow");
        expect(vfs_is_file("/home/dev/workspace/hello.c"),
               "reject leaves tree intact");
    }

    /* --- path policy: .. escape rejected --- */
    {
        uint8_t bad[256];
        expect(encode_one(bad, sizeof(bad), "/home/../etc/peak", "z", 1) > 0,
               "encode .. path");
        reset_vfs();
        expect(vfs_load_ramdisk(bad, sizeof(bad)) == PEAK_EACCES,
               "load rejects .. component");
    }

    /* --- workspace profile: only /home --- */
    {
        uint8_t blob[512];
        const char *ok = "/home/dev/a.txt";
        const char *deny = "/etc/peak/x";
        expect(encode_one(blob, sizeof(blob), ok, "hi", 2) > 0, "encode /home");
        reset_vfs();
        privacy_set_persist_profile(1);
        expect(vfs_load_ramdisk(blob, sizeof(blob)) == 0, "workspace allows /home");
        expect(vfs_is_file("/home/dev/a.txt"), "workspace file present");

        expect(encode_one(blob, sizeof(blob), deny, "no", 2) > 0, "encode /etc/peak");
        expect(vfs_load_ramdisk(blob, sizeof(blob)) == PEAK_EACCES,
               "workspace denies /etc/peak");
    }

    /* --- private profile denies all persist paths --- */
    {
        uint8_t blob[256];
        expect(encode_one(blob, sizeof(blob), "/home/x", "a", 1) > 0, "encode home");
        reset_vfs();
        privacy_set_persist_profile(0);
        expect(vfs_load_ramdisk(blob, sizeof(blob)) == PEAK_EACCES,
               "private denies /home");
        expect(!peakfs_path_allowed_for_profile("/home/x", 0),
               "util private deny");
    }

    /* --- truncated / bad magic --- */
    {
        uint8_t junk[16];
        memset(junk, 0, sizeof(junk));
        reset_vfs();
        expect(vfs_load_ramdisk(junk, sizeof(junk)) == PEAK_EIO, "bad magic");
        expect(vfs_load_ramdisk(junk, 4) == PEAK_EIO, "too short");
    }

    /* --- audit.log preserved across clear when not in blob --- */
    {
        uint8_t blob[512];
        const char *audit = "audit-line\n";
        reset_vfs();
        privacy_set_persist_profile(2);
        vfs_mkdir("/var");
        vfs_mkdir("/var/peak");
        expect(vfs_write_file("/var/peak/audit.log", audit, strlen(audit)) == 0,
               "seed audit");
        expect(encode_one(blob, sizeof(blob), "/home/dev/keep.txt", "k", 1) > 0,
               "encode keep");
        expect(vfs_load_ramdisk(blob, sizeof(blob)) == 0, "load without audit");
        got = 0;
        memset(buf, 0, sizeof(buf));
        expect(vfs_read_file("/var/peak/audit.log", buf, sizeof(buf), &got) == 0,
               "audit preserved");
        expect(got == strlen(audit) && memcmp(buf, audit, got) == 0,
               "audit bytes match");
        expect(vfs_is_file("/home/dev/keep.txt"), "keep.txt loaded");
    }

    /* --- copy_file --- */
    {
        reset_vfs();
        vfs_mkdir("/home");
        expect(vfs_write_file("/home/a", "abc", 3) == 0, "src write");
        expect(vfs_copy_file("/home/a", "/home/b") == 0, "copy_file");
        got = 0;
        memset(buf, 0, sizeof(buf));
        expect(vfs_read_file("/home/b", buf, sizeof(buf), &got) == 0 && got == 3,
               "copy contents");
    }

    /* --- remove_tree --- */
    {
        reset_vfs();
        vfs_mkdir("/home");
        vfs_mkdir("/home/d");
        vfs_write_file("/home/d/f", "1", 1);
        expect(vfs_remove_tree("/home") == 0, "remove_tree");
        expect(!vfs_exists("/home"), "tree gone");
    }

    if (fails) {
        fprintf(stderr, "%d test(s) failed\n", fails);
        return 1;
    }
    printf("OK — vfs/peakfs host unit tests passed\n");
    return 0;
}
