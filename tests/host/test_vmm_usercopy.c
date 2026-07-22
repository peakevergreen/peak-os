/*
 * Host tests for VMM user-copy helpers and syscall-sized validation paths
 * (links kernel/vmm.c).
 */
#include "types.h"
#include "vmm.h"
#include "pmm.h"
#include "guiproto.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define USER_RW (VMM_PRESENT | VMM_USER | VMM_WRITE)
#define USER_RO (VMM_PRESENT | VMM_USER)

static int fails;
static void *user_win;
static uint64_t user_va;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static uint64_t setup_as(uint32_t flags) {
    user_win = aligned_alloc(PAGE_SIZE, PAGE_SIZE);
    expect(user_win != NULL, "alloc user window");
    user_va = (uint64_t)(uintptr_t)user_win;

    uint64_t cr3 = vmm_create_address_space();
    void *frame = pmm_alloc();
    expect(cr3 && frame, "create address space");
    expect(vmm_map_page_flags(cr3, user_va, (uint64_t)(uintptr_t)frame, flags) == 0,
           "map user page");
    vmm_host_test_set_cr3(cr3);
    return cr3;
}

static void teardown_as(uint64_t cr3) {
    (void)cr3;
    if (user_win) {
        free(user_win);
        user_win = NULL;
    }
    vmm_host_test_set_cr3(0);
    /* Host page tables use malloc frames; skip vmm_destroy in unit tests. */
}

int main(void) {
    vmm_init(0);
    vmm_enable_nx();

    /* Range validation — no user mapping required. */
    expect(!access_ok(NULL, 1), "null user ptr rejected");
    expect(!access_ok((void *)(uintptr_t)0x800000000000ULL, 8),
           "non-canonical user ptr rejected");
    expect(!access_ok((void *)(uintptr_t)0x1000, (size_t)-1),
           "length overflow rejected");

    /* copy_from_user / copy_to_user on a mapped RW user page. */
    uint64_t cr3 = setup_as(USER_RW);

    char kbuf[64];
    memset(kbuf, 0, sizeof(kbuf));
    memcpy(user_win, "peak-copy", 10);
    expect(copy_from_user(kbuf, user_win, 10) == 0, "copy_from_user ok");
    expect(!strcmp(kbuf, "peak-copy"), "copy_from_user bytes");

    memcpy(kbuf, "outbound", 9);
    expect(copy_to_user(user_win, kbuf, 9) == 0, "copy_to_user ok");
    expect(!strncmp((char *)user_win, "outbound", 9), "copy_to_user bytes");

    /* Non-user canonical address rejected before memcpy. */
    expect(copy_from_user(kbuf, (void *)(uintptr_t)0x0000FFFF80000000ULL, 4) != 0,
           "copy_from_user non-user canonical");

    /* copyinstr — syscall path validation. */
    memcpy(user_win, "/bin/echo", 10);
    char path[32];
    expect(copyinstr_from_user(path, user_win, sizeof(path)) == 0, "copyinstr ok");
    expect(!strcmp(path, "/bin/echo"), "copyinstr string");

    memcpy(user_win, "nul\xFFtr", 7);
    path[0] = '?';
    expect(copyinstr_from_user(path, user_win, 7) != 0,
           "copyinstr requires terminator within max_len");

    memset(user_win, 'x', PAGE_SIZE);
    expect(copyinstr_from_user(path, user_win, 16) != 0,
           "copyinstr max_len enforced");

    /* SYS_peakgui-sized struct copy (mirrors syscall.c). */
    struct gui_msg msg_in, msg_out;
    memset(&msg_in, 0, sizeof(msg_in));
    msg_in.op = GUI_OP_CREATE;
    msg_in.w = 200;
    msg_in.h = 120;
    snprintf(msg_in.title, sizeof(msg_in.title), "Syscall");
    memcpy(user_win, &msg_in, sizeof(msg_in));
    memset(&msg_out, 0, sizeof(msg_out));
    expect(copy_from_user(&msg_out, user_win, sizeof(msg_out)) == 0,
           "peakgui struct copy");
    expect(msg_out.op == GUI_OP_CREATE && msg_out.w == 200, "peakgui fields");
    expect(!strcmp(msg_out.title, "Syscall"), "peakgui title");

    teardown_as(cr3);

    /* Read-only user mapping rejects write probe via page tables. */
    cr3 = setup_as(USER_RO);
    expect(!access_ok_write(user_win, 8), "RO mapping not writable");
    memcpy(user_win, "readonly", 9);
    expect(copy_from_user(kbuf, user_win, 9) == 0, "copy_from_user on RO map");

    teardown_as(cr3);

    if (fails) {
        fprintf(stderr, "%d vmm_usercopy test(s) failed\n", fails);
        return 1;
    }
    printf("test_vmm_usercopy: ok\n");
    return 0;
}
