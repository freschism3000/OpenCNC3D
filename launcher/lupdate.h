/*
 * lupdate.h -- "is there a newer build, and can you put it on this disk".
 *
 * THE SITE IS THE SOURCE OF TRUTH. cnc3dgame.com already has a Builds section,
 * and it is generated from three routes the launcher reads directly:
 *
 *   GET /api/builds                 the newest release and its assets
 *   GET /api/changelog              the changelog, entry by entry
 *   GET /api/download?asset=<id>    the zip, via a signed redirect
 *
 * So there is no separate publishing step and no key in the binary. The repo is
 * private and its release assets are not public; the site proxies them, which is
 * what lets a launcher on a player's machine reach a build without holding any
 * credential at all. Put a build on the site and every launcher offers it.
 *
 * The one thing the site does not publish is a checksum or a data fingerprint,
 * and lupdate.c's header says exactly what the launcher does about that.
 *
 * EVERYTHING SLOW RUNS ON A WORKER THREAD. The UI thread only ever reads the
 * phase, the progress and a copied-out string, so a stalled socket cannot freeze
 * the window: a launcher that stops repainting while it waits on a server is
 * indistinguishable from one that has crashed.
 */

#ifndef LUPDATE_H
#define LUPDATE_H

typedef enum
{
    LU_NOTCONFIGURED = 0, /* no host was stamped in: the check is not possible  */
    LU_IDLE,
    LU_CHECKING,
    LU_UPTODATE,
    LU_AVAILABLE,   /* a newer build exists; notes are loaded                   */
    LU_DOWNLOADING, /* progress is meaningful                                   */
    LU_APPLYING,    /* verifying and unpacking; progress is meaningful          */
    LU_DONE,        /* installed. The caller restarts or plays.                 */
    LU_FAILED       /* status carries the reason, in a player's words           */
} LU_Phase;

typedef struct LU_State LU_State;

/* `dir` is the installed folder (the one holding dosmenu.pack). `version` is what
 * is installed now, as a bare number like "0.6.2", or NULL when it is not known
 * yet. Never returns NULL: a state that cannot work is still a state that can say
 * why.
 *
 * CALL IT BEFORE ANYTHING ELSE READS THE FOLDER. It takes the install lock and,
 * holding it, finishes whatever an earlier update left unfinished, so the
 * version, the menu art and everything else read afterwards describe one build
 * rather than half of two. Only when it gets the lock and the undo is not refused:
 * while another launcher holds the lock, or something still has open a file the
 * undo has to move, it recovers nothing, says nothing, and the folder is read as it
 * stands. lu_destroy releases the lock, and has to be called before the game is
 * started.
 *
 * A folder that is not an install (lz_is_install) gets no lock and no recovery,
 * and cannot be updated: the state still checks and still reports. */
LU_State *lu_create(const char *dir, const char *version);
void lu_destroy(LU_State *u);

/* What is installed, read once lu_create has finished the folder. */
void lu_set_installed(LU_State *u, const char *version);

/* Both start a worker and return at once. Calling either while one is running is
 * ignored rather than queued. */
void lu_check(LU_State *u);
void lu_apply(LU_State *u);

LU_Phase lu_phase(const LU_State *u);
int lu_progress(const LU_State *u); /* thousandths, or -1 for "length unknown" */

/* Copied out under the lock, so the caller may hold the result across frames. */
void lu_status(LU_State *u, char *buf, int len);
void lu_latest(LU_State *u, char *buf, int len);

/* The remote build's changelog. NULL until a check has fetched it. The pointer is
 * owned by the state and is only replaced between checks, which the UI thread
 * serialises by only asking for it when the phase says it exists. */
const char *lu_notes(LU_State *u);

/* True once the state has produced something the UI has not yet turned into a
 * rebuilt text panel. Clears on read. */
int lu_notes_changed(LU_State *u);

/* What this build was compiled to talk to, for the About line and for a support
 * question that starts "which host is it even trying". Never NULL. */
const char *lu_host_summary(void);

#endif /* LUPDATE_H */
