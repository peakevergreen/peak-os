#include "vfs.h"
#include "types.h"

#include <string.h>

static struct vfs_node stub_node;
static char stub_path[VFS_PATH_MAX];
static int stub_have;

void img_decode_host_set_vfs_file(const char *path, uint8_t *data, size_t size) {
    stub_have = 0;
    stub_path[0] = '\0';
    memset(&stub_node, 0, sizeof(stub_node));
    if (!path || !data || size == 0)
        return;
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(stub_path); i++)
        stub_path[i] = path[i];
    stub_path[i] = '\0';
    stub_node.type = VFS_FILE;
    stub_node.data = data;
    stub_node.size = size;
    stub_have = 1;
}

void img_decode_host_clear_vfs_file(void) {
    stub_have = 0;
    stub_path[0] = '\0';
    memset(&stub_node, 0, sizeof(stub_node));
}

struct vfs_node *vfs_lookup(const char *path) {
    if (!stub_have || !path || strcmp(path, stub_path) != 0)
        return 0;
    return &stub_node;
}
