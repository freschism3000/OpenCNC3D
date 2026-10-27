/*
 * lmedia.h -- the movies and the music: which files they are, where they come from,
 * putting them in the game folder and taking them out again.
 *
 * THE RELEASES DO NOT CARRY THEM. Both are fetched from disc images on archive.org,
 * straight out of the images by HTTP byte range. The 1995 cutscenes
 * (dosdata/movies/<NAME>.VQA) come from the two Command & Conquer Gold discs, GDI and
 * Nod, which EA released as freeware in 2007: each movie is one contiguous run of bytes
 * inside a disc's MOVIES.MIX. The music is the two archives the game plays it from,
 * dosdata/SCORES.MIX and dosdata/transit/TRANSIT.MIX, each one whole file on the Covert
 * Operations disc, because the Gold discs carry only part of the score and no menu
 * theme. No installer is run, no archive is unpacked, nothing is decoded: the bytes that
 * land are the bytes on the disc, checked against a SHA-256 pinned in the manifest.
 * Older packages carried loose themes in dosdata/music; those are legacy names, removed
 * with the music and never fetched.
 *
 * RED ALERT'S, under the same two boxes, where its module is installed: its movies
 * (ra3d/movies/<NAME>.VQA) and its score (ra3d/audio/SCORES.MIX) come from the two 1996
 * Red Alert discs, Allied and Soviet, which EA released as freeware in 2008. The score
 * is the one file that is not only the disc's bytes: the manifest pins the plain header
 * the game's audio reader needs, and the disc's data block follows it.
 *
 * THE GAME NEVER ASKS ANY OF THIS. It plays a movie whose file is there and skips one
 * that is not, and it plays the score when the archives are there and is silent when
 * they are not. File presence is the only switch, so what is on the disk and what the
 * game does cannot disagree.
 *
 * THE MANIFEST (launcher/lmedia_manifest.c, generated and committed, or a file named
 * on the command line for a test) is text, one record per line, '#' for a comment:
 *
 *   OCMEDIA 1
 *   mirror <DISC> <url> <base>
 *   module <folder>
 *   file <component> <relpath> <size> <sha256> <DISC> <offset> [<head>]
 *   legacy <component> <relpath>
 *
 * DISC is GDI, NOD, COVERT, RA_ALLIED or RA_SOVIET. A mirror's base is the byte where
 * the disc image starts inside what its URL serves (0 for a raw .ISO, the stored entry's
 * data offset for a zip holding one), and mirrors are tried in the order listed. A
 * file's bytes are [base + offset, base + offset + size) of a mirror of its disc; with a
 * head (hex, at most 2 KB) they are the head and then the first size - head bytes of
 * that range. The component is movies or music. A module line names a folder whose
 * files are a module's (lmodule.h knows which modules there are): they count, are
 * fetched and are summed into the sizes only where that module is installed. A legacy
 * path belongs to a component and is removed with it, and is never fetched.
 *
 * THE RECORD, opencnc3d-media.txt at the top of the install, says what the player
 * chose and how far the disk has got:
 *
 *   movies yes|no
 *   music yes|no
 *   movies_state complete|pending|none
 *   music_state complete|pending|none
 *
 * "pending" is a disk that does not match the choice yet: a download that was
 * cancelled, failed or killed, or a removal that was refused. Only this file writes
 * the record, always whole (a new copy renamed over the old), and only while it holds
 * the install lock. The updater's extractor refuses any archive that carries the name,
 * so an update never writes over it or removes it.
 *
 * EVERYTHING SLOW RUNS ON A WORKER THREAD, as the updater's does. The caller only
 * reads the phase, the byte counts and copied-out strings.
 */

#ifndef LMEDIA_H
#define LMEDIA_H

typedef enum
{
    LM_IDLE,
    LM_FETCHING,  /* downloading; lm_bytes moves                                  */
    LM_CHECKING,  /* checking a file's SHA-256, before or after its download     */
    LM_REMOVING,  /* taking out a component the player unticked                  */
    LM_DONE,      /* the disk matches the choice                                  */
    LM_FAILED,    /* lm_status says why, in a player's words                     */
    LM_CANCELLED  /* stopped; what arrived is kept, and the next job resumes it  */
} LM_Phase;

typedef struct LM_State LM_State;

/* `dir` is the install. `manifest_override_path` names a manifest file to read in
 * place of the compiled-in one (a test's), or NULL. Never returns NULL short of
 * running out of memory: a manifest that cannot be read still makes a state, whose
 * lm_status says what is wrong with it and whose lm_apply refuses. */
LM_State *lm_create(const char *dir, const char *manifest_override_path);

/* Per-game settings retain separate records and never act on the other game.
 * ra is 0 for Dawn3D, 1 for RA3D. English is included, never an optional pack.
 * Language packs use the movies slot internally; apply(pack, wanted, 0).
 * The optional language catalog override is for local range-server tests. */
LM_State *lm_create_game(const char *dir, const char *manifest_override_path, int ra);
int lm_language_pack(int ra, int index, char *code, int code_len, char *name, int name_len);
LM_State *lm_create_language(const char *dir, int ra, const char *code, const char *catalog_override);

/* Cancels a running job and waits for it. Call it BEFORE lu_destroy when both exist:
 * a job inside a launcher that already holds the install lock runs under that lock. */
void lm_destroy(LM_State *m);

/* 1 for a component when every one of its files is in the folder with its size, 0
 * otherwise (and for a component the manifest has no files for). A module's files are
 * among them only where the module is installed. A cheap look at the disk: nothing is
 * hashed. */
void lm_installed(LM_State *m, int *movies, int *music);

/* 1 for a component when ANY of its names is in the folder: one of its files, a
 * module's included whether the module is there or not, one of its legacy files, or a
 * part file a download left on either. This is what an untick
 * would remove, which is not the same as lm_installed's "all of it": a download that
 * stopped half way, or the shorter score an older package carried, is present and not
 * installed. `movies_bytes` and `music_bytes` (either may be NULL) say how much that is
 * on the disk. A cheap look: nothing is hashed. */
void lm_present_any(LM_State *m, int *movies, int *music, long long *movies_bytes,
                    long long *music_bytes);

/* THE CHOICE THE RECORD HOLDS, read from opencnc3d-media.txt: for each component, 1 if
 * the player last asked for it and 0 if not, and in each `pending` (may be NULL) 1 when
 * the record says the disk had not got there yet (a download cancelled, failed or
 * killed, or a removal refused). Returns 0, leaving everything at 0, when the folder
 * has no record or it does not say both. */
int lm_wanted(LM_State *m, int *movies, int *music, int *movies_pending, int *music_pending);

/* What each component weighs, in bytes, from the manifest: the game's own files and
 * those of every module installed in the folder at the moment of asking. */
void lm_sizes(LM_State *m, long long *movies_bytes, long long *music_bytes);
/* Preview a physically game-scoped manifest before that game is installed. No
 * files or records change; applies only to module choice UI. */
void lm_game_sizes(LM_State *m, long long *movies_bytes, long long *music_bytes);

/* Make the folder match a choice, on a worker thread: fetch every wanted file that is
 * missing or does not match its SHA-256, and remove every file of a component that is
 * not wanted. Returns 1 once the worker is started, 0 when it is refused (a job is
 * already running, or the manifest is unusable), with lm_status saying why.
 *
 * The worker takes the install lock for the folder, or runs under the one this
 * process already holds (lz_held_here); a folder that is not an install, or that
 * another launcher holds, fails the job with a reason. Files are removed by the exact
 * names the manifest gives (file and legacy, and each one's .ocpart), never by
 * pattern, and never through a link: a path whose way passes a symbolic link or a
 * junction, or that is one, fails the job and is left alone. A download lands on
 * "<name>.ocpart", resumes from there, and is renamed onto its name only once its
 * SHA-256 matches. */
int lm_apply(LM_State *m, int want_movies, int want_music);
/* Installer/Browser choices only add selected content. Unchecked components,
 * including their existing preference and completion state, are untouched. */
int lm_apply_add_only(LM_State *m, int movies, int music);

void lm_cancel(LM_State *m);
LM_Phase lm_phase(LM_State *m);

/* For the bar: bytes so far and bytes in this job's downloads. Both are 0 while
 * nothing is to be downloaded (the job only checks or removes), which the gauge
 * draws as "length unknown". `done` counts what earlier, cancelled attempts left in
 * the .ocpart files, so a resumed job starts part way along. It never goes down
 * within a job: a file thrown away for a bad checksum and fetched again from another
 * mirror holds the count where it was until the new copy passes it. */
void lm_bytes(LM_State *m, long long *done, long long *total);

/* What the worker is on, e.g. "MOVIES: GDI1.VQA (12 OF 75)", or "" when idle. */
void lm_item(LM_State *m, char *buf, int len);

/* Why the last job failed or stopped, in a player's words, or "". */
void lm_status(LM_State *m, char *buf, int len);

/* "312.4 MB", "1.9 GB": MiB below a GiB, GiB from there, one decimal each. */
void lm_format_size(long long bytes, char *buf, int len);

/* 1 if opencnc3d-media.txt exists in `dir`, which means a choice was made there. */
int lm_choice_recorded(const char *dir);

/* NOT PART OF THE INTERFACE A WINDOW NEEDS; for the headless mode and its tests. */

/* How long a transfer may move under 1 KB a second before it counts as stalled and is
 * tried again. 30 seconds unless set; a test sets a few. */
void lm_set_stall(LM_State *m, int seconds);

/* 1 when the last job failed without ever reaching a single host it tried: the
 * machine is offline, or every mirror is gone. */
int lm_offline(LM_State *m);

#endif /* LMEDIA_H */
