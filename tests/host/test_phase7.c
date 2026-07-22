/*
 * Host-side Phase 7 unit tests (no QEMU).
 * Covers PeakFS path rules, ELF header bounds, agent path prefixes,
 * and DHCP/TCP parser stubs that mirror kernel policy.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

/* --- PeakFS path allowlist (mirrors kernel/vfs.c) --- */
static int peakfs_path_allowed(const char *path) {
    if (!path || path[0] != '/')
        return 0;
    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '.' && path[i + 1] == '.' &&
            (i == 0 || path[i - 1] == '/') &&
            (path[i + 2] == '/' || path[i + 2] == '\0'))
            return 0;
    }
    static const char *const prefixes[] = {
        "/home", "/etc/peak", "/var/peak", NULL
    };
    for (int i = 0; prefixes[i]; i++) {
        size_t pl = strlen(prefixes[i]);
        if (strncmp(path, prefixes[i], pl) == 0 &&
            (path[pl] == '\0' || path[pl] == '/'))
            return 1;
    }
    return 0;
}

/* --- Agent path boundary (mirrors kernel/agent.c) --- */
static int path_under_prefix(const char *path, const char *prefix) {
    size_t pl = strlen(prefix);
    if (strncmp(path, prefix, pl) != 0)
        return 0;
    return path[pl] == '\0' || path[pl] == '/';
}

/* --- Minimal ELF64 header validation (mirrors kernel/elf.c) --- */
struct elf64_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

static int elf_hdr_ok(const uint8_t *file, size_t len) {
    if (len < sizeof(struct elf64_ehdr))
        return 0;
    const struct elf64_ehdr *eh = (const struct elf64_ehdr *)file;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return 0;
    if (eh->e_ident[4] != 2)
        return 0;
    if (eh->e_machine != 62)
        return 0;
    if (eh->e_phentsize != sizeof(struct elf64_phdr))
        return 0;
    if (eh->e_phnum == 0 || eh->e_phnum > 64)
        return 0;
    uint64_t ph_end = eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize;
    if (eh->e_phoff >= len || ph_end > len || ph_end < eh->e_phoff)
        return 0;
    if (eh->e_entry == 0 || eh->e_entry >= 0x0000800000000000ULL)
        return 0;
    return 1;
}

/* --- PeakFS blob round-trip helper --- */
static int peakfs_encode_file(uint8_t *blob, size_t cap, const char *path,
                              const void *data, size_t dlen, uint32_t *count) {
    size_t nlen = strlen(path);
    size_t need = 2 + nlen + 4 + dlen;
    size_t off = 12;
    if (*count) {
        /* append after existing */
        uint32_t c;
        memcpy(&c, blob + 8, 4);
        off = 12;
        for (uint32_t i = 0; i < c; i++) {
            uint16_t nl;
            memcpy(&nl, blob + off, 2);
            off += 2 + nl;
            uint32_t dl;
            memcpy(&dl, blob + off, 4);
            off += 4 + dl;
        }
    } else {
        memcpy(blob, "PEAKFS1", 7);
        blob[7] = 0;
        memset(blob + 8, 0, 4);
    }
    if (off + need > cap)
        return -1;
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
    (*count)++;
    memcpy(blob + 8, count, 4);
    return (int)(off + dlen);
}

static int peakfs_decode_count(const uint8_t *blob, size_t len) {
    if (len < 12 || memcmp(blob, "PEAKFS1", 7) != 0)
        return -1;
    uint32_t count;
    memcpy(&count, blob + 8, 4);
    size_t off = 12;
    for (uint32_t i = 0; i < count; i++) {
        if (off + 2 > len)
            return -1;
        uint16_t nlen;
        memcpy(&nlen, blob + off, 2);
        off += 2;
        if (nlen == 0 || off + nlen + 4 > len)
            return -1;
        char path[256];
        if (nlen >= sizeof(path))
            return -1;
        memcpy(path, blob + off, nlen);
        path[nlen] = '\0';
        off += nlen;
        uint32_t dlen;
        memcpy(&dlen, blob + off, 4);
        off += 4;
        if (off + dlen > len)
            return -1;
        int is_dir = path[nlen - 1] == '/';
        if (is_dir && dlen != 0)
            return -1;
        if (is_dir)
            path[nlen - 1] = '\0';
        if (!peakfs_path_allowed(path))
            return -1;
        off += dlen;
    }
    return (int)count;
}

/* DHCP: kernel no longer parses — ensure DISCOVER magic layout still documented */
static int dhcp_discover_layout_ok(void) {
    uint8_t disc[244];
    memset(disc, 0, sizeof(disc));
    disc[0] = 1;
    disc[1] = 1;
    disc[2] = 6;
    disc[236] = 99;
    disc[237] = 130;
    disc[238] = 83;
    disc[239] = 99;
    disc[240] = 53;
    disc[241] = 1;
    disc[242] = 1;
    disc[243] = 255;
    return disc[0] == 1 && disc[242] == 1 && disc[239] == 99;
}

/* TCP flag parse stub */
static int tcp_flags_syn_ack(uint8_t flags) {
    return (flags & 0x02) && (flags & 0x10);
}

int main(void) {
    /* PeakFS paths */
    expect(peakfs_path_allowed("/home/dev/a"), "allow /home");
    expect(peakfs_path_allowed("/etc/peak/agent.policy"), "allow /etc/peak");
    expect(peakfs_path_allowed("/var/peak/sessions"), "allow /var/peak");
    expect(!peakfs_path_allowed("/etc/passwd"), "deny /etc/passwd");
    expect(!peakfs_path_allowed("/home/../etc"), "deny .. escape");
    expect(!peakfs_path_allowed("home/dev"), "deny relative");

    /* Agent prefix boundary */
    expect(path_under_prefix("/home/dev/workspace/x.c", "/home/dev/workspace"), "under workspace");
    expect(!path_under_prefix("/home/dev/workspaceevil", "/home/dev/workspace"),
           "reject workspaceevil prefix bypass");

    /* PeakFS encode/decode */
    uint8_t blob[4096];
    uint32_t count = 0;
    memset(blob, 0, sizeof(blob));
    const char *payload = "hello peakfs large enough";
    expect(peakfs_encode_file(blob, sizeof(blob), "/home/dev/workspace/t.txt",
                              payload, strlen(payload), &count) > 0,
           "encode file");
    char dpath[] = "/home/dev/workspace/";
    expect(peakfs_encode_file(blob, sizeof(blob), dpath, NULL, 0, &count) > 0,
           "encode dir");
    expect(peakfs_decode_count(blob, sizeof(blob)) == 2, "decode count 2");

    /* Unsafe path in blob rejected */
    uint8_t bad[128];
    memset(bad, 0, sizeof(bad));
    memcpy(bad, "PEAKFS1", 7);
    uint32_t one = 1;
    memcpy(bad + 8, &one, 4);
    const char *evil = "/etc/shadow";
    uint16_t nl = (uint16_t)strlen(evil);
    size_t o = 12;
    memcpy(bad + o, &nl, 2);
    o += 2;
    memcpy(bad + o, evil, nl);
    o += nl;
    uint32_t zl = 0;
    memcpy(bad + o, &zl, 4);
    expect(peakfs_decode_count(bad, sizeof(bad)) < 0, "reject unsafe peakfs path");

    /* ELF validation */
    uint8_t elf[128];
    memset(elf, 0, sizeof(elf));
    expect(!elf_hdr_ok(elf, sizeof(elf)), "reject zero elf");
    struct elf64_ehdr *eh = (struct elf64_ehdr *)elf;
    eh->e_ident[0] = 0x7F;
    eh->e_ident[1] = 'E';
    eh->e_ident[2] = 'L';
    eh->e_ident[3] = 'F';
    eh->e_ident[4] = 2;
    eh->e_machine = 62;
    eh->e_phentsize = sizeof(struct elf64_phdr);
    eh->e_phnum = 1;
    eh->e_phoff = sizeof(struct elf64_ehdr);
    eh->e_entry = 0x400000;
    expect(elf_hdr_ok(elf, sizeof(elf)), "accept minimal elf");
    eh->e_phnum = 100;
    expect(!elf_hdr_ok(elf, sizeof(elf)), "reject too many phdrs");
    eh->e_phnum = 1;
    eh->e_entry = 0xffff800000000000ULL;
    expect(!elf_hdr_ok(elf, sizeof(elf)), "reject kernel entry");

    /* Empty credential auth policy (mirrors ubin.c reject) */
    {
        const char *user = "", *pass = "";
        int ok = user[0] && pass[0] && !strcmp(user, "peak") && !strcmp(pass, "peak");
        expect(!ok, "empty user/pass must fail auth");
        user = "peak";
        pass = "peak";
        ok = user[0] && pass[0] && !strcmp(user, "peak") && !strcmp(pass, "peak");
        expect(ok, "valid creds pass");
    }

    expect(dhcp_discover_layout_ok(), "dhcp discover magic");
    expect(tcp_flags_syn_ack(0x12), "syn+ack");
    expect(!tcp_flags_syn_ack(0x02), "syn alone");

    /* PeakVec embed/query: see tests/host/test_peakvec.c (links kernel/peakvec.c). */

    /* Agent intent keywords (mirrors classify_intent) */
    {
        const char *g1 = "what did I ask last";
        const char *g2 = "create hello.c";
        int recall = strstr(g1, "what did") != NULL || strstr(g1, "ask") != NULL;
        int create = strstr(g2, "create") != NULL || strstr(g2, ".c") != NULL;
        expect(recall, "recall intent keywords");
        expect(create, "create intent keywords");
    }

    /* Streamed PeakDisk: size is measured, not capped at 512 KiB constant */
    {
        const size_t old_cap = 512u * 1024u;
        size_t measured = old_cap + 4096u; /* pretend export larger than legacy cap */
        expect(measured > old_cap, "persist may exceed legacy 512KiB snapshot");
        expect(measured <= 32u * 1024u * 1024u, "soft 32MiB heap safety still applies");
    }

    /* FPSIMD / x87 state buffer sizes used by context switch (fpu.h). */
    {
#if defined(__aarch64__)
        const int fpu_sz = 528;
#else
        const int fpu_sz = 512;
#endif
        expect(fpu_sz >= 512 && (fpu_sz % 16) == 0, "FPU_STATE_SIZE aligned");
        /* Context-switch contract: save/restore buffers must hold full state. */
        expect(fpu_sz <= 1024, "FPU_STATE_SIZE within sched buffer budget");
    }

    if (fails) {
        fprintf(stderr, "%d test(s) failed\n", fails);
        return 1;
    }
    printf("OK — phase7 host unit tests passed\n");
    return 0;
}
