/* Native text-GET transport fixture. Failure injection wraps the real libcurl
 * call, so recovery still has to reach the private loopback HTTP server. */
#include <curl/curl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int attempts, mode;
static CURLcode real_perform(CURL *handle) { return curl_easy_perform(handle); }
static CURLcode fixture_perform(CURL *handle)
{
    CURLcode result;
    attempts++;
    if (mode == 1 && attempts == 1) return CURLE_COULDNT_RESOLVE_HOST;
    if (mode == 2 && attempts == 1) return CURLE_COULDNT_CONNECT;
    if (mode == 3) return CURLE_COULDNT_RESOLVE_HOST;
    if (mode == 4) return CURLE_PEER_FAILED_VERIFICATION;
    if (mode == 5) return CURLE_OPERATION_TIMEDOUT;
    result = real_perform(handle);
    if (mode == 6 || mode == 7)
        fprintf(stderr, "HTTP-NATIVE|mode=%d|attempt=%d|curl=%d\n", mode, attempts, (int)result);
    return result;
}
#define curl_easy_perform fixture_perform
#include "../../launcher/lnet.c"
#undef curl_easy_perform

int main(int argc, char **argv)
{
    char err[512], *text;
    int unreached = 0, expected;
    assert(argc == 4);
    mode = atoi(argv[1]); expected = atoi(argv[3]); err[0] = '\0';
    text = ln_get_text_reach(argv[2], NULL, 4096, &unreached, err, sizeof err);
    if (attempts != expected)
        fprintf(stderr, "HTTP-ATTEMPTS|mode=%d|actual=%d|expected=%d|detail=%s\n", mode, attempts, expected, err);
    assert(attempts == expected);
    if (mode == 1 || mode == 2) {
        assert(text && !strcmp(text, "RECOVERED\n") && !unreached);
    } else {
        assert(!text);
        if (mode == 3 || mode == 6 || mode == 7) {
            assert(unreached && strstr(err, "Check your connection and try again."));
            assert(strstr(err, mode == 7 ? "connect" : "resolved"));
        } else if (mode == 4 || mode == 5) {
            assert(unreached);
        } else {
            assert(!unreached && strstr(err, "HTTP 503"));
        }
    }
    printf("HTTP-RETRY|PASS|mode=%d|attempts=%d|reached=%d|status=%s\n",
           mode, attempts, !unreached, text ? "recovered" : err);
    free(text);
    return 0;
}
