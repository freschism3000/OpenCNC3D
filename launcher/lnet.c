/*
 * lnet.c -- see lnet.h. Two backends, one behaviour.
 */

#include "lnet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LN_AGENT "CNC3D-Launcher/1.0"
#define LN_KEY_HEADER "X-CNC3D-Key: "

/* A download lands here first and is renamed on success. Anything that dies
 * halfway leaves a .part behind, which is obviously not a finished file, rather
 * than a truncated zip that unpacks into half a game. */
static void ln_partname(const char *path, char *out, int outlen)
{
    snprintf(out, (size_t)outlen, "%s.part", path);
}

/* ======================================================================== *
 * Windows: WinINet.
 * ======================================================================== */
#ifdef _WIN32

#include <windows.h>
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

/* Open a URL and hand back the request handle plus the two things the caller
 * needs to know about it. Returns NULL with `err` set. */
static HINTERNET ln_open(HINTERNET *session, const char *url, const char *key,
                         long long *length, char *err, int errlen)
{
    char headers[512];
    HINTERNET req;
    DWORD status = 0, len = sizeof status, idx = 0;
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE
                  | INTERNET_FLAG_NO_UI | INTERNET_FLAG_KEEP_CONNECTION;

    *length = -1;
    *session = InternetOpenA(LN_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!*session) {
        ln_win_err(err, errlen, "could not start a connection");
        return NULL;
    }

    headers[0] = '\0';
    if (key && *key)
        snprintf(headers, sizeof headers, LN_KEY_HEADER "%s\r\n", key);

    req = InternetOpenUrlA(*session, url, headers[0] ? headers : NULL,
                           headers[0] ? (DWORD)strlen(headers) : 0, flags, 0);
    if (!req) {
        ln_win_err(err, errlen, "could not reach the update server");
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
    HINTERNET session = NULL, req;
    long long length;
    char *buf;
    long got = 0;

    req = ln_open(&session, url, key, &length, err, errlen);
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

int ln_get_file(const char *url, const char *key, const char *path, LN_Progress cb,
                void *user, char *err, int errlen)
{
    HINTERNET session = NULL, req;
    long long length, done = 0;
    char part[1200];
    FILE *f;
    char chunk[64 * 1024];

    req = ln_open(&session, url, key, &length, err, errlen);
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
    CURL *c = curl_easy_init();
    struct curl_slist *hdrs;
    LN_Sink sink;
    char detail[CURL_ERROR_SIZE];
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
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
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

#endif
