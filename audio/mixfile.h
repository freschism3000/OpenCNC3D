/*
 * mixfile.h -- Westwood MIX archive reader, file backed.
 *
 * Header: u16 count, u32 datasize, then count * (u32 id, u32 offset, u32 size).
 * Data starts right after the table, offsets are relative to that point.
 * Names are not stored, only mix_id() of the uppercased name.
 *
 * File backed on purpose. SCORES.MIX is 54 MB and the Windows 98 tier will not
 * hold it, so the reader keeps the directory in RAM (33 to 148 entries, a few kB)
 * and seeks for the bytes. That also makes music streaming a plain fseek into the
 * archive, which is what the 1995 engine did.
 */

#ifndef MIXFILE_H
#define MIXFILE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MixFile MixFile;

unsigned int mix_id(const char *name);

MixFile *mixfile_open(const char *path, char *err, int errlen);
void mixfile_close(MixFile *mx);

int mixfile_count(const MixFile *mx);
const char *mixfile_path(const MixFile *mx);

/* Locate an entry. Returns 1 and fills offset (absolute, from file start) and size,
 * or 0 if the name is not in this archive. */
int mixfile_find(const MixFile *mx, const char *name, long *offset, long *size);
int mixfile_find_id(const MixFile *mx, unsigned int id, long *offset, long *size);

/* Entry by table position, for auditing archives whose names we do not know. */
int mixfile_entry(const MixFile *mx, int i, unsigned int *id, long *offset, long *size);

/* Read `len` bytes at absolute `offset`. Returns bytes read. Not reentrant across
 * threads on one MixFile: one file handle, one seek cursor. */
int mixfile_read(MixFile *mx, long offset, void *dst, int len);

#ifdef __cplusplus
}
#endif

#endif /* MIXFILE_H */
