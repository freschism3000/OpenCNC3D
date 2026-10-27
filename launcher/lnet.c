/*
 * lnet.c -- see lnet.h. Two backends, one behaviour.
 */

#include "lnet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define LN_AGENT "OpenCNC3D-Launcher/1.0"
#define LN_KEY_HEADER "X-OpenCNC3D-Key: "

static void ln_connection_err(char *err, int errlen, int resolve)
{
    snprintf(err, (size_t)errlen, "%s Check your connection and try again.",
             resolve ? "The server address could not be resolved."
                     : "Could not connect to the server.");
}

/* A download lands here first and is renamed on success. Anything that dies
 * halfway leaves a .part behind, which is obviously not a finished file, rather
 * than a truncated zip that unpacks into half a game. */
static void ln_partname(const char *path, char *out, int outlen)
{
    snprintf(out, (size_t)outlen, "%s.part", path);
}

/* ======================================================================== *
 * Shared by both backends: the ranged download's bookkeeping.
 * ======================================================================== */

long long ln_file_size(const char *path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)
        || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return -1;
    return ((long long)d.nFileSizeHigh << 32) | (long long)d.nFileSizeLow;
#else
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    return (long long)st.st_size;
#endif
}

static int ln_starts_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        int a = (unsigned char)*s, b = (unsigned char)*prefix;
        if (a >= 'A' && a <= 'Z')
            a += 'a' - 'A';
        if (a != b)
            return 0;
    }
    return 1;
}

/* "bytes 1000-1999/600000", or with a star for an unknown total, from after the colon.
 * Leaves -1 in both for anything else. */
static void ln_parse_content_range(const char *v, long long *first, long long *last)
{
    long long a = -1, b = -1;
    *first = *last = -1;
    while (*v == ' ' || *v == '\t')
        v++;
    if (!ln_starts_ci(v, "bytes"))
        return;
    v += 5;
    if (sscanf(v, " %lld-%lld", &a, &b) == 2 && a >= 0 && b >= a) {
        *first = a;
        *last = b;
    }
}

/* The part file, opened to append at its end. One longer than the range cannot be a
 * piece of it, so it is started again; one exactly as long is finished already and
 * the caller does not open it at all. */
static FILE *ln_part_open(const char *part, long long length, long long *have)
{
    long long n = ln_file_size(part);
    FILE *f;
    if (n < 0 || n > length) {
        n = 0;
        f = fopen(part, "wb");
    } else {
        f = fopen(part, "ab");
    }
    *have = f ? n : 0;
    return f;
}

static int ln_range_err(int rc, const char *url, long status, int stall_secs, int how,
                        char *err, int errlen)
{
    /* The player is never told which host a file comes from: one sentence fits
     * every mirror, and the mirror list can change without the text changing. */
    const char *host = "the download server";
    (void)url;
    switch (rc) {
    case LN_RANGE_NOCONNECT:
        snprintf(err, (size_t)errlen, "could not reach %s", host);
        break;
    case LN_RANGE_HTTP:
        snprintf(err, (size_t)errlen, "%s answered HTTP %ld", host, status);
        break;
    case LN_RANGE_NORANGE:
        if (how == 1)
            snprintf(err, (size_t)errlen,
                     "%s sent the whole file instead of the part asked for, so nothing was "
                     "taken from it",
                     host);
        else if (how == 2)
            snprintf(err, (size_t)errlen, "%s redirected the download too many times", host);
        else
            snprintf(err, (size_t)errlen,
                     "%s sent a different part of the file than the one asked for", host);
        break;
    case LN_RANGE_BROKEN:
        if (how == 1)
            snprintf(err, (size_t)errlen, "%s stopped sending for %d seconds", host, stall_secs);
        else
            snprintf(err, (size_t)errlen, "the download from %s stopped part way", host);
        break;
    case LN_RANGE_DISK:
        snprintf(err, (size_t)errlen, "the disk would not take the download");
        break;
    case LN_RANGE_CANCEL:
        snprintf(err, (size_t)errlen, "cancelled");
        break;
    default:
        if (errlen > 0)
            err[0] = '\0';
        break;
    }
    return rc;
}

/* ======================================================================== *
 * Windows: WinINet.
 * ======================================================================== */
#ifdef _WIN32

#include <wininet.h>

static void ln_win_err(char *err, int errlen, const char *what)
{
    DWORD code = GetLastError();
    /* WinINet's own errors are not in the system message table, so the number is
     * the useful part and is always printed. 12007 is "host not found", 12029 is
     * "cannot connect", 12175 is a certificate problem; a report that carries the
     * number can be answered without guessing. */
    snprintf(err, (size_t)errlen, "%s (WinINet error %lu)", what, (unsigned long)code);
}

/* BOUND EVERY WAIT. WinINet's own defaults are minutes long, and a caller that is
 * waiting for one of these on a worker thread cannot be shut down until it returns:
 * a screen closing behind an unanswered request would sit there frozen. libcurl is
 * already bounded on the other backend, so this only brings Windows into line. */
static void ln_win_timeouts(HINTERNET h)
{
    DWORD connect_ms = 15000, send_ms = 20000, receive_ms = 20000;
    InternetSetOptionA(h, INTERNET_OPTION_CONNECT_TIMEOUT, &connect_ms, sizeof connect_ms);
    InternetSetOptionA(h, INTERNET_OPTION_SEND_TIMEOUT, &send_ms, sizeof send_ms);
    InternetSetOptionA(h, INTERNET_OPTION_RECEIVE_TIMEOUT, &receive_ms, sizeof receive_ms);
}

/* Open a URL and hand back the request handle plus the two things the caller
 * needs to know about it. Returns NULL with `err` set. */
static int ln_win_classify(DWORD code);

static HINTERNET ln_open(HINTERNET *session, const char *url, const char *key,
                         long long *length, int retry, int *unreached, char *err, int errlen)
{
    char headers[512];
    HINTERNET req;
    DWORD status = 0, len = sizeof status, idx = 0, code = 0;
    int attempt;
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
                  | INTERNET_FLAG_NO_UI | INTERNET_FLAG_KEEP_CONNECTION;

    *length = -1;
    *session = InternetOpenA(LN_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!*session) {
        ln_win_err(err, errlen, "could not start a connection");
        return NULL;
    }
    ln_win_timeouts(*session);

    headers[0] = '\0';
    if (key && *key)
        snprintf(headers, sizeof headers, LN_KEY_HEADER "%s\r\n", key);

    for (attempt = 0; attempt < (retry ? 2 : 1); attempt++) {
        req = InternetOpenUrlA(*session, url, headers[0] ? headers : NULL,
                               headers[0] ? (DWORD)strlen(headers) : 0, flags, 0);
        if (req)
            break;
        code = GetLastError();
        if (code != ERROR_INTERNET_NAME_NOT_RESOLVED && code != ERROR_INTERNET_CANNOT_CONNECT)
            break;
        fprintf(stderr, "network: WinINet error %lu, connection attempt %d/%d\n",
                (unsigned long)code, attempt + 1, retry ? 2 : 1);
    }
    if (!req) {
        if (unreached)
            *unreached = ln_win_classify(code) == LN_RANGE_NOCONNECT;
        if (code == ERROR_INTERNET_NAME_NOT_RESOLVED || code == ERROR_INTERNET_CANNOT_CONNECT)
            ln_connection_err(err, errlen, code == ERROR_INTERNET_NAME_NOT_RESOLVED);
        else {
            SetLastError(code);
            ln_win_err(err, errlen, "could not reach the update server");
        }
        InternetCloseHandle(*session);
        *session = NULL;
        return NULL;
    }

    if (HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status,
                       &len, &idx)
        && status != 200) {
        /* 401 and 403 are the interesting ones: they mean the key is wrong or
         * expired, which is a different problem from the server being down and
         * has a different answer. Say which. */
        if (status == 401 || status == 403)
            snprintf(err, (size_t)errlen,
                     "the update server refused this launcher's key (HTTP %lu)",
                     (unsigned long)status);
        else if (status == 404)
            snprintf(err, (size_t)errlen, "the update server has no such file (HTTP 404)");
        else
            snprintf(err, (size_t)errlen, "the update server answered HTTP %lu",
                     (unsigned long)status);
        InternetCloseHandle(req);
        InternetCloseHandle(*session);
        *session = NULL;
        return NULL;
    }

    {
        char clen[64];
        DWORD n = sizeof clen;
        idx = 0;
        if (HttpQueryInfoA(req, HTTP_QUERY_CONTENT_LENGTH, clen, &n, &idx)) {
            clen[n < sizeof clen ? n : sizeof clen - 1] = '\0';
            *length = _atoi64(clen);
        }
    }
    return req;
}

char *ln_get_text(const char *url, const char *key, long cap, char *err, int errlen)
{
    return ln_get_text_reach(url, key, cap, NULL, err, errlen);
}

char *ln_get_text_reach(const char *url, const char *key, long cap, int *unreached, char *err,
                        int errlen)
{
    HINTERNET session = NULL, req;
    long long length;
    char *buf;
    long got = 0;

    if (unreached)
        *unreached = 0;
    req = ln_open(&session, url, key, &length, 1, unreached, err, errlen);
    if (!req)
        return NULL;
    if (length > cap) {
        snprintf(err, (size_t)errlen, "the reply is %lld bytes, which is not a manifest",
                 length);
        InternetCloseHandle(req);
        InternetCloseHandle(session);
        return NULL;
    }

    buf = (char *)malloc((size_t)cap + 1);
    if (!buf) {
        snprintf(err, (size_t)errlen, "out of memory");
        InternetCloseHandle(req);
        InternetCloseHandle(session);
        return NULL;
    }
    for (;;) {
        DWORD n = 0;
        if (!InternetReadFile(req, buf + got, (DWORD)(cap - got), &n)) {
            ln_win_err(err, errlen, "the connection broke while reading");
            free(buf);
            InternetCloseHandle(req);
            InternetCloseHandle(session);
            return NULL;
        }
        if (n == 0)
            break;
        got += (long)n;
        if (got >= cap)
            break;
    }
    buf[got] = '\0';
    InternetCloseHandle(req);
    InternetCloseHandle(session);
    return buf;
}

char *ln_post_text(const char *url, const char *key, const char *body,
                   const char *content_type, long cap, char *err, int errlen)
{
    /* A POST CANNOT GO THROUGH InternetOpenUrl, which is why this does not simply
     * reuse ln_open: that call chooses the method itself and has nowhere to put a
     * body. The URL has to be taken apart and the request built by hand instead. */
    HINTERNET session = NULL, conn = NULL, req = NULL;
    URL_COMPONENTSA uc;
    char host[256], path[1024], headers[640];
    DWORD status = 0, len = sizeof status, idx = 0;
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
                  | INTERNET_FLAG_NO_UI | INTERNET_FLAG_KEEP_CONNECTION;
    const char *ctype = (content_type && *content_type) ? content_type : "application/json";
    const DWORD blen = (DWORD)(body ? strlen(body) : 0);
    char *buf;
    long got = 0;

    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host;
    uc.dwHostNameLength = sizeof host;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = sizeof path;
    if (!InternetCrackUrlA(url, 0, 0, &uc) || !host[0]) {
        snprintf(err, (size_t)errlen, "that is not an address this build can post to");
        return NULL;
    }
    if (!path[0]) {
        path[0] = '/';
        path[1] = '\0';
    }
    /* HttpOpenRequest DOES NOT READ THE SCHEME. Left to itself it would talk plain
     * HTTP to port 443 and fail in a way that looks like the server being down, so
     * the flag has to be set here from what the URL actually said. */
    if (uc.nScheme == INTERNET_SCHEME_HTTPS)
        flags |= INTERNET_FLAG_SECURE;

    session = InternetOpenA(LN_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!session) {
        ln_win_err(err, errlen, "could not start a connection");
        return NULL;
    }
    ln_win_timeouts(session);

    conn = InternetConnectA(session, host, uc.nPort, NULL, NULL,
                            INTERNET_SERVICE_HTTP, 0, 0);
    if (!conn) {
        ln_win_err(err, errlen, "could not reach the game list");
        InternetCloseHandle(session);
        return NULL;
    }

    req = HttpOpenRequestA(conn, "POST", path, NULL, NULL, NULL, flags, 0);
    if (!req) {
        ln_win_err(err, errlen, "could not reach the game list");
        InternetCloseHandle(conn);
        InternetCloseHandle(session);
        return NULL;
    }

    snprintf(headers, sizeof headers, "Content-Type: %s\r\n%s%s%s",
             ctype,
             (key && *key) ? LN_KEY_HEADER : "",
             (key && *key) ? key : "",
             (key && *key) ? "\r\n" : "");

    if (!HttpSendRequestA(req, headers, (DWORD)strlen(headers),
                          (LPVOID)body, blen)) {
        ln_win_err(err, errlen, "could not reach the game list");
        InternetCloseHandle(req);
        InternetCloseHandle(conn);
        InternetCloseHandle(session);
        return NULL;
    }

    /* THE STATUS IS NOT OPTIONAL HERE. On the GET side a failed query lets the body
     * through, which for a manifest is merely unhelpful; for a publish it would
     * report success for a row the server rejected, so a status nobody could read
     * is treated as a failure rather than shrugged at. */
    if (!HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status,
                        &len, &idx)) {
        ln_win_err(err, errlen, "the game list gave no answer this build could read");
        InternetCloseHandle(req);
        InternetCloseHandle(conn);
        InternetCloseHandle(session);
        return NULL;
    }
    if (status != 200) {
        snprintf(err, (size_t)errlen, "the game list refused this game (HTTP %lu)",
                 (unsigned long)status);
        InternetCloseHandle(req);
        InternetCloseHandle(conn);
        InternetCloseHandle(session);
        return NULL;
    }

    buf = (char *)malloc((size_t)cap + 1);
    if (!buf) {
        snprintf(err, (size_t)errlen, "out of memory");
        InternetCloseHandle(req);
        InternetCloseHandle(conn);
        InternetCloseHandle(session);
        return NULL;
    }
    for (;;) {
        DWORD n = 0;
        if (!InternetReadFile(req, buf + got, (DWORD)(cap - got), &n)) {
            ln_win_err(err, errlen, "the connection broke while reading");
            free(buf);
            InternetCloseHandle(req);
            InternetCloseHandle(conn);
            InternetCloseHandle(session);
            return NULL;
        }
        if (n == 0)
            break;
        got += (long)n;
        if (got >= cap)
            break;
    }
    buf[got] = '\0';
    InternetCloseHandle(req);
    InternetCloseHandle(conn);
    InternetCloseHandle(session);
    return buf;
}

int ln_get_file(const char *url, const char *key, const char *path, LN_Progress cb,
                void *user, char *err, int errlen)
{
    HINTERNET session = NULL, req;
    long long length, done = 0;
    char part[1200];
    FILE *f;
    char chunk[64 * 1024];

    req = ln_open(&session, url, key, &length, 0, NULL, err, errlen);
    if (!req)
        return 0;

    ln_partname(path, part, sizeof part);
    f = fopen(part, "wb");
    if (!f) {
        snprintf(err, (size_t)errlen, "could not write to %s", part);
        InternetCloseHandle(req);
        InternetCloseHandle(session);
        return 0;
    }

    for (;;) {
        DWORD n = 0;
        if (!InternetReadFile(req, chunk, (DWORD)sizeof chunk, &n)) {
            ln_win_err(err, errlen, "the download stopped part way");
            fclose(f);
            remove(part);
            InternetCloseHandle(req);
            InternetCloseHandle(session);
            return 0;
        }
        if (n == 0)
            break;
        if (fwrite(chunk, 1, n, f) != n) {
            snprintf(err, (size_t)errlen, "the disk would not take the whole download");
            fclose(f);
            remove(part);
            InternetCloseHandle(req);
            InternetCloseHandle(session);
            return 0;
        }
        done += n;
        if (cb && !cb(user, done, length)) {
            snprintf(err, (size_t)errlen, "cancelled");
            fclose(f);
            remove(part);
            InternetCloseHandle(req);
            InternetCloseHandle(session);
            return 0;
        }
    }
    fclose(f);
    InternetCloseHandle(req);
    InternetCloseHandle(session);

    /* WHAT WAS PROMISED AGAINST WHAT ARRIVED. InternetReadFile signals end of
     * stream with a zero-length read and does not care whether the server
     * delivered the Content-Length it advertised, so a transfer cut in half
     * finishes here looking exactly like a complete one. libcurl raises
     * CURLE_PARTIAL_FILE for the same case on the other platform, and the
     * launcher's selftest proves it does; this is that check, at the same layer,
     * in the same words, for the platform whose HTTP stack stays quiet. */
    if (length > 0 && done != length) {
        snprintf(err, (size_t)errlen, "the download stopped early. Try Update again.");
        remove(part);
        return 0;
    }

    remove(path); /* MoveFile/rename will not overwrite on Windows */
    if (rename(part, path) != 0) {
        snprintf(err, (size_t)errlen, "could not put the download in place");
        remove(part);
        return 0;
    }
    return 1;
}

/* A RANGED DOWNLOAD, WITH ITS REDIRECTS FOLLOWED BY HAND. Whether WinINet sends a
 * header the caller added again after following a redirect by itself is not
 * documented, and a hop that dropped "Range" would come back as the whole disc image.
 * So the request is opened with INTERNET_FLAG_NO_AUTO_REDIRECT and every Location is
 * followed here, with Range on every request. That also takes a mirror from https to
 * plain http, which WinINet's own redirect handling refuses unless told otherwise. */
static int ln_win_classify(DWORD code)
{
    switch (code) {
    case ERROR_INTERNET_NAME_NOT_RESOLVED: /* 12007 */
    case ERROR_INTERNET_CANNOT_CONNECT:    /* 12029 */
    case ERROR_INTERNET_TIMEOUT:           /* 12002, while connecting */
    case ERROR_INTERNET_SECURITY_CHANNEL_ERROR:
    case ERROR_INTERNET_INVALID_CA:
    case ERROR_INTERNET_SEC_CERT_CN_INVALID:
    case ERROR_INTERNET_SEC_CERT_DATE_INVALID:
    case ERROR_INTERNET_SEC_CERT_REVOKED:
    case ERROR_INTERNET_SEC_CERT_ERRORS:
    case 12175: /* ERROR_WINHTTP_SECURE_FAILURE's WinINet twin */
        return LN_RANGE_NOCONNECT;
    default:
        return LN_RANGE_BROKEN;
    }
}

/* STOP MEANS STOP, EVEN INSIDE A READ. A WinINet call on a host that has stopped
 * sending blocks until the receive timeout, which is the stall bound (30 seconds unless
 * a caller sets another), and a cancel flag looked at between reads is only heard once
 * it returns: Setup's Stop gave up waiting before that and killed the launcher, and a
 * window closed in the middle of a download sat frozen for as long.
 *
 * So for as long as a request is open, a small thread watches the caller's flag, and
 * the moment it is set it closes the request handle. That is WinINet's own way to
 * cancel a call blocked on a handle from another thread: the call returns at once,
 * failed, and the reader sees the flag and says cancelled. Which side closes the handle
 * is settled by one interlocked exchange, so it is closed once and only once: the
 * watcher closes it only while the reader has not taken it back, and the reader, once
 * it has, waits for the watcher to be gone before closing anything itself. A read
 * started in the moment between the reader's own look at the flag and the watcher's
 * close fails on a closed handle, which is the same answer. */
typedef struct
{
    volatile int *cancel;
    HINTERNET req;
    volatile LONG state; /* 0 watching, 1 the reader took the handle back, 2 the watcher closed it */
    HANDLE thread;
} LN_Watch;

static DWORD WINAPI ln_watch_main(LPVOID p)
{
    LN_Watch *w = (LN_Watch *)p;
    for (;;) {
        if (InterlockedCompareExchange(&w->state, 0, 0) != 0)
            return 0;
        if (*w->cancel) {
            if (InterlockedCompareExchange(&w->state, 2, 0) == 0)
                InternetCloseHandle(w->req);
            return 0;
        }
        Sleep(50);
    }
}

static void ln_watch_start(LN_Watch *w, volatile int *cancel, HINTERNET req)
{
    w->cancel = cancel;
    w->req = req;
    w->state = 0;
    w->thread = (cancel && req) ? CreateThread(NULL, 64 * 1024, ln_watch_main, w, 0, NULL) : NULL;
    if (!w->thread)
        w->state = 1; /* nobody is watching: the handle is the reader's alone */
}

/* Ends the watch. 1 when the request handle is still open and the reader's to close. */
static int ln_watch_stop(LN_Watch *w)
{
    InterlockedCompareExchange(&w->state, 1, 0);
    if (w->thread) {
        WaitForSingleObject(w->thread, INFINITE);
        CloseHandle(w->thread);
        w->thread = NULL;
    }
    return w->state != 2;
}

/* The three handles of one hop, the request through the watch. */
static void ln_close_hop(LN_Watch *w, HINTERNET req, HINTERNET conn, HINTERNET session)
{
    if (ln_watch_stop(w) && req)
        InternetCloseHandle(req);
    if (conn)
        InternetCloseHandle(conn);
    if (session)
        InternetCloseHandle(session);
}

int ln_get_range(const char *url, const char *key, const char *part, long long offset,
                 long long length, int stall_secs, volatile int *cancel, LN_Progress cb,
                 void *user, long *status, char *err, int errlen)
{
    char now[8192], next[8192];
    long long have = 0;
    FILE *f;
    int hop, rc = LN_RANGE_NORANGE, how = 2;
    long last = 0;

    if (status)
        *status = 0;
    if (length <= 0)
        return ln_range_err(LN_RANGE_NORANGE, url, 0, stall_secs, 0, err, errlen);
    if (ln_file_size(part) == length)
        return LN_RANGE_OK;
    f = ln_part_open(part, length, &have);
    if (!f)
        return ln_range_err(LN_RANGE_DISK, url, 0, stall_secs, 0, err, errlen);
    if (stall_secs < 1)
        stall_secs = 30;
    snprintf(now, sizeof now, "%s", url);

    for (hop = 0; hop <= 5; hop++) {
        URL_COMPONENTSA uc;
        char host[256], path[4096], extra[4096], target[8192], headers[640];
        HINTERNET session = NULL, conn = NULL, req = NULL;
        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI
                      | INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_KEEP_CONNECTION;
        DWORD st = 0, len = sizeof st, idx = 0;
        DWORD connect_ms = 15000, send_ms = 20000, receive_ms = (DWORD)stall_secs * 1000;
        long long start = offset + have, end = offset + length - 1;
        LN_Watch watch;

        memset(&watch, 0, sizeof watch);
        watch.state = 1;
        if (cancel && *cancel) {
            fclose(f);
            return ln_range_err(LN_RANGE_CANCEL, now, 0, stall_secs, 0, err, errlen);
        }
        memset(&uc, 0, sizeof uc);
        uc.dwStructSize = sizeof uc;
        uc.lpszHostName = host;
        uc.dwHostNameLength = sizeof host;
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = sizeof path;
        uc.lpszExtraInfo = extra;
        uc.dwExtraInfoLength = sizeof extra;
        host[0] = path[0] = extra[0] = '\0';
        if (!InternetCrackUrlA(now, 0, 0, &uc) || !host[0]
            || (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)) {
            rc = LN_RANGE_NOCONNECT;
            snprintf(err, (size_t)errlen, "that is not an address this build can download from");
            fclose(f);
            return rc;
        }
        snprintf(target, sizeof target, "%s%s", path[0] ? path : "/", extra);
        if (uc.nScheme == INTERNET_SCHEME_HTTPS)
            flags |= INTERNET_FLAG_SECURE;

        session = InternetOpenA(LN_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!session) {
            fclose(f);
            return ln_range_err(LN_RANGE_NOCONNECT, now, 0, stall_secs, 0, err, errlen);
        }
        InternetSetOptionA(session, INTERNET_OPTION_CONNECT_TIMEOUT, &connect_ms, sizeof connect_ms);
        InternetSetOptionA(session, INTERNET_OPTION_SEND_TIMEOUT, &send_ms, sizeof send_ms);
        InternetSetOptionA(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &receive_ms, sizeof receive_ms);

        conn = InternetConnectA(session, host, uc.nPort, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
        req = conn ? HttpOpenRequestA(conn, "GET", target, NULL, NULL, NULL, flags, 0) : NULL;
        /* Watched from before the request is sent: connecting and waiting for the
         * headers block too. */
        ln_watch_start(&watch, cancel, req);
        snprintf(headers, sizeof headers, "Range: bytes=%lld-%lld\r\n%s%s%s", start, end,
                 (key && *key) ? LN_KEY_HEADER : "", (key && *key) ? key : "",
                 (key && *key) ? "\r\n" : "");
        if (!req || !HttpSendRequestA(req, headers, (DWORD)strlen(headers), NULL, 0)) {
            DWORD code = GetLastError();
            ln_close_hop(&watch, req, conn, session);
            fclose(f);
            if (cancel && *cancel)
                return ln_range_err(LN_RANGE_CANCEL, now, 0, stall_secs, 0, err, errlen);
            rc = ln_win_classify(code);
            ln_range_err(rc, now, 0, stall_secs, 0, err, errlen);
            {
                size_t n = strlen(err);
                snprintf(err + n, (size_t)errlen - n, " (WinINet error %lu)", (unsigned long)code);
            }
            return rc;
        }

        if (!HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &st, &len, &idx))
            st = 0;
        last = (long)st;
        if (status)
            *status = last;

        if (st == 301 || st == 302 || st == 303 || st == 307 || st == 308) {
            DWORD n = sizeof next;
            idx = 0;
            next[0] = '\0';
            if (!HttpQueryInfoA(req, HTTP_QUERY_LOCATION, next, &n, &idx) || !next[0]) {
                ln_close_hop(&watch, req, conn, session);
                fclose(f);
                return ln_range_err(LN_RANGE_HTTP, now, last, stall_secs, 0, err, errlen);
            } else {
                char combined[8192];
                DWORD cn = sizeof combined;
                next[n < sizeof next ? n : sizeof next - 1] = '\0';
                if (!strncmp(next, "http://", 7) || !strncmp(next, "https://", 8))
                    snprintf(combined, sizeof combined, "%s", next);
                else if (!InternetCombineUrlA(now, next, combined, &cn, ICU_NO_ENCODE))
                    combined[0] = '\0';
                ln_close_hop(&watch, req, conn, session);
                if (!combined[0]) {
                    fclose(f);
                    return ln_range_err(LN_RANGE_HTTP, now, last, stall_secs, 0, err, errlen);
                }
                snprintf(now, sizeof now, "%s", combined);
                rc = LN_RANGE_NORANGE;
                how = 2;
                continue; /* the next hop, with Range again */
            }
        }

        if (st == 416) {
            rc = LN_RANGE_NORANGE;
            how = 0;
        } else if (st >= 400 || st == 0) {
            rc = LN_RANGE_HTTP;
        } else if (st != 206) {
            rc = LN_RANGE_NORANGE;
            how = 1;
        } else {
            char cr[256];
            DWORD n = sizeof cr;
            long long first = -1, lastb = -1;
            idx = 0;
            if (HttpQueryInfoA(req, HTTP_QUERY_CONTENT_RANGE, cr, &n, &idx)) {
                cr[n < sizeof cr ? n : sizeof cr - 1] = '\0';
                ln_parse_content_range(cr, &first, &lastb);
            }
            if (first != start) {
                rc = LN_RANGE_NORANGE;
                how = 0;
            } else {
                char chunk[64 * 1024];
                rc = LN_RANGE_OK;
                while (have < length) {
                    DWORD got = 0, want = (DWORD)sizeof chunk;
                    if ((cancel && *cancel) || (cb && !cb(user, have, length))) {
                        rc = LN_RANGE_CANCEL;
                        break;
                    }
                    if ((long long)want > length - have)
                        want = (DWORD)(length - have);
                    if (!InternetReadFile(req, chunk, want, &got)) {
                        rc = LN_RANGE_BROKEN;
                        how = GetLastError() == ERROR_INTERNET_TIMEOUT ? 1 : 0;
                        if (cancel && *cancel)
                            rc = LN_RANGE_CANCEL; /* the watcher closed the handle */
                        break;
                    }
                    if (got == 0) {
                        rc = LN_RANGE_BROKEN; /* the stream ended before the range did */
                        how = 0;
                        break;
                    }
                    if (fwrite(chunk, 1, got, f) != got) {
                        rc = LN_RANGE_DISK;
                        break;
                    }
                    have += got;
                }
                if (rc == LN_RANGE_OK && cb)
                    cb(user, have, length);
            }
        }
        ln_close_hop(&watch, req, conn, session);
        break;
    }

    /* Whatever a closed handle made the last call say, a Stop was the reason. */
    if (rc != LN_RANGE_OK && cancel && *cancel)
        rc = LN_RANGE_CANCEL;
    if (fclose(f) != 0 && rc == LN_RANGE_OK)
        rc = LN_RANGE_DISK;
    if (rc != LN_RANGE_OK)
        return ln_range_err(rc, now, last, stall_secs, how, err, errlen);
    return LN_RANGE_OK;
}

/* ======================================================================== *
 * macOS: libcurl, which is in the SDK.
 * ======================================================================== */
#else

#include <curl/curl.h>

typedef struct
{
    char *buf;
    long got, cap;
} LN_Sink;

static size_t ln_sink(void *data, size_t sz, size_t n, void *user)
{
    LN_Sink *s = (LN_Sink *)user;
    long add = (long)(sz * n);
    if (s->got + add > s->cap)
        return 0; /* over the cap: curl reports this as a write error, which it is */
    memcpy(s->buf + s->got, data, (size_t)add);
    s->got += add;
    return (size_t)add;
}

typedef struct
{
    FILE *f;
    LN_Progress cb;
    void *user;
    long long done, total;
} LN_FileSink;

static size_t ln_file_sink(void *data, size_t sz, size_t n, void *user)
{
    LN_FileSink *s = (LN_FileSink *)user;
    size_t add = sz * n;
    if (fwrite(data, 1, add, s->f) != add)
        return 0;
    s->done += (long long)add;
    if (s->cb && !s->cb(s->user, s->done, s->total))
        return 0;
    return add;
}

static int ln_xfer(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ul,
                   curl_off_t uln)
{
    LN_FileSink *s = (LN_FileSink *)user;
    (void)dlnow;
    (void)ul;
    (void)uln;
    /* Content-Length arrives before the first byte of body, so this is where the
     * total becomes known. Left at -1 when the server does not say, which the
     * gauge draws as a barber pole rather than as a made-up percentage. */
    if (dltotal > 0)
        s->total = (long long)dltotal;
    /* ASKED WHILE NOTHING ARRIVES TOO. libcurl calls this about once a second even
     * when the socket is silent, which the write callback is not; so a caller that
     * wants out (a window closing over a stalled download) is heard here rather than
     * only when the next byte comes, which might be never. */
    if (s->cb && !s->cb(s->user, s->done, s->total))
        return 1;
    return 0;
}

static struct curl_slist *ln_headers(const char *key)
{
    char line[512];
    if (!key || !*key)
        return NULL;
    snprintf(line, sizeof line, LN_KEY_HEADER "%s", key);
    return curl_slist_append(NULL, line);
}

static void ln_curl_err(char *err, int errlen, CURLcode rc, long status,
                        const char *detail)
{
    if (rc == CURLE_COULDNT_RESOLVE_HOST || rc == CURLE_COULDNT_RESOLVE_PROXY ||
        rc == CURLE_COULDNT_CONNECT) {
        fprintf(stderr, "network: libcurl error %d (%s): %s\n", (int)rc,
                curl_easy_strerror(rc), detail && *detail ? detail : curl_easy_strerror(rc));
        ln_connection_err(err, errlen, rc != CURLE_COULDNT_CONNECT);
        return;
    }
    if (rc == CURLE_PARTIAL_FILE) {
        /* The server said N bytes and sent fewer. curl's own words for this are
         * "transfer closed with 15862 bytes remaining to read", which is exactly
         * right and is not a sentence to show a player. The launcher has its own
         * length check for the platforms whose HTTP layer does not notice; this
         * is the same failure caught one layer earlier, so it gets the same
         * words. */
        snprintf(err, (size_t)errlen,
                 "the download stopped early. Try Update again.");
        return;
    }
    if (rc == CURLE_OPERATION_TIMEDOUT) {
        /* curl's "Operation too slow. Less than 1024 bytes/sec transferred the last 30
         * seconds" is accurate and is not a sentence for a player either. */
        snprintf(err, (size_t)errlen, "the connection stalled. Try again.");
        return;
    }
    if (rc == CURLE_ABORTED_BY_CALLBACK) {
        snprintf(err, (size_t)errlen, "cancelled");
        return;
    }
    if (rc == CURLE_HTTP_RETURNED_ERROR) {
        if (status == 401 || status == 403)
            snprintf(err, (size_t)errlen,
                     "the update server refused this launcher's key (HTTP %ld)", status);
        else if (status == 404)
            snprintf(err, (size_t)errlen, "the update server has no such file (HTTP 404)");
        else
            snprintf(err, (size_t)errlen, "the update server answered HTTP %ld", status);
        return;
    }
    snprintf(err, (size_t)errlen, "%s", detail && *detail ? detail : curl_easy_strerror(rc));
}

char *ln_get_text(const char *url, const char *key, long cap, char *err, int errlen)
{
    return ln_get_text_reach(url, key, cap, NULL, err, errlen);
}

char *ln_get_text_reach(const char *url, const char *key, long cap, int *unreached, char *err,
                        int errlen)
{
    CURL *c = curl_easy_init();
    struct curl_slist *hdrs;
    LN_Sink sink;
    char detail[CURL_ERROR_SIZE];
    CURLcode rc;
    long status = 0;
    int attempt;

    if (unreached)
        *unreached = 0;
    if (!c) {
        snprintf(err, (size_t)errlen, "could not start a connection");
        return NULL;
    }
    sink.buf = (char *)malloc((size_t)cap + 1);
    sink.got = 0;
    sink.cap = cap;
    if (!sink.buf) {
        snprintf(err, (size_t)errlen, "out of memory");
        curl_easy_cleanup(c);
        return NULL;
    }
    detail[0] = '\0';
    hdrs = ln_headers(key);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, LN_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    if (hdrs)
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, ln_sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    for (attempt = 0; attempt < 2; attempt++) {
        rc = curl_easy_perform(c);
        if (attempt || (rc != CURLE_COULDNT_RESOLVE_HOST && rc != CURLE_COULDNT_RESOLVE_PROXY &&
                        rc != CURLE_COULDNT_CONNECT))
            break;
        fprintf(stderr, "network: libcurl error %d (%s): %s; retrying once\n", (int)rc,
                curl_easy_strerror(rc), detail[0] ? detail : curl_easy_strerror(rc));
        sink.got = 0;
        detail[0] = '\0';
    }
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (unreached && rc != CURLE_OK) {
        double ct = 0;
        curl_easy_getinfo(c, CURLINFO_CONNECT_TIME, &ct);
        *unreached = rc == CURLE_COULDNT_RESOLVE_HOST || rc == CURLE_COULDNT_RESOLVE_PROXY
                     || rc == CURLE_COULDNT_CONNECT || rc == CURLE_SSL_CONNECT_ERROR
                     || rc == CURLE_PEER_FAILED_VERIFICATION
                     || (rc == CURLE_OPERATION_TIMEDOUT && ct <= 0);
    }
    if (hdrs)
        curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        ln_curl_err(err, errlen, rc, status, detail);
        free(sink.buf);
        return NULL;
    }
    sink.buf[sink.got] = '\0';
    return sink.buf;
}

char *ln_post_text(const char *url, const char *key, const char *body,
                   const char *content_type, long cap, char *err, int errlen)
{
    CURL *c = curl_easy_init();
    struct curl_slist *hdrs = NULL;
    LN_Sink sink;
    char detail[CURL_ERROR_SIZE];
    char ctype_hdr[128];
    char key_hdr[256];
    CURLcode rc;
    long status = 0;

    if (!c) {
        snprintf(err, (size_t)errlen, "could not start a connection");
        return NULL;
    }
    sink.buf = (char *)malloc((size_t)cap + 1);
    sink.got = 0;
    sink.cap = cap;
    if (!sink.buf) {
        snprintf(err, (size_t)errlen, "out of memory");
        curl_easy_cleanup(c);
        return NULL;
    }
    detail[0] = '\0';

    snprintf(ctype_hdr, sizeof ctype_hdr, "Content-Type: %s",
             (content_type && *content_type) ? content_type : "application/json");
    hdrs = curl_slist_append(hdrs, ctype_hdr);
    if (key && *key) {
        snprintf(key_hdr, sizeof key_hdr, LN_KEY_HEADER "%s", key);
        hdrs = curl_slist_append(hdrs, key_hdr);
    }
    /* A HUNDRED-CONTINUE ROUND TRIP FOR A ROW THIS SIZE IS PURE LATENCY, and some
     * servers never answer it, which turns a publish into a stall. */
    hdrs = curl_slist_append(hdrs, "Expect:");

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, LN_AGENT);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body ? body : "");
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)(body ? strlen(body) : 0));
    /* REDIRECTS ARE REFUSED RATHER THAN FOLLOWED. Following one turns a POST into a
     * GET and drops the body, so the publish would report success having sent
     * nothing. Left unfollowed it comes back as a status that is not 200 and is
     * reported as the failure it is. */
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, ln_sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) {
        ln_curl_err(err, errlen, rc, status, detail);
        free(sink.buf);
        return NULL;
    }
    if (status != 200) {
        snprintf(err, (size_t)errlen, "the game list refused this game (HTTP %ld)", status);
        free(sink.buf);
        return NULL;
    }
    sink.buf[sink.got] = '\0';
    return sink.buf;
}

int ln_get_file(const char *url, const char *key, const char *path, LN_Progress cb,
                void *user, char *err, int errlen)
{
    CURL *c = curl_easy_init();
    struct curl_slist *hdrs;
    LN_FileSink sink;
    char part[1200], detail[CURL_ERROR_SIZE];
    CURLcode rc;
    long status = 0;

    if (!c) {
        snprintf(err, (size_t)errlen, "could not start a connection");
        return 0;
    }
    ln_partname(path, part, sizeof part);
    sink.f = fopen(part, "wb");
    if (!sink.f) {
        snprintf(err, (size_t)errlen, "could not write to %s", part);
        curl_easy_cleanup(c);
        return 0;
    }
    sink.cb = cb;
    sink.user = user;
    sink.done = 0;
    sink.total = -1;
    detail[0] = '\0';
    hdrs = ln_headers(key);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, LN_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    /* A STALLED TRANSFER ENDS. With only a connect timeout, a server that stopped
     * sending left this waiting forever, and the window that started it could not
     * close, because closing waits for this thread. Under 1 KB a second for 30
     * seconds is a download that is not coming. */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    if (hdrs)
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, ln_file_sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, ln_xfer);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &sink);
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (hdrs)
        curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    fclose(sink.f);

    if (rc != CURLE_OK) {
        ln_curl_err(err, errlen, rc, status, detail);
        remove(part);
        return 0;
    }
    remove(path);
    if (rename(part, path) != 0) {
        snprintf(err, (size_t)errlen, "could not put the download in place");
        remove(part);
        return 0;
    }
    return 1;
}

/* The ranged download's state while curl runs. Every response in a redirect chain
 * passes through the header callback, which starts again at each status line, so
 * what the write callback checks is always the response whose body it is given:
 * curl hands the bodies of the redirects it follows to nobody. */
typedef struct
{
    FILE *f;
    long long have, length, start;
    long status;
    long long cr_first, cr_last;
    int checked;
    int verdict; /* -1 while undecided, else an LN_RANGE_* decided in a callback */
    int how;
    volatile int *cancel;
    LN_Progress cb;
    void *user;
} LN_RangeSink;

static size_t ln_range_header(char *data, size_t sz, size_t n, void *user)
{
    LN_RangeSink *s = (LN_RangeSink *)user;
    size_t len = sz * n, k;
    char line[512];

    k = len < sizeof line - 1 ? len : sizeof line - 1;
    memcpy(line, data, k);
    line[k] = '\0';
    if (!strncmp(line, "HTTP/", 5)) {
        const char *sp = strchr(line, ' ');
        s->status = sp ? strtol(sp + 1, NULL, 10) : 0;
        s->cr_first = s->cr_last = -1;
    } else if (ln_starts_ci(line, "content-range:")) {
        ln_parse_content_range(line + 14, &s->cr_first, &s->cr_last);
    }
    return len;
}

static size_t ln_range_sink(void *data, size_t sz, size_t n, void *user)
{
    LN_RangeSink *s = (LN_RangeSink *)user;
    size_t add = sz * n;

    if (!s->checked) {
        /* THE FIRST BYTE DECIDES. A 200 is the whole file and a 206 that starts
         * anywhere else is not this piece: either is refused before a byte of it is
         * written, which is also what stops the rest of it arriving. */
        s->checked = 1;
        if (s->status != 206) {
            s->verdict = LN_RANGE_NORANGE;
            s->how = 1;
            return 0;
        }
        if (s->cr_first != s->start) {
            s->verdict = LN_RANGE_NORANGE;
            s->how = 0;
            return 0;
        }
    }
    if ((long long)add > s->length - s->have) {
        /* More than was asked for: the bytes that complete the piece are taken, and
         * the transfer is stopped, with the piece whole. */
        size_t take = (size_t)(s->length - s->have);
        if (take && fwrite(data, 1, take, s->f) != take) {
            s->verdict = LN_RANGE_DISK;
            return 0;
        }
        s->have += (long long)take;
        s->verdict = LN_RANGE_OK;
        return 0;
    }
    if (add && fwrite(data, 1, add, s->f) != add) {
        s->verdict = LN_RANGE_DISK;
        return 0;
    }
    s->have += (long long)add;
    if (s->cb && !s->cb(s->user, s->have, s->length)) {
        s->verdict = LN_RANGE_CANCEL;
        return 0;
    }
    return add;
}

static int ln_range_xfer(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ul,
                         curl_off_t uln)
{
    LN_RangeSink *s = (LN_RangeSink *)user;
    (void)dltotal;
    (void)dlnow;
    (void)ul;
    (void)uln;
    if ((s->cancel && *s->cancel) || (s->cb && !s->cb(s->user, s->have, s->length))) {
        s->verdict = LN_RANGE_CANCEL;
        return 1;
    }
    return 0;
}

int ln_get_range(const char *url, const char *key, const char *part, long long offset,
                 long long length, int stall_secs, volatile int *cancel, LN_Progress cb,
                 void *user, long *status, char *err, int errlen)
{
    CURL *c;
    struct curl_slist *hdrs;
    LN_RangeSink s;
    char range[96], detail[CURL_ERROR_SIZE];
    CURLcode rc;
    long st = 0;
    int out;

    if (status)
        *status = 0;
    if (length <= 0)
        return ln_range_err(LN_RANGE_NORANGE, url, 0, stall_secs, 0, err, errlen);
    if (ln_file_size(part) == length)
        return LN_RANGE_OK;
    if (stall_secs < 1)
        stall_secs = 30;

    memset(&s, 0, sizeof s);
    s.f = ln_part_open(part, length, &s.have);
    if (!s.f)
        return ln_range_err(LN_RANGE_DISK, url, 0, stall_secs, 0, err, errlen);
    s.length = length;
    s.start = offset + s.have;
    s.cr_first = s.cr_last = -1;
    s.verdict = -1;
    s.cancel = cancel;
    s.cb = cb;
    s.user = user;

    c = curl_easy_init();
    if (!c) {
        fclose(s.f);
        return ln_range_err(LN_RANGE_NOCONNECT, url, 0, stall_secs, 0, err, errlen);
    }
    snprintf(range, sizeof range, "%lld-%lld", s.start, offset + length - 1);
    detail[0] = '\0';
    hdrs = ln_headers(key);
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, LN_AGENT);
    curl_easy_setopt(c, CURLOPT_RANGE, range);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#endif
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, (long)stall_secs);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    if (hdrs)
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, ln_range_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &s);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, ln_range_sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, ln_range_xfer);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &s);
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &st);
    if (status)
        *status = st;

    if (s.verdict >= 0) {
        out = s.verdict;
    } else if (rc == CURLE_OK) {
        if (st != 206) {
            out = LN_RANGE_NORANGE; /* a 200 with nothing in it, say */
            s.how = 1;
        } else if (s.have != s.length) {
            out = LN_RANGE_BROKEN;
        } else {
            out = LN_RANGE_OK;
        }
    } else if (rc == CURLE_HTTP_RETURNED_ERROR) {
        out = st == 416 ? LN_RANGE_NORANGE : LN_RANGE_HTTP;
    } else if (rc == CURLE_COULDNT_RESOLVE_HOST || rc == CURLE_COULDNT_RESOLVE_PROXY
               || rc == CURLE_COULDNT_CONNECT || rc == CURLE_SSL_CONNECT_ERROR
               || rc == CURLE_PEER_FAILED_VERIFICATION) {
        out = LN_RANGE_NOCONNECT;
    } else if (rc == CURLE_OPERATION_TIMEDOUT) {
        /* Either the connect never finished, which is a host that is not there, or
         * the transfer fell under the speed limit, which is a host that stopped. */
        double ct = 0;
        curl_easy_getinfo(c, CURLINFO_CONNECT_TIME, &ct);
        if (ct <= 0) {
            out = LN_RANGE_NOCONNECT;
        } else {
            out = LN_RANGE_BROKEN;
            s.how = 1;
        }
    } else if (rc == CURLE_TOO_MANY_REDIRECTS) {
        out = LN_RANGE_NORANGE;
        s.how = 2;
    } else if (rc == CURLE_ABORTED_BY_CALLBACK) {
        out = LN_RANGE_CANCEL;
    } else {
        out = LN_RANGE_BROKEN;
    }

    if (hdrs)
        curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    if (fclose(s.f) != 0 && out == LN_RANGE_OK)
        out = LN_RANGE_DISK;
    if (out == LN_RANGE_OK && cb)
        cb(user, s.have, s.length);
    if (out != LN_RANGE_OK)
        return ln_range_err(out, url, st, stall_secs, s.how, err, errlen);
    return LN_RANGE_OK;
}

#endif
