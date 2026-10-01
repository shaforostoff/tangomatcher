# ---------------------------------------------------------------------------
# Tango Tagger for foobar2000
#
# Windows:
#   cmake -S . -B build/x64 -A x64
#   cmake --build build/x64 --config Release
#
# macOS:
#   cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/mac
#
# Or just run scripts\build_release.ps1 or scripts/build_release_macos.sh,
# which do every architecture the platform ships and package the result. The
# foobar2000 SDK, the LZMA SDK and - on Windows - WTL are fetched automatically
# the first time you configure; see README.md.
#
# The lyrics are read from ../publicdomain-lyrics at build time and embedded,
# LZMA-compressed, in the component.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(foo_tangotagger
        VERSION 0.1.0
        DESCRIPTION "Tango Tagger for foobar2000"
        LANGUAGES C CXX)   # the LZMA SDK is C

if(NOT WIN32 AND NOT APPLE)
    message(FATAL_ERROR
        "foo_tangotagger targets the Windows and macOS builds of foobar2000.")
endif()

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

# --- options ---------------------------------------------------------------
set(FB2K_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/foobar2000_sdk"
    CACHE PATH "Location of the unpacked foobar2000 SDK")
set(FB2K_WTL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/wtl"
    CACHE PATH "Location of the unpacked WTL (Windows only)")
set(LZMA_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/lzma_sdk"
    CACHE PATH "Location of the unpacked LZMA SDK")
set(TANGOTAGGER_LYRICS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../publicdomain-lyrics"
    CACHE PATH "Folder of lyrics XML files to embed")
set(TANGOTAGGER_DISCOGRAPHY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../publicdomain-xmldata"
    CACHE PATH "Folder of discography XML files to embed")
# OFF embeds every lyrics file in the folder, for a build of your own. ON
# embeds only files marked pd_status="public domain" - the build to publish.
option(FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY "Embed only lyrics marked public domain" OFF)
option(FB2K_SDK_AUTO_DOWNLOAD "Fetch the SDKs and WTL if they are not present" ON)
option(FOO_TANGOTAGGER_STATIC_CRT "Link the C runtime statically (no VC redist needed)" ON)
option(FOO_TANGOTAGGER_LTO        "Whole program optimisation in Release builds" ON)
set(FOO_TANGOTAGGER_WIN32_WINNT "0x0601" CACHE STRING "Minimum Windows version (0x0601 = Windows 7)")

if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

# --- target architecture ---------------------------------------------------
if(APPLE)
    if(CMAKE_OSX_ARCHITECTURES)
        set(FOO_TANGOTAGGER_TARGET_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(FOO_TANGOTAGGER_TARGET_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]64")
        set(FOO_TANGOTAGGER_TARGET_ARCH "ARM64EC")
    else()
        set(FOO_TANGOTAGGER_TARGET_ARCH "x64")
    endif()
else()
    set(FOO_TANGOTAGGER_TARGET_ARCH "x86")
endif()
message(STATUS "Target architecture: ${FOO_TANGOTAGGER_TARGET_ARCH}")

# --- dependencies ----------------------------------------------------------
set(_deps_present TRUE)
if(NOT EXISTS "${FB2K_SDK_DIR}/foobar2000/SDK/foobar2000.h")
    set(_deps_present FALSE)
endif()
if(NOT EXISTS "${LZMA_SDK_DIR}/C/LzmaDec.c")
    set(_deps_present FALSE)
endif()
if(WIN32 AND NOT EXISTS "${FB2K_WTL_DIR}/Include/atlapp.h")
    set(_deps_present FALSE)
endif()

if(NOT _deps_present)
    if(NOT FB2K_SDK_AUTO_DOWNLOAD)
        message(FATAL_ERROR
            "The build prerequisites were not found under ${CMAKE_CURRENT_SOURCE_DIR}/external.\n"
            "Run scripts/get_sdk.ps1 (Windows) or scripts/get_sdk.sh (macOS), "
            "or configure with -DFB2K_SDK_AUTO_DOWNLOAD=ON.")
    endif()
    if(WIN32)
        # PowerShell rather than file(DOWNLOAD): see the note in scripts/get_sdk.ps1.
        find_program(FOO_TANGOTAGGER_POWERSHELL NAMES powershell.exe pwsh.exe REQUIRED)
        execute_process(
            COMMAND "${FOO_TANGOTAGGER_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
                    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/get_sdk.ps1"
            RESULT_VARIABLE _sdk_result)
    else()
        find_program(FOO_TANGOTAGGER_BASH NAMES bash REQUIRED)
        execute_process(
            COMMAND "${FOO_TANGOTAGGER_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/get_sdk.sh"
                    -d "${CMAKE_CURRENT_SOURCE_DIR}/external"
            RESULT_VARIABLE _sdk_result)
    endif()
    if(NOT _sdk_result EQUAL 0)
        message(FATAL_ERROR "Could not obtain the build prerequisites (exit ${_sdk_result}).")
    endif()
endif()

if(NOT EXISTS "${TANGOTAGGER_LYRICS_DIR}")
    message(FATAL_ERROR "The lyrics folder was not found: ${TANGOTAGGER_LYRICS_DIR}\n"
                        "Set TANGOTAGGER_LYRICS_DIR to the folder of lyrics XML files.")
endif()

# --- global build settings -------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(FOO_TANGOTAGGER_STATIC_CRT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options($<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/O2>
                        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:/Oi>)
    add_compile_options(/EHsc /Zi /utf-8 /MP)
    if(MSVC_VERSION GREATER_EQUAL 1929)
        add_compile_options(/external:W0)
    endif()
    # A PDB for Release too: foobar2000's crash reporter is only useful with
    # symbols.
    add_link_options($<$<NOT:$<CONFIG:Debug>>:/DEBUG>
                     $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
                     $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>)
    if(FOO_TANGOTAGGER_TARGET_ARCH STREQUAL "x86")
        add_compile_options(/arch:SSE2)
    endif()
endif()

if(APPLE)
    add_compile_options($<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>)
    add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)
    add_compile_options(-Wall)
    add_compile_options($<$<NOT:$<CONFIG:Debug>>:-g>)
endif()

if(FOO_TANGOTAGGER_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/fb2k_sdk.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/lzma_sdk.cmake")

# --- the embedded lyrics -----------------------------------------------------
# pack_lyrics is a build-machine tool: it runs during the build and is never
# shipped. Every XML file is a dependency, so editing one re-packs; adding or
# removing one is picked up by CONFIGURE_DEPENDS.
add_executable(pack_lyrics tools/pack_lyrics/main.cpp tools/pack_common.h)
target_include_directories(pack_lyrics PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(pack_lyrics PRIVATE lzma_enc)
set_target_properties(pack_lyrics PROPERTIES FOLDER "tools")

file(GLOB TANGOTAGGER_LYRICS_FILES CONFIGURE_DEPENDS "${TANGOTAGGER_LYRICS_DIR}/*.xml")
set(TANGOTAGGER_LYRICS_BLOB "${CMAKE_CURRENT_BINARY_DIR}/generated/lyrics_blob.cpp")
set(_pack_flags "")
if(FOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY)
    set(_pack_flags "--public-domain-only")
    message(STATUS "Lyrics: public domain only")
else()
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

# --- the embedded discographies ---------------------------------------------
# Facts about recordings - title, singers, date, genre - so the same in both
# builds.
add_executable(pack_discography
    tools/pack_discography/main.cpp
    core/title_match.cpp)
target_include_directories(pack_discography PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(pack_discography PRIVATE lzma_enc)
set_target_properties(pack_discography PROPERTIES FOLDER "tools")

file(GLOB TANGOTAGGER_DISCOGRAPHY_FILES CONFIGURE_DEPENDS "${TANGOTAGGER_DISCOGRAPHY_DIR}/*.xml")
set(TANGOTAGGER_DISCOGRAPHY_BLOB "${CMAKE_CURRENT_BINARY_DIR}/generated/discography_blob.cpp")
add_custom_command(
    OUTPUT "${TANGOTAGGER_DISCOGRAPHY_BLOB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND pack_discography "${TANGOTAGGER_DISCOGRAPHY_DIR}" "${TANGOTAGGER_DISCOGRAPHY_BLOB}"
    COMMAND "${CMAKE_COMMAND}" -E touch_nocreate "${TANGOTAGGER_DISCOGRAPHY_BLOB}"
    DEPENDS pack_discography ${TANGOTAGGER_DISCOGRAPHY_FILES}
    COMMENT "Packing discographies from ${TANGOTAGGER_DISCOGRAPHY_DIR}"
    VERBATIM)

# --- core: the data and the matching, no SDK -------------------------------
add_library(tangotagger_core STATIC
    core/disco_match.cpp
    core/disco_match.h
    core/disco_tags.cpp
    core/disco_tags.h
    core/discography.cpp
    core/discography.h
    core/lyrics_db.cpp
    core/lyrics_db.h
    core/payload_format.h
    core/text_links.cpp
    core/text_links.h
    core/title_match.cpp
    core/title_match.h
    "${TANGOTAGGER_LYRICS_BLOB}"
    "${TANGOTAGGER_DISCOGRAPHY_BLOB}")
target_include_directories(tangotagger_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(tangotagger_core PUBLIC lzma_dec)
set_target_properties(tangotagger_core PROPERTIES FOLDER "foo_tangotagger")
if(MSVC)
    target_compile_options(tangotagger_core PRIVATE /W4)
else()
    target_compile_options(tangotagger_core PRIVATE -Wall -Wextra)
endif()

add_subdirectory(foo_tangotagger)

# The same matching as a command line tool, for trying it on a collection.
add_executable(match_titles tools/match_titles/main.cpp)
target_link_libraries(match_titles PRIVATE tangotagger_core)
set_target_properties(match_titles PROPERTIES FOLDER "tools")

# The discography matching from the command line: one track's tags per line.
add_executable(match_recordings tools/match_recordings/main.cpp)
target_link_libraries(match_recordings PRIVATE tangotagger_core)
set_target_properties(match_recordings PROPERTIES FOLDER "tools")

option(FOO_TANGOTAGGER_BUILD_TESTS "Build the verification harness" ON)
if(FOO_TANGOTAGGER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core_test)
endif()
