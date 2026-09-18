/*
 * lzip.h -- unpack a downloaded release zip over the install it updates, and
 * prove it is the one that was meant before unpacking a byte of it.
 *
 * A zip arriving over the network is REMOTE DATA and is treated as such:
 *
 *   - Its SHA-256 is checked against the manifest before anything is written.
 *     A truncated or tampered download is refused, not unpacked and then noticed.
 *   - Every entry's path is checked. An entry that is absolute, that names a
 *     drive, or that contains "..", is refused outright and stops the extraction:
 *     a zip that can write outside the folder it was pointed at is a zip that can
 *     write anywhere on the machine.
 *   - Every entry's bytes are checked against the CRC-32 and size the archive
 *     records for it, so a release published without a SHA-256 still cannot
 *     install a corrupt file and report success.
 *   - Zip64 is refused rather than misread. The release zips are ~500 MB, so the
 *     32-bit fields are correct today; the day one is not, this says so instead
 *     of extracting from the wrong offset.
 *
 * The unix permission bits in the central directory ARE honoured on POSIX. They
 * carry the executable bit, and an update that installed a game binary without
 * one would leave a folder that looks complete and cannot start.
 *
 * THE FOLDER IT UNPACKS INTO IS IN USE. On Windows the launcher doing the update
 * has its own .exe and SDL2.dll loaded from that folder, and neither can be
 * opened for writing. So lz_extract never rewrites a file that is already
 * identical, writes a changed one beside itself and swaps it in by renaming, so
 * that no name ever holds a partial file, journals every change before making it,
 * puts everything back if any entry fails, and writes the named commit markers
 * last. The extractor's header in lzip.c carries the argument.
 *
 * AND IT CHANGES NOTHING BUT THE INSTALL. Every call here that changes the disk
 * needs a held LZ_Lock, lz_lock is the only thing that makes one, and lz_lock
 * refuses a folder that is not an install. Inside the install, the only files
 * removed that the launcher did not itself just write are copies an update's
 * journal records that update making, each by the exact name its line gives.
 */

#ifndef LZIP_H
#define LZIP_H

#include <stddef.h>

/* The journal lz_extract keeps in the folder it is updating, for as long as the
 * update is unfinished. Its presence at start-up means the last update was killed
 * or could not be fully undone, and lz_settle finishes the job. */
#define LZ_JOURNAL "cnc3d-update.journal"

/* The journal of an update that is over, kept only because some of the old copies
 * it names could not be removed yet (on Windows, the launcher that ran the update
 * still had them loaded). lz_settle removes those copies later, by name. The
 * Windows installer also renames an unfinished journal to this, because the files
 * it installs replace everything that update would have undone. */
#define LZ_DONE "cnc3d-update.done"

/* Called per entry, so a long extraction can move a gauge. Return 0 to abort. */
typedef int (*LZ_Progress)(void *user, int done, int total);

/* Hex, lowercase, 64 characters plus a NUL. Returns 1 on success. */
int lz_sha256_file(const char *path, char *hex65, char *err, int errlen);

/* 1 when `dir` is a C&C 3D install, the only kind of folder the calls below will
 * change: it holds the install record, cnc3d-install.txt, as a plain file, which
 * both release packagers write into every package. What the record says is not
 * asked: launchers up to v0.6.11 rewrote it in place, so a kill could leave it empty,
 * and that folder is still an install. Or, for the moment an update is replacing
 * that record, its journal names the record as stepped aside and the stepped-aside
 * copy is there.
 *
 * A journal alone is not enough, nor the game's menu pack: a folder can hold
 * either without being a game folder (a launcher copied out by hand, a pack copied
 * to try something). Reads only. */
int lz_is_install(const char *dir);

/* ONE PROCESS AT A TIME PER INSTALL. lz_extract and lz_settle both move files
 * another run may be relying on, so the caller takes this lock on the folder
 * first and holds it for as long as it might call either, and both take the
 * folder from the lock rather than from the caller.
 *
 * Returns 1 when taken, 0 when another process holds it, -1 when it could not be
 * taken at all (a folder that cannot be written, typically), and -2, having done
 * nothing at all to the folder, when lz_is_install says it is not an install.
 * Released by lz_unlock, and by the system when the process exits, however it
 * exits. */
typedef struct
{
    void *handle;   /* Windows: the lock file's HANDLE */
    int fd;         /* POSIX: the folder itself, flock()ed */
    int held;       /* set by lz_lock only, and only on an install */
    char dir[1024]; /* the install it was taken on */
} LZ_Lock;

int lz_lock(const char *dir, LZ_Lock *lock);
void lz_unlock(LZ_Lock *lock);

/* A file the caller supplies whole, rather than one read out of the archive. */
typedef struct
{
    const char *name; /* inside the install, forward slashed; NULL ends a list */
    const char *bytes;
    size_t len;
} LZ_File;

/* Extract every entry of `zip` into the install `install` was taken on, creating
 * directories as needed. `strip` drops that many leading path components from each
 * entry, which is how a zip whose entries all begin "CNC3D-macos-v0.6.3/" lands as
 * the folder's contents rather than as a folder inside it.
 *
 * `last` is a NULL-terminated list of names, as they read after stripping, that
 * are written only once every other entry has succeeded, whatever order the zip
 * lists them in. NULL for none.
 *
 * `extra` is a list of files the caller supplies, ended by a NULL name, written
 * after everything in `last` and inside the same journal, so they are undone with
 * the rest. An archive entry of the same name is not written at all. NULL for none.
 *
 * Each changed file is written as "<name>.cnc3d-new" and renamed into place once
 * it is whole and checked, so a name never holds a partial file, even after a kill.
 * An entry reached through a symbolic link or a junction inside the install fails
 * the extraction, naming the folder, as does one on a name the launcher keeps for
 * itself, and a changed file whose "<name>.cnc3d-new" is already taken: whatever is
 * on that name is left alone.
 *
 * Returns 1 on success. On failure every file this call created is removed and
 * every file it replaced is put back, and a changed file that cannot be renamed
 * aside fails the extraction rather than being written over. The one thing a
 * failure cannot always finish is putting a file back while something holds the
 * new copy: `err` then says so, the old copy is kept under its .old name, and the
 * journal left in the install has lz_settle put it back on a later call. While
 * such a journal remains, a new extraction first tries to finish it, and refuses
 * to start if it cannot. */
int lz_extract(const LZ_Lock *install, const char *zip, int strip, const char *const *last,
               const LZ_File *extra, LZ_Progress cb, void *user, char *err, int errlen);

/* 1 when every entry of the archive sits under ONE top-level folder, which makes
 * 1 the right `strip` for lz_extract; 0 for a flat archive or an unreadable one.
 * Read off the archive because the packagers do not agree: the binary-only zip is
 * flat on macOS and wrapped in a folder on Windows. An archive whose files all
 * live in one genuine subfolder (only "content/...", say) would also read as
 * wrapped; no release zip is shaped like that. Reads only. */
int lz_wrapped(const char *zip);

/* Clear up after earlier extractions, in the install `install` was taken on.
 * Does nothing without a held lock, or when called from inside an extraction in
 * the same process.
 *
 * First, a journal left by an update that was killed, or whose undo was refused,
 * is finished: an update that never reached its commit point is undone, one that
 * did has its old copies removed. While an undo is still refused, the journal
 * stays and nothing else is touched, because the .old files it names are the only
 * good copies. Then the copies LZ_DONE names are removed where they can be.
 *
 * What it removes is only ever the exact copy a journal line records its update
 * making: "<name>.old<n>" for an "A <n>" line or the undo's "S <n>" line, and
 * "<name>.cnc3d-new" for an "A" or "N" line, each only while <name> itself exists.
 * Nothing is looked for by pattern: any other file beside <name> is never touched,
 * whatever it is called. */
void lz_settle(const LZ_Lock *install);

/* Make a directory and every missing parent. Returns 1 if it exists afterwards. */
int lz_mkdirs(const char *path);

#endif /* LZIP_H */
