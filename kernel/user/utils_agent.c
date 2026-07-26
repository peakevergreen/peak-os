#include "libpeak.h"
#include "vfs.h"
#include "shell.h"
#include "console.h"
#include "agent.h"
#include "util.h"
#include "peakvec.h"
#include "sysmon.h"

int upeak_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    console_write("Peak OS 0.2.0-ai — research workstation\n");
    console_write("Agent tools: fs.read fs.write fs.list fs.exec fs.stat fs.mkdir fs.rm\n");
    console_write("             fs.search fs.grep fs.diff sys.info net.ping net.fetch mem.recall audit.tail console.print\n");
    console_write("Try: ask \"summarize workspace\"   ask \"search README\"   audit   memory   peakvec   policy\n");
    console_write("Desktop: gui → Agent app (approve writes with Y/N)\n");
    return 0;
}

int uask_main(int argc, char **argv) {
    if (peak_wants_help(argc, argv) || argc < 2) {
        peak_usage("ask", "<prompt...>  (quotes: ask \"create fib.c\"  ask \"run ls .\")");
        return argc < 2 ? 1 : 0;
    }
    if (argc == 2) {
        agent_ask(argv[1]);
        return 0;
    }
    char buf[512];
    peak_join_args(argc, argv, 1, buf, sizeof(buf));
    agent_ask(buf);
    return 0;
}

static void print_session_tail(const char *cmd, const char *path, const char *empty_hint) {
    char buf[2048];
    size_t len = 0;
    if (vfs_read_file(path, buf, sizeof(buf) - 1, &len) != 0 || !len) {
        console_printf("%s: %s\n", cmd, empty_hint);
        return;
    }
    buf[len] = '\0';

    int lines = 0;
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '\n')
            lines++;
    if (len && buf[len - 1] != '\n')
        lines++;

    console_printf("%s: %s (%zu bytes, %d lines)\n", cmd, path, len, lines);
    console_write("--- recent entries ---\n");

    const char *start = buf;
    int tail_lines = 0;
    for (const char *p = buf + len; p > buf; p--) {
        if (p[-1] == '\n') {
            tail_lines++;
            if (tail_lines > 12) {
                start = p;
                break;
            }
        }
    }

    int n = 1;
    for (const char *p = start; *p; p++) {
        if (p == start || p[-1] == '\n') {
            const char *eol = strchr(p, '\n');
            size_t ll = eol ? (size_t)(eol - p) : strlen(p);
            if (ll > 0) {
                char line[96];
                size_t show = ll < sizeof(line) - 1 ? ll : sizeof(line) - 1;
                memcpy(line, p, show);
                line[show] = '\0';
                console_printf("%3d  %s\n", n++, line);
            }
        }
    }
    if (n == 1)
        console_write("(no printable lines)\n");
    console_write("--- end ---\n");
}

int uaudit_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print_session_tail("audit", "/var/peak/audit.log",
                       "(empty — agent actions appear here after ask)");
    return 0;
}

static void print_peakvec_index_line(const char *ns) {
    struct peakvec_stats st;
    peakvec_stats(ns, &st);
    console_printf("PeakVec [%s]: %u / %u entries (table %u, max %u)",
                   ns && ns[0] ? ns : "agent",
                   (unsigned)st.count, (unsigned)st.max_entries,
                   (unsigned)st.capacity, (unsigned)st.max_entries);
    if (st.use_blob) console_printf(", blob id %u", (unsigned)st.blob_id);
    else console_write(", vfs fallback");
    if (st.ann_active)
        console_printf(", ivf-lite (>= %u)", (unsigned)st.ann_threshold);
    else if (st.count)
        console_printf(", brute (< %u)", (unsigned)st.ann_threshold);
    console_putc('\n');
}

static void print_peakvec_hits(const struct peakvec_hit *hits, int n) {
    for (int i = 0; i < n; i++) {
        if (!hits[i].key[0])
            continue;
        int score = hits[i].score_milli;
        console_printf("  %d.%03d  %s", score / 1000, score % 1000, hits[i].key);
        if (hits[i].meta[0])
            console_printf("  %s", hits[i].meta);
        console_putc('\n');
    }
}

static void print_peakvec_explain(const struct peakvec_query_explain *ex) {
    console_printf("explain: mode=%s bucket=%u live=%u probed=%u remainder=%u scored=%u skipped=%u",
                   ex->use_ann ? "ivf-lite" : "brute",
                   (unsigned)ex->query_bucket,
                   (unsigned)ex->ns_live,
                   (unsigned)ex->bucket_probed,
                   (unsigned)ex->remainder,
                   (unsigned)ex->scored,
                   (unsigned)ex->skipped_early);
    if (ex->ann_shortcut)
        console_write(" shortcut=1");
    console_putc('\n');
}

static int peakvec_run_query(int argc, char **argv) {
    const char *ns = "agent";
    int topk = 3;
    int explain = 0;
    int timing = 0;
    char text[256];
    text[0] = '\0';

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--explain")) {
            explain = 1;
            continue;
        }
        if (!strcmp(argv[i], "--timing")) {
            timing = 1;
            continue;
        }
        if (!strcmp(argv[i], "-k") && i + 1 < argc) {
            topk = peak_atoi(argv[++i]);
            if (topk <= 0)
                topk = 1;
            if (topk > PEAKVEC_TOPK_MAX)
                topk = PEAKVEC_TOPK_MAX;
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1])
            continue;
        if (!text[0])
            peak_join_args(argc, argv, i, text, sizeof(text));
        break;
    }
    if (!text[0]) {
        peak_usage("peakvec query", "<text...> [-k N] [--explain] [--timing]");
        return 1;
    }

    int16_t vec[PEAKVEC_DIM];
    peakvec_embed_text(text, vec);
    struct peakvec_hit hits[PEAKVEC_TOPK_MAX];
    struct peakvec_query_explain qx;
    int n = peakvec_query_ex(ns, vec, topk, hits, explain || timing ? &qx : NULL);
    if (n < 0) {
        console_write("peakvec query: failed\n");
        return 1;
    }
    console_printf("peakvec query [%s]: %d hit%s for \"%s\"\n",
                   ns, n, n == 1 ? "" : "s", text);
    print_peakvec_hits(hits, n);
    if (explain)
        print_peakvec_explain(&qx);
    if (timing) {
        char tbuf[16];
        sysmon_format_us(qx.elapsed_us, tbuf, sizeof(tbuf));
        console_printf("timing: %s (sysmon peakvec_us)\n", tbuf);
    }
    return 0;
}

int umemory_main(int argc, char **argv) {
    (void)argc; (void)argv;
    print_peakvec_index_line("agent");
    print_session_tail("memory", "/var/peak/sessions/memory.txt",
                       "(empty — ask records turn summaries here)");
    return 0;
}

static int peakvec_namespace_list(void) {
    console_write("PeakVec namespaces: agent (default), session\n");
    print_peakvec_index_line("agent");
    print_peakvec_index_line("session");
    return 0;
}

int upeakvec_main(int argc, char **argv) {
    const char *ns = "agent";
    if (peak_wants_help(argc, argv)) {
        peak_usage("peakvec", "stats [namespace] | namespace | query <text...> [-k N] [--explain] [--timing]");
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "namespace"))
        return peakvec_namespace_list();
    if (argc >= 2 && !strcmp(argv[1], "query"))
        return peakvec_run_query(argc, argv);
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') continue;
        if (!strcmp(argv[i], "stats")) {
            if (i + 1 < argc && argv[i + 1][0] != '-')
                ns = argv[i + 1];
            break;
        }
        ns = argv[i];
        break;
    }
    print_peakvec_index_line(ns);
    return 0;
}

static void policy_print_csv(const char *label, const char *csv) {
    console_printf("%s:\n", label);
    if (!csv || !csv[0]) {
        console_write("  (none)\n");
        return;
    }
    const char *p = csv;
    while (*p) {
        while (*p == ',')
            p++;
        if (!*p)
            break;
        console_write("  ");
        while (*p && *p != ',')
            console_putc(*p++);
        console_putc('\n');
    }
}

int upolicy_main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[2048];
    size_t len = 0;
    console_write("policy: /etc/peak/agent.policy\n\n");
    if (vfs_read_file("/etc/peak/agent.policy", buf, sizeof(buf) - 1, &len) != 0) {
        console_write("(empty — defaults apply)\n\n");
        console_write("allow_paths:\n");
        console_write("  /home/dev/workspace\n");
        console_write("  /var/peak/sessions\n\n");
        console_write("allow_tools:\n");
        console_write("  fs.read fs.write fs.list console.print fs.exec\n");
        console_write("  mem.recall audit.tail fs.stat fs.mkdir fs.rm fs.search fs.grep fs.diff\n");
        console_write("  sys.info net.ping net.fetch\n\n");
        console_write("deny_tools:\n  (none)\n\n");
        console_write("require_approval: fs.write\n");
        return 0;
    }
    buf[len] = '\0';

    char paths[512] = "";
    char allow[512] = "";
    char deny[512] = "";
    char approval[64] = "fs.write";

    const char *p = buf;
    while (*p) {
        if (!strncmp(p, "allow_paths=", 12)) {
            size_t i = 0;
            for (p += 12; *p && *p != '\n' && i + 1 < sizeof(paths); p++)
                paths[i++] = *p;
            paths[i] = '\0';
        } else if (!strncmp(p, "allow_tools=", 12)) {
            size_t i = 0;
            for (p += 12; *p && *p != '\n' && i + 1 < sizeof(allow); p++)
                allow[i++] = *p;
            allow[i] = '\0';
        } else if (!strncmp(p, "deny_tools=", 11)) {
            size_t i = 0;
            for (p += 11; *p && *p != '\n' && i + 1 < sizeof(deny); p++)
                deny[i++] = *p;
            deny[i] = '\0';
        } else if (!strncmp(p, "require_approval=", 17)) {
            size_t i = 0;
            for (p += 17; *p && *p != '\n' && i + 1 < sizeof(approval); p++)
                approval[i++] = *p;
            approval[i] = '\0';
        }
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }

    policy_print_csv("allow_paths", paths);
    console_putc('\n');
    policy_print_csv("allow_tools", allow);
    console_putc('\n');
    policy_print_csv("deny_tools", deny);
    console_printf("\nrequire_approval: %s\n", approval[0] ? approval : "(none)");
    return 0;
}
