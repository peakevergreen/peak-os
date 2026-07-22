/* /bin container staging: ctr build/run/ps/logs; ctrd ping helper. */
#include "libpeak.h"
#include "ctr.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "net.h"
#include "util.h"

/* Image tags look like name:tag (contain ':'); context paths usually do not. */
static int looks_like_image_tag(const char *s) {
    if (!s || !s[0])
        return 0;
    for (const char *p = s; *p; p++) {
        if (*p == ':')
            return 1;
    }
    return 0;
}

static int contains_web(const char *s) {
    if (!s)
        return 0;
    for (const char *p = s; *p; p++) {
        if (!strncmp(p, "web", 3))
            return 1;
    }
    return 0;
}

static int path_is_web_demo(const char *path) {
    if (!path)
        return 0;
    size_t n = strlen(path);
    if (n >= 8 && !strcmp(path + n - 8, "web-demo"))
        return 1;
    return !strcmp(path, "web-demo") || !strcmp(path, "./web-demo");
}

int uctr_main(int argc, char **argv) {
    ctr_init();

    if (argc < 2 || peak_wants_help(argc, argv)) {
        console_write("usage:\n");
        console_write("  ctr build [path] [-t tag]   build Dockerfile (default: cwd)\n");
        console_write("  ctr run [-p port] [--name n] [img]\n");
        console_write("  ctr ps | logs [name] | stop [name]\n");
        console_write("  ctr ping\n");
        console_write("Demos:\n");
        console_write("  cd demo && ctr build && ctr run\n");
        console_write("  cd web-demo && ctr build && ctr run\n");
        console_write("  ctr build web-demo -t peak/web:latest\n");
        console_write("  ctr run -p 8080 --name peak-web peak/web:latest\n");
        console_write("In-guest Peak runtime - TCP listen + Browser virtual GET.\n");
        return 0;
    }

    if (!strcmp(argv[1], "ping")) {
        char resp[128];
        ctr_ping(resp, sizeof(resp));
        console_write(resp);
        console_write("\n");
        return 0;
    }

    if (!strcmp(argv[1], "build")) {
        /* Default context is the current directory so `cd web-demo && ctr build` works. */
        const char *ctx_arg = ".";
        const char *tag = NULL;
        int tagged = 0;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-t") && i + 1 < argc) {
                tag = argv[++i];
                tagged = 1;
            } else if (argv[i][0] != '-') {
                /* `ctr build peak/web:latest` → treat as tag, keep cwd as context */
                if (!tagged && looks_like_image_tag(argv[i])) {
                    tag = argv[i];
                    tagged = 1;
                } else {
                    ctx_arg = argv[i];
                }
            }
        }
        char abs[VFS_PATH_MAX];
        if (shell_resolve_path(ctx_arg, abs, sizeof(abs)) != 0) {
            peak_perror("ctr", "bad context path");
            return 1;
        }
        if (!tagged) {
            if (path_is_web_demo(abs) || path_is_web_demo(ctx_arg))
                tag = "peak/web:latest";
            else
                tag = "peak/demo:latest";
        }
        console_printf("ctr: building %s from %s\n", tag, abs);
        char blog[1024];
        if (ctr_build(abs, tag, blog, sizeof(blog)) != 0) {
            console_write(blog);
            peak_perror("ctr", "build failed");
            return 1;
        }
        console_write(blog);
        console_printf("ctr: built %s\n", tag);
        return 0;
    }

    if (!strcmp(argv[1], "run")) {
        const char *img = NULL;
        const char *port = NULL;
        const char *name = NULL;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-p") && i + 1 < argc)
                port = argv[++i];
            else if (!strcmp(argv[i], "--name") && i + 1 < argc)
                name = argv[++i];
            else if (argv[i][0] != '-')
                img = argv[i];
        }
        /* No image given → run whatever was built most recently this boot. */
        char lb_tag[64], lb_port[16];
        int have_last = (ctr_last_built(lb_tag, sizeof(lb_tag), lb_port, sizeof(lb_port)) == 0);
        if (!img)
            img = have_last ? lb_tag : "peak/demo:latest";
        /* Default name derived from the image so run/stop/logs stay consistent. */
        if (!name)
            name = contains_web(img) ? "peak-web" : "peak-demo";
        /* Port precedence: explicit -p > image EXPOSE > sensible default. */
        char exp[16];
        if (!port) {
            if (ctr_image_expose(img, exp, sizeof(exp)) == 0 && exp[0])
                port = exp;
            else if (have_last && !strcmp(img, lb_tag) && lb_port[0])
                port = lb_port;
            else
                port = contains_web(img) ? "8080" : "18080";
        }
        char rlog[512];
        console_printf("ctr: running %s on :%s (in-guest)...\n", img, port);
        if (ctr_run(img, name, port, rlog, sizeof(rlog)) != 0) {
            console_write(rlog);
            peak_perror("ctr", "run failed");
            return 1;
        }
        console_write(rlog);
        struct net_info ni;
        net_get_info(&ni);
        char ipb[32];
        net_format_ip(ni.ip, ipb, sizeof(ipb));
        console_write("ctr: Browser → http://127.0.0.1:");
        console_write(port);
        console_write("/\n");
        if (ni.up && ni.ip) {
            console_printf("ctr: LAN     → http://%s:%s/\n", ipb, port);
        }
        return 0;
    }

    if (!strcmp(argv[1], "ps")) {
        char out[1024];
        ctr_ps(out, sizeof(out));
        console_write(out);
        return 0;
    }

    if (!strcmp(argv[1], "logs")) {
        const char *name = "peak-demo";
        if (argc >= 3)
            name = argv[2];
        char out[1024];
        if (ctr_logs(name, out, sizeof(out)) != 0) {
            peak_perror("ctr", "no such container");
            return 1;
        }
        console_write(out);
        return 0;
    }

    if (!strcmp(argv[1], "stop")) {
        const char *name = "peak-demo";
        if (argc >= 3)
            name = argv[2];
        if (ctr_stop(name) != 0) {
            peak_perror("ctr", "no such container");
            return 1;
        }
        console_printf("ctr: stopped %s\n", name);
        return 0;
    }

    peak_perror("ctr", "unknown subcommand");
    return 1;
}

int uctrd_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    ctr_init();
    console_write("ctrd: Peak Container Runtime ready (in-guest).\n");
    console_write("  cd demo && ctr build && ctr run\n");
    console_write("  cd web-demo && ctr build && ctr run\n");
    console_write("  GUI: Browser → http://127.0.0.1:8080/\n");
    console_write("  Host/LAN: http://localhost:8080/ (user-net port forward)\n");
    char resp[128];
    ctr_ping(resp, sizeof(resp));
    console_write("ctrd: ");
    console_write(resp);
    console_write("\n");
    return 0;
}
