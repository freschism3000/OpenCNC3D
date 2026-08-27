/*
 * lpath.h -- where am I, and what folder is the game in.
 *
 * Two questions, one answer each, in one place because getting either wrong is
 * silent. SDL_GetBasePath is NOT the answer to the first one on macOS: inside an
 * app bundle it returns Contents/Resources, not Contents/MacOS, so a launcher
 * that used it to find itself would rename a file that does not exist and then
 * let an update write over the binary it is running out of. _NSGetExecutablePath
 * and GetModuleFileName are exact, and this file is the only thing that knows the
 * difference.
 */

#ifndef LPATH_H
#define LPATH_H

/* The running executable's full path. Returns 1 on success. */
int lp_self(char *out, int outlen);

/* Cut the last component off a path, in place. */
void lp_dirname(char *path);

#endif /* LPATH_H */
