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
WIN_MENU_C="menu/dosmenu.c menu/dosops.c menu/doslobby.c menu/dosmenu_shell.c"
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

# The C++ half.
WIN_EYES_CPP="game/cnc_eyes.cpp"
WIN_APP_CPP="app/cnc3d.cpp"
