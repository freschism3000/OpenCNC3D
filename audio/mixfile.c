#include "mixfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    unsigned int id;
    unsigned int offset;
    unsigned int size;
} MixEntry;

struct MixFile
{
    FILE *fh;
    char path[512];
    int count;
    long data_start;
    MixEntry *dir;
};

/* The TD id hash: rotate left one, add the next four bytes as a little endian word,
 * uppercased, zero padded at the tail. Verified against the sidebar baker. */
unsigned int mix_id(const char *name)
{
    unsigned int v = 0;
    int len = (int)strlen(name);
    int i = 0;

    while (i < len) {
        unsigned int a = 0;
        int j;
        for (j = 0; j < 4; j++) {
            unsigned char c;
            a >>= 8;
            if (i + j < len) {
                c = (unsigned char)name[i + j];
                if (c >= 'a' && c <= 'z')
                    c = (unsigned char)(c - 'a' + 'A');
                a |= ((unsigned int)c) << 24;
            }
        }
        v = ((v << 1) | (v >> 31));
        v += a;
        i += 4;
    }
    return v;
}

static unsigned short le16(const unsigned char *p) { return (unsigned short)(p[0] | (p[1] << 8)); }

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16)
           | ((unsigned int)p[3] << 24);
}

MixFile *mixfile_open(const char *path, char *err, int errlen)
{
    MixFile *mx;
    unsigned char hdr[6];
    unsigned char *table;
    int i;

    mx = (MixFile *)calloc(1, sizeof(MixFile));
    if (!mx)
        return NULL;
    mx->fh = fopen(path, "rb");
    if (!mx->fh) {
        snprintf(err, (size_t)errlen, "cannot open %s", path);
        free(mx);
        return NULL;
    }
    strncpy(mx->path, path, sizeof mx->path - 1);

    if (fread(hdr, 1, 6, mx->fh) != 6) {
        snprintf(err, (size_t)errlen, "%s: truncated MIX header", path);
        mixfile_close(mx);
        return NULL;
    }
    mx->count = (int)le16(hdr);
    if (mx->count <= 0 || mx->count > 20000) {
        /* An encrypted or RA style MIX would land here. TD's own archives do not. */
        snprintf(err, (size_t)errlen, "%s: implausible entry count %d (encrypted MIX?)", path,
                 mx->count);
        mixfile_close(mx);
        return NULL;
    }

    table = (unsigned char *)malloc((size_t)mx->count * 12u);
    mx->dir = (MixEntry *)malloc((size_t)mx->count * sizeof(MixEntry));
    if (!table || !mx->dir) {
        free(table);
        snprintf(err, (size_t)errlen, "%s: out of memory", path);
        mixfile_close(mx);
        return NULL;
    }
    if (fread(table, 1, (size_t)mx->count * 12u, mx->fh) != (size_t)mx->count * 12u) {
        free(table);
        snprintf(err, (size_t)errlen, "%s: truncated MIX directory", path);
        mixfile_close(mx);
        return NULL;
    }
    for (i = 0; i < mx->count; i++) {
        mx->dir[i].id = le32(table + i * 12);
        mx->dir[i].offset = le32(table + i * 12 + 4);
        mx->dir[i].size = le32(table + i * 12 + 8);
    }
    free(table);
    mx->data_start = 6 + (long)mx->count * 12;
    return mx;
}

void mixfile_close(MixFile *mx)
{
    if (!mx)
        return;
    if (mx->fh)
        fclose(mx->fh);
    free(mx->dir);
    free(mx);
}

int mixfile_count(const MixFile *mx) { return mx ? mx->count : 0; }
const char *mixfile_path(const MixFile *mx) { return mx ? mx->path : ""; }

int mixfile_find_id(const MixFile *mx, unsigned int id, long *offset, long *size)
{
    int i;
    if (!mx)
        return 0;
    for (i = 0; i < mx->count; i++) {
        if (mx->dir[i].id == id) {
            if (offset)
                *offset = mx->data_start + (long)mx->dir[i].offset;
            if (size)
                *size = (long)mx->dir[i].size;
            return 1;
        }
    }
    return 0;
}

int mixfile_find(const MixFile *mx, const char *name, long *offset, long *size)
{
    return mixfile_find_id(mx, mix_id(name), offset, size);
}

int mixfile_entry(const MixFile *mx, int i, unsigned int *id, long *offset, long *size)
{
    if (!mx || i < 0 || i >= mx->count)
        return 0;
    if (id)
        *id = mx->dir[i].id;
    if (offset)
        *offset = mx->data_start + (long)mx->dir[i].offset;
    if (size)
        *size = (long)mx->dir[i].size;
    return 1;
}

int mixfile_read(MixFile *mx, long offset, void *dst, int len)
{
    if (!mx || len <= 0)
        return 0;
    if (fseek(mx->fh, offset, SEEK_SET) != 0)
        return 0;
    return (int)fread(dst, 1, (size_t)len, mx->fh);
}
