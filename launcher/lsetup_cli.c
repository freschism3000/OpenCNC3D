/*
 * lsetup_cli.c -- see lsetup_cli.h.
 *
 * NOTHING HERE DOWNLOADS, CHECKS OR WRITES THE GAME ON ITS OWN. The game package goes
 * through lupdate (the site's API, the updater's resume, hash and extractor), the
 * movies and music through lmedia, and this file only drives the two from a command
 * line and reports on them, so the installer is exercising the same code a player's
 * Update button does.
 *
 * THE PERCENTAGE IS ONE NUMBER FOR THE WHOLE INSTALL. The game package and the movies
 * and music are summed into one total before the first byte, so the bar fills once
 * from left to right instead of filling for the game and starting again for the
 * movies. It never goes backwards: a file refetched after a bad checksum takes its
 * bytes off the count, and the bar waits for them to come back rather than dropping.
 */

#include "lsetup_cli.h"
#include "lmedia.h"
#include "lmodule.h"
#include "lcfg.h"
#include "lnet.h"
#include "lupdate.h"
#include "lzip.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define LS_TICK_MS 250

typedef struct
{
    const char *progress; /* the progress file, or NULL */
    const char *cancel;   /* the cancel file, or NULL   */
    int pct;              /* the last one written: it never goes down */
    long beat;
    int cancelled;        /* the cancel file was seen */
    Uint32 last_print;
    Uint32 last_write;    /* --fetch-menu-art: its download reports from the callback */
} LS_Report;

static int ls_exists(const char *path)
{
    return path && *path && ln_file_size(path) >= 0;
}

/* One line of the file, with anything that would break the line format taken out. */
static void ls_clean(char *dst, size_t dstlen, const char *src)
{
    size_t i = 0;
    for (; src && *src && i + 1 < dstlen; src++)
        dst[i++] = (*src == '\n' || *src == '\r') ? ' ' : *src;
    dst[i] = '\0';
}

static void ls_replace(const char *from, const char *to)
{
#ifdef _WIN32
    int tries;
    for (tries = 0; tries < 40; tries++) {
        if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING))
            return;
        Sleep(10); /* the installer reading the old copy at this moment */
    }
    remove(from);
#else
    if (rename(from, to) != 0)
        remove(from);
#endif
}

static const char *ls_state_word(int phase_word)
{
    static const char *const w[] = {"fetching", "checking", "removing", "done", "failed",
                                    "cancelled"};
    return w[phase_word];
}

enum { LS_FETCHING, LS_CHECKING, LS_REMOVING, LS_DONE, LS_FAILED, LS_CANCELLED };

/* Write the progress file (when there is one) and, once a second or at the end, a
 * line on stdout for a person or a test reading the console. */
static void ls_report(LS_Report *r, const char *tag, int state, long long done, long long total,
                      const char *item, const char *reason, int final)
{
    char text[128], a[32], b[32], line_item[256], line_reason[600];
    int pct = r->pct;
    Uint32 now = SDL_GetTicks();

    long long shown = 0;

    if (total > 0) {
        long long d = done < 0 ? 0 : (done > total ? total : done);
        int p = (int)((d * 100) / total);
        if (p > pct)
            pct = p;
        shown = d;
    }
    if (state == LS_DONE)
        pct = 100;
    r->pct = pct;
    r->beat++;

    if (total > 0) {
        /* THE TWO SIZES SAY THE SAME PERCENTAGE AS THE NUMBER BESIDE THEM. The number
         * is held where it got to while the bytes catch up (a refetched file, a total
         * that changed from an estimate to the job's own figure), so the size so far
         * is held with it: the smallest byte count that makes that percentage. */
        if ((shown * 100) / total < pct)
            shown = ((long long)pct * total + 99) / 100;
        if (shown > total)
            shown = total;
        lm_format_size(shown, a, sizeof a);
        lm_format_size(total, b, sizeof b);
        snprintf(text, sizeof text, "%s of %s (%d%%)", a, b, pct);
    } else {
        snprintf(text, sizeof text, "%d%%", pct);
    }
    ls_clean(line_item, sizeof line_item, item);
    ls_clean(line_reason, sizeof line_reason, reason);

    if (r->progress && *r->progress) {
        char tmp[1200];
        FILE *f;
        snprintf(tmp, sizeof tmp, "%s.tmp", r->progress);
        f = fopen(tmp, "wb");
        if (f) {
            int ok = fprintf(f, "state=%s\npct=%d\ntext=%s\nitem=%s\nbeat=%ld\nreason=%s\nend\n",
                             ls_state_word(state), pct, text, line_item, r->beat, line_reason)
                     > 0;
            if (fclose(f) != 0)
                ok = 0;
            if (ok)
                ls_replace(tmp, r->progress);
            else
                remove(tmp);
        }
    }

    if (final || now - r->last_print >= 1000) {
        r->last_print = now;
        printf("%s|progress|state=%s|pct=%d|done=%lld|total=%lld|item=%s\n", tag,
               ls_state_word(state), pct, done, total, line_item);
        fflush(stdout);
    }
}

/* "movies,music" and the rest. Returns 0 for anything else. */
static int ls_parse_choice(const char *s, int *movies, int *music)
{
    char buf[64], *p, *tok;
    *movies = *music = 0;
    if (!s || strlen(s) >= sizeof buf)
        return 0;
    if (!strcmp(s, "none"))
        return 1;
    snprintf(buf, sizeof buf, "%s", s);
    for (p = buf; (tok = strtok(p, ",")) != NULL; p = NULL) {
        if (!strcmp(tok, "movies"))
            *movies = 1;
        else if (!strcmp(tok, "music"))
            *music = 1;
        else
            return 0;
    }
    return *movies || *music;
}

static int ls_lm_state(LM_Phase p)
{
    switch (p) {
    case LM_REMOVING:
        return LS_REMOVING;
    case LM_CHECKING:
        return LS_CHECKING;
    case LM_DONE:
        return LS_DONE;
    case LM_FAILED:
        return LS_FAILED;
    case LM_CANCELLED:
        return LS_CANCELLED;
    default:
        return LS_FETCHING;
    }
}

/* What the record says now, for the result line; "-" for each while there is none. */
static void ls_print_record(const char *dir)
{
    char path[1200], line[256], movies[32] = "-", music[32] = "-";
    FILE *f;
    snprintf(path, sizeof path, "%s/opencnc3d-media.txt", dir);
    f = fopen(path, "rb");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            sscanf(line, "movies_state %31s", movies);
            sscanf(line, "music_state %31s", music);
        }
        fclose(f);
    }
    printf("MEDIA|result|movies=%s|music=%s\n", movies, music);
}

/* Drive one movies-and-music job to its end. `base_done` and `base_total` are what the
 * game package added to the bar before it (0 for --media-install). Returns the exit
 * code. */
static int ls_run_media(LS_Report *r, LM_State *m, int movies, int music, long long base_done,
                        long long base_total, long long estimate, int add_only)
{
    char item[256], status[600];
    LM_Phase ph;
    long long done = 0, total = 0;

    /* A Stop that came before the job began: nothing is started, so nothing is
     * recorded either. */
    if (r->cancelled || ls_exists(r->cancel)) {
        r->cancelled = 1;
        ls_report(r, "MEDIA", LS_CANCELLED, base_done, base_total + estimate, "",
                  "the setup was stopped before the movies and music were downloaded", 1);
        return 2;
    }
    if (!(add_only ? lm_apply_add_only(m, movies, music) : lm_apply(m, movies, music))) {
        lm_status(m, status, sizeof status);
        ls_report(r, "MEDIA", LS_FAILED, base_done, base_total + estimate, "", status, 1);
        fprintf(stderr, "movies and music: %s\n", status);
        return 1;
    }
    for (;;) {
        ph = lm_phase(m);
        if (!r->cancelled && ls_exists(r->cancel)) {
            r->cancelled = 1;
            lm_cancel(m);
        }
        lm_bytes(m, &done, &total);
        lm_item(m, item, sizeof item);
        if (ph == LM_DONE || ph == LM_FAILED || ph == LM_CANCELLED)
            break;
        /* Until the job knows its own total, the estimate holds the bar's scale. */
        ls_report(r, "MEDIA", ls_lm_state(ph), base_done + done,
                  base_total + (total > 0 ? total : estimate), item, "", 0);
        SDL_Delay(LS_TICK_MS);
    }
    lm_status(m, status, sizeof status);
    ls_report(r, "MEDIA", ls_lm_state(ph), base_done + done,
              base_total + (total > 0 ? total : estimate), ph == LM_DONE ? "" : item, status, 1);
    if (ph == LM_DONE)
        return 0;
    if (ph == LM_CANCELLED)
        return 2;
    fprintf(stderr, "movies and music: %s\n", status);
    return lm_offline(m) ? 3 : 1;
}

/* The bytes the chosen components will download into `dir`, before the job has
 * looked: the components not on the disk already, whole. */
static long long ls_media_estimate(LM_State *m, int movies, int music)
{
    long long mb = 0, ub = 0;
    int have_movies = 0, have_music = 0;
    lm_sizes(m, &mb, &ub);
    lm_installed(m, &have_movies, &have_music);
    return (movies && !have_movies ? mb : 0) + (music && !have_music ? ub : 0);
}

static int ls_media_install(const char *dir, const char *spec, const char *manifest, int stall,
                            int game, int add_only, LS_Report *r)
{
    int movies, music, rc;
    LM_State *m;

    if (!ls_parse_choice(spec, &movies, &music)) {
        fprintf(stderr, "--media-install takes movies,music or movies or music or none, not '%s'\n",
                spec);
        ls_report(r, "MEDIA", LS_FAILED, 0, 0, "", "the setup was asked for something it does not know",
                  1);
        return 1;
    }
    if(game>=0 && !(game?lmod_ra_present(dir,NULL,0):lmod_td_present(dir))){
        ls_report(r,"MEDIA",LS_FAILED,0,0,"","The selected game must be installed before its optional media.",1);return 1;
    }
    m = game<0?lm_create(dir,manifest):lm_create_game(dir,manifest,game);
    if (!m) {
        ls_report(r, "MEDIA", LS_FAILED, 0, 0, "", "out of memory", 1);
        return 1;
    }
    lm_set_stall(m, stall);
    printf("MEDIA|start|dir=%s|movies=%d|music=%d\n", dir, movies, music);
    rc = ls_run_media(r, m, movies, music, 0, 0, ls_media_estimate(m, movies, music), add_only);
    if(game>=0){int mo,mu;if(lm_wanted(m,&mo,&mu,NULL,NULL))printf("MEDIA|result|game=%s|movies=%s|music=%s\n",game?"ra":"td",mo?"yes":"no",mu?"yes":"no");}
    else ls_print_record(dir);
    lm_destroy(m);
    return rc;
}

static int ls_install_game(const char *dir, const char *spec, const char *manifest, int stall,
                           const char *build, LS_Report *r)
{
    int movies = 0, music = 0, want_media = spec != NULL, rc = 0;
    LM_State *m = NULL;
    LU_State *u;
    LU_Phase ph;
    char status[600], name[300], item[400];
    long long done = 0, total = -1, estimate = 0, game_total = 0;

    if (want_media && !ls_parse_choice(spec, &movies, &music)) {
        fprintf(stderr, "--media takes movies,music or movies or music or none, not '%s'\n", spec);
        ls_report(r, "GAME", LS_FAILED, 0, 0, "", "the setup was asked for something it does not know",
                  1);
        return 1;
    }
    if (want_media) {
        m = lm_create(dir, manifest);
        if (!m) {
            ls_report(r, "GAME", LS_FAILED, 0, 0, "", "out of memory", 1);
            return 1;
        }
        lm_set_stall(m, stall);
        estimate = ls_media_estimate(m, movies, music);
    }

    printf("GAME|start|dir=%s|site=%s\n", dir, lu_host_summary());
    u = lu_create_fresh(dir);
    if (!u) {
        lm_destroy(m);
        ls_report(r, "GAME", LS_FAILED, 0, 0, "", "out of memory", 1);
        return 1;
    }
    /* --build: Setup installs its own version, never whatever is newest (lu_want) */
    lu_want(u, build);
    if (build && *build)
        printf("GAME|build|%s\n", build);

    /* 1. Which build. */
    lu_check(u);
    for (;;) {
        ph = lu_phase(u);
        if (ph != LU_CHECKING)
            break;
        if (!r->cancelled && ls_exists(r->cancel)) {
            r->cancelled = 1;
            lu_cancel(u);
        }
        if (build && *build) {
            snprintf(item, sizeof item, "GAME: ASKING OPENCNC3D.COM FOR V%s",
                     build[0] == 'v' ? build + 1 : build);
            ls_report(r, "GAME", LS_FETCHING, 0, 0, item, "", 0);
        } else {
            ls_report(r, "GAME", LS_FETCHING, 0, 0, "GAME: ASKING OPENCNC3D.COM FOR THE NEWEST BUILD",
                      "", 0);
        }
        SDL_Delay(LS_TICK_MS);
    }
    lu_status(u, status, sizeof status);
    if (ph == LU_FAILED || r->cancelled) {
        int off = lu_unreachable(u);
        if (r->cancelled) {
            ls_report(r, "GAME", LS_CANCELLED, 0, 0, "", "the setup was stopped", 1);
            rc = 2;
        } else {
            ls_report(r, "GAME", LS_FAILED, 0, 0, "", status, 1);
            fprintf(stderr, "game: %s\n", status);
            rc = off ? 3 : 1;
        }
        lm_destroy(m);
        lu_destroy(u);
        return rc;
    }

    /* 2. The package, unless the folder holds the newest build already. */
    if (ph == LU_AVAILABLE) {
        lu_apply(u);
        for (;;) {
            ph = lu_phase(u);
            if (ph == LU_DONE || ph == LU_FAILED)
                break;
            if (!r->cancelled && ls_exists(r->cancel)) {
                r->cancelled = 1;
                lu_cancel(u);
            }
            lu_bytes(u, &done, &total);
            lu_download_name(u, name, sizeof name);
            if (total > 0)
                game_total = total;
            if (ph == LU_APPLYING) {
                int p = lu_progress(u);
                if (p >= 0)
                    snprintf(item, sizeof item, "GAME: INSTALLING %s (%d%%)", name, p / 10);
                else
                    snprintf(item, sizeof item, "GAME: CHECKING %s", name);
                ls_report(r, "GAME", LS_CHECKING, game_total, game_total + estimate, item, "", 0);
            } else {
                snprintf(item, sizeof item, "GAME: %s", name);
                ls_report(r, "GAME", LS_FETCHING, done, (total > 0 ? total : 0) + estimate, item, "",
                          0);
            }
            SDL_Delay(LS_TICK_MS);
        }
        lu_status(u, status, sizeof status);
        lu_download_name(u, name, sizeof name);
        if (ph != LU_DONE) {
            if (r->cancelled) {
                ls_report(r, "GAME", LS_CANCELLED, done, game_total + estimate, "",
                          "the setup was stopped. What arrived is kept, and the next one carries on "
                          "from there",
                          1);
                rc = 2;
            } else {
                ls_report(r, "GAME", LS_FAILED, done, game_total + estimate, "", status, 1);
                fprintf(stderr, "game: %s\n", status);
                rc = 1;
            }
            lm_destroy(m);
            lu_destroy(u);
            return rc;
        }
        printf("GAME|result|installed=%s\n", name);
    } else {
        char latest[64];
        lu_latest(u, latest, sizeof latest);
        printf("GAME|result|uptodate=v%s\n", latest);
    }

    /* A STOP THAT CAME WHILE THE PACKAGE WAS BEING UNPACKED, too late for the unpacking
     * to hear it: the game is in, and the setup ends here, cancelled, rather than go on
     * to download the movies and music the player has just asked it not to. And one
     * that came in the moment since the last look. */
    if (!r->cancelled && ls_exists(r->cancel))
        r->cancelled = 1;
    if (r->cancelled) {
        ls_report(r, "GAME", LS_CANCELLED, game_total, game_total + estimate, "",
                  "the setup was stopped. The game is installed; the movies and music were "
                  "not downloaded",
                  1);
        lm_destroy(m);
        lu_destroy(u);
        return 2;
    }

    /* 3. The movies and music, under the lock the install took, which this process
     *    still holds, so no other launcher can start on the folder in between. Their
     *    size is asked again now the package is in: a package that carries a module
     *    brings that module's movies and score into the download. The bar does not step
     *    back for a larger total; it waits for the bytes to catch up. */
    if (want_media) {
        estimate = ls_media_estimate(m, movies, music);
        rc = ls_run_media(r, m, movies, music, game_total, game_total, estimate, 0);
        ls_print_record(dir);
    } else {
        ls_report(r, "GAME", LS_DONE, game_total, game_total, "", "", 1);
    }
    lm_destroy(m); /* before lu_destroy, whose lock the job ran under */
    lu_destroy(u);
    return rc;
}

/* --fetch-menu-art. */
static int ls_pack_tick(void *user, long long done, long long total)
{
    LS_Report *r = (LS_Report *)user;
    if (SDL_GetTicks() - r->last_write >= LS_TICK_MS) {
        r->last_write = SDL_GetTicks();
        if (!r->cancelled && ls_exists(r->cancel))
            r->cancelled = 1;
        ls_report(r, "PACK", LS_FETCHING, done, total, "MENU ART", "", 0);
    }
    return !r->cancelled;
}

static int ls_fetch_pack(const char *dir, LS_Report *r)
{
    char err[512];
    int unreached = 0;
    printf("PACK|start|dir=%s|site=%s\n", dir, lu_host_summary());
    ls_report(r, "PACK", LS_FETCHING, 0, 0, "MENU ART", "", 0);
    if (lu_fetch_pack(dir, ls_pack_tick, r, &unreached, err, sizeof err)) {
        ls_report(r, "PACK", LS_DONE, 0, 0, "", "", 1);
        printf("PACK|result|installed=dosmenu.pack\n");
        return 0;
    }
    if (r->cancelled) {
        ls_report(r, "PACK", LS_CANCELLED, 0, 0, "", "the download of the menu art was stopped", 1);
        return 2;
    }
    ls_report(r, "PACK", LS_FAILED, 0, 0, "", err, 1);
    fprintf(stderr, "menu art: %s\n", err);
    return unreached ? 3 : 1;
}

static int ls_module_progress(void *user,int done,int total)
{
    LS_Report *r=(LS_Report *)user;
    if(r->cancel&&ls_exists(r->cancel)){r->cancelled=1;return 0;}
    ls_report(r,"MODULE",LS_CHECKING,done,total,"Installing verified game files","",0);return 1;
}
static int ls_install_module(const char *dir,int target,const char *file,const char *sha,
                              const char *site,LS_Report *r)
{
    char err[512];LMOD_Catalog catalog;int ok;
    ls_report(r,"MODULE",LS_CHECKING,0,1000,"Checking game package","",0);
    if(file)ok=lmod_install_file(dir,target,LCFG_BUILD,file,sha,ls_module_progress,r,err,sizeof err);
    else{
        ok=site?lmod_catalog_fetch_at(site,LCFG_BUILD,&catalog,err,sizeof err):lmod_catalog_fetch(LCFG_BUILD,&catalog,err,sizeof err);
        if(ok)ok=lmod_install_remote(dir,target,catalog.version[0]?catalog.version:LCFG_BUILD,&catalog,ls_module_progress,r,err,sizeof err);
    }
    ls_report(r,"MODULE",ok?LS_DONE:r->cancelled?LS_CANCELLED:LS_FAILED,ok?1000:0,1000,"",ok?"":err,1);
    printf("MODULE-INSTALL|%s|%s\n",ok?"PASS":"FAIL",ok?"verified module installed":err);
    return ok?0:r->cancelled?2:1;
}

int lsetup_cli_main(int argc, char **argv, int *handled)
{
    const char *dir = NULL, *media_install = NULL, *media = NULL, *manifest = NULL;
    const char *build = NULL,*module_file=NULL,*module_sha=NULL,*module_site=NULL;
    LS_Report r;
    int install_game = 0, fetch_pack = 0, stall = 30, i, rc,media_game=-1,module_target=-1,media_add_only=0;

    *handled = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--"))
            break; /* everything after belongs to the game */
        if (!strcmp(argv[i], "--media-install") || !strcmp(argv[i], "--install-game")
            || !strcmp(argv[i], "--fetch-menu-art") || !strcmp(argv[i],"--install-module"))
            *handled = 1;
    }
    if (!*handled)
        return 0;

#ifdef _WIN32
    /* The launcher is a windowed program, so its output goes nowhere unless it joins
     * the console of whatever started it, as --check does. */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif

    memset(&r, 0, sizeof r);
    r.pct = 0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--"))
            break;
        if(!strcmp(a,"--install-module")&&v){
            module_target=!strcmp(v,"td")?0:(!strcmp(v,"ra")||!strcmp(v,"ra3d"))?1:-1;
            if(module_target<0){fprintf(stderr,"--install-module takes td or ra3d\n");return 1;}i++;
        }else if(!strcmp(a,"--module-file")&&v){module_file=v;i++;}
        else if(!strcmp(a,"--module-sha")&&v){module_sha=v;i++;}
        else if(!strcmp(a,"--module-site")&&v){
            if(strncmp(v,"http://127.0.0.1:",17)&&strncmp(v,"http://localhost:",17)){fprintf(stderr,"--module-site requires a loopback qualification server\n");return 1;}
            module_site=v;i++;
        }else if(!strcmp(a,"--media-add-only")){media_add_only=1;
        }else if(!strcmp(a,"--media-game")&&v){
            media_game=!strcmp(v,"td")?0:!strcmp(v,"ra")?1:-1;
            if(media_game<0){fprintf(stderr,"--media-game takes td or ra\n");return 1;}i++;
        }else if (!strcmp(a, "--install-game")) {
            install_game = 1;
        } else if (!strcmp(a, "--fetch-menu-art")) {
            fetch_pack = 1;
        } else if (!strcmp(a, "--media-install") && v) {
            media_install = v;
            i++;
        } else if (!strcmp(a, "--build") && v) {
            build = v;
            i++;
        } else if (!strcmp(a, "--media") && v) {
            media = v;
            i++;
        } else if (!strcmp(a, "--dir") && v) {
            dir = v;
            i++;
        } else if (!strcmp(a, "--media-manifest") && v) {
            manifest = v;
            i++;
        } else if (!strcmp(a, "--progress-file") && v) {
            r.progress = v;
            i++;
        } else if (!strcmp(a, "--cancel-file") && v) {
            r.cancel = v;
            i++;
        } else if (!strcmp(a, "--stall-seconds") && v) {
            stall = atoi(v);
            i++;
        } else if (!strcmp(a, "--media-install")) {
            fprintf(stderr, "--media-install needs movies,music or movies or music or none\n");
            return 1;
        }
    }

    if (!dir || !*dir) {
        fprintf(stderr, "--media-install and --install-game need --dir <the game folder>\n");
        ls_report(&r, install_game ? "GAME" : "MEDIA", LS_FAILED, 0, 0, "",
                  "the setup was not told which folder to use", 1);
        return 1;
    }
    if ((install_game ? 1 : 0) + (media_install ? 1 : 0) + fetch_pack + (module_target>=0?1:0) > 1) {
        fprintf(stderr, "use --install-game with --media, or --media-install or --fetch-menu-art "
                        "on its own\n");
        ls_report(&r, "GAME", LS_FAILED, 0, 0, "", "the setup was asked for two things at once", 1);
        return 1;
    }
    if((module_file&&!module_sha)||(!module_file&&module_sha)){
        fprintf(stderr,"--module-file and --module-sha must be supplied together\n");return 1;
    }
    if(media_add_only&&(!media_install||media_game<0)){
        fprintf(stderr,"--media-add-only requires --media-install and --media-game td|ra\n");return 1;
    }
    /* Refuse before catalog requests, lock/recovery, cache writes or removing a
     * caller's cancellation file. Media-only jobs remain independent. */
    if ((install_game || module_target >= 0) && lz_is_private_developer(dir)) {
        fprintf(stderr, "This private source build is updated from its source checkout, not public game packages.\n");
        return 1;
    }
    if (stall < 1)
        stall = 30;

    /* A cancel file left by an earlier run would stop this one before it started. */
    if (r.cancel && ls_exists(r.cancel))
        remove(r.cancel);

    if(module_target>=0)
        rc=ls_install_module(dir,module_target,module_file,module_sha,module_site,&r);
    else if (fetch_pack)
        rc = ls_fetch_pack(dir, &r);
    else if (install_game)
        rc = ls_install_game(dir, media, manifest, stall, build, &r);
    else
        rc = ls_media_install(dir, media_install, manifest, stall, media_game, media_add_only, &r);
    printf("SETUP|exit|%d\n", rc);
    fflush(stdout);
    return rc;
}
