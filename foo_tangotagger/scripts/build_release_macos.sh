#!/usr/bin/env bash
#
# Builds foo_tangotagger for macOS and packages the result as an installable
# .fb2k-component in dist/. The counterpart of scripts\build_release.ps1.
#
# Configures and builds with CMake, runs the test suite, pulls the debug info
# out into a .dSYM, strips and signs the bundle, then assembles:
#
#     foo_tangotagger-<version>-mac.fb2k-component
#       mac/foo_tangotagger.component     universal: Apple Silicon and Intel
#
# mac/ is where foobar2000 for Mac looks, and every other platform ignores a
# subfolder it does not understand - which is what --merge is for. Given the
# archive scripts\build_release.ps1 produced on Windows, it unpacks that
# alongside the macOS bundle and writes one component that installs on both:
#
#     foo_tangotagger-<version>.fb2k-component
#       foo_tangotagger.dll               32 bit Windows, foobar2000 1.x and 2.x
#       x64/foo_tangotagger.dll           64 bit Windows, foobar2000 2.x
#       mac/foo_tangotagger.component     macOS, foobar2000 for Mac 2.6 and newer
#
# Debug symbols go into a separate archive that is NOT part of the component,
# as on Windows - keep it so a crash report can be resolved.
#
# The SDK is fetched on the first configure; see scripts/get_sdk.sh.
#
# Usage:
#     scripts/build_release_macos.sh [options]
#
#     -a, --arch <list>     architectures to build, comma separated:
#                           arm64, x86_64, or both. Default: both, which is
#                           what makes the bundle universal.
#     -c, --config <name>   CMake build type. Default: Release.
#     -j, --jobs <n>        compile jobs to run at once. Default: one fewer
#                           than the machine has cores, so that the build
#                           leaves a core - and the memory that goes with it -
#                           for everything else running. 1 builds serially,
#                           which is what to ask for on a machine that is
#                           already short of RAM: each clang holds a whole
#                           translation unit, and this one's include a
#                           precompiled header over the foobar2000 SDK.
#     -s, --sign <identity> codesign identity. Default: - , which is an ad-hoc
#                           signature. See "Signing" below.
#     -m, --merge <archive> fold the macOS bundle into this existing
#                           .fb2k-component, producing one archive that
#                           installs on Windows and macOS alike.
#         --public-domain   embed only lyrics marked pd_status="public domain":
#                           the build to publish. Without it every lyrics file
#                           goes in and the archive is named -personal - for
#                           your own use only.
#         --skip-tests      do not run the verification harness. Not recommended.
#         --clean           wipe the build directory first.
#
# There is no counterpart to build_release.ps1's -Dynamic: it selects between
# the static and DLL Visual C++ runtimes, and there is no such choice to make
# against the system libc++.
#
# Signing
# -------
# The component is ad-hoc signed by default, which is what the SDK's own Xcode
# project does and what a component needs: on Apple Silicon the kernel refuses
# to map unsigned code at all, so an unsigned bundle would simply fail to load
# on half the machines it is meant for. Cross-building a universal binary does
# not sign it for you, so this is not optional.
#
# A Developer ID and notarization are NOT needed. foobar2000 for Mac runs under
# the hardened runtime but ships com.apple.security.cs.disable-library-validation,
# which is exactly the entitlement that lets it load code signed by somebody
# else - or by nobody. Pass -s "Developer ID Application: ..." if you have one
# and would rather ship a signed component anyway; nothing here requires it.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist_dir="$root/dist"

arch_list="arm64,x86_64"
configuration="Release"
jobs=""
sign_identity="-"
merge_archive=""
skip_tests=0
public_domain=0
clean=0

die() { echo "error: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--arch)    arch_list="${2:-}"; shift 2 ;;
        -c|--config)  configuration="${2:-}"; shift 2 ;;
        -j|--jobs)    jobs="${2:-}"; shift 2 ;;
        -s|--sign)    sign_identity="${2:-}"; shift 2 ;;
        -m|--merge)   merge_archive="${2:-}"; shift 2 ;;
        --public-domain) public_domain=1; shift ;;
        --skip-tests) skip_tests=1; shift ;;
        --clean)      clean=1; shift ;;
        # Every comment line down to the first that is not one, rather than a
        # line range: the range was two lines short of the header the moment an
        # option grew a line, and said nothing about it.
        -h|--help)    awk 'NR==1 {next} /^#/ {sub(/^# ?/, ""); print; next} {exit}' \
                          "${BASH_SOURCE[0]}"; exit 0 ;;
        *)            die "unknown option: $1" ;;
    esac
done

# --- how much of the machine to use ----------------------------------------
# Not all of it. `cmake --build --parallel` with no number starts one job per
# core, and a job here is a clang holding a translation unit that has the
# foobar2000 SDK precompiled into it - which is enough that a machine with
# other work on it can run out of memory and have the build killed, with an
# exit code and no explanation. One fewer than the cores leaves room; -j 1
# leaves a lot more.
if [[ -z "$jobs" ]]; then
    cores="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
    if [[ "$cores" -gt 1 ]]; then jobs=$(( cores - 1 )); else jobs=1; fi
fi
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: --jobs wants a positive whole number, not '$jobs'" >&2
    exit 2
fi

command -v cmake    >/dev/null || die "cmake was not found on PATH. Install CMake 3.21 or newer."
command -v codesign >/dev/null || die "codesign was not found. Install the Xcode command line tools."
command -v clang    >/dev/null || die "clang was not found. Install the Xcode command line tools."

# --- architectures ---------------------------------------------------------
# One configure covers both: a universal binary is two slices of one build,
# not two builds. Which is also why there is no loop here and there is one in
# the Windows script.
cmake_archs=""
arch_tag=""
IFS=',' read -r -a requested <<< "$arch_list"
for a in "${requested[@]}"; do
    case "$a" in
        arm64|x86_64) ;;
        *) die "unknown architecture '$a'; expected arm64 or x86_64" ;;
    esac
    cmake_archs="${cmake_archs:+$cmake_archs;}$a"
done
[[ -n "$cmake_archs" ]] || die "no architectures requested"

if [[ "${#requested[@]}" -eq 2 ]]; then
    arch_tag=""          # universal, which is the shipping shape
    arch_words="universal (Apple Silicon and Intel)"
else
    arch_tag="-${requested[0]}"
    arch_words="${requested[0]} only - not the shipping configuration"
fi

# --- version, straight out of the project so the archive name cannot drift --
version="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9][0-9.]*\).*$/\1/p' "$root/CMakeLists.txt" | head -1)"
[[ -n "$version" ]] || die "could not read VERSION out of CMakeLists.txt"
echo "foo_tangotagger $version"
echo "  architecture: $arch_words"
echo "  jobs: $jobs"

# --- what is being built, and so which build tree and which archive ---------
# Anything that changes the binary gets its own directory and its own archive
# name, and the suffixes compose. Sharing either would be a trap rather than a
# convenience: every one of these is a CMake cache variable, so a plain build
# run after a switched one into the same directory would keep what was cached
# and package it as the shipping component without saying so.
cmake_args=()
suffix=""

# Named either way, so a tree configured the other way cannot carry its cached
# choice into this archive.
if [[ $public_domain -eq 1 ]]; then
    cmake_args+=(-DFOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY=ON)
    echo "  lyrics: public domain only"
else
    cmake_args+=(-DFOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY=OFF)
    suffix="-personal"
    echo "  lyrics: everything in the folder - PERSONAL build, do not publish"
fi


build_dir="$root/build/mac${arch_tag:-"-universal"}$suffix"
stage="$root/build/_package_mac"
symbols="$root/build/_symbols_mac"

[[ $clean -eq 1 ]] && rm -rf "$build_dir"
rm -rf "$stage" "$symbols"
mkdir -p "$stage" "$symbols" "$dist_dir"

# --- build -----------------------------------------------------------------
echo
echo "=== Configuring ==="
cmake -S "$root" -B "$build_dir" \
      -DCMAKE_BUILD_TYPE="$configuration" \
      -DCMAKE_OSX_ARCHITECTURES="$cmake_archs" \
      ${cmake_args[@]+"${cmake_args[@]}"}

echo
echo "=== Building ==="
cmake --build "$build_dir" --config "$configuration" --parallel "$jobs"

if [[ $skip_tests -eq 0 ]]; then
    echo
    echo "=== Testing ==="
    # The harnesses are built universal along with everything else, so what
    # runs here is this machine's slice. The other one is compiled and linked
    # but cannot be executed on this hardware, so it is not covered - run this
    # script on a machine of each architecture to cover both.
    ctest --test-dir "$build_dir" -C "$configuration" --output-on-failure
fi

bundle="$build_dir/foo_tangotagger/foo_tangotagger.component"
binary="$bundle/Contents/MacOS/foo_tangotagger"
[[ -d "$bundle" && -f "$binary" ]] || die "expected output missing: $bundle"

# --- symbols ---------------------------------------------------------------
# Read out of the build tree's own binary, before anything has been taken out
# of it.
echo
echo "=== Symbols ==="
dsym="$symbols/foo_tangotagger.component.dSYM"
dsymutil "$binary" -o "$dsym"
echo "  $(basename "$dsym")"

# --- stage -----------------------------------------------------------------
# The payload for the other platform, if there is one, is unpacked FIRST, so
# that nothing is written to dist/ until everything that is going into the
# archive is already on disk. That is what makes it safe to merge an archive
# in dist/ over itself, which is the obvious thing to want: the Windows build
# lands there under the name this then writes.
if [[ -n "$merge_archive" ]]; then
    [[ -f "$merge_archive" ]] || die "no such archive: $merge_archive"
    echo
    echo "=== Merging $(basename "$merge_archive") ==="
    cmake -E chdir "$stage" cmake -E tar xf "$(cd "$(dirname "$merge_archive")" && pwd)/$(basename "$merge_archive")"

    if [[ ! -f "$stage/foo_tangotagger.dll" && ! -f "$stage/x64/foo_tangotagger.dll" ]]; then
        echo "  warning: no foo_tangotagger.dll in it - is that really a foo_tangotagger component?" >&2
    fi
    # A macOS payload already in there is this script's previous output, and
    # the one being built replaces it rather than merging with it.
    if [[ -d "$stage/mac" ]]; then
        echo "  replacing the macOS payload it already carried"
        rm -rf "$stage/mac"
    fi
    # An `&&` as the last command in the loop body would make the loop's exit
    # status its own, and `set -e` would end the script there on an archive
    # that turned out to hold nothing.
    for f in "$stage"/*; do
        if [[ -e "$f" ]]; then
            echo "  kept: ${f#$stage/}"
        fi
    done
fi

mkdir -p "$stage/mac"
cp -R "$bundle" "$stage/mac/"

# --- strip, then sign, and both on the staged copy --------------------------
# Not on the build tree's own bundle. Stripping is destructive and the build
# does not know it happened, so a second run over an unchanged tree would find
# a binary with nothing left to pull a .dSYM out of - and would say so as a
# warning while writing an empty symbols archive. Signing has to come after
# stripping either way, because taking bytes out of a signed binary is exactly
# what a signature is there to detect.
staged_bundle="$stage/mac/foo_tangotagger.component"
xcrun strip -x -S "$staged_bundle/Contents/MacOS/foo_tangotagger"

echo
echo "=== Signing ==="
if [[ "$sign_identity" == "-" ]]; then
    echo "  ad-hoc - no Developer ID, and none is needed; see the note in this script"
else
    echo "  $sign_identity"
fi
codesign --force --sign "$sign_identity" --timestamp=none "$staged_bundle"
codesign --verify --strict "$staged_bundle" || die "the signature did not verify"

# --- package ---------------------------------------------------------------
# cmake -E tar for the same reason the Windows script uses it, and because a
# plain zip is all a bundle needs: the signature lives in the Mach-O and in
# Contents/_CodeSignature, both of them ordinary files, and the mode bits
# survive the round trip.
echo
echo "=== Package ==="
if [[ -n "$merge_archive" ]]; then
    component="$dist_dir/foo_tangotagger-$version$suffix.fb2k-component"
else
    component="$dist_dir/foo_tangotagger-$version-mac$arch_tag$suffix.fb2k-component"
fi
symbols_zip="$dist_dir/foo_tangotagger-$version-mac$arch_tag$suffix-symbols.zip"
rm -f "$component" "$symbols_zip"

cmake -E chdir "$stage"   cmake -E tar cf "$component"   --format=zip .
cmake -E chdir "$symbols" cmake -E tar cf "$symbols_zip" --format=zip .

size=$(stat -f%z "$component")
printf '  %s  (%d bytes)\n' "$component" "$size"
cmake -E tar tf "$component" | sed 's/^/      /'
printf '  %s  (%d bytes)\n' "$symbols_zip" "$(stat -f%z "$symbols_zip")"

cat <<EOF

To install: drag the .fb2k-component file onto foobar2000, or use
Preferences > Components > Install...
EOF

# A string rather than an array: /bin/bash on macOS is 3.2, where an empty
# array counts as unset and `set -u` stops the script over it.
notes=""
if [[ $public_domain -eq 0 ]]; then
    notes="$notes It carries lyrics that are not in the public domain; build with --public-domain for anything you publish."
fi
if [[ -n "$arch_tag" ]]; then
    notes="$notes It carries ${requested[0]} only, so it will not load on the other architecture."
fi
if [[ -n "$notes" ]]; then
    echo
    echo "This is not the release build.$notes"
    echo "Install it to test, and do not publish it under that name."
fi
