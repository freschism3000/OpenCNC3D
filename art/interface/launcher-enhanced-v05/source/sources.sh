# The launcher's source list, read by BOTH build scripts.
#
# ONE LIST, TWO PLATFORMS. Both platform builds read this shared list so a
# source file cannot be added to one build and omitted from the other.
#
# lnet.c is the one file that is genuinely different per platform, and the
# difference lives INSIDE it (libcurl on macOS, WinINet on Windows) rather than
# in this list. That keeps the list honest: every platform compiles every file.

#
# lmedia_manifest.c is GENERATED and committed: the movies and music, file by file,
# with where each one sits inside the freeware disc images and its SHA-256. Making it
# needs the network, and the list it holds is a decision, so it is source rather than
# a build product.

LAUNCHER_SOURCES="launcher.c lui.c lskin.c lsignal.c llogo.c lpath.c lcmdline.c lnet.c ljson.c lzip.c lupdate.c \
                  lmedia.c lmedia_manifest.c lmodule.c lsetup_cli.c \
                  ../game/dosbar.c ../menu/dosmenu.c ../video/pngwrite.c"
