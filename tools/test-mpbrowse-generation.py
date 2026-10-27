#!/usr/bin/env python3
"""Deterministically test the live browser worker with a queued SDL scheduler.

Compiles menu/mpbrowse.c and launcher/ljson.c unchanged. Only thread scheduling,
mutexes and HTTP transport are stand-ins. Runs no game, GPU or network request.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

SDL = r'''
#ifndef PRIVATE_MODEL_SDL_H
#define PRIVATE_MODEL_SDL_H
struct SDL_mutex { int unused; };
struct SDL_Thread { int (*fn)(void *); void *arg; int ran; };
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_Thread SDL_Thread;
SDL_mutex *SDL_CreateMutex(void);
int SDL_LockMutex(SDL_mutex *);
int SDL_UnlockMutex(SDL_mutex *);
SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *, void *);
void SDL_DetachThread(SDL_Thread *);
#endif
'''

MODEL = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"
#include "mpbrowse.h"

static SDL_mutex locks[16];
static int nlocks;
static SDL_Thread jobs[16];
static int njobs, active_job = -1;
static int response_rows[16], nested_job[16], before_forget[16];
static int rows_after_nested, phase_after_nested;
static char fourteen[8192];
static int checks, fails;
static const char *case_name;
static int fail_malloc, fail_thread, cancel_on_failure;
static unsigned job_allocated, job_freed, job_live, malloc_attempts;
static void *owned[16];

static void check(int condition, const char *what) {
    ++checks;
    if (!condition) {
        ++fails;
        printf("FAIL|%s|%s\n", case_name, what);
    }
}
#define CK(test) check((test), #test)
static void *job_malloc(size_t bytes) {
    void *p; ++malloc_attempts;
    if (fail_malloc) {
        fail_malloc = 0;
        if (cancel_on_failure) mb_forget();
        return NULL;
    }
    p = malloc(bytes); assert(p); assert(job_live < 16);
    owned[job_live++] = p; ++job_allocated; return p;
}
static void job_free(void *p) {
    unsigned i;
    for (i=0; i<job_live; ++i) if (owned[i] == p) {
        owned[i] = owned[--job_live]; ++job_freed; break;
    }
    free(p);
}
SDL_mutex *SDL_CreateMutex(void) { assert(nlocks < 16); return &locks[nlocks++]; }
int SDL_LockMutex(SDL_mutex *m) { assert(m != NULL); return 0; }
int SDL_UnlockMutex(SDL_mutex *m) { assert(m != NULL); return 0; }
SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *name, void *arg) {
    SDL_Thread *t; (void)name;
    if (fail_thread) {
        fail_thread = 0;
        if (cancel_on_failure) mb_forget();
        return NULL;
    }
    assert(njobs < 16); t = &jobs[njobs++];
    t->fn = fn; t->arg = arg; t->ran = 0; return t;
}
void SDL_DetachThread(SDL_Thread *t) { assert(t != NULL); }
unsigned long rc_draw_peer_id(void) { return 1; }
static void run_job(int);

/* Interpose only allocations in the unchanged production browser, leaving the
   real JSON parser and HTTP fixture allocations independent of job failures. */
#define malloc job_malloc
#define free job_free
#include MPBROWSE_SOURCE
#undef malloc
#undef free

static int count_rows(void) { MB_Row rows[MB_MAX_ROWS]; return mb_rows(rows, MB_MAX_ROWS); }
char *ln_get_text(const char *url, const char *key, long cap, char *err, int errlen) {
    int this_job = active_job; const char *reply;
    (void)url; (void)key; (void)cap;
    assert(this_job >= 0);
    if (before_forget[this_job]) {
        mb_forget(); mb_refresh(); assert(njobs == 2);
        run_job(1);
        rows_after_nested = count_rows(); phase_after_nested = mb_phase();
    } else if (nested_job[this_job] >= 0) {
        run_job(nested_job[this_job]);
        rows_after_nested = count_rows(); phase_after_nested = mb_phase();
    }
    if (response_rows[this_job] < 0) {
        snprintf(err, (size_t)errlen, "private transport refusal"); return NULL;
    }
    reply = response_rows[this_job] ? fourteen : "{\"games\":[]}";
    return strdup(reply);
}
char *ln_post_text(const char *url, const char *key, const char *body,
                   const char *content_type, long cap, char *err, int errlen) {
    (void)url; (void)key; (void)body; (void)content_type;
    (void)cap; (void)err; (void)errlen; return strdup("{}");
}
static void run_job(int n) {
    int saved = active_job; assert(n >= 0 && n < njobs && !jobs[n].ran);
    jobs[n].ran = 1; active_job = n; CK(jobs[n].fn(jobs[n].arg) == 0);
    active_job = saved;
}
static void reset(const char *name) {
    int i;
    /* Every queued request must have completed before a case drops its state. */
    assert(job_live == 0);
    memset(&s_b, 0, sizeof s_b); memset(&s_p, 0, sizeof s_p);
    memset(s_url, 0, sizeof s_url); s_url_lock = NULL;
    nlocks = njobs = 0; active_job = -1;
    rows_after_nested = phase_after_nested = -1;
    fail_malloc = fail_thread = cancel_on_failure = 0;
    job_allocated = job_freed = job_live = malloc_attempts = 0;
    case_name = name;
    for (i=0; i<16; ++i) { response_rows[i] = 14; nested_job[i] = -1; before_forget[i] = 0; }
    CK(mb_set_url("http://127.0.0.1/model") == 1);
}
static void finish_case(void) {
    CK(job_live == 0 && job_allocated == job_freed);
    printf("CASE|%s|phase=%d|rows=%d|allocated=%u|freed=%u\n",
           case_name, mb_phase(), count_rows(), job_allocated, job_freed);
}
static void late_start_case(const char *name, int old_reply_rows) {
    reset(name);
    mb_refresh(); CK(njobs == 1 && s_b.generation == 1);
    mb_forget(); CK(s_b.generation == 2 && mb_phase() == MB_IDLE);
    mb_refresh(); CK(njobs == 2 && s_b.generation == 3);
    response_rows[0] = old_reply_rows; nested_job[0] = 1;
    run_job(0);
    CK(rows_after_nested == 14 && phase_after_nested == MB_READY);
    CK(count_rows() == 14 && mb_phase() == MB_READY);
    CK(mb_error()[0] == '\0'); finish_case();
}
int main(void) {
    int i, used = snprintf(fourteen, sizeof fourteen, "{\"games\":[");
    (void)job_malloc; /* The before-fix browser makes no job allocation. */
    for (i=0; i<14; ++i) {
        used += snprintf(fourteen + used, sizeof fourteen - (size_t)used,
            "%s{\"id\":\"%032x\",\"name\":\"Fixture%d\",\"map\":\"TEST\","
            "\"scenario\":\"SCM01EA\",\"players\":1,\"max\":4,\"relay\":true,"
            "\"room\":\"#AAA-001\"}", i ? "," : "", i+1, i);
    }
    snprintf(fourteen + used, sizeof fourteen - (size_t)used, "]}");
    late_start_case("late-old-start-both14", 14);
    late_start_case("late-old-start-old0", 0);
    late_start_case("late-old-start-old-network-failure", -1);

    reset("old-started-before-forget-old0");
    mb_refresh(); response_rows[0] = 0; before_forget[0] = 1; run_job(0);
    CK(rows_after_nested == 14 && count_rows() == 14);
    CK(mb_phase() == MB_READY && s_b.generation == 3); finish_case();

    reset("old-starts-after-forget-no-refresh");
    mb_refresh(); mb_forget(); CK(mb_phase() == MB_IDLE); run_job(0);
    CK(count_rows() == 0 && mb_phase() == MB_IDLE); finish_case();

    reset("only-one-in-flight");
    mb_refresh(); mb_refresh(); CK(njobs == 1);
    CK(job_allocated == 1 && job_live == 1);
    run_job(0); CK(count_rows() == 14 && mb_phase() == MB_READY); finish_case();

    reset("job-allocation-failure-retry");
    fail_malloc = 1; mb_refresh();
    CK(njobs == 0 && job_live == 0 && malloc_attempts == 1);
    CK(mb_phase() == MB_FAILED && !strcmp(mb_error(), "COULD NOT LOOK FOR GAMES."));
    /* On old source the injected allocation is absent: drain that request so
       every case remains valid and records the failed regression accurately. */
    if (njobs) run_job(0);
    mb_refresh(); run_job(njobs-1);
    CK(count_rows() == 14 && mb_phase() == MB_READY); finish_case();

    reset("thread-creation-failure-retry");
    fail_thread = 1; mb_refresh();
    CK(njobs == 0 && job_allocated == 1 && job_freed == 1 && job_live == 0);
    CK(mb_phase() == MB_FAILED && !strcmp(mb_error(), "COULD NOT LOOK FOR GAMES."));
    mb_refresh(); run_job(0);
    CK(count_rows() == 14 && mb_phase() == MB_READY); finish_case();

    reset("thread-failure-after-forget");
    fail_thread = cancel_on_failure = 1; mb_refresh();
    CK(njobs == 0 && job_live == 0);
    CK(mb_phase() == MB_IDLE && mb_error()[0] == '\0'); finish_case();

    reset("allocation-failure-after-forget");
    fail_malloc = cancel_on_failure = 1; mb_refresh();
    CK(njobs == 0 && job_live == 0);
    CK(mb_phase() == MB_IDLE && mb_error()[0] == '\0');
    if (njobs) run_job(0);
    finish_case();
    printf("MODEL|checks=%d|fails=%d|no_gpu=1|no_network=1\n", checks, fails);
    return fails ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--source', type=Path, help='Override only the browser source for before/after proof')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--cc', default=shutil.which('clang') or shutil.which('cc'))
    parser.add_argument('--expect-failure', action='store_true', help='For a known pre-fix source only')
    args = parser.parse_args()
    repo = args.repo.resolve()
    source = (args.source or repo / 'menu/mpbrowse.c').resolve()
    out = args.out or Path(tempfile.mkdtemp(prefix='mpbrowse-generation-'))
    out.mkdir(parents=True, exist_ok=True)
    (out / 'SDL.h').write_text(SDL)
    (out / 'model.c').write_text(MODEL)
    command = [args.cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(out),
               '-I', str(repo / 'menu'), '-DMPBROWSE_SOURCE='+json.dumps(str(source)),
               str(out / 'model.c'), str(repo / 'launcher/ljson.c'), '-o', str(out / 'model')]
    subprocess.run(command, check=True)
    result = subprocess.run([str(out / 'model')], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (out / 'result.log').write_text(result.stdout + result.stderr)
    report = dict(status='PASS' if result.returncode == 0 else 'FAIL', exit=result.returncode,
                  source=str(source), source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  json_parser_sha256=hashlib.sha256((repo / 'launcher/ljson.c').read_bytes()).hexdigest(),
                  compiler_command=command, output=str(out), result=result.stdout,
                  scope='Unchanged live browser and JSON parser; queued SDL scheduler and HTTP fixtures only; no game, GPU or network.',
                  expected_old_failure_observed=args.expect_failure and result.returncode != 0)
    (out / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(result.stdout, end='')
    if result.stderr: print(result.stderr, end='')
    print('Receipt:', out / 'report.json')
    return (0 if result.returncode != 0 else 1) if args.expect_failure else result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
