#!/usr/bin/env bash
#
# Builds the DeaDBeeF plugin for macOS and packages it in dist/ as an
# installer:
#
#     dist/ddb_tangotagger-<version>-macos-universal[-personal].pkg
#
# which puts ddb_tangotagger.dylib - Apple Silicon and Intel, with the Cocoa
# windows - in ~/Library/Application Support/Deadbeef/Plugins, the one folder
# DeaDBeeF for Mac loads a user's plugins from. It installs for the current
# user only, which needs no administrator password, and asks for DeaDBeeF to
# be quit first if it is running: replacing a signed library under a process
# that has it mapped gets the process killed.
#
# DeaDBeeF for Mac ships as one universal application, and a plugin loads
# only into a process of its own architecture, so the plugin is universal
# too. It needs macOS 10.13 on Intel, which is what DeaDBeeF itself asks for,
# and 11 on Apple Silicon, which is the first to run there.
#
# What it checks before packing:
#
#   * both architectures are in the library, and ddb_tangotagger_load is the only
#     symbol either exports;
#   * it links to nothing but the system's own libraries and frameworks, so it
#     needs nothing installed beside it - libc++ is the system's, and AppKit
#     is the one DeaDBeeF has already loaded;
#   * the signature is valid. Apple Silicon will not map unsigned code, and
#     stripping the library breaks the linker's, so it is signed again after;
#   * the library loads and its entry point answers, as DeaDBeeF calls it, in
#     every architecture this machine can run.
#
# Signing
# -------
# There are two signatures, made with two different identities:
#
#   * the library's, by codesign. Ad-hoc by default, which is enough for
#     DeaDBeeF: it does not run under the hardened runtime, so it loads a
#     plugin signed by anybody, or by nobody in particular. --codesign names a
#     "Developer ID Application: ..." identity instead, which notarization
#     requires; the library is then also signed with a secure timestamp and
#     the hardened runtime flag, as the notary service asks of all code.
#
#   * the installer's, by productbuild. Unsigned by default, and then
#     Gatekeeper will not open it from a double-click once it has been
#     downloaded: the user has to right-click it and choose Open, or on
#     macOS 15 and later allow it under System Settings > Privacy & Security.
#     --sign names a "Developer ID Installer: ..." identity.
#
# Developer ID identities come in pairs under one name and team, so given
# --sign "Developer ID Installer: X" and no --codesign, the library is signed
# with "Developer ID Application: X", if the keychain has it.
#
# --notarize then sends the signed installer to Apple's notary service, waits
# for the verdict, staples the ticket to the .pkg and checks that Gatekeeper
# accepts it - which is what lets it open with no questions, even offline.
# It needs both signatures and a notarytool profile in the keychain, made
# once with:
#
#     xcrun notarytool store-credentials <profile> --apple-id <id> --team-id <team>
#
# What the installer puts on disk carries no quarantine flag either way.
#
# Debug symbols go into a separate archive that is NOT part of the package -
# keep it so a crash report can be resolved:
#
#     dist/ddb_tangotagger-<version>-macos-universal[-personal]-symbols.zip
#       ddb_tangotagger.dylib.dSYM
#
# Publishing to DeaDBeeF's plugin list is not done here.
#
# bpmcore comes from the foo_bpm checkout beside this repository, or from
# GitHub when there is none, as for the component; FOO_BPM_DIR in the
# environment names another checkout.
#
# Usage:
#     scripts/build_release_deadbeef_macos.sh [options]
#
#     --no-cocoa            leave the windows out; the archive name says
#                           -nococoa.
#         --public-domain   embed only lyrics marked pd_status="public domain":
#                           the build to publish. Without it every lyrics file
#                           goes in and the installer is named -personal - for
#                           your own use only, as build_release_macos.sh names
#                           the component.
#     -a, --arch <list>     architectures to build, comma separated: arm64,
#                           x86_64, or both. Default: both. Anything else is
#                           named in the archive.
#     -s, --sign <identity> sign the installer, with a "Developer ID
#                           Installer: ..." identity. Default: unsigned.
#         --codesign <identity>
#                           sign the library, with a "Developer ID
#                           Application: ..." identity, or - for ad-hoc.
#                           Default: the partner of --sign's identity, or
#                           ad-hoc without one. See "Signing" above.
#         --notarize        notarize and staple the signed installer.
#         --notary-profile <name>
#                           the notarytool keychain profile to notarize with.
#                           Required with --notarize.
#     -j, --jobs <n>        compile jobs to run at once. Default: one fewer
#                           than the machine has cores. 1 on a machine short
#                           of memory.
#         --skip-tests      do not run the tests. Not recommended.
#         --clean           wipe the build directory first.
#         --install         also copy the library straight to where the
#                           installer puts it, to try it in the player
#                           without clicking through the installer, with the
#                           .dSYM beside it, where lldb finds it. Quit and
#                           start DeaDBeeF again to load it.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
with_cocoa=1
arch_list="arm64,x86_64"
sign_identity=""        # the library's, by codesign; worked out below
installer_identity=""   # the installer's, by productbuild
notarize=0
notary_profile=""
jobs=""
public_domain=0
skip_tests=0
clean=0
install=0

die() { echo "error: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-cocoa)   with_cocoa=0; shift ;;
        -a|--arch)    arch_list="${2:-}"; shift 2 ;;
        -s|--sign)    installer_identity="${2:-}"; shift 2
                      [[ -n "$installer_identity" ]] || die "--sign wants an identity" ;;
        --codesign)   sign_identity="${2:-}"; shift 2
                      [[ -n "$sign_identity" ]] || die "--codesign wants an identity, or - for ad-hoc" ;;
        --notarize)   notarize=1; shift ;;
        --notary-profile) notary_profile="${2:-}"; shift 2 ;;
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

if [[ "$(uname -s)" != "Darwin" ]]; then
    die "this builds the macOS plugin, on macOS. See deadbeef_tangotagger/README.md for Windows and Linux."
fi

if [[ -z "$jobs" ]]; then
    cores="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"
    if [[ "$cores" -gt 1 ]]; then jobs=$(( cores - 1 )); else jobs=1; fi
fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || die "--jobs wants a positive whole number, not '$jobs'"

# --- identities, settled before anything is built --------------------------
# Whether the keychain holds a valid identity of this exact name: codesigning
# ones for the library, any for the installer.
has_identity() {
    security find-identity -v -p "$2" 2>/dev/null | grep -Fq "\"$1\""
}
if [[ -n "$installer_identity" ]]; then
    has_identity "$installer_identity" basic \
        || die "the keychain has no valid identity \"$installer_identity\" to sign the installer with"
fi
if [[ -z "$sign_identity" ]]; then
    sign_identity="-"
    if [[ "$installer_identity" == "Developer ID Installer: "* ]]; then
        partner="Developer ID Application: ${installer_identity#Developer ID Installer: }"
        if has_identity "$partner" codesigning; then sign_identity="$partner"; fi
    fi
elif [[ "$sign_identity" != "-" ]]; then
    has_identity "$sign_identity" codesigning \
        || die "the keychain has no valid codesigning identity \"$sign_identity\""
fi
if [[ $notarize -eq 1 ]]; then
    [[ -n "$installer_identity" ]] \
        || die "--notarize needs a signed installer: --sign \"Developer ID Installer: ...\""
    [[ "$sign_identity" != "-" ]] \
        || die "--notarize needs the library signed with a Developer ID: --codesign \"Developer ID Application: ...\", or the partner of --sign's identity in the keychain"
    [[ -n "$notary_profile" ]] || die "--notarize needs --notary-profile <name>"
    # Asked now rather than after the build: a profile that is missing or
    # whose password has lapsed fails here in seconds.
    xcrun notarytool history --keychain-profile "$notary_profile" >/dev/null 2>&1 \
        || die "notarytool cannot use the keychain profile '$notary_profile'. Make it with: xcrun notarytool store-credentials $notary_profile --apple-id <id> --team-id <team>"
elif [[ -n "$notary_profile" ]]; then
    die "--notary-profile is only for --notarize"
fi

# --- architectures, in a fixed order whatever order they were asked in -----
archs=()
for a in arm64 x86_64; do
    [[ ",$arch_list," == *",$a,"* ]] && archs+=("$a")
done
for a in ${arch_list//,/ }; do
    [[ "$a" == arm64 || "$a" == x86_64 ]] || die "unknown architecture '$a': arm64 or x86_64"
done
[[ ${#archs[@]} -gt 0 ]] || die "--arch named no architecture"
if [[ ${#archs[@]} -eq 2 ]]; then arch_name="universal"; else arch_name="${archs[0]}"; fi
cmake_archs="$(IFS=';'; echo "${archs[*]}")"

# --- version, straight out of the project so the archive name cannot drift --
version="$(sed -n 's/^[[:space:]]*VERSION[[:space:]]*\([0-9][0-9.]*\).*$/\1/p' "$root/CMakeLists.txt" | head -1)"
[[ -n "$version" ]] || die "could not read VERSION out of CMakeLists.txt"

# Anything that changes the binary changes the name, and the suffixes compose,
# so that no switched build can be taken for the one that ships.
suffix=""
[[ $with_cocoa -eq 0 ]] && suffix="$suffix-nococoa"
[[ $public_domain -eq 0 ]] && suffix="$suffix-personal"
package="ddb_tangotagger-$version-macos-$arch_name$suffix"

# --- where things are ------------------------------------------------------
build_dir="$root/build/ddb-macos-$arch_name$suffix"
out="$root/dist"
revision="$(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo unknown)"
if ! git -C "$root" diff --quiet HEAD -- 2>/dev/null; then revision="$revision-dirty"; fi

for tool in cmake ctest cc codesign dsymutil dwarfdump strip lipo nm otool zip pkgbuild productbuild pkgutil lsbom cpio; do
    command -v "$tool" >/dev/null || die "$tool was not found on PATH. The Xcode command line tools have it: xcode-select --install"
done
[[ $clean -eq 1 ]] && rm -rf "$build_dir"
echo "ddb_tangotagger $version ($revision), $arch_name"
echo "  library signed: $([[ "$sign_identity" == "-" ]] && echo ad-hoc || echo "$sign_identity")"
echo "  installer signed: ${installer_identity:-no}$([[ $notarize -eq 1 ]] && echo ", notarized with profile $notary_profile")"

stage="$build_dir/_package"
rm -rf "$stage"
mkdir -p "$stage/plugins" "$out"

# --- configure and build ---------------------------------------------------
cmake_args=(-DTT_DDB_COCOA=$([[ $with_cocoa -eq 1 ]] && echo ON || echo OFF) -DTT_DDB_GTK=OFF)
[[ $with_cocoa -eq 0 ]] && echo "  windows: none - not the shipping configuration"
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
# -g in Release too: the symbols go into the .dSYM below, and it changes
# nothing about the code. The deployment target is DeaDBeeF's own, named so a
# cached tree cannot hold another. The build tree's own copy is signed ad-hoc
# whatever the release is signed with: it is stripped and signed again below
# anyway, and a Developer ID signature there would ask the timestamp server
# on every build.
cmake -S "$root/deadbeef_tangotagger" -B "$build_dir" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-g" -DCMAKE_CXX_FLAGS="-g" -DCMAKE_OBJCXX_FLAGS="-g" \
      -DCMAKE_OSX_ARCHITECTURES="$cmake_archs" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 \
      -DTT_CODESIGN_IDENTITY=- \
      "${cmake_args[@]}"

echo
echo "=== Building ==="
cmake --build "$build_dir" --parallel "$jobs"

host_arch="$(uname -m)"
if [[ $skip_tests -eq 0 ]]; then
    echo
    echo "=== Testing ==="
    if [[ " ${archs[*]} " == *" $host_arch "* ]]; then
        ctest --test-dir "$build_dir" --output-on-failure
    elif [[ "$host_arch" == arm64 && " ${archs[*]} " == *" x86_64 "* ]]; then
        # An Intel-only build on Apple Silicon runs its tests under Rosetta,
        # which ctest starts by itself.
        ctest --test-dir "$build_dir" --output-on-failure
    else
        echo "  not run: this machine is $host_arch and cannot run ${archs[*]}"
    fi
fi

lib="$build_dir/ddb_tangotagger.dylib"
[[ -f "$lib" ]] || die "the build produced no $lib"

# --- symbols ---------------------------------------------------------------
echo
echo "=== Splitting off the symbols ==="
# From the linked library, which still points at the object files the debug
# information is in; the copy is what gets stripped.
dsymutil "$lib" -o "$stage/ddb_tangotagger.dylib.dSYM"
shipped="$stage/plugins/ddb_tangotagger.dylib"
cp "$lib" "$shipped"
# -x: every local symbol, keeping the exported one DeaDBeeF looks for.
# Stripping changes the code the signature covers, so it is signed again
# after, and strip's warning that it will be invalid is expected. The
# signature is not taken off first: codesign --remove-signature leaves a gap
# in __LINKEDIT that strip then refuses to process.
strip -x "$shipped" 2> >(grep -v 'will invalidate the code signature' >&2)
# A Developer ID signature gets what the notary service asks of all code: a
# secure timestamp and the hardened runtime flag. The flag governs the
# process a main executable starts and changes nothing for a library that
# DeaDBeeF loads.
codesign_args=(--force --sign "$sign_identity")
[[ "$sign_identity" != "-" ]] && codesign_args+=(--timestamp --options runtime)
codesign "${codesign_args[@]}" "$shipped" 2> >(grep -v 'replacing existing signature' >&2)

# --- checks ----------------------------------------------------------------
echo
echo "=== Checking ==="

have_archs="$(lipo -archs "$shipped" | tr ' ' '\n' | sort | xargs)"
want_archs="$(printf '%s\n' "${archs[@]}" | sort | xargs)"
[[ "$have_archs" == "$want_archs" ]] || die "expected $want_archs in the library, found $have_archs"
echo "architectures: $have_archs"

# The one symbol DeaDBeeF looks for, and nothing else, in every slice.
for a in "${archs[@]}"; do
    exports="$(nm -arch "$a" -gUj "$shipped" | sort | xargs)"
    [[ "$exports" == "_ddb_tangotagger_load" ]] || die "expected _ddb_tangotagger_load and nothing else exported ($a), got: $exports"
done
echo "exports: ddb_tangotagger_load"

# The oldest macOS each slice runs on, which the linker records.
for a in "${archs[@]}"; do
    minos="$(otool -arch "$a" -l "$shipped" | awk '
        /LC_BUILD_VERSION|LC_VERSION_MIN_MACOSX/ {look = 1}
        look && ($1 == "minos" || $1 == "version") {print $2; exit}')"
    echo "needs ($a): macOS ${minos:-unknown} or newer"
done

# Nothing but the system's own: a library from Homebrew or MacPorts would be
# on the build machine and nowhere else.
echo "links:"
links="$(otool -L "$shipped" | awk 'NR > 1 && $1 !~ /:$/ {print $1}' | sort -u)"
while IFS= read -r l; do
    echo "    $l"
    [[ "$l" == /usr/lib/* || "$l" == /System/Library/* ]] || die "links to $l, which is not part of macOS"
done <<< "$links"

codesign --verify --strict "$shipped" || die "the signature does not verify"
echo "signature: $(codesign -dv "$shipped" 2>&1 | sed -n 's/^Signature=//p;s/^Authority=//p' | head -1)"

# The symbols belong to this build, slice by slice.
lib_uuids="$(dwarfdump --uuid "$shipped" | awk '{print $2}' | sort | xargs)"
dsym_uuids="$(dwarfdump --uuid "$stage/ddb_tangotagger.dylib.dSYM" | awk '{print $2}' | sort | xargs)"
[[ -n "$lib_uuids" && "$lib_uuids" == "$dsym_uuids" ]] || die "the .dSYM does not match the library"

# Load it the way DeaDBeeF does, and ask it what it is, in every architecture
# this machine runs: its own, and Intel under Rosetta on Apple Silicon.
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
runnable=()
for a in "${archs[@]}"; do
    if [[ "$a" == "$host_arch" ]]; then runnable+=("$a")
    elif [[ "$a" == x86_64 ]] && arch -x86_64 /usr/bin/true 2>/dev/null; then runnable+=("$a")
    fi
done
[[ ${#runnable[@]} -gt 0 ]] || echo "load check: not run, this machine runs none of ${archs[*]}"
info=""
for a in "${runnable[@]}"; do
    cc -arch "$a" -mmacosx-version-min=10.13 -I"$root/deadbeef_tangotagger/include" \
       "$build_dir/pluginfo.c" -o "$build_dir/pluginfo-$a"
    info="$(arch -"$a" "$build_dir/pluginfo-$a" "$shipped")" || die "the packaged library does not load ($a)"
    echo "loads ($a):"
    echo "$info" | sed 's/^/    /'
done
if [[ -n "$info" ]]; then
    loaded_version="$(echo "$info" | sed -n 's/^version="\(.*\)"$/\1/p')"
    [[ "$version" == "$loaded_version".* ]] || die "the plugin says it is $loaded_version, the project $version"
fi

# --- packing ---------------------------------------------------------------
echo
echo "=== Packing ==="
installer="$out/$package.pkg"
symbols_archive="$out/$package-symbols.zip"
# The .zip is what this script made before it made an installer.
rm -f "$installer" "$symbols_archive" "$out/$package.zip"
pkg_id="io.github.shaforostoff.ddb_tangotagger"
pkg_work="$build_dir/_installer"
rm -rf "$pkg_work"
mkdir -p "$pkg_work/root" "$pkg_work/resources"

# The payload is the library alone, not the folders above it: an installer
# that carries ~/Library in its payload can reset that folder's permissions
# to its own. Installer makes the folders as needed.
cp "$shipped" "$pkg_work/root/ddb_tangotagger.dylib"
chmod 755 "$pkg_work/root"
chmod 644 "$pkg_work/root/ddb_tangotagger.dylib"
pkgbuild --quiet \
         --root "$pkg_work/root" \
         --identifier "$pkg_id" \
         --version "$version" \
         --install-location "/Library/Application Support/Deadbeef/Plugins" \
         "$pkg_work/ddb_tangotagger.pkg"

# The code's licence, and then the data's, which is not the same: the
# installer shows both before it installs anything.
{
    cat "$root/LICENSE"
    cat <<'EOF'

----------------------------------------------------------------------------

The data the plugin embeds is not under the MIT License. The Tango Time
Travel discographies are (c) Tango Time Travel / Moving Art Studio ASBL,
https://tangotimetravel.be/category/release-notes/, licensed under Creative
Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0),
https://creativecommons.org/licenses/by-sa/4.0/. The discography data in
this plugin, adapted from theirs, is shared under the same licence. Tango
Time Travel does not endorse this plugin. The lyrics and the other
discographies keep their own status.
EOF
} > "$pkg_work/resources/LICENSE.txt"
if [[ $public_domain -eq 1 ]]; then
    lyrics_note="The lyrics it carries are in the public domain in Argentina."
else
    lyrics_note=""
fi
cat > "$pkg_work/resources/welcome.html" <<EOF
<html><body style="font-family: -apple-system, sans-serif; font-size: 13px">
<p><b>Tango Tagger $version</b> for DeaDBeeF writes tango lyrics and
discography data into your files: it matches the selected tracks against the
lyrics and orchestra discographies built into it, and lets you choose what to
write.</p>
<p>$lyrics_note</p>
<p>It is installed for you alone, in
<code>~/Library/Application&nbsp;Support/Deadbeef/Plugins</code>, where DeaDBeeF
looks for your plugins. It needs DeaDBeeF 1.8.0 or later.</p>
</body></html>
EOF
cat > "$pkg_work/resources/conclusion.html" <<'EOF'
<html><body style="font-family: -apple-system, sans-serif; font-size: 13px">
<p>Start DeaDBeeF. <b>Preferences &gt; Plugins</b> lists <i>Tango
Tagger</i>, and the playlist's context menu has a <b>Tango Tagger</b> submenu:
<b>Find lyrics...</b> and <b>Match discographies...</b>.</p>
<p>To remove it, delete <code>ddb_tangotagger.dylib</code> from
<code>~/Library/Application&nbsp;Support/Deadbeef/Plugins</code>.</p>
</body></html>
EOF

# Home-folder installs only: the install location above is then taken
# relative to the user's home, and no administrator password is asked for.
# DeaDBeeF has no folder for everyone's plugins outside its own bundle.
host_archs="$(IFS=','; echo "${archs[*]}")"
cat > "$pkg_work/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Tango Tagger for DeaDBeeF</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="LICENSE.txt" mime-type="text/plain"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
    <options customize="never" require-scripts="false" hostArchitectures="$host_archs"/>
    <allowed-os-versions><os-version min="10.13"/></allowed-os-versions>
    <choices-outline>
        <line choice="default"><line choice="$pkg_id"/></line>
    </choices-outline>
    <choice id="default"/>
    <choice id="$pkg_id" visible="false"><pkg-ref id="$pkg_id"/></choice>
    <pkg-ref id="$pkg_id" version="$version" onConclusion="none">ddb_tangotagger.pkg</pkg-ref>
    <pkg-ref id="$pkg_id">
        <must-close><app id="com.deadbeef.DeaDBeeF"/></must-close>
    </pkg-ref>
</installer-gui-script>
EOF

sign_args=()
[[ -n "$installer_identity" ]] && sign_args=(--sign "$installer_identity" --timestamp)
productbuild --quiet \
             --distribution "$pkg_work/distribution.xml" \
             --resources "$pkg_work/resources" \
             --package-path "$pkg_work" \
             ${sign_args[@]+"${sign_args[@]}"} \
             "$installer"

# What went in is the library checked above, and nothing else.
pkgutil --expand "$installer" "$pkg_work/expanded"
payload="$(lsbom -s "$pkg_work/expanded/ddb_tangotagger.pkg/Bom" | sort | xargs)"
[[ "$payload" == ". ./ddb_tangotagger.dylib" ]] || die "the installer carries more than the library: $payload"
mkdir -p "$pkg_work/payload"
(cd "$pkg_work/payload" && gzip -dc ../expanded/ddb_tangotagger.pkg/Payload | cpio -i --quiet)
cmp -s "$pkg_work/payload/ddb_tangotagger.dylib" "$shipped" \
    || die "the library in the installer is not the one checked"
if [[ -n "$installer_identity" ]]; then
    pkgutil --check-signature "$installer" >/dev/null || die "the installer's signature does not verify"
    echo "installer: signed by $installer_identity"
else
    echo "installer: unsigned - see --sign"
fi

# --- notarizing ------------------------------------------------------------
if [[ $notarize -eq 1 ]]; then
    echo
    echo "=== Notarizing ==="
    # Waits for the verdict, which is usually a few minutes and occasionally
    # much longer; the submission goes on at Apple's end if this is stopped,
    # and `xcrun notarytool history` finds it again.
    result="$pkg_work/notary.plist"
    # Its exit code is not the verdict - a rejection can end either way - so
    # the answer is read out of what it prints.
    xcrun notarytool submit "$installer" --keychain-profile "$notary_profile" \
          --wait --output-format plist > "$result" || true
    submission="$(plutil -extract id raw -o - "$result" 2>/dev/null || true)"
    status="$(plutil -extract status raw -o - "$result" 2>/dev/null || true)"
    [[ -n "$submission" ]] || die "notarytool did not submit the installer: $(cat "$result")"
    echo "submission $submission: ${status:-no status}"
    if [[ "$status" != "Accepted" ]]; then
        # The log names every file the service objected to, and why.
        xcrun notarytool log "$submission" --keychain-profile "$notary_profile" \
              "$pkg_work/notary-log.json" >/dev/null 2>&1 \
            && cat "$pkg_work/notary-log.json" >&2
        die "notarization was not accepted: ${status:-unknown}"
    fi
    # The ticket goes into the .pkg itself, so Gatekeeper need not ask Apple
    # for it: an installer opened offline is accepted too.
    xcrun stapler staple -q "$installer" || die "stapling the ticket to the installer failed"
    xcrun stapler validate -q "$installer" || die "the stapled ticket does not validate"
    assessment="$(spctl --assess --type install -vv "$installer" 2>&1)" \
        || die "Gatekeeper does not accept the installer:
$assessment"
    echo "gatekeeper: $(echo "$assessment" | sed -n 's/^source=//p')"
fi

# -X: no extended attributes or uid/gid, so the archive is the same whoever
# builds it.
(cd "$stage" && zip -q -X -r "$symbols_archive" ddb_tangotagger.dylib.dSYM)

echo "dist/$(basename "$installer")"
echo "    ddb_tangotagger.dylib  ($(stat -f %z "$shipped") bytes, revision $revision)"
echo "    installs to ~/Library/Application Support/Deadbeef/Plugins"
echo "dist/$(basename "$symbols_archive")"
echo "    ddb_tangotagger.dylib.dSYM     keep for crash reports; do not ship"

# --- installing, to try it -------------------------------------------------
if [[ $install -eq 1 ]]; then
    echo
    echo "=== Installing ==="
    plugin_dir="$HOME/Library/Application Support/Deadbeef/Plugins"
    mkdir -p "$plugin_dir"
    # Replaced rather than overwritten in place: a running DeaDBeeF has the
    # old file mapped, and macOS kills a process whose signed code changes
    # under it. DeaDBeeF skips names starting with a dot, so the half-copied
    # file is never loaded.
    cp "$shipped" "$plugin_dir/.ddb_tangotagger.dylib.new"
    mv -f "$plugin_dir/.ddb_tangotagger.dylib.new" "$plugin_dir/ddb_tangotagger.dylib"
    # Beside the library, where lldb and the crash reporter look for it.
    # DeaDBeeF loads only names ending in .dylib, so it is not taken for one.
    rm -rf "$plugin_dir/ddb_tangotagger.dylib.dSYM"
    cp -R "$stage/ddb_tangotagger.dylib.dSYM" "$plugin_dir/"
    echo "$plugin_dir/ddb_tangotagger.dylib"
    echo "$plugin_dir/ddb_tangotagger.dylib.dSYM"
    if pgrep -x DeaDBeeF >/dev/null 2>&1; then
        echo "DeaDBeeF is running: quit and start it again to load this build."
    fi
    echo "Then: Preferences > Plugins should list Tango Tagger, and the track"
    echo "context menu a Tango Tagger submenu. Window > Log Window shows what it did."
fi
