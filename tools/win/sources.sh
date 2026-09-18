# The source lists for the Windows build, in one place.
#
# READ THIS BEFORE ADDING A FILE.
#
# .github/workflows/build.yml explains why CI calls build.sh rather than spelling out its
# own compile line: two recipes for one binary drift, and a drifted recipe fails on every
# push. This file is a second recipe, so it is exactly the thing that comment warns
# about.
#
# It exists anyway, because a cross-compile genuinely needs different tools, flags and
# libraries, and pretending otherwise would mean bending the Mac script into knots. The
# drift is instead caught rather than avoided: tools/win/check-sources.sh compares these
# lists against what app/build.sh and game/build.sh actually compile, and build-win.sh
# runs it FIRST and refuses to build if they disagree. Add a source to the Mac build and
# the Windows build fails on the next run with the filename in the message, which is the
# behaviour the twenty red commits deserved.

# Portable C89: the DOS sidebar rasteriser, the 640x480 HUD, the Options dialog. No GL,
# no SDL. game/build.sh compiles these separately to keep them that way, and the whole
# point of that discipline is this build.
WIN_GAME_C="game/dosbar.c game/hud640.c game/dosopt.c game/dossave.c"

# The 1995 menu shell and the movie player.
# mpbrowse.c is the internet game list as the screen sees it: it asks the list service for
# open games and publishes this machine's own, both on a worker so the frame never waits.
WIN_MENU_C="menu/dosmenu.c menu/dosops.c menu/doslobby.c menu/dosmp.c \
            menu/dosmenu_shell.c menu/mpbrowse.c"
WIN_VIDEO_C="video/vqaplay.c video/movieplay.c video/moviesnd.c video/pngwrite.c"

# The audio engine. Every file here is portable C except audio_sdl.c, which owns the
# device. On Windows SDL2 supplies WASAPI/DirectSound underneath, so the same file works
# unchanged; a Win98 backend would replace this one file and nothing else in the list.
WIN_AUDIO_C="audio/sosadpcm.c audio/wsadpcm.c audio/wsaud.c audio/mixfile.c \
             audio/sndbank.c audio/mixer.c audio/sfxtable.c audio/sfxname.c \
             audio/cncaudio.c audio/wavio.c audio/audiotap.c audio/audioboot.c \
             audio/audio_sdl.c"

# The app's own C. campaign.c is the score/campaign screen; logo3d.c is the spinning
# faction emblem it draws on top of that screen, and campaign.o calls straight into it
# (logo3d_open/_close/_draw), so this is not optional decoration -- leaving it out is a
# link error, not a missing feature. It is portable C: the only platform-specific line in
# it is the <OpenGL/gl.h> vs <GL/gl.h> #ifdef every other file here already carries.
WIN_APP_C="app/campaign.c app/logo3d.c"

# The lockstep networking. lockstep.c is portable C89 with no I/O in it at all and
# net_udp.c is the one file that knows what a socket is, which is the same split the
# renderer keeps between scene assembly and the graphics API. Windows needs -lws2_32 for
# the second one; the link line below carries it.
#
# netbeacon.c is the LAN announcement and the browser's side of it. It carries its own
# <windows.h> branch for the millisecond clock, so it was written to cross-compile and is
# not being made to.
#
# The gate and tool binaries beside them (gate_lockstep.c, gate_netloop.c, gate_tunnel.c,
# gate_beacon.c,
# gate_lobby.c, netcheck.c) are NOT here on purpose: they are standalone, exactly like
# gate_optlayout.c and playvqa.c, and tools/win/check-sources.sh excludes them by name for
# that reason.
# roomcode.c is the room code: a relayed host's whole address, six characters of
# Crockford base32 and the random draw behind it. Part of the game rather than a tool,
# because both ends of a relayed match need it -- the host to publish one, the joiner to
# read one. Its GATE (gate_roomcode.c) is standalone and excluded by check-sources.sh.
WIN_NET_C="net/lockstep.c net/net_udp.c net/netmatch.c net/netbeacon.c net/roomcode.c"

# THE HTTP CLIENT, SHARED WITH THE LAUNCHER RATHER THAN COPIED. The game needs it for one
# thing only, the internet game list, and there is no reason for two clients in one tree
# that would then disagree about timeouts and redirects. Both files include nothing of the
# launcher's beyond their own headers, which is what makes sharing them free. Each platform
# brings its own transport underneath: a system DLL on one, a library in the SDK on the
# other, and neither is something a player has to install.
WIN_HTTP_C="launcher/lnet.c launcher/ljson.c"

# The C++ half.
WIN_EYES_CPP="game/cnc_eyes.cpp"
WIN_APP_CPP="app/cnc3d.cpp"
