cmake_minimum_required(VERSION 3.25)

# THE XL BRAIN -- the Enhanced-only engine fork (docs/design-xl-brain.md).
#
# This tree is a REAL SOURCE COPY of brain/vanilla at tag brain-xl-forkpoint,
# reduced to the one thing it exists to build: TiberianDawnXL, the widened-
# coordinate remaster DLL for big maps and high player counts. Everything the
# vanilla root offered as an option is pinned here, because the tree IS the fork:
# no Red Alert, no game executables, no SDL/OpenAL/network front-end, no tools,
# no tests. brain/xl/common must stay byte-identical to brain/vanilla/common
# (tools/check-xl-common.sh), so this root supplies the same variables that
# common/CMakeLists.txt reads from the vanilla root, with the non-remaster ones
# left OFF so only `common` + `commonr` are built from it.
#
# Divergence from the vanilla root is logged in DIVERGENCE.md like every other
# file in this fork.

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
include(FeatureSummary)
include(CheckCXXCompilerFlag)

if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo" FORCE)
endif()

project(TiberianDawnXLBrain C CXX)

# The one target this tree builds. These are set()s rather than option()s on
# purpose: configuring the XL tree any other way is not a supported build.
set(BUILD_REMASTERTD ON)
set(BUILD_REMASTERRA OFF)
set(BUILD_VANILLATD OFF)
set(BUILD_VANILLARA OFF)
set(BUILD_TESTS OFF)
set(NETWORKING OFF)
set(SDL1 OFF)
set(SDL2 OFF)
set(OPENAL OFF)
set(DSOUND OFF)
set(DDRAW OFF)
set(MAP_EDITORTD OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(Compilers)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_FIND_FRAMEWORK "LAST")

add_definitions(-DTRUE_FALSE_DEFINED)
add_definitions(-DENGLISH)

if(WIN32)
    add_definitions(-DWIN32 -D_WINDOWS -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -DNOMINMAX)
    set(COMMON_LIBS winmm)
endif()

set(VANILLA_DEFS "")
set(VANILLA_LIBS "")

set(REMASTER_DEFS _USRDLL REMASTER_BUILD)
set(REMASTER_LIBS "")

add_subdirectory(common)
add_subdirectory(tiberiandawn)

feature_summary(WHAT ENABLED_FEATURES DESCRIPTION "Enabled features:")
