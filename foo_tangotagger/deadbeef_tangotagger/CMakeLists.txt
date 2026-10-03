# ---------------------------------------------------------------------------
# Tango Tagger for DeaDBeeF
#
# A project of its own rather than a subdirectory of ../CMakeLists.txt, which
# is the foobar2000 component and needs the foobar2000 SDK to configure at
# all. This needs a compiler, the LZMA SDK (fetched into ../external on the
# first configure, as for the component) and bpmcore from foo_bpm; the
# DeaDBeeF plugin headers are in include/. The matching and the data are
# ../core and the XML folders beside this repository, the same as the
# component's.
#
# Linux:
#   cmake -S deadbeef_tangotagger -B build/ddb -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ddb
#
# macOS (universal, Apple Silicon and Intel):
#   cmake -S deadbeef_tangotagger -B build/ddb-mac -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ddb-mac
#
# Windows (MSYS2 MinGW, the toolchain DeaDBeeF itself is built with):
#   cmake -S deadbeef_tangotagger -B build/ddb-mingw -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ddb-mingw
#
# Windows (Visual Studio, without windows):
#   cmake -S deadbeef_tangotagger -B build/ddb-x64 -A x64
#   cmake --build build/ddb-x64 --config Release
#
# -DTT_DDB_GTK=ON builds the results windows and the lyrics panel for
# DeaDBeeF's GTK 3 interface, and needs the GTK 3 development files
# (pkg-config gtk+-3.0). On by default on Linux and with MinGW; off by default
# for MSVC, which cannot link the GTK DeaDBeeF ships, and on macOS.
#
# -DTT_DDB_COCOA=ON builds the results windows in Cocoa for DeaDBeeF for Mac.
# On by default on macOS, and only there. One or the other, not both.
#
# With neither, the plugin has no windows: it writes what it is sure of and
# reports the rest in the log.
#
# Then `ctest --test-dir <build dir> -C Release`. See README.md for where the
# built library goes.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)   # CMAKE_MSVC_RUNTIME_LIBRARY
endif()

# Universal by default: DeaDBeeF for Mac ships as one, and a plugin loads only
# into a process of its own architecture. Set before project(), which is when
# CMake settles it.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures to build")
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

# The version has one home, project(VERSION) in ../CMakeLists.txt.
get_filename_component(TT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
file(READ "${TT_ROOT}/CMakeLists.txt" _top_level)
if(NOT _top_level MATCHES "project\\(foo_tangotagger[^)]*VERSION[ \t\r\n]+([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR "Could not read the version out of ${TT_ROOT}/CMakeLists.txt")
endif()
set(TT_VERSION_MAJOR ${CMAKE_MATCH_1})
set(TT_VERSION_MINOR ${CMAKE_MATCH_2})
set(TT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TT_ROOT}/CMakeLists.txt")

project(ddb_tangotagger
        VERSION ${TT_VERSION}
        DESCRIPTION "Tango Tagger for DeaDBeeF"
        LANGUAGES C CXX)   # the LZMA SDK, pffft and kiss_fft are C

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The static libraries end up inside a shared one.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# One exported symbol, ddb_tangotagger_load; nothing else of ours should be
# able to collide with another plugin's.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# No Visual C++ redistributable needed on the machine it is installed on.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/utf-8 /EHsc)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_compile_options(/arch:SSE2)
    endif()
elseif(MINGW AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_compile_options(-msse2 -mfpmath=sse)
endif()

# --- options ---------------------------------------------------------------
set(LZMA_SDK_DIR "${TT_ROOT}/external/lzma_sdk"
    CACHE PATH "Location of the unpacked LZMA SDK")
set(TANGOTAGGER_LYRICS_DIR "${TT_ROOT}/../xml-lyrics-publicdomain"
    CACHE PATH "Folder of lyrics XML files to embed")
set(TANGOTAGGER_DISCOGRAPHY_CC_DIR "${TT_ROOT}/../xml-discographies-cc-by-sa-4.0"
    CACHE PATH "Folder of CC BY-SA discography XML files to embed, preferred over the others")
set(TANGOTAGGER_DISCOGRAPHY_DIR "${TT_ROOT}/../xml-discographies-publicdomain"
    CACHE PATH "Folder of discography XML files to embed")
set(TANGOTAGGER_FINGERPRINT_DIR "${TT_ROOT}/../xml-fingerprints"
    CACHE PATH "Folder of audio fingerprint XML files to embed with the discographies")
set(FOO_BPM_DIR "${TT_ROOT}/../../foo_bpm"
    CACHE PATH "Checkout of foo_bpm, for bpmcore")
set(FOO_BPM_GIT_TAG "main" CACHE STRING "foo_bpm revision to fetch when FOO_BPM_DIR has no checkout")
# OFF embeds every lyrics file in the folder, for a build of your own. ON
# embeds only files marked pd_status="public domain" - the build to publish.
option(FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY "Embed only lyrics marked public domain" OFF)
option(TT_LZMA_AUTO_DOWNLOAD "Fetch the LZMA SDK if it is not present" ON)

if(APPLE OR MSVC)
    set(_tt_gtk_default OFF)
else()
    set(_tt_gtk_default ON)
endif()
option(TT_DDB_GTK "Build the windows for DeaDBeeF's GTK 3 interface" ${_tt_gtk_default})
option(TT_DDB_COCOA "Build the windows for DeaDBeeF's Cocoa interface, on macOS" ${APPLE})
if(TT_DDB_COCOA AND NOT APPLE)
    message(FATAL_ERROR "TT_DDB_COCOA is for macOS; turn it off here.")
endif()
if(TT_DDB_COCOA AND TT_DDB_GTK)
    message(FATAL_ERROR "TT_DDB_GTK and TT_DDB_COCOA are both on; a build has one set of windows.")
endif()

# --- the LZMA SDK ----------------------------------------------------------
if(NOT EXISTS "${LZMA_SDK_DIR}/C/LzmaDec.c")
    if(NOT TT_LZMA_AUTO_DOWNLOAD)
        message(FATAL_ERROR "The LZMA SDK was not found in ${LZMA_SDK_DIR}.\n"
                            "Run ../scripts/get_sdk.sh -o lzma_sdk, or configure with -DTT_LZMA_AUTO_DOWNLOAD=ON.")
    endif()
    get_filename_component(_lzma_parent "${LZMA_SDK_DIR}" DIRECTORY)
    if(WIN32 AND NOT MINGW)
        find_program(TT_POWERSHELL NAMES powershell.exe pwsh.exe REQUIRED)
        execute_process(
            COMMAND "${TT_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                    -File "${TT_ROOT}/scripts/get_sdk.ps1" -Destination "${_lzma_parent}" -Only lzma_sdk
            RESULT_VARIABLE _lzma_result)
    else()
        find_program(TT_BASH NAMES bash REQUIRED)
        execute_process(
            COMMAND "${TT_BASH}" "${TT_ROOT}/scripts/get_sdk.sh" -d "${_lzma_parent}" -o lzma_sdk
            RESULT_VARIABLE _lzma_result)
    endif()
    if(NOT _lzma_result EQUAL 0)
        message(FATAL_ERROR "Could not obtain the LZMA SDK (exit ${_lzma_result}).")
    endif()
endif()
include("${TT_ROOT}/cmake/lzma_sdk.cmake")

if(NOT EXISTS "${TANGOTAGGER_LYRICS_DIR}")
    message(FATAL_ERROR "The lyrics folder was not found: ${TANGOTAGGER_LYRICS_DIR}\n"
                        "Set TANGOTAGGER_LYRICS_DIR to the folder of lyrics XML files.")
endif()

# --- the embedded data -----------------------------------------------------
# The component's packers, run at build time and never shipped: the same
# lyrics, discographies and fingerprints as foo_tangotagger embeds.
add_executable(pack_lyrics "${TT_ROOT}/tools/pack_lyrics/main.cpp" "${TT_ROOT}/tools/pack_common.h")
target_include_directories(pack_lyrics PRIVATE "${TT_ROOT}/core")
target_link_libraries(pack_lyrics PRIVATE lzma_enc)

file(GLOB TANGOTAGGER_LYRICS_FILES CONFIGURE_DEPENDS "${TANGOTAGGER_LYRICS_DIR}/*.xml")
set(TANGOTAGGER_LYRICS_BLOB "${CMAKE_CURRENT_BINARY_DIR}/generated/lyrics_blob.cpp")
set(_pack_flags "")
if(FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY)
    set(_pack_flags "--public-domain-only")
    set(TT_PUBLIC_DOMAIN_ONLY 1)
    message(STATUS "Lyrics: public domain only")
else()
    set(TT_PUBLIC_DOMAIN_ONLY 0)
    message(STATUS "Lyrics: everything in ${TANGOTAGGER_LYRICS_DIR} - a personal build, not for publishing")
endif()
add_custom_command(
    OUTPUT "${TANGOTAGGER_LYRICS_BLOB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND pack_lyrics "${TANGOTAGGER_LYRICS_DIR}" "${TANGOTAGGER_LYRICS_BLOB}" ${_pack_flags}
    # pack_lyrics leaves an unchanged file alone; without the touch the output
    # would stay older than the XML and the step would rerun on every build.
    COMMAND "${CMAKE_COMMAND}" -E touch_nocreate "${TANGOTAGGER_LYRICS_BLOB}"
    DEPENDS pack_lyrics ${TANGOTAGGER_LYRICS_FILES}
    COMMENT "Packing lyrics from ${TANGOTAGGER_LYRICS_DIR}"
    VERBATIM)

add_executable(pack_discography
    "${TT_ROOT}/tools/pack_discography/main.cpp"
    "${TT_ROOT}/core/fingerprint.cpp"
    "${TT_ROOT}/core/title_match.cpp")
target_include_directories(pack_discography PRIVATE "${TT_ROOT}/core")
target_link_libraries(pack_discography PRIVATE lzma_enc)

file(GLOB TANGOTAGGER_FINGERPRINT_FILES CONFIGURE_DEPENDS "${TANGOTAGGER_FINGERPRINT_DIR}/*.xml")
set(_fingerprint_flags "")
if(TANGOTAGGER_FINGERPRINT_FILES)
    set(_fingerprint_flags --fingerprints "${TANGOTAGGER_FINGERPRINT_DIR}")
    message(STATUS "Fingerprints: ${TANGOTAGGER_FINGERPRINT_DIR}")
else()
    message(STATUS "Fingerprints: none (${TANGOTAGGER_FINGERPRINT_DIR} has no .xml files)")
endif()

file(GLOB TANGOTAGGER_DISCOGRAPHY_FILES CONFIGURE_DEPENDS
     "${TANGOTAGGER_DISCOGRAPHY_CC_DIR}/*.xml" "${TANGOTAGGER_DISCOGRAPHY_DIR}/*.xml")
set(TANGOTAGGER_DISCOGRAPHY_BLOB "${CMAKE_CURRENT_BINARY_DIR}/generated/discography_blob.cpp")
add_custom_command(
    OUTPUT "${TANGOTAGGER_DISCOGRAPHY_BLOB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND pack_discography "${TANGOTAGGER_DISCOGRAPHY_BLOB}"
            "${TANGOTAGGER_DISCOGRAPHY_CC_DIR}" "${TANGOTAGGER_DISCOGRAPHY_DIR}" ${_fingerprint_flags}
    COMMAND "${CMAKE_COMMAND}" -E touch_nocreate "${TANGOTAGGER_DISCOGRAPHY_BLOB}"
    DEPENDS pack_discography ${TANGOTAGGER_DISCOGRAPHY_FILES} ${TANGOTAGGER_FINGERPRINT_FILES}
    COMMENT "Packing discographies from ${TANGOTAGGER_DISCOGRAPHY_CC_DIR} and ${TANGOTAGGER_DISCOGRAPHY_DIR}"
    VERBATIM)

if(APPLE)
    # The packers read their folders with std::filesystem, which is macOS
    # 10.15. They only ever run on the machine building, so the plugin's
    # 10.13 is not theirs to keep; the later flag wins.
    foreach(_tool pack_lyrics pack_discography)
        target_compile_options(${_tool} PRIVATE -mmacosx-version-min=10.15)
        target_link_options(${_tool} PRIVATE -mmacosx-version-min=10.15)
    endforeach()
endif()

# --- bpmcore ---------------------------------------------------------------
# Measures the chroma and onsets a fingerprint is made of. A checkout beside
# this repository's, or fetched from GitHub when there is none - as the
# component does.
if(EXISTS "${FOO_BPM_DIR}/bpmcore/bpmcore.h")
    set(_foo_bpm_source "${FOO_BPM_DIR}")
else()
    include(FetchContent)
    FetchContent_Declare(foo_bpm
        GIT_REPOSITORY https://github.com/shaforostoff/foo_bpm.git
        GIT_TAG "${FOO_BPM_GIT_TAG}"
        GIT_SHALLOW TRUE)
    FetchContent_GetProperties(foo_bpm)
    if(NOT foo_bpm_POPULATED)
        FetchContent_Populate(foo_bpm)
    endif()
    set(_foo_bpm_source "${foo_bpm_SOURCE_DIR}")
endif()
message(STATUS "bpmcore: ${_foo_bpm_source}")
include("${_foo_bpm_source}/cmake/fft_backend.cmake")
if(BPMCORE_FFT_BACKEND STREQUAL "kiss")
    set(_fft_target kiss_fft)
else()
    set(_fft_target pffft)
endif()
add_subdirectory("${_foo_bpm_source}/${_fft_target}" "${CMAKE_CURRENT_BINARY_DIR}/foo_bpm/${_fft_target}")
add_subdirectory("${_foo_bpm_source}/bpmcore" "${CMAKE_CURRENT_BINARY_DIR}/foo_bpm/bpmcore")

# --- core: the data and the matching ---------------------------------------
add_library(tangotagger_core STATIC
    "${TT_ROOT}/core/disco_fingerprint.cpp"
    "${TT_ROOT}/core/disco_match.cpp"
    "${TT_ROOT}/core/disco_tags.cpp"
    "${TT_ROOT}/core/discography.cpp"
    "${TT_ROOT}/core/fingerprint.cpp"
    "${TT_ROOT}/core/lyrics_db.cpp"
    "${TT_ROOT}/core/text_links.cpp"
    "${TT_ROOT}/core/title_match.cpp"
    "${TANGOTAGGER_LYRICS_BLOB}"
    "${TANGOTAGGER_DISCOGRAPHY_BLOB}")
target_include_directories(tangotagger_core PUBLIC "${TT_ROOT}/core")
target_link_libraries(tangotagger_core PUBLIC lzma_dec)

find_package(Threads REQUIRED)

if(TT_DDB_COCOA)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
    message(STATUS "Windows: Cocoa")
elseif(TT_DDB_GTK)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GTK3 IMPORTED_TARGET gtk+-3.0>=3.10)
    endif()
    if(NOT GTK3_FOUND)
        message(FATAL_ERROR
            "TT_DDB_GTK is on but GTK 3.10 or later was not found through pkg-config.\n"
            "Install the GTK 3 development files (libgtk-3-dev; in MSYS2 "
            "mingw-w64-x86_64-gtk3), or configure with -DTT_DDB_GTK=OFF "
            "for a plugin without windows.")
    endif()
    message(STATUS "Windows: GTK ${GTK3_VERSION}")
else()
    message(STATUS "Windows: none")
endif()

configure_file(tt_version.h.in "${CMAKE_CURRENT_BINARY_DIR}/tt_version.h" @ONLY)

# --- the plugin ------------------------------------------------------------
# Compiled once as an object library, so the host test links exactly the code
# the player loads. The window implementation is not in it: that is chosen
# per build below, and the host test links a recording stand-in in its place.
add_library(tt_plugin_objects OBJECT
    tt_disco.cpp
    tt_disco.h
    tt_fields.cpp
    tt_fields.h
    tt_lyrics.cpp
    tt_lyrics.h
    tt_plugin.cpp
    tt_plugin.h
    tt_review.h
    tt_ui.h
    tt_version.h.in
    include/deadbeef/deadbeef.h)
target_include_directories(tt_plugin_objects PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}")
if(MSVC)
    # deadbeef.h includes <dirent.h> and <unistd.h>, which MSVC does not have.
    target_include_directories(tt_plugin_objects PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/compat/msvc")
    target_compile_definitions(tt_plugin_objects PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_options(tt_plugin_objects PRIVATE /W4)
else()
    target_compile_options(tt_plugin_objects PRIVATE -Wall -Wextra)
endif()
target_link_libraries(tt_plugin_objects PUBLIC tangotagger_core bpmcore Threads::Threads)

if(TT_DDB_GTK)
    add_library(tt_ui OBJECT gtk/ui_gtk.cpp include/deadbeef/gtkui_api.h)
    target_link_libraries(tt_ui PUBLIC PkgConfig::GTK3)
    target_compile_options(tt_ui PRIVATE -Wall -Wextra)
elseif(TT_DDB_COCOA)
    add_library(tt_ui OBJECT cocoa/ui_cocoa.mm)
    target_compile_options(tt_ui PRIVATE -fobjc-arc -Wall -Wextra)
    # AppKit is DeaDBeeF's, already loaded; the plugin only links against it.
    target_link_libraries(tt_ui PUBLIC "-framework Cocoa")
else()
    add_library(tt_ui OBJECT ui_none.cpp)
endif()
target_include_directories(tt_ui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${TT_ROOT}/core")
if(MSVC)
    target_include_directories(tt_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat/msvc")
endif()

# DeaDBeeF derives the entry point from the file name - ddb_tangotagger.<ext>
# must export ddb_tangotagger_load - and looks for .dll, .dylib or .so by
# platform.
add_library(ddb_tangotagger MODULE $<TARGET_OBJECTS:tt_plugin_objects> $<TARGET_OBJECTS:tt_ui>)
target_link_libraries(ddb_tangotagger PRIVATE tangotagger_core bpmcore Threads::Threads)
if(TT_DDB_GTK)
    # Resolved against the GTK the player has already loaded.
    target_link_libraries(ddb_tangotagger PRIVATE PkgConfig::GTK3)
elseif(TT_DDB_COCOA)
    target_link_libraries(ddb_tangotagger PRIVATE "-framework Cocoa")
endif()
set_target_properties(ddb_tangotagger PROPERTIES OUTPUT_NAME "ddb_tangotagger" PREFIX "")
if(APPLE)
    set_target_properties(ddb_tangotagger PROPERTIES SUFFIX ".dylib")
elseif(WIN32)
    set_target_properties(ddb_tangotagger PROPERTIES SUFFIX ".dll")
endif()

if(APPLE)
    # Apple Silicon will not map unsigned code, and the linker does not sign a
    # universal binary for us. DeaDBeeF does not run under the hardened
    # runtime, so an ad-hoc signature is all it takes; -DTT_CODESIGN_IDENTITY
    # names a real one.
    set(TT_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity; - for ad-hoc")
    add_custom_command(TARGET ddb_tangotagger POST_BUILD
        COMMAND codesign --force --sign "${TT_CODESIGN_IDENTITY}" "$<TARGET_FILE:ddb_tangotagger>"
        VERBATIM)
elseif(MINGW)
    # Everything the plugin needs inside the one file, but GTK, which must be
    # the copy DeaDBeeF has loaded.
    if(TT_DDB_GTK)
        target_link_options(ddb_tangotagger PRIVATE -static-libgcc -static-libstdc++)
    else()
        target_link_options(ddb_tangotagger PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Nothing is resolved from the host - the whole player API arrives as a
    # table of function pointers - so an undefined symbol is a mistake.
    target_link_options(ddb_tangotagger PRIVATE -Wl,--no-undefined)
endif()

# Export ddb_tangotagger_load and nothing else: hidden visibility leaves out
# the std:: templates the standard headers mark visible, so the linker is
# told the whole list. Windows exports only what is marked dllexport already.
if(APPLE)
    target_link_options(ddb_tangotagger PRIVATE "LINKER:-exported_symbol,_ddb_tangotagger_load")
elseif(NOT WIN32)
    target_link_options(ddb_tangotagger PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/ddb_tangotagger.map")
    set_property(TARGET ddb_tangotagger APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ddb_tangotagger.map")
endif()

# --- tests -----------------------------------------------------------------
option(TT_DDB_BUILD_TESTS "Build the DeaDBeeF plugin's tests" ON)
if(TT_DDB_BUILD_TESTS)
    enable_testing()

    # The plugin driven through a stand-in for the player: the actions, the
    # rows, the windows' model and every field written, without DeaDBeeF.
    add_executable(ddb_tangotagger_hosttest tests/host_test.cpp tests/fake_host.cpp tests/fake_host.h
        tests/ui_recorder.cpp tests/ui_recorder.h)
    target_link_libraries(ddb_tangotagger_hosttest PRIVATE tt_plugin_objects)
    add_test(NAME ddb_tangotagger_hosttest COMMAND ddb_tangotagger_hosttest)

    # The windows on the same stand-in, with made-up tracks behind them. Not
    # tests - they put windows up - but the way to look at them without
    # DeaDBeeF; given a directory, they save each window there and exit.
    if(TT_DDB_GTK)
        add_executable(tt_gtk_preview tests/gtk_preview.cpp tests/fake_host.cpp tests/fake_host.h
            tests/preview_tracks.h $<TARGET_OBJECTS:tt_ui>)
        target_link_libraries(tt_gtk_preview PRIVATE tt_plugin_objects PkgConfig::GTK3)
    endif()
    if(TT_DDB_COCOA)
        add_executable(tt_cocoa_preview tests/cocoa_preview.mm tests/fake_host.cpp tests/fake_host.h
            tests/preview_tracks.h $<TARGET_OBJECTS:tt_ui>)
        target_compile_options(tt_cocoa_preview PRIVATE -fobjc-arc)
        target_link_libraries(tt_cocoa_preview PRIVATE tt_plugin_objects "-framework Cocoa")
    endif()
endif()
