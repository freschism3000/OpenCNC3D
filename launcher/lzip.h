/*
 * lzip.h -- unpack a downloaded release zip, and prove it is the one that was
 * meant before unpacking a byte of it.
 *
 * A zip arriving over the network is REMOTE DATA and is treated as such:
 *
 *   - Its SHA-256 is checked against the manifest before anything is written.
 *     A truncated or tampered download is refused, not unpacked and then noticed.
 *   - Every entry's path is checked. An entry that is absolute, that names a
 *     drive, or that contains "..", is refused outright and stops the extraction:
 *     a zip that can write outside the folder it was pointed at is a zip that can
 *     write anywhere on the machine.
 *   - Zip64 is refused rather than misread. The release zips are ~500 MB, so the
 *     32-bit fields are correct today; the day one is not, this says so instead
 *     of extracting from the wrong offset.
 *
 * The unix permission bits in the central directory ARE honoured on POSIX. They
 * carry the executable bit, and an update that installed a game binary without
 * one would leave a folder that looks complete and cannot start.
 */

#ifndef LZIP_H
#define LZIP_H

/* Called per entry, so a long extraction can move a gauge. Return 0 to abort. */
typedef int (*LZ_Progress)(void *user, int done, int total);

/* Hex, lowercase, 64 characters plus a NUL. Returns 1 on success. */
int lz_sha256_file(const char *path, char *hex65, char *err, int errlen);

/* Extract every entry of `zip` under `dest`, creating directories as needed.
 * `strip` drops that many leading path components from each entry, which is how
 * a zip whose entries all begin "CNC3D-macos-v0.6.3/" lands as the folder's
 * contents rather than as a folder inside it. Returns 1 on success. */
int lz_extract(const char *zip, const char *dest, int strip, LZ_Progress cb, void *user,
               char *err, int errlen);

/* Does the archive carry this exact path, after `strip` leading components are
 * dropped? Asked before an extraction that would replace the running program:
 * stepping aside for a zip that turns out not to carry a replacement leaves the
 * folder with no launcher at all, which is a way to brick an install with a
 * successful update. Returns 1 for yes, 0 for no or for an unreadable archive. */
int lz_has_entry(const char *zip, int strip, const char *rel);

/* Make a directory and every missing parent. Returns 1 if it exists afterwards. */
int lz_mkdirs(const char *path);

#endif /* LZIP_H */
