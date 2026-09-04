/*
 * t1_script.h -- scripted input, so this build can be tested without a person at it.
 *
 * WHY THIS EXISTS, stated plainly because it is a test harness in the shipping binary.
 *
 * A fullscreen Glide app has no window and therefore no WM_ messages: it reads the mouse
 * with GetCursorPos and the buttons with GetAsyncKeyState. On this machine those two do
 * NOT behave the same way under synthetic input. Measured, 700 frames, three different
 * injection methods (AutoIt MouseDown/MouseUp, AutoIt MouseClick, and mouse_event called
 * straight through user32): the pointer moved exactly where it was told every time, and
 * the button state was seen ZERO times. The keyboard is fine, 17 frames of a scripted D.
 * A real hand works: the project owner has selected units, dragged a band and given move orders.
 *
 * So the OS input layer cannot be driven from a script on this box, and everything
 * downstream of it -- the hit test, the build clicks, the radar jump, the selection, the
 * orders -- would be untestable without a person in the chair. This channel replaces the
 * OS read with a file, at exactly the point the OS read happens, so every line of game
 * logic below it is the SAME code the hand drives. It tests everything except the layer
 * that was already proven by hand, which is the right seam.
 *
 * It is not a cheat and not a demo: it is off unless a script file is named, and it is
 * how the overnight regression runs happen.
 *
 * FORMAT, one command per line, blank lines and ; comments ignored:
 *
 *     <frame> move <x> <y>       put the pointer at screen x,y
 *     <frame> lb <0|1>           left button state from here on
 *     <frame> rb <0|1>           right button state
 *     <frame> key <NAME> <n>     hold a key for n frames (D, LEFT, RIGHT, UP, DOWN,
 *                                PGUP, PGDN, CTRL, ESC)
 *     <frame> cam <x> <z>         put the camera on that cell, exactly
 *     <frame> over <0|1>         force the mission-end banner (1 won, 0 lost). A TEST
 *                                affordance: a scripted run cannot play a mission to its
 *                                real end, and a banner nobody has ever seen draw is not
 *                                a feature, it is a hope.
 *     <frame> place              commit the pending building at the first legal cell
 *     <frame> shot <name.bmp>    read the front buffer back and write it to disk
 *     <frame> quit               stop the run
 */

#ifndef T1_SCRIPT_H
#define T1_SCRIPT_H

#define T1S_MAX 256

typedef struct
{
    int  frame;
    char cmd[8];
    int  a, b;
    char s[28];
} T1_ScriptCmd;

typedef struct
{
    T1_ScriptCmd cmd[T1S_MAX];
    int  n, next;
    int  mx, my, lb, rb;
    int  camx, camz, camset;   /* a one-shot camera placement */
    int  over, overset;
    int  hold[256];              /* frames of key press remaining, by virtual key code */
    char shot[28];               /* set for one frame when a shot is due */
    int  place;                  /* set for one frame by `place`: commit the pending
                                  * building at the FIRST LEGAL ORIGIN the engine will
                                  * accept. A test affordance, like `over`: a script
                                  * cannot know which cell is legal, and a placement path
                                  * that has never placed anything is not tested. */
    int  quit;
    int  fired;                  /* how many commands have been consumed */
} T1_Script;

/* 0 if the file is not there, which is the normal case. */
int  t1_script_load(T1_Script *s, const char *path);

/* Consume everything due at this frame. Call once per frame, before the input is read. */
void t1_script_step(T1_Script *s, int frame);

/* Is this virtual key down, as far as the script is concerned? */
int  t1_script_key(const T1_Script *s, int vk);

#endif /* T1_SCRIPT_H */
