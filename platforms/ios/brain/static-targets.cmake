# Inject with CMAKE_PROJECT_INCLUDE while configuring the real brain/vanilla root.
# Its own CMAKE_SOURCE_DIR and all simulation sources remain unchanged. This hook
# changes only TiberianDawn's library type and its interface compilation unit.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "The static brain hook is exclusively for iOS builds")
endif()
get_filename_component(OPENCNC3D_IOS_BRAIN_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(OPENCNC3D_IOS_ADAPTER "${CMAKE_BINARY_DIR}/ios-static-brain.cpp")
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${OPENCNC3D_IOS_BRAIN_DIR}/generate_adapter.py"
    "${CMAKE_SOURCE_DIR}/tiberiandawn/dllinterface.cpp"
    "${OPENCNC3D_IOS_ADAPTER}"
    COMMAND_ERROR_IS_FATAL ANY)

# A scoped build hook rather than a patch to the authoritative simulation tree.
function(add_library target)
    if(target STREQUAL "TiberianDawn")
        set(sources ${ARGN})
        list(REMOVE_ITEM sources SHARED dllinterface.cpp)
        # The shared desktop DLL carries an unused standalone main. A static
        # archive must not compete with SDL/UIKit's application entry point.
        set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/startup.cpp" APPEND PROPERTY
            COMPILE_DEFINITIONS main=opencnc3d_ios_td_legacy_main)
        _add_library(${target} STATIC ${sources} "${OPENCNC3D_IOS_ADAPTER}")
        target_include_directories(${target} PUBLIC "${OPENCNC3D_IOS_BRAIN_DIR}")
        add_executable(opencnc3d-ios-brain-link-check
            "${OPENCNC3D_IOS_BRAIN_DIR}/link_check.cpp")
        set_target_properties(opencnc3d-ios-brain-link-check PROPERTIES
            MACOSX_BUNDLE OFF)
        target_link_libraries(opencnc3d-ios-brain-link-check PRIVATE ${target})
    else()
        _add_library(${target} ${ARGN})
    endif()
    if(target STREQUAL "TiberianDawn" OR target STREQUAL "commonr" OR target STREQUAL "common")
        target_compile_options(${target} PRIVATE -include "${OPENCNC3D_IOS_BRAIN_DIR}/sdk_compat.h")
    endif()
endfunction()
