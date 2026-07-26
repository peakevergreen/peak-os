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
#include "blobstore.h"

void vfs_host_blob_reset(void);

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

    /* --- many siblings (bucket collision stress) --- */
    {
        reset_vfs();
        vfs_mkdir("/home");
        char path[64];
        for (int i = 0; i < 40; i++) {
            snprintf(path, sizeof(path), "/home/f%02d", i);
            expect(vfs_write_file(path, "x", 1) == 0, "sibling write");
        }
        for (int i = 0; i < 40; i++) {
            snprintf(path, sizeof(path), "/home/f%02d", i);
            expect(vfs_is_file(path), "sibling lookup");
        }
        expect(vfs_node_count() > 40, "nodes allocated");
    }

    /* --- walk / readdir errno --- */
    {
        reset_vfs();
        /* Missing path returns before invoking cb. */
        expect(vfs_walk("/missing", NULL, NULL) == PEAK_ENOENT, "walk missing → ENOENT");
        expect(vfs_readdir("/missing", NULL, 1) == PEAK_EINVAL, "readdir bad args");
        vfs_mkdir("/home");
        struct vfs_dirent ents[4];
        expect(vfs_readdir("/home", ents, 4) >= 0, "readdir ok");
    }

    /* --- blob-backed large file (bind + ranged I/O + PeakFS export) --- */
    {
        reset_vfs();
        vfs_host_blob_reset();
        expect(blobstore_available(), "blobstore on mem disk");
        vfs_mkdir("/home");
        vfs_mkdir("/home/dev");
        uint32_t id = 0;
        expect(blobstore_create(&id, 8192) == 0, "create blob");
        const char payload[] = "large-file-payload";
        expect(blobstore_write(id, 0, payload, sizeof(payload)) == (int)sizeof(payload),
               "seed blob");
        expect(vfs_bind_blob("/home/dev/big.bin", id, sizeof(payload)) == 0,
               "bind blob to vfs");
        got = 0;
        memset(buf, 0, sizeof(buf));
        expect(vfs_read_at("/home/dev/big.bin", 0, buf, sizeof(buf), &got) == 0,
               "read_at blob");
        expect(got == sizeof(payload) && memcmp(buf, payload, got) == 0,
               "blob read bytes");

        const char tail[] = "-tail";
        expect(vfs_write_at("/home/dev/big.bin", sizeof(payload), tail, sizeof(tail)) == 0,
               "write_at extends blob");
        size_t total = sizeof(payload) + sizeof(tail);
        char wide[64];
        got = 0;
        memset(wide, 0, sizeof(wide));
        expect(vfs_read_at("/home/dev/big.bin", 0, wide, sizeof(wide), &got) == 0,
               "read extended blob");
        expect(got == total, "extended size");
        expect(memcmp(wide, payload, sizeof(payload)) == 0, "head preserved");
        expect(memcmp(wide + sizeof(payload), tail, sizeof(tail)) == 0, "tail appended");

        expect(vfs_create_blob_file("/home/dev/new.bin", 16) == 0, "create_blob_file");
        expect(vfs_write_at("/home/dev/new.bin", 0, "fresh", 5) == 0, "write new blob file");

        uint8_t blob[65536];
        int need = vfs_export_ramdisk_size();
        expect(need > 12, "export size with blob file");
        int n = vfs_export_ramdisk(blob, sizeof(blob));
        expect(n == need, "export with blob");
        expect(vfs_load_ramdisk(blob, (size_t)n) == 0, "reload export with blob");
        got = 0;
        memset(buf, 0, sizeof(buf));
        expect(vfs_read_file("/home/dev/big.bin", buf, sizeof(buf), &got) == 0,
               "big.bin after reload");
        expect(got == total, "blob round-trip size");
    }

    /* --- write error messages --- */
    {
        expect(vfs_write_file("/home/dev", "x", 1) == PEAK_EISDIR, "reject write to dir");
        expect(strstr(vfs_last_error(), "directory") != NULL ||
               strstr(vfs_last_error(), "is a directory") != NULL,
               "write dir error message");
    }

    /* --- errno: unlink / rmdir / stat / normalize / mkdir --- */
    {
        reset_vfs();
        vfs_mkdir("/home");
        expect(vfs_write_file("/home/f", "x", 1) == 0, "seed file");
        expect(vfs_unlink("/nope") == PEAK_ENOENT, "unlink missing → ENOENT");
        expect(vfs_unlink("/home") == PEAK_EISDIR, "unlink dir → EISDIR");
        expect(vfs_rmdir("/home/f") == PEAK_ENOTDIR, "rmdir file → ENOTDIR");
        expect(vfs_rmdir("/nope") == PEAK_ENOENT, "rmdir missing → ENOENT");

        struct vfs_stat st;
        expect(vfs_stat("/nope", &st) == PEAK_ENOENT, "stat missing → ENOENT");
        expect(vfs_stat("/home", NULL) == PEAK_EINVAL, "stat null st → EINVAL");

        char longpath[VFS_PATH_MAX];
        longpath[0] = '/';
        memset(longpath + 1, 'a', VFS_NAME_MAX);
        longpath[1 + VFS_NAME_MAX] = '\0';
        expect(vfs_normalize(longpath, norm, sizeof(norm)) == PEAK_EINVAL,
               "normalize overlong component → EINVAL");

        expect(vfs_mkdir("/home/f") == NULL, "mkdir on file → NULL");
        expect(vfs_is_file("/home/f"), "file still exists after mkdir fail");
    }

    /* --- blob backing stat hint (via lookup) --- */
    {
        reset_vfs();
        vfs_host_blob_reset();
        if (blobstore_available()) {
            vfs_mkdir("/home");
            uint32_t id = 0;
            expect(blobstore_create(&id, 64) == 0, "blob for stat test");
            expect(vfs_bind_blob("/home/blobby", id, 4) == 0, "bind blob file");
            struct vfs_node *n = vfs_lookup("/home/blobby");
            expect(n && n->blob_id != 0, "blob_id set on node");
        }
    }

    /* --- mode bits + chmod --- */
    {
        reset_vfs();
        expect(vfs_mkdir("/home") != NULL, "mode mkdir");
        expect(vfs_write_file("/home/f", "x", 1) == 0, "mode seed file");
        struct vfs_stat st;
        expect(vfs_stat("/home/f", &st) == 0, "mode stat file");
        expect(st.mode == VFS_MODE_FILE, "default file mode 0644");
        expect(vfs_stat("/home", &st) == 0, "mode stat dir");
        expect(st.mode == VFS_MODE_DIR, "default dir mode 0755");
        char modebuf[12];
        vfs_mode_string(VFS_DIR, st.mode, modebuf, sizeof(modebuf));
        expect(strcmp(modebuf, "drwxr-xr-x") == 0, "mode string dir");
        expect(vfs_chmod("/home/f", 0755) == 0, "chmod octal");
        expect(vfs_stat("/home/f", &st) == 0 && st.mode == 0755, "chmod applied");
        expect(vfs_chmod("/home/f", 0700) == 0, "chmod 0700");
        expect(vfs_stat("/home/f", &st) == 0 && st.mode == 0700, "mode 0700");
        expect(vfs_chmod("/nope", 0644) == PEAK_ENOENT, "chmod missing");
        expect(vfs_chmod("/", 0755) == PEAK_EINVAL, "chmod root denied");
    }

    { reset_vfs(); vfs_mkdir("/home"); expect(vfs_write_file("/home/target","hello",5)==0,"tgt"); expect(vfs_symlink("/home/target","/home/link")==0,"sym"); struct vfs_stat st; expect(vfs_stat("/home/link",&st)==0,"st"); expect(st.type==VFS_SYMLINK,"typ"); expect(!strcmp(st.link_target,"/home/target"),"tgt2"); char target[VFS_PATH_MAX]; size_t n=0; expect(vfs_readlink("/home/link",target,sizeof(target),&n)==0,"rl"); char resolved[VFS_PATH_MAX]; expect(vfs_resolve("/home/link",resolved,sizeof(resolved))==0,"rs"); char buf[16]; size_t got=0; expect(vfs_read_file("/home/link",buf,sizeof(buf),&got)==0,"rd"); expect(vfs_symlink("target","/home/rel")==0,"rel"); expect(vfs_resolve("/home/rel",resolved,sizeof(resolved))==0,"rs2"); expect(vfs_symlink("/home/loop_b","/home/loop_a")==0,"la"); expect(vfs_symlink("/home/loop_a","/home/loop_b")==0,"lb"); expect(vfs_resolve("/home/loop_b",resolved,sizeof(resolved))==PEAK_ELOOP,"el"); }

    if (fails) {
        fprintf(stderr, "%d test(s) failed\n", fails);
        return 1;
    }
    printf("OK — vfs/peakfs host unit tests passed\n");
    return 0;
}
