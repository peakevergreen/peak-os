#include <stdio.h>
#include <string.h>

#include "agent_internal.h"

static int fails;

static void expect(int ok, const char *msg) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int count_csv_fields(const char *csv) {
    if (!csv || !csv[0])
        return 0;
    int n = 1;
    for (const char *p = csv; *p; p++)
        if (*p == ',')
            n++;
    return n;
}

int main(void) {
    const char *cat = agent_tools_catalog();
    expect(cat != NULL, "catalog non-null");
    expect(agent_tools_catalog_len() == strlen(cat), "catalog len");
    expect(count_csv_fields(cat) == 16, "sixteen agent tools");
    expect(strstr(cat, "fs.read") != NULL, "fs.read listed");
    expect(strstr(cat, "fs.grep") != NULL, "fs.grep listed");
    expect(strstr(cat, "fs.diff") != NULL, "fs.diff listed");
    expect(strstr(cat, "net.ping") != NULL, "net.ping listed");
    expect(strstr(cat, "net.fetch") != NULL, "net.fetch listed");
    expect(strstr(cat, "mem.recall") != NULL, "mem.recall listed");
    expect(strstr(cat, "audit.tail") != NULL, "audit.tail listed");

    if (fails) {
        fprintf(stderr, "%d agent tools test(s) failed\n", fails);
        return 1;
    }
    printf("test_agent_tools: ok\n");
    return 0;
}
