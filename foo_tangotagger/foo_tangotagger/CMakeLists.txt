# The component itself.
#
# Windows: foo_tangotagger.dll, the name foo_tangotagger.cpp asserts with
# VALIDATE_COMPONENT_FILENAME.
#
# macOS: a loadable bundle, foo_tangotagger.component. The two platforms'
# payloads share one .fb2k-component archive and are told apart by where they
# sit in it:
#
#     foo_tangotagger.dll            32 bit Windows
#     x64/foo_tangotagger.dll        64 bit Windows
#     mac/foo_tangotagger.component  macOS

# The SDK release is pinned in scripts\get_sdk.ps1; the about box reads it
# from there.
set(_get_sdk "${CMAKE_SOURCE_DIR}/scripts/get_sdk.ps1")
file(READ "${_get_sdk}" _get_sdk_text)
if(_get_sdk_text MATCHES "Version[ \t]*=[ \t]*'([0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9])'")
    set(FOO_TANGOTAGGER_SDK_VERSION "${CMAKE_MATCH_1}")
else()
    message(WARNING "Could not read the SDK release out of ${_get_sdk}.")
    set(FOO_TANGOTAGGER_SDK_VERSION "unknown")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_get_sdk}")

configure_file(version.h.in "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

# --- sources ---------------------------------------------------------------
set(FOO_TANGOTAGGER_SOURCES
    contextmenu.cpp
    file_info_filter_lyrics.cpp
    foo_tangotagger.cpp
    lyrics_panel_content.cpp
    lyrics_rows.cpp

    file_info_filter_lyrics.h
    guid.h
    lyrics_field.h
    lyrics_panel_content.h
    lyrics_rows.h
    stdafx.h
    tangotagger_ui.h
    version.h.in

    "${CMAKE_CURRENT_BINARY_DIR}/version.h")

if(WIN32)
    list(APPEND FOO_TANGOTAGGER_SOURCES
        lyrics_dialog.cpp
        lyrics_dialog.h
        lyrics_panel.cpp
        foo_tangotagger.rc
        resource.h)
else()
    list(APPEND FOO_TANGOTAGGER_SOURCES
        mac/lyrics_panel_mac.mm
        mac/lyrics_window_mac.mm
        mac/foobar2000-mac-class-suffix.h

        # foobar2000_get_interface lives here; see cmake/fb2k_sdk.cmake for why
        # it is compiled into the component rather than linked out of the SDK.
        "${FB2K_COMPONENT_CLIENT_SOURCE}")
endif()

add_library(foo_tangotagger MODULE ${FOO_TANGOTAGGER_SOURCES})

if(WIN32)
    set_target_properties(foo_tangotagger PROPERTIES
        OUTPUT_NAME "foo_tangotagger"
        PREFIX ""
        SUFFIX ".dll")
else()
    set_target_properties(foo_tangotagger PROPERTIES
        OUTPUT_NAME "foo_tangotagger"
        BUNDLE TRUE
        BUNDLE_EXTENSION "component"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Tango Tagger"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.foobar2000.component.foo-tangotagger"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "(c) 2026 Nick Shaforostov")
    # The SDK's macOS helpers #error without a class suffix.
    target_include_directories(foo_tangotagger PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/mac")
endif()

target_link_libraries(foo_tangotagger PRIVATE fb2k::sdk tangotagger_core)
target_include_directories(foo_tangotagger PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if(WIN32)
    target_compile_definitions(foo_tangotagger PRIVATE NOMINMAX)
endif()

# The about box says which data this build carries.
if(FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY)
    target_compile_definitions(foo_tangotagger PRIVATE FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY=1)
else()
    target_compile_definitions(foo_tangotagger PRIVATE FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY=0)
endif()

if(APPLE AND FOO_TANGOTAGGER_LTO)
    # Keep LTO temporaries so dsymutil can find the debug info; see the same
    # block in foo_bpm/foo_rubato/CMakeLists.txt.
    set(_lto_objects "${CMAKE_CURRENT_BINARY_DIR}/lto")
    file(MAKE_DIRECTORY "${_lto_objects}")
    target_link_options(foo_tangotagger PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:LINKER:-object_path_lto,${_lto_objects}>")
endif()

target_precompile_headers(foo_tangotagger PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/stdafx.h>")

if(MSVC)
    target_compile_options(foo_tangotagger PRIVATE /W4 /wd4100)
else()
    target_compile_options(foo_tangotagger PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

set_target_properties(foo_tangotagger PROPERTIES FOLDER "foo_tangotagger")
