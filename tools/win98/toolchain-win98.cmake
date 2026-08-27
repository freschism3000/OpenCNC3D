# CMake toolchain for the Windows 98 target.
#
# It is a copy of brain/vanilla/cmake/i686-mingw-w64-toolchain.cmake with the Win98 C
# runtime recipe added, and it lives HERE rather than there on purpose: brain/vanilla is
# a checkout of someone else's GPL project and the repository conventions keeps our changes out of it.
#
# The recipe itself is explained in tools/win98/build.sh and docs/win98-port.md section 2.
# The short version: this mingw is UCRT-only and its libmsvcrt.a is a relabelled UCRT
# import library, so the default link produces a DLL importing api-ms-win-crt-*.dll, which
# Windows 98 does not have. Naming every library ourselves, ending in the genuine
# libmsvcrt-os.a, produces one that imports msvcrt.dll instead.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_RC_FLAGS -DGCC_WINDRES)

set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32 $ENV{CMAKE_FIND_ROOT_PATH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_LIBRARY_PREFIXES "lib" "")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll" ".dll.a" ".lib" ".a")

# Pentium II baseline, matching tools/win98/build.sh. The test box is a Pentium III but
# requiring SSE would be a change to the Tier 1 hardware contract, not a flag.
set(W98_ARCH "-march=pentium2 -mtune=pentium3")
# Win98 refuses a PE that claims a newer OS than itself.
set(W98_PEVER "-Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0")

set(CMAKE_C_FLAGS_INIT   "${W98_ARCH} -D_WIN32_WINNT=0x0400 -DWINVER=0x0400")
set(CMAKE_CXX_FLAGS_INIT "${W98_ARCH} -D_WIN32_WINNT=0x0400 -DWINVER=0x0400")

# CMAKE_*_STANDARD_LIBRARIES lands at the END of the link line, which is exactly where the
# library group has to be. The group is required because libgcc_eh pulls in winpthreads,
# which calls back into the CRT for snprintf, _setjmp3, longjmp and _endthreadex.
# shell32 and oleaut32 are here because -nodefaultlibs dropped mingw's default set and
# the brain needs four symbols from them: SHGetSpecialFolderPathW in PathsClass::User_Path
# (common/paths_win.cpp:100) and the SafeArray trio in the map EDITOR interface
# (tiberiandawn/dllinterfaceeditor.cpp), which we never call but which is compiled in.
#
# WATCH THE WIDE ONE. SHGetSpecialFolderPathW is a -W entry point, and Windows 9x stubs
# most wide functions to failure rather than implementing them. Linking against it is fine
# as long as Win98's shell32 EXPORTS it, because an unresolved import stops the DLL loading
# at all. Whether it does is tested on the box, not assumed here.
set(W98_LIBS "-Wl,--start-group -l:libstdc++.a -lmingw32 -lmingwex -lmsvcrt-os -lgcc -lgcc_eh -l:libwinpthread.a -Wl,--end-group -lkernel32 -luser32 -lgdi32 -lshell32 -lole32 -loleaut32 -ladvapi32 -lwinmm")
set(CMAKE_C_STANDARD_LIBRARIES   "${W98_LIBS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "${W98_LIBS}" CACHE STRING "" FORCE)

# -nodefaultlibs, not -nostdlib: the DLL still needs dllcrt2.o for its entry point.
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-nodefaultlibs ${W98_PEVER}")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-nodefaultlibs ${W98_PEVER}")
