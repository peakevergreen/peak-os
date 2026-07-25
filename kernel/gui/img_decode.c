#include "img_decode.h"
#include "heap.h"
#include "vfs.h"
#include "util.h"

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void img_decode_free(struct img_decoded *img) {
    if (!img)
        return;
    if (img->rgb)
        kfree(img->rgb);
    img->rgb = NULL;
    img->rgb_len = 0;
    img->w = img->h = 0;
}

int img_decode_ppm(const uint8_t *data, size_t len, struct img_decoded *out) {
    if (!data || !out || len < 16 || data[0] != 'P' || data[1] != '6')
        return -1;
    size_t i = 2;
    while (i < len && is_space((char)data[i]))
        i++;
    while (i < len && data[i] == '#') {
        while (i < len && data[i] != '\n')
            i++;
        while (i < len && is_space((char)data[i]))
            i++;
    }
    uint32_t w = 0, h = 0, maxv = 0;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        w = w * 10 + (uint32_t)(data[i++] - '0');
    while (i < len && is_space((char)data[i]))
        i++;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        h = h * 10 + (uint32_t)(data[i++] - '0');
    while (i < len && is_space((char)data[i]))
        i++;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        maxv = maxv * 10 + (uint32_t)(data[i++] - '0');
    if (i >= len || !is_space((char)data[i]) || w == 0 || h == 0 || maxv != 255)
        return -1;
    i++;
    size_t need = (size_t)w * (size_t)h * 3;
    if (i + need > len)
        return -1;
    uint8_t *rgb = (uint8_t *)kmalloc(need);
    if (!rgb)
        return -1;
    memcpy(rgb, data + i, need);
    out->w = w;
    out->h = h;
    out->rgb = rgb;
    out->rgb_len = need;
    return 0;
}

int img_decode_bmp(const uint8_t *data, size_t len, struct img_decoded *out) {
    if (!data || !out || len < 54)
        return -1;
    if (data[0] != 'B' || data[1] != 'M')
        return -1;
    uint32_t off = (uint32_t)data[10] | ((uint32_t)data[11] << 8) |
                   ((uint32_t)data[12] << 16) | ((uint32_t)data[13] << 24);
    int32_t dib = (int32_t)((uint32_t)data[14] | ((uint32_t)data[15] << 8) |
                            ((uint32_t)data[16] << 16) | ((uint32_t)data[17] << 24));
    if (dib < 40)
        return -1;
    int32_t w = (int32_t)((uint32_t)data[18] | ((uint32_t)data[19] << 8) |
                          ((uint32_t)data[20] << 16) | ((uint32_t)data[21] << 24));
    int32_t h = (int32_t)((uint32_t)data[22] | ((uint32_t)data[23] << 8) |
                          ((uint32_t)data[24] << 16) | ((uint32_t)data[25] << 24));
    uint16_t bpp = (uint16_t)(data[28] | (data[29] << 8));
    uint32_t comp = (uint32_t)data[30] | ((uint32_t)data[31] << 8) |
                    ((uint32_t)data[32] << 16) | ((uint32_t)data[33] << 24);
    if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32) || comp != 0)
        return -1;
    int top_down = h < 0;
    uint32_t uw = (uint32_t)(w < 0 ? -w : w);
    uint32_t uh = (uint32_t)(h < 0 ? -h : h);
    if (off >= len || uw == 0 || uh == 0 || uw > 8192 || uh > 8192)
        return -1;
    size_t need = (size_t)uw * (size_t)uh * 3;
    uint8_t *rgb = (uint8_t *)kmalloc(need);
    if (!rgb)
        return -1;
    uint32_t row_bytes = ((uw * (uint32_t)bpp + 31) / 32) * 4;
    const uint8_t *src = data + off;
    for (uint32_t row = 0; row < uh; row++) {
        uint32_t sy = top_down ? row : (uh - 1 - row);
        if (off + sy * row_bytes + row_bytes > len) {
            kfree(rgb);
            return -1;
        }
        const uint8_t *srow = src + sy * row_bytes;
        uint8_t *drow = rgb + (size_t)row * (size_t)uw * 3;
        for (uint32_t x = 0; x < uw; x++) {
            const uint8_t *p = srow + (size_t)x * (bpp / 8);
            drow[x * 3 + 0] = p[2];
            drow[x * 3 + 1] = p[1];
            drow[x * 3 + 2] = p[0];
        }
    }
    out->w = uw;
    out->h = uh;
    out->rgb = rgb;
    out->rgb_len = need;
    return 0;
}

int img_decode_file(const char *path, struct img_decoded *out) {
    if (!path || !out)
        return -1;
    struct vfs_node *n = vfs_lookup(path);
    if (!n || n->type != VFS_FILE || !n->data || n->size < 16)
        return -1;
    size_t nl = 0;
    for (const char *p = path; *p; p++)
        nl++;
    if (nl >= 4 && !strcmp(path + nl - 4, ".bmp"))
        return img_decode_bmp(n->data, n->size, out);
    if (nl >= 4 && !strcmp(path + nl - 4, ".ppm"))
        return img_decode_ppm(n->data, n->size, out);
    if (n->size >= 2 && n->data[0] == 'P' && n->data[1] == '6')
        return img_decode_ppm(n->data, n->size, out);
    if (n->size >= 2 && n->data[0] == 'B' && n->data[1] == 'M')
        return img_decode_bmp(n->data, n->size, out);
    return -1;
}
