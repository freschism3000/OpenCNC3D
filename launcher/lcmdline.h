/*
 * lcmdline.h -- the game's command line: what the launcher hands on after "--".
 *
 * Everything after a bare "--" on the launcher's own command line belongs to the
 * game. Carrying it across has gone wrong without a word in two ways, and both are
 * answered here:
 *
 *  1. A list that fills up and simply stops copying. A run that forwarded 17
 *     arguments into a list of 16 handed the game "--script" without its file, and
 *     the game printed its usage and exited 2 with nothing from the launcher to say
 *     an argument had been left behind. LC_GAME_ARGS is now the game's own limit,
 *     and the launcher refuses, naming what it would drop, rather than dropping.
 *  2. On Windows, CreateProcess takes ONE string, not a list, so every argument is
 *     written into it and the game's C runtime splits it again. Written bare, an
 *     argument holding a space (a save folder under "Program Files") arrives as two,
 *     and a fixed buffer that runs out cuts the last one in half.
 *
 * No SDL and no platform calls, so the Windows half can be tested on any machine.
 */

#ifndef LCMDLINE_H
#define LCMDLINE_H

#include <stddef.h>

/* HOW MANY ARGUMENTS THE LAUNCHER CARRIES TO THE GAME, the three it adds itself
 * for the map editor included. The game's own shell keeps the arguments it does
 * not handle itself in a list that stops at 250, so a longer list here would only
 * move the silent cut one program further down. */
#define LC_GAME_ARGS 256
#define LC_EDITOR_ARGS 3

/* Append one argument to a Windows command line in out[0..cap), after a space
 * unless it is the first, quoted exactly as far as the C runtime's parser (and
 * CommandLineToArgvW) needs to give it back byte for byte. *len is the length
 * so far and is advanced. Returns 1, or 0 when it does not fit, leaving out and
 * *len as they were. */
int lc_win_append(char *out, size_t cap, size_t *len, const char *arg);

#endif /* LCMDLINE_H */
