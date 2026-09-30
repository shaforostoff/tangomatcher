# ---------------------------------------------------------------------------
# fb2k_sdk.cmake
#
# Builds the foobar2000 SDK as static libraries and exposes them through the
# interface target `fb2k::sdk`.
#
# Unlike a DSP-only component, foo_tangotagger puts up dialogs and a preferences
# page, so it needs the whole stack. What that stack is differs by platform:
#
#   Windows   pfc, the SDK proper, helpers/ (input_helper,
#             preferences_page_impl, uSetDlgItemText) and libPPUI
#             (listview_helper). The last two pull in ATL, which comes with
#             Visual Studio, and WTL, which scripts\get_sdk.ps1 fetches
#             alongside the SDK. shared.dll ships with foobar2000 itself, so
#             only its import library is linked.
#
#   macOS     pfc, the SDK proper, the part of helpers/ that is not Win32, and
#             shared/ built from source - there is no shared.dylib to import
#             against, and the SDK's own Xcode projects compile it into the
#             component. No WTL, and no libPPUI: it is Win32 window classes
#             from top to bottom and has no Xcode project in the SDK.
#
# Which files each platform compiles is not guesswork. The SDK ships Xcode
# projects for pfc, SDK, helpers, shared and the component client, and the
# exclusion lists below are the difference between what is in each directory
# and what those projects build.
# ---------------------------------------------------------------------------

if(NOT EXISTS "${FB2K_SDK_DIR}/foobar2000/SDK/foobar2000.h")
    message(FATAL_ERROR "FB2K_SDK_DIR does not look like a foobar2000 SDK: ${FB2K_SDK_DIR}")
endif()
if(WIN32 AND NOT EXISTS "${FB2K_WTL_DIR}/Include/atlapp.h")
    message(FATAL_ERROR "FB2K_WTL_DIR does not look like a WTL tree: ${FB2K_WTL_DIR}")
endif()
if(APPLE AND NOT EXISTS "${FB2K_SDK_DIR}/foobar2000/helpers-mac/fb2k-platform.h")
    message(FATAL_ERROR
        "This foobar2000 SDK has no macOS support (foobar2000/helpers-mac is missing): ${FB2K_SDK_DIR}")
endif()

# --- settings every SDK library and the component itself share --------------
add_library(fb2k_common INTERFACE)
# Both roots, matching the stock project files: SDK sources include <pfc/...>
# and <libPPUI/...> from the top, and <SDK/...>, <helpers/...> and
# <helpers-mac/...> from foobar2000/. SYSTEM keeps third party warnings out of
# our build log.
target_include_directories(fb2k_common SYSTEM INTERFACE
    "${FB2K_SDK_DIR}"
    "${FB2K_SDK_DIR}/foobar2000")
if(WIN32)
    target_include_directories(fb2k_common SYSTEM INTERFACE "${FB2K_WTL_DIR}/Include")
    # WIN32_LEAN_AND_MEAN is deliberately NOT set: pfc/timers.h calls timeGetTime,
    # which only appears once windows.h has pulled in mmsystem.h.
    # NOMINMAX is deliberately NOT set: libPPUI calls min() and max() expecting
    # windows.h to have defined them. The cost is that our own code has to write
    # (std::min)(a, b) to keep the macro from eating the call.
    target_compile_definitions(fb2k_common INTERFACE
        UNICODE _UNICODE
        _CRT_SECURE_NO_WARNINGS
        _WIN32_WINNT=${FOO_TANGOTAGGER_WIN32_WINNT}
        WINVER=${FOO_TANGOTAGGER_WIN32_WINNT})
endif()

# --- pfc -------------------------------------------------------------------
if(WIN32)
    file(GLOB FB2K_PFC_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/pfc/*.cpp")
    # POSIX-only translation unit; win-objects.cpp is its counterpart.
    list(FILTER FB2K_PFC_SOURCES EXCLUDE REGEX "synchro_nix\\.cpp$")
else()
    # Every .cpp and the one .mm. Nothing is excluded here: win-objects.cpp is
    # #ifdef'd out from the inside and compiles to nothing off Windows, which
    # is how the SDK's own Xcode project builds it too.
    file(GLOB FB2K_PFC_SOURCES CONFIGURE_DEPENDS
         "${FB2K_SDK_DIR}/pfc/*.cpp" "${FB2K_SDK_DIR}/pfc/*.mm")
endif()
add_library(fb2k_pfc STATIC ${FB2K_PFC_SOURCES})
target_link_libraries(fb2k_pfc PUBLIC fb2k_common)
if(WIN32)
    target_link_libraries(fb2k_pfc PUBLIC winmm)
endif()

# --- SDK + component client ------------------------------------------------
file(GLOB FB2K_SDK_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/foobar2000/SDK/*.cpp")
if(APPLE)
    file(GLOB _fb2k_sdk_mm CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/foobar2000/SDK/*.mm")
    list(APPEND FB2K_SDK_SOURCES ${_fb2k_sdk_mm})
endif()
# component_client.cpp holds foobar2000_get_interface, the one symbol the host
# looks up in a component, and core_api's implementation beside it.
#
# On macOS it is compiled into the component itself rather than into this
# static library, and fb2k::sdk names it for foo_tangotagger/CMakeLists.txt to add.
# In an archive it would be a member no symbol in the component refers to -
# nothing calls foobar2000_get_interface from inside - and a member nothing
# refers to is a member the linker is entitled to leave out. It would in fact
# be pulled in today, because the same file defines core_api::get_main_window
# and the component does call that, but a component that stopped calling
# core_api would then silently link to a bundle exporting nothing, and
# foobar2000 would refuse to load it without being able to say why.
set(FB2K_COMPONENT_CLIENT_SOURCE
    "${FB2K_SDK_DIR}/foobar2000/foobar2000_component_client/component_client.cpp")
if(NOT APPLE)
    list(APPEND FB2K_SDK_SOURCES "${FB2K_COMPONENT_CLIENT_SOURCE}")
endif()
add_library(fb2k_sdk_core STATIC ${FB2K_SDK_SOURCES})
target_link_libraries(fb2k_sdk_core PUBLIC fb2k_pfc)

# --- helpers ---------------------------------------------------------------
file(GLOB FB2K_HELPER_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/foobar2000/helpers/*.cpp")
if(APPLE)
    # Win32 window code with no #ifdef around it - the SDK simply leaves these
    # out of its Xcode project rather than guarding them.
    list(FILTER FB2K_HELPER_SOURCES EXCLUDE REGEX
        "/(AutoComplete|CTableEditHelper-Legacy|DarkMode|WindowPositionUtils|image_load_save|inplace_edit|ui_element_helpers|win32_dialog|window_placement_helper)\\.cpp$")
endif()
add_library(fb2k_helpers STATIC ${FB2K_HELPER_SOURCES})
target_link_libraries(fb2k_helpers PUBLIC fb2k_sdk_core)

# --- libPPUI (Windows only) ------------------------------------------------
if(WIN32)
    file(GLOB FB2K_PPUI_SOURCES CONFIGURE_DEPENDS "${FB2K_SDK_DIR}/libPPUI/*.cpp")
    add_library(fb2k_ppui STATIC ${FB2K_PPUI_SOURCES})
    target_link_libraries(fb2k_ppui PUBLIC fb2k_pfc gdiplus uxtheme comctl32 shlwapi)
    target_link_libraries(fb2k_helpers PUBLIC fb2k_ppui)
endif()

# --- shared ----------------------------------------------------------------
if(WIN32)
    # shared.dll ships with foobar2000 itself; the SDK only carries import libs.
    if(FOO_TANGOTAGGER_TARGET_ARCH STREQUAL "x64")
        set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-x64.lib")
    elseif(FOO_TANGOTAGGER_TARGET_ARCH STREQUAL "ARM64EC")
        set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-ARM64EC.lib")
    else()
        set(_fb2k_shared_lib "${FB2K_SDK_DIR}/foobar2000/shared/shared-Win32.lib")
    endif()
    if(NOT EXISTS "${_fb2k_shared_lib}")
        message(FATAL_ERROR "Missing foobar2000 import library: ${_fb2k_shared_lib}")
    endif()
    target_link_libraries(fb2k_sdk_core PUBLIC "${_fb2k_shared_lib}")
else()
    # There is no shared library to import against on macOS: the handful of
    # entry points it holds are compiled into the component, as the SDK's own
    # shared.xcodeproj does. The exclusions are its Win32 half - file dialogs,
    # the system tray, the minidump writer.
    file(GLOB FB2K_SHARED_SOURCES CONFIGURE_DEPENDS
         "${FB2K_SDK_DIR}/foobar2000/shared/*.cpp" "${FB2K_SDK_DIR}/foobar2000/shared/*.mm")
    list(FILTER FB2K_SHARED_SOURCES EXCLUDE REGEX
        "/(Utility|crash_info|filedialogs|filedialogs_vista|font_description|minidump|modal_dialog|systray|text_drawing|utf8api)\\.cpp$")
    add_library(fb2k_shared STATIC ${FB2K_SHARED_SOURCES})
    target_link_libraries(fb2k_shared PUBLIC fb2k_common)
    target_link_libraries(fb2k_sdk_core PUBLIC fb2k_shared)
endif()

# --- umbrella --------------------------------------------------------------
add_library(fb2k_sdk INTERFACE)
target_link_libraries(fb2k_sdk INTERFACE fb2k_helpers)
if(APPLE)
    target_link_libraries(fb2k_sdk INTERFACE "-framework Cocoa")
endif()
add_library(fb2k::sdk ALIAS fb2k_sdk)

set(_fb2k_targets fb2k_pfc fb2k_sdk_core fb2k_helpers)
if(WIN32)
    list(APPEND _fb2k_targets fb2k_ppui)
else()
    list(APPEND _fb2k_targets fb2k_shared)
endif()
set_target_properties(${_fb2k_targets} PROPERTIES FOLDER "foobar2000 SDK")

# The SDK is third party code; do not let its warnings drown out ours.
# /wd4996 covers the SDK deliberately calling its own deprecated APIs.
foreach(_t ${_fb2k_targets})
    if(MSVC)
        target_compile_options(${_t} PRIVATE /W3 /wd4996)
    else()
        target_compile_options(${_t} PRIVATE -w)
    endif()
endforeach()
