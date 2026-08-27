/*
 * lnet.h -- the one HTTP client the launcher needs, on each platform's own.
 *
 * macOS links libcurl, which ships in the SDK. Windows uses WinINet, which is a
 * system DLL and needs nothing fetched, nothing bundled and no second SSL stack
 * beside the one the OS already trusts. Both are three calls; neither is a
 * dependency the player has to install.
 *
 * ONE SHAPE OF REQUEST IS SUPPORTED and that is deliberate: a GET, with the
 * launcher's key in a header, following redirects, failing loudly on any status
 * that is not 200. The launcher never posts, never uploads and never sends
 * anything about the machine it is on, so there is nothing else to model.
 */

#ifndef LNET_H
#define LNET_H

/* Return 0 to abort the transfer. `total` is -1 when the server did not say. */
typedef int (*LN_Progress)(void *user, long long done, long long total);

/* Small responses, into memory. Returns a NUL-terminated buffer the caller frees,
 * or NULL with `err` filled in. Capped, because a manifest is a few hundred bytes
 * and anything claiming to be megabytes is not one. */
char *ln_get_text(const char *url, const char *key, long cap, char *err, int errlen);

/* Large responses, straight to a file. Returns 1 on success. The file is written
 * whole or not at all: it lands on a temporary name and is renamed on success, so
 * an interrupted download cannot be mistaken for a finished one. */
int ln_get_file(const char *url, const char *key, const char *path, LN_Progress cb,
                void *user, char *err, int errlen);

#endif /* LNET_H */
