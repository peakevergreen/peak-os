/* /bin Peak JS CLI: js -e 'code' | js file.js */
#include "libpeak.h"
#include "js.h"
#include "heap.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "util.h"

int ujs_main(int argc, char **argv) {
    if (argc < 2 || peak_wants_help(argc, argv)) {
        console_write("usage:\n");
        console_write("  js -e 'code'     evaluate expression/statements\n");
        console_write("  js <file.js>     run a script from VFS\n");
        console_write("Peak-authored bytecode JS (no JIT).\n");
        return 0;
    }

    struct js_runtime *rt = js_rt_create();
    if (!rt) {
        peak_perror("js", "runtime create failed");
        return 1;
    }

    char result[128];
    int rc = 0;
    if (!strcmp(argv[1], "-e") || !strcmp(argv[1], "--eval")) {
        if (argc < 3) {
            peak_usage("js", "-e 'code'");
            js_rt_destroy(rt);
            return 1;
        }
        if (js_eval(rt, argv[2], "<cli>", result, sizeof(result)) != 0) {
            console_printf("js error: %s\n", js_last_error(rt));
            rc = 1;
        } else {
            console_printf("%s\n", result);
        }
    } else {
        char abs[VFS_PATH_MAX];
        if (shell_resolve_path(argv[1], abs, sizeof(abs)) != 0) {
            peak_perror("js", "bad path");
            js_rt_destroy(rt);
            return 1;
        }
        char *src = kmalloc(65536);
        if (!src) {
            peak_perror("js", "oom");
            js_rt_destroy(rt);
            return 1;
        }
        size_t n = 0;
        if (vfs_read_file(abs, src, 65535, &n) != 0) {
            kfree(src);
            peak_perror("js", "cannot read script");
            js_rt_destroy(rt);
            return 1;
        }
        src[n] = '\0';
        if (js_eval(rt, src, abs, result, sizeof(result)) != 0) {
            console_printf("js error: %s\n", js_last_error(rt));
            rc = 1;
        } else {
            console_printf("%s\n", result);
        }
        kfree(src);
    }

    /* Drain timers briefly for simple scripts */
    for (int i = 0; i < 20 && js_pending_work(rt); i++) {
        js_tick(rt);
        for (volatile int j = 0; j < 10000; j++)
            ;
    }

    js_rt_destroy(rt);
    return rc;
}
