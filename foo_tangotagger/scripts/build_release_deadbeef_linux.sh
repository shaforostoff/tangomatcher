#!/usr/bin/env bash
#
# Builds the DeaDBeeF plugin for Linux with this machine's toolchain and
# packages it in dist/, the way DeaDBeeF's plugin builder
# (github.com/DeaDBeeF-Player/deadbeef-plugin-builder) packs the plugins it
# distributes:
#
#     dist/ddb_tangotagger-<version>-linux-<arch>[-personal].zip
#       plugins/ddb_tangotagger.so
#
# What it takes from the builder:
#
#   * libstdc++ and libgcc are linked in, so the plugin asks for no particular
#     C++ runtime and cannot clash with another plugin's. Only
#     ddb_tangotagger_load is exported (deadbeef_tangotagger/ddb_tangotagger.map),
#     which this checks;
#   * the library is loaded and its entry point called, as the builder's
#     pluginfo does, before anything is packed;
#   * readelf's list of what it links to is printed, as the builder's README
#     asks of every plugin.
#
# What it does not take is the builder's old build environment, so the plugin
# runs on distros whose glibc is at least as new as this machine's - which the
# script says at the end. A build on Debian testing needs glibc 2.38, which is
# Ubuntu 24.04 or Debian 13; build on the oldest distro you mean to support.
#
# The GTK 3 windows and the lyrics panel are built in (see
# deadbeef_tangotagger/README.md), which needs
# the GTK 3 development files - libgtk-3-dev on Debian and Ubuntu - and makes
# the plugin need libgtk-3.so.0 at run time, which every DeaDBeeF running its
# GTK 3 interface has already loaded. --no-gtk builds it without windows.
#
# Debug symbols go into a separate archive that is NOT part of the package -
# keep it so a crash report can be resolved:
#
#     dist/ddb_tangotagger-<version>-linux-<arch>[-personal]-symbols.zip
#       ddb_tangotagger.so.debug
#
# Publishing to DeaDBeeF's plugin list is not done here.
#
# The first configure fetches the LZMA SDK into external/, which on Linux
# needs bsdtar (libarchive-tools) or 7-Zip to unpack; see scripts/get_sdk.sh.
# bpmcore comes from the foo_bpm checkout beside this repository, or from
# GitHub when there is none, as for the component; FOO_BPM_DIR in the
# environment names another checkout.
#
# Usage:
#     scripts/build_release_deadbeef_linux.sh [options]
#
#     --no-gtk              leave the windows out; the archive name says -nogtk.
#         --public-domain   embed only lyrics marked pd_status="public domain":
#                           the build to publish. Without it every lyrics file
#                           goes in and the archive is named -personal - for
#                           your own use only, as build_release_macos.sh names
#                           the component.
#     -j, --jobs <n>        compile jobs to run at once. Default: one fewer
#                           than the machine has cores.
#         --skip-tests      do not run the tests. Not recommended.
#         --clean           wipe the build directory first.
#         --install         also copy the packed library to where DeaDBeeF
#                           looks for a user's plugins, to try it in the
#                           player: ~/.local/lib/deadbeef, or $XDG_LOCAL_HOME
#                           where that is set - DeaDBeeF takes that variable
#                           as the plugin folder itself. The debug symbols go
#                           in .debug/ under it, where gdb finds them. Restart
#                           DeaDBeeF to load it.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
with_gtk=1
jobs=""
public_domain=0
skip_tests=0
clean=0
install=0

die() { echo "error: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-gtk)     with_gtk=0; shift ;;
        -j|--jobs)    jobs="${2:-}"; shift 2 ;;
        --public-domain) public_domain=1; shift ;;
        --skip-tests) skip_tests=1; shift ;;
        --clean)      clean=1; shift ;;
        --install)    install=1; shift ;;
        -h|--help)    awk 'NR==1 {next} /^#/ {sub(/^# ?/, ""); print; next} {exit}' \
                          "${BASH_SOURCE[0]}"; exit 0 ;;
        *)            die "unknown option: $1" ;;
    esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
    die "this builds the Linux plugin, on Linux. See deadbeef_tangotagger/README.md for Windows and macOS."
fi

if [[ -z "$jobs" ]]; then
    cores="$(nproc 2>/dev/null || echo 2)"
    if [[ "$cores" -gt 1 ]]; then jobs=$(( cores - 1 )); else jobs=1; fi
fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || die "--jobs wants a positive whole number, not '$jobs'"

# --- version, straight out of the project so the archive name cannot drift --
version="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9][0-9.]*\).*$/\1/p' "$root/CMakeLists.txt" | head -1)"
[[ -n "$version" ]] || die "could not read VERSION out of CMakeLists.txt"

# Anything that changes the binary changes the name, and the suffixes compose,
# so that no switched build can be taken for the one that ships.
suffix=""
[[ $with_gtk -eq 0 ]] && suffix="$suffix-nogtk"
[[ $public_domain -eq 0 ]] && suffix="$suffix-personal"
package="ddb_tangotagger-$version-linux-$(uname -m)$suffix"

# --- where things are ------------------------------------------------------
build_dir="$root/build/ddb-linux$suffix"
out="$root/dist"
revision="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if ! git -C "$root" diff --quiet HEAD -- 2>/dev/null; then revision="$revision-dirty"; fi

for tool in cmake cc c++ zip objcopy objdump strip readelf nm; do
    command -v "$tool" >/dev/null || die "$tool was not found on PATH."
done
if [[ $with_gtk -eq 1 ]] && ! pkg-config --exists 'gtk+-3.0 >= 3.10' 2>/dev/null; then
    die "the GTK 3 development files were not found (libgtk-3-dev on Debian and Ubuntu, gtk3-devel on Fedora). Install them, or pass --no-gtk for a plugin without windows."
fi
[[ $clean -eq 1 ]] && rm -rf "$build_dir"
echo "ddb_tangotagger $version ($revision)"

stage="$build_dir/_package"
rm -rf "$stage"
mkdir -p "$stage/plugins" "$out"

# --- configure and build ---------------------------------------------------
cmake_args=(-DTT_DDB_GTK=$([[ $with_gtk -eq 1 ]] && echo ON || echo OFF))
[[ $with_gtk -eq 0 ]] && echo "  windows: none - not the shipping configuration"
# Named rather than left to the default, so a cached tree cannot ship the
# other one: the lyrics, and the transform the fingerprints are measured with.
if [[ $public_domain -eq 1 ]]; then
    cmake_args+=(-DFOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY=ON)
    echo "  lyrics: public domain only"
else
    cmake_args+=(-DFOO_TANGOTAGGER_PUBLIC_DOMAIN_ONLY=OFF)
    echo "  lyrics: everything - a personal build, not for publishing (see --public-domain)"
fi
cmake_args+=(-DBPMCORE_FFT_BACKEND=pffft -DBPMCORE_FFT_SCALAR=float)
[[ -n "${FOO_BPM_DIR:-}" ]] && cmake_args+=(-DFOO_BPM_DIR="$FOO_BPM_DIR")

echo
echo "=== Configuring ==="
# -g in Release too: the symbols are split off below, and it changes nothing
# about the code. The C++ runtime is linked in and every symbol of it hidden,
# which is the difference between a plugin that loads everywhere and one that
# needs a libstdc++ at least as new as the build machine's.
cmake -S "$root/deadbeef_tangotagger" -B "$build_dir" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-g" -DCMAKE_CXX_FLAGS="-g" \
      -DCMAKE_MODULE_LINKER_FLAGS="-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL" \
      "${cmake_args[@]}"

echo
echo "=== Building ==="
cmake --build "$build_dir" --parallel "$jobs"

if [[ $skip_tests -eq 0 ]]; then
    echo
    echo "=== Testing ==="
    ctest --test-dir "$build_dir" --output-on-failure
fi

lib="$build_dir/ddb_tangotagger.so"
[[ -f "$lib" ]] || die "the build produced no $lib"

# --- symbols ---------------------------------------------------------------
echo
echo "=== Splitting off the symbols ==="
cp "$lib" "$stage/plugins/ddb_tangotagger.so"
objcopy --only-keep-debug "$stage/plugins/ddb_tangotagger.so" "$stage/ddb_tangotagger.so.debug"
strip --strip-unneeded "$stage/plugins/ddb_tangotagger.so"
objcopy --add-gnu-debuglink="$stage/ddb_tangotagger.so.debug" "$stage/plugins/ddb_tangotagger.so"
shipped="$stage/plugins/ddb_tangotagger.so"

# --- checks: what the builder's README asks of every plugin ---------------
echo
echo "=== Checking ==="

# The one symbol DeaDBeeF looks for, and nothing else: another plugin's
# statically linked libstdc++ must not be able to see ours, or ours it.
exports="$(nm -D --defined-only "$shipped" | awk '$2 ~ /^[TDBRVWiu]$/ {print $3}' | sort)"
[[ "$exports" == "ddb_tangotagger_load" ]] || die "expected ddb_tangotagger_load and nothing else exported, got:
$exports"
echo "exports: $exports"

if objdump -T "$shipped" | grep -q 'GLIBCXX_\|CXXABI_'; then
    die "the library still asks for a shared libstdc++:
$(objdump -T "$shipped" | grep -o 'GLIBCXX_[0-9.]*\|CXXABI_[0-9.]*' | sort -uV)"
fi
glibc="$(objdump -T "$shipped" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
echo "needs: ${glibc:-no versioned glibc symbols}"
echo "links:"
readelf -d "$shipped" | awk '/NEEDED/ {print "    " $NF}'

# pluginfo's check: load it the way DeaDBeeF does, and ask it what it is.
cat > "$build_dir/pluginfo.c" <<'EOF'
#define DDB_API_LEVEL 10
#include <deadbeef/deadbeef.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char ** argv)
{
    void * h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    DB_plugin_t * (*load)(DB_functions_t *) =
        (DB_plugin_t * (*)(DB_functions_t *)) dlsym(h, "ddb_tangotagger_load");
    if (!load) { fprintf(stderr, "no ddb_tangotagger_load\n"); return 1; }
    /* The entry point keeps the table and calls nothing in it. */
    static DB_functions_t api;
    memset(&api, 0, sizeof(api));
    DB_plugin_t * p = load(&api);
    if (!p || p->type != DB_PLUGIN_MISC || !p->id) { fprintf(stderr, "not a misc plugin\n"); return 1; }
    printf("id=\"%s\"\nname=\"%s\"\nversion=\"%d.%d\"\napi=\"%d.%d\"\nwebsite=\"%s\"\n",
           p->id, p->name, p->version_major, p->version_minor,
           p->api_vmajor, p->api_vminor, p->website ? p->website : "");
    return 0;
}
EOF
cc -I"$root/deadbeef_tangotagger/include" "$build_dir/pluginfo.c" -o "$build_dir/pluginfo" -ldl
info="$("$build_dir/pluginfo" "$shipped")" || die "the packaged library does not load"
echo "$info" | sed 's/^/    /'
loaded_version="$(echo "$info" | sed -n 's/^version="\(.*\)"$/\1/p')"
[[ "$version" == "$loaded_version".* ]] || die "the plugin says it is $loaded_version, the project $version"

# --- packing ---------------------------------------------------------------
echo
echo "=== Packing ==="
archive="$out/$package.zip"
symbols_archive="$out/$package-symbols.zip"
rm -f "$archive" "$symbols_archive"
# -X: no uid/gid or timestamps beyond the basics, so the archive is the same
# whoever builds it.
(cd "$stage" && zip -q -X -r "$archive" plugins)
(cd "$stage" && zip -q -X "$symbols_archive" ddb_tangotagger.so.debug)

echo "dist/$(basename "$archive")"
echo "    plugins/ddb_tangotagger.so  ($(stat -c %s "$shipped") bytes, revision $revision)"
echo "dist/$(basename "$symbols_archive")"
echo "    ddb_tangotagger.so.debug  keep for crash reports; do not ship"
if [[ $public_domain -eq 0 ]]; then
    echo
    echo "A personal build: it carries lyrics that are not in the public domain."
    echo "Build with --public-domain for anything you publish."
fi
echo
echo "Runs where glibc is ${glibc#GLIBC_} or newer - no older than the glibc it was built against."

# --- installing, to try it -------------------------------------------------
if [[ $install -eq 1 ]]; then
    echo
    echo "=== Installing ==="
    # Where DeaDBeeF looks first. It takes XDG_LOCAL_HOME, where set, as the
    # plugin folder itself rather than as a prefix - see plug_load_all in its
    # src/plugins.c.
    plugin_dir="${XDG_LOCAL_HOME:-$HOME/.local/lib/deadbeef}"
    mkdir -p "$plugin_dir/.debug"
    # Replaced rather than overwritten in place: a running DeaDBeeF has the
    # old file mapped, and writing into it would change code under it.
    install -m 644 "$shipped" "$plugin_dir/.ddb_tangotagger.so.new"
    mv -f "$plugin_dir/.ddb_tangotagger.so.new" "$plugin_dir/ddb_tangotagger.so"
    install -m 644 "$stage/ddb_tangotagger.so.debug" "$plugin_dir/.debug/ddb_tangotagger.so.debug"
    echo "$plugin_dir/ddb_tangotagger.so"
    echo "$plugin_dir/.debug/ddb_tangotagger.so.debug"
    if pgrep -x deadbeef >/dev/null 2>&1; then
        echo "DeaDBeeF is running: quit and start it again to load this build."
    fi
    echo "Then: Preferences > Plugins should list Tango Tagger, the track context"
    echo "menu a Tango Tagger submenu, and View > Design Mode the Tango Lyrics"
    echo "widget. View > Log shows what it did."
fi
