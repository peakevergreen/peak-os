#include "blockdev.h"

static const struct blockdev_ops *g_bd;

void blockdev_register(const struct blockdev_ops *ops) {
    g_bd = ops;
}

const struct blockdev_ops *blockdev_get(void) {
    return g_bd;
}

int blockdev_present(void) {
    return g_bd && g_bd->present && g_bd->present();
}

int blockdev_read(uint64_t lba, uint32_t count, void *buf) {
    if (!g_bd || !g_bd->read)
        return -1;
    return g_bd->read(lba, count, buf);
}

int blockdev_write(uint64_t lba, uint32_t count, const void *buf) {
    if (!g_bd || !g_bd->write)
        return -1;
    return g_bd->write(lba, count, buf);
}

int blockdev_flush(void) {
    if (!g_bd || !g_bd->flush)
        return 0;
    return g_bd->flush();
}
