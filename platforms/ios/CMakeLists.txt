cmake_minimum_required(VERSION 3.24)
project(OpenCNC3DiOS LANGUAGES C CXX OBJCXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  message(FATAL_ERROR "Use the iOS build.sh entry point; no desktop install is performed.")
endif()
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_OBJCXX_STANDARD 14)
set(OPENCNC3D_SOURCE_ROOT "$ENV{HOME}/CNC3D" CACHE PATH "Authoritative current-main source checkout")
if(NOT EXISTS "${OPENCNC3D_SOURCE_ROOT}/game/cnc_eyes.cpp")
  message(FATAL_ERROR "Missing current-main source: ${OPENCNC3D_SOURCE_ROOT}")
endif()
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
  set(IOS_SDK iphonesimulator)
  set(BRAIN_VARIANT simulator)
  set(IOS_PLATFORM_NAME iPhoneSimulator)
else()
  set(IOS_SDK iphoneos)
  set(BRAIN_VARIANT device)
  set(IOS_PLATFORM_NAME iPhoneOS)
endif()
set(OPENCNC3D_IOS_DEPENDENCY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/install" CACHE PATH "Isolated iOS dependency install root")
set(OPENCNC3D_IOS_BRAIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/brain" CACHE PATH "Isolated static brain build root")
set(DEPS "${OPENCNC3D_IOS_DEPENDENCY_ROOT}/${IOS_SDK}-arm64")
set(BRAIN "${OPENCNC3D_IOS_BRAIN_ROOT}/build-${BRAIN_VARIANT}/libOpenCNC3DBrain.a")
foreach(required "${DEPS}/lib/libSDL2.a" "${DEPS}/lib/libGL4ES.a" "${BRAIN}")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Build dependencies first: missing ${required}")
  endif()
endforeach()
set(R "${OPENCNC3D_SOURCE_ROOT}")
set(CORE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ios_game_bridge.cpp" "${R}/game/dosbar.c" "${R}/game/hud640.c" "${R}/game/dosopt.c" "${R}/game/dossave.c"
  "${R}/menu/dosmenu.c" "${R}/menu/dosops.c" "${R}/menu/doslobby.c" "${R}/menu/dosmp.c" "${R}/menu/enhanced_menu.cpp"
  "${R}/video/pngwrite.c" "${R}/app/logo3d.c"
  "${R}/net/lockstep.c" "${R}/net/net_udp.c" "${R}/net/netmatch.c" "${R}/net/netbeacon.c" "${R}/net/roomcode.c")
foreach(audio sosadpcm wsadpcm wsaud mixfile sndbank mixer sfxtable sfxname cncaudio wavio audiotap audioboot audio_sdl)
  list(APPEND CORE "${R}/audio/${audio}.c")
endforeach()
add_library(ios_game OBJECT ${CORE})
target_compile_definitions(ios_game PRIVATE OPENCNC3D_NO_MAIN=1 OPENCNC3D_IOS=1 GL_SILENCE_DEPRECATION=1 __int64=long\ long
  OPENCNC3D_GAME_SOURCE="${R}/game/cnc_eyes.cpp")
target_compile_options(ios_game PRIVATE -fms-extensions -fdeclspec
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/brain/sdk_compat.h"
  "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_compat.h")
set(INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/compat" "${DEPS}/include/SDL2" "${DEPS}/include/gl4es"
  "${R}/game" "${R}/menu" "${R}/video" "${R}/audio" "${R}/brain/vanilla/tiberiandawn")
target_include_directories(ios_game PRIVATE ${INCLUDES})
add_executable(OpenCNC3DiOS MACOSX_BUNDLE src/main.mm src/ios_graphics.mm src/ios_events.cpp src/ios_controls.mm $<TARGET_OBJECTS:ios_game>)
target_compile_options(OpenCNC3DiOS PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_include_directories(OpenCNC3DiOS PRIVATE ${INCLUDES} src)
target_link_libraries(OpenCNC3DiOS PRIVATE "${BRAIN}" "${DEPS}/lib/libSDL2main.a" "${DEPS}/lib/libSDL2.a" "${DEPS}/lib/libGL4ES.a" z c++
  "-framework UIKit" "-framework Foundation" "-framework CoreGraphics" "-framework QuartzCore" "-framework OpenGLES"
  "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreVideo" "-framework CoreMotion"
  "-weak_framework GameController" "-weak_framework CoreHaptics")
set_target_properties(OpenCNC3DiOS PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "org.opencnc3d.ios.development"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
  XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES")
if(CMAKE_GENERATOR STREQUAL "Xcode")
  set_target_properties(OpenCNC3DiOS PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")
endif()
set(OPENCNC3D_DEVELOPMENT_TEAM "" CACHE STRING "Apple team for an explicitly signed device build")
if(OPENCNC3D_DEVELOPMENT_TEAM)
  set_target_properties(OpenCNC3DiOS PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${OPENCNC3D_DEVELOPMENT_TEAM}")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(CMAKE_GENERATOR STREQUAL "Xcode")
  add_custom_command(TARGET OpenCNC3DiOS POST_BUILD
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/resources/stage-xcode.sh" "${R}/playable" VERBATIM)
else()
  add_custom_command(TARGET OpenCNC3DiOS POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/resources/stage.py" --source-install "${R}/playable" --output "$<TARGET_BUNDLE_DIR:OpenCNC3DiOS>/GameData"
    VERBATIM)
endif()
