/*
 * lnet.h -- the one HTTP client the launcher needs, on each platform's own.
 *
 * macOS links libcurl, which ships in the SDK. Windows uses WinINet, which is a
 * system DLL and needs nothing fetched, nothing bundled and no second SSL stack
 * beside the one the OS already trusts. Both are three calls; neither is a
 * dependency the player has to install.
 *
 * THE LAUNCHER USES ONE SHAPE OF REQUEST and that is deliberate: a GET, with the
 * launcher's key in a header, following redirects, failing loudly on any status
 * that is not 200. It never posts, never uploads and never sends anything about
 * the machine it is on.
 *
 * THE GAME ALSO LINKS THIS FILE, for the internet game list, and that one does
 * post: a host has to say its game is open. ln_post_text is the whole of that
 * addition. It is the same two backends and the same error sentences, so there
 * is one HTTP client here rather than a second one grown beside it.
 */

#ifndef LNET_H
#define LNET_H

/* Return 0 to abort the transfer. `total` is -1 when the server did not say. */
typedef int (*LN_Progress)(void *user, long long done, long long total);

/* Small responses, into memory. Returns a NUL-terminated buffer the caller frees,
 * or NULL with `err` filled in. Capped, because a manifest is a few hundred bytes
 * and anything claiming to be megabytes is not one. */
char *ln_get_text(const char *url, const char *key, long cap, char *err, int errlen);

/* POST a small body and read the small answer back, otherwise exactly ln_get_text:
 * a NUL-terminated buffer the caller frees, or NULL with `err` filled in.
 * `content_type` may be NULL, which means application/json.
 *
 * THE ROUTE MUST ANSWER 200 AND MUST NOT REDIRECT. Both backends turn a redirected
 * POST into a GET and drop the body on the way, so a 301 here does not fail, it
 * silently sends nothing. That is a property of HTTP redirect handling rather than
 * of either library, and it is the service's job not to redirect this route.
 *
 * A REPLY THAT FILLS THE CAP EXACTLY WAS TRUNCATED. The two backends disagree about
 * an over-long reply -- one fails the transfer, the other stops reading and hands
 * back what it has -- so the only answer that means the same thing on both is the
 * length: strlen(result) == cap is truncation, with no ambiguity, because the
 * buffer is one byte longer than the cap. The same is true of ln_get_text. */
char *ln_post_text(const char *url, const char *key, const char *body,
                   const char *content_type, long cap, char *err, int errlen);

/* Large responses, straight to a file. Returns 1 on success. The file is written
 * whole or not at all: it lands on a temporary name and is renamed on success, so
 * an interrupted download cannot be mistaken for a finished one. */
int ln_get_file(const char *url, const char *key, const char *path, LN_Progress cb,
                void *user, char *err, int errlen);

#endif /* LNET_H */
