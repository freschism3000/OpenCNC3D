/* ============================================================================
 *  C&C3D.app's main executable, on macOS. A NATIVE BINARY, and that is the whole
 *  point of it: this file replaces a bash script that did the same job.
 *
 *  WHY IT STOPPED BEING A SCRIPT (6 Sep 2026, after two failed LAN tests).
 *  macOS 15 asks the player once whether an app may talk to the local network,
 *  and refuses its packets with "No route to host" until they say yes. The
 *  question is only ever asked of something the system can NAME: an app bundle
 *  with a code signature. A bundle whose CFBundleExecutable is a shell script
 *  cannot carry one -- ad-hoc signing such a bundle makes macOS refuse to open it
 *  outright ("not supported on this version of macOS") -- so the game was never
 *  named, never asked, and every join failed while the host's advert still
 *  arrived. Measured on two Macs: the same binary sending the same packet to the
 *  same address succeeded from a shell and failed from the app.
 *
 *  Everything below is the script's own logic, in C, comments included, because
 *  App Translocation is still the thing standing between a downloaded zip and
 *  "Could not find the game."
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#include <sys/wait.h>
#include <errno.h>

static int is_file(const char* p) { struct stat s; return p && *p && stat(p, &s) == 0 && S_ISREG(s.st_mode); }
static int is_exec(const char* p) { struct stat s; return p && *p && stat(p, &s) == 0 && (s.st_mode & S_IXUSR); }

/* <name> in folder d, or, while an update there is unfinished, the copy of it that
   update stepped aside. An update replaces a file by two renames, the old one to
   <name>.old (.old2 to .old9 while the name before it is taken) and then the new one onto
   the name, and a kill between the two leaves the name empty. Refusing the folder then
   would mean no launcher ever starts in it to finish the update. "Unfinished" is the
   journal beside the install record, the same pair the launcher itself looks for. */
static int has_named(const char* d, const char* name, int exec)
{
    char p[2048];
    int n;
    snprintf(p, sizeof p, "%s/%s", d, name);
    if (exec ? is_exec(p) : is_file(p)) return 1;
    snprintf(p, sizeof p, "%s/cnc3d-update.journal", d); if (!is_file(p)) return 0;
    snprintf(p, sizeof p, "%s/cnc3d-install.txt", d);    if (!is_file(p)) return 0;
    for (n = 1; n <= 9; n++) {
        if (n == 1) snprintf(p, sizeof p, "%s/%s.old", d, name);
        else        snprintf(p, sizeof p, "%s/%s.old%d", d, name, n);
        if (exec ? is_exec(p) : is_file(p)) return 1;
    }
    return 0;
}

/* A folder is "the game" only if it carries the binary AND two things only the real
   package has. Testing for cnc3d alone would let a stray copy of the binary win. */
static int is_game_dir(const char* d)
{
    if (!d || !*d) return 0;
    return has_named(d, "cnc3d", 1) && has_named(d, "TiberianDawn.dylib", 0)
        && has_named(d, "dosmenu.pack", 0);
}

/* osascript, for the two moments a player has to be told something. Quoting is
   done by passing the text as an argument rather than pasting it into the script. */
static void alert(const char* msg)
{
    char cmd[4096];
    snprintf(cmd, sizeof cmd,
             "/usr/bin/osascript -e 'on run argv' "
             "-e 'display alert \"C&C3D\" message (item 1 of argv) as critical' "
             "-e 'end run' -- %s >/dev/null 2>&1", "\"$MSG\"");
    setenv("MSG", msg, 1);
    (void)system(cmd);
}

static int ask_choose(const char* msg)
{
    char cmd[4096];
    FILE* f;
    char out[64] = {0};
    setenv("MSG", msg, 1);
    snprintf(cmd, sizeof cmd,
             "/usr/bin/osascript -e 'on run argv' "
             "-e 'display alert \"C&C3D\" message (item 1 of argv) buttons {\"Quit\", \"Choose Folder\"} "
             "default button \"Choose Folder\" as critical' "
             "-e 'return button returned of result' -e 'end run' -- \"$MSG\" 2>/dev/null");
    f = popen(cmd, "r");
    if (!f) return 0;
    if (!fgets(out, sizeof out, f)) out[0] = '\0';
    pclose(f);
    return strncmp(out, "Choose Folder", 13) == 0;
}

static int pick_folder(char* out, size_t outmax)
{
    FILE* f = popen("/usr/bin/osascript -e 'try' "
                    "-e 'POSIX path of (choose folder with prompt \"Select the C&C3D folder\")' "
                    "-e 'end try' 2>/dev/null", "r");
    size_t n;
    if (!f) return 0;
    out[0] = '\0';
    if (!fgets(out, (int)outmax, f)) { pclose(f); return 0; }
    pclose(f);
    n = strlen(out);
    while (n && (out[n-1] == '\n' || out[n-1] == '/')) out[--n] = '\0';
    return out[0] != '\0';
}

int main(void)
{
    char exe[2048]; uint32_t sz = sizeof exe;
    char bundle[2048], here[2048], dir[2048] = {0};
    const char* home = getenv("HOME");
    static const char* const NEED[] = { "dosmenu.pack", "cameos.pack", "dossidebar.pack",
                                        "dosinfantry.pack", "TiberianDawn.dylib" };
    if (_NSGetExecutablePath(exe, &sz) != 0) return 1;

    /* .../C&C3D.app/Contents/MacOS/cnc3d-launch  ->  the folder holding the .app */
    {
        char tmp[2048]; snprintf(tmp, sizeof tmp, "%s", exe);
        snprintf(bundle, sizeof bundle, "%s", dirname(tmp));      /* MacOS   */
        snprintf(tmp, sizeof tmp, "%s", bundle);
        snprintf(bundle, sizeof bundle, "%s", dirname(tmp));      /* Contents*/
        snprintf(tmp, sizeof tmp, "%s", bundle);
        snprintf(bundle, sizeof bundle, "%s", dirname(tmp));      /* .app    */
        snprintf(tmp, sizeof tmp, "%s", bundle);
        snprintf(here, sizeof here, "%s", dirname(tmp));          /* beside it */
    }

    if (is_game_dir(here)) {
        snprintf(dir, sizeof dir, "%s", here);      /* normal: shipped in place */
    } else {
        /* Translocated, or copied out on its own. Look where people actually unzip
           things, and one level in as well, because the zip expands to a build-named
           folder. $HOME/CNC3D/playable stays on the list for development use. */
        char bases[8][2048]; int nb = 0, i;
        if (home) {
            snprintf(bases[nb++], 2048, "%s/Downloads", home);
            snprintf(bases[nb++], 2048, "%s/Desktop", home);
            snprintf(bases[nb++], 2048, "%s/Applications", home);
        }
        snprintf(bases[nb++], 2048, "/Applications");
        if (home) {
            snprintf(bases[nb++], 2048, "%s", home);
            snprintf(bases[nb++], 2048, "%s/Documents", home);
            snprintf(bases[nb++], 2048, "%s/CNC3D/playable", home);
        }
        for (i = 0; i < nb && !dir[0]; i++) {
            DIR* d;
            struct dirent* e;
            if (is_game_dir(bases[i])) { snprintf(dir, sizeof dir, "%s", bases[i]); break; }
            d = opendir(bases[i]);
            if (!d) continue;
            while ((e = readdir(d)) != NULL) {
                char cand[2048];
                if (e->d_name[0] == '.') continue;
                if (strncasecmp(e->d_name, "cnc3d", 5) != 0 && strstr(e->d_name, "OpenCNC 3D") == NULL)
                    continue;
                snprintf(cand, sizeof cand, "%s/%s", bases[i], e->d_name);
                if (is_game_dir(cand)) { snprintf(dir, sizeof dir, "%s", cand); break; }
            }
            closedir(d);
        }
    }

    if (!dir[0]) {
        const char* msg = strstr(bundle, "/AppTranslocation/")
            ? "macOS opened C&C3D from a temporary location, so the app cannot see the rest of the game. "
              "This is because the download is not signed by Apple -- it is not a damaged download.\n\n"
              "Choose the C&C3D folder (the one holding cnc3d and the .pack files) and it will start, "
              "and stay fixed for next time."
            : "Could not find the game files next to this app.\n\n"
              "Choose the C&C3D folder (the one holding cnc3d and the .pack files) and it will start.";
        if (!ask_choose(msg)) return 1;
        if (!pick_folder(dir, sizeof dir) || !is_game_dir(dir)) {
            alert("That folder does not contain the game.\n\n"
                  "Look for the folder holding cnc3d, TiberianDawn.dylib and the .pack files. "
                  "It is the folder the download unzipped into.");
            return 1;
        }
    }

    if (chdir(dir) != 0) { alert("Could not open the game folder."); return 1; }

    for (size_t i = 0; i < sizeof NEED / sizeof NEED[0]; i++) {
        if (!has_named(".", NEED[i], 0)) {
            char m[512];
            snprintf(m, sizeof m, "Missing %s in %s.", NEED[i], dir);
            alert(m);
            return 1;
        }
    }

    /* Quarantine is what caused the translocation, and it also makes macOS refuse the
       unsigned binaries one at a time. Clearing it on the folder we are about to run is
       safe and stops this recurring on every launch. Quiet on failure. */
    {
        char cmd[2200];
        setenv("GDIR", dir, 1);
        /* DETACHED, never waited on: on a folder the size of this one macOS may first
           ask the player for permission to read it, and the game must not sit dark
           behind that dialog. It matters only for a fresh download, and by the time it
           matters again the sweep has long finished. */
        snprintf(cmd, sizeof cmd, "/usr/bin/xattr -dr com.apple.quarantine \"$GDIR\" >/dev/null 2>&1 &");
        (void)system(cmd);
    }

    /* NO ARGUMENTS BEYOND THE WINDOW SIZE, which is not arbitrary and is not the
       launcher's to know: the sidebar magnifies by whole numbers only, so the height has
       to be a multiple of 480. Everything else is the binary's own default, so the
       player's choices in the Visuals screen survive instead of being overridden.
       THE LAUNCHER FIRST, the game behind it; a missing launcher must never be a Mac
       that cannot play.

       A CHILD, NOT AN exec, AND THAT IS THE WHOLE POINT OF THIS PROGRAM.
       macOS decides what an app may do -- the local network above all -- by asking about
       the APP, and it can only ask about a bundle it can name and verify. exec replaces
       this process with a binary that lives OUTSIDE the bundle, and from that moment the
       system sees no app at all: it never asks, and it refuses every packet to the LAN
       with "No route to host". Measured on two Macs over a day. Staying alive as the
       parent keeps the game attributed to C&C3D.app, so the question is asked once and
       the player's answer sticks. The cost is one idle process for the session. */
    /* EXCEPT WHILE AN UPDATE IS UNFINISHED. cnc3d-update.journal in the folder means
       the launcher was stopped part way through an update, and only the launcher
       reads the journal and puts the folder back. Starting the game instead would
       run whatever mix of two builds the update left, and leave it that way on every
       later open. The launcher is replaced by renaming, so the one moment it is
       missing is between two renames, and then the copy it was renamed to is the
       launcher that was running the update, which can finish it. */
    {
        pid_t pid = fork();
        if (pid == 0) {
            if (is_exec("./cnc3d-launcher"))
                execl("./cnc3d-launcher", "cnc3d-launcher", "--", "--w", "1600", "--h", "960", (char*)NULL);
            if (is_file("./cnc3d-update.journal")) {
                char old[32];
                int n;
                for (n = 1; n <= 9; n++) {
                    if (n == 1) snprintf(old, sizeof old, "./cnc3d-launcher.old");
                    else        snprintf(old, sizeof old, "./cnc3d-launcher.old%d", n);
                    if (is_exec(old))
                        execl(old, "cnc3d-launcher", "--", "--w", "1600", "--h", "960", (char*)NULL);
                }
                _exit(126);
            }
            execl("./cnc3d", "cnc3d", "--w", "1600", "--h", "960", (char*)NULL);
            _exit(127);
        }
        if (pid > 0) {
            int st = 0;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) { }
            if (WIFEXITED(st) && WEXITSTATUS(st) == 126) {
                alert("An update to C&C3D did not finish, and the launcher that finishes it is "
                      "missing, so the game was not started.\n\n"
                      "Download the game again and replace this folder with it.");
                return 1;
            }
            if (WIFEXITED(st) && WEXITSTATUS(st) == 127) {
                alert("Could not start the game.");
                return 1;
            }
            return 0;
        }
    }
    alert("Could not start the game.");
    return 1;
}
