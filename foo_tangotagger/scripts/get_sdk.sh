#!/usr/bin/env bash
#
# Downloads and unpacks the build prerequisites into external/, for a macOS
# build - or, with -o lzma_sdk, the one deadbeef_tangotagger needs, on macOS
# or Linux. The counterpart of scripts/get_sdk.ps1.
#
#     external/foobar2000_sdk/    foobar2000 SDK
#     external/lzma_sdk/          LZMA SDK (7-Zip's LZMA codec)
#
# WTL is a set of Win32 window classes and the macOS build has no use for it,
# so it is not fetched here.
#
# The releases, URLs and checksums are not repeated in this file. They are read
# out of scripts/get_sdk.ps1, which stays their one home, so the two platforms
# cannot drift apart.
#
# bsdtar - /usr/bin/tar on every supported macOS - reads .7z, so there is no
# 7-Zip install in this. curl and shasum are likewise stock. On Linux, where
# tar is GNU tar and cannot, bsdtar (libarchive-tools) or 7-Zip (7z, 7za or
# 7zz) unpacks it instead, and sha256sum stands in for shasum.
#
# Usage:
#     scripts/get_sdk.sh [-d <destination>] [-f] [-o <dir>]...
#
#     -d  where to unpack. Default: external/ next to this repository.
#     -f  re-download and re-unpack even if everything is already there.
#     -o  fetch only this dependency, by the folder it unpacks into
#         (foobar2000_sdk, lzma_sdk); may be repeated.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
destination="$root/external"
force=0
only=()

while getopts ':d:fo:h' opt; do
    case "$opt" in
        d) destination="$OPTARG" ;;
        f) force=1 ;;
        o) only+=("$OPTARG") ;;
        h) sed -n '2,28p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Usage: $0 [-d <destination>] [-f] [-o <dir>]..." >&2; exit 2 ;;
    esac
done

sha256_of() {
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
    else sha256sum "$1" | cut -d' ' -f1
    fi
}

# unpack <archive> <into>: tar where it reads the archive - bsdtar does, GNU
# tar does not read .7z - and 7-Zip where it does not.
unpack() {
    if tar -xf "$1" -C "$2" 2>/dev/null; then return 0; fi
    local tool
    for tool in bsdtar 7zz 7z 7za; do
        command -v "$tool" >/dev/null 2>&1 || continue
        case "$tool" in
            bsdtar) bsdtar -xf "$1" -C "$2" && return 0 ;;
            *)      "$tool" x -y -o"$2" "$1" >/dev/null && return 0 ;;
        esac
    done
    return 1
}

wanted() {
    [[ ${#only[@]} -eq 0 ]] && return 0
    local o
    for o in "${only[@]}"; do [[ "$o" == "$1" ]] && return 0; done
    return 1
}

pinned="$root/scripts/get_sdk.ps1"
if [[ ! -f "$pinned" ]]; then
    echo "error: $pinned is missing; it is where the dependencies are pinned." >&2
    exit 1
fi

mkdir -p "$destination"

# fetch <dir> <file that must exist after unpacking>...
#
# The dependency's block in get_sdk.ps1 is picked by its Dir: awk collects
# each [pscustomobject] and prints the fields of the one that unpacks there.
fetch() {
    local dep_dir="$1"; shift
    wanted "$dep_dir" || return 0

    local block
    block="$(awk -v want="$dep_dir" '
        /\[pscustomobject\]/ { block = ""; next }
        /^[[:space:]]*}/       { if (block ~ ("Dir[[:space:]]*=[[:space:]]*." want ".")) print block; block = ""; next }
                               { block = block $0 "\n" }
    ' "$pinned")"

    read_field() {
        printf '%s\n' "$block" \
            | sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*'\\(.*\\)'.*$/\\1/p" \
            | head -1
    }

    local name version archive_name url sha256
    name="$(read_field Name)"
    version="$(read_field Version)"
    archive_name="$(read_field Archive)"
    url="$(read_field Url)"
    sha256="$(read_field Sha256 | tr '[:upper:]' '[:lower:]')"

    if [[ -z "$version" || -z "$archive_name" || -z "$url" || -z "$sha256" ]]; then
        echo "error: could not read the $dep_dir pin out of $pinned." >&2
        exit 1
    fi

    local dir="$destination/$dep_dir"
    local archive="$destination/$archive_name"
    local stamp="$dir/.$dep_dir-$version.stamp"

    if [[ -f "$stamp" && $force -eq 0 ]]; then
        echo "$name $version already unpacked in $dir"
        return 0
    fi

    # --- fetch -------------------------------------------------------------
    local have_archive=0
    if [[ -f "$archive" ]]; then
        if [[ "$(sha256_of "$archive")" == "$sha256" ]]; then
            have_archive=1
            echo "Reusing $archive"
        else
            echo "Discarding $archive (checksum mismatch)"
            rm -f "$archive"
        fi
    fi

    if [[ $have_archive -eq 0 ]]; then
        echo "Downloading $url"
        if ! curl -fsSL --retry 2 -o "$archive" "$url"; then
            rm -f "$archive"
            echo "error: failed to download $name." >&2
            echo "       Fetch $url by hand, drop it in $destination, and re-run." >&2
            exit 1
        fi

        local got
        got="$(sha256_of "$archive")"
        if [[ "$got" != "$sha256" ]]; then
            rm -f "$archive"
            echo "error: $name: checksum mismatch." >&2
            echo "         expected $sha256" >&2
            echo "         got      $got" >&2
            exit 1
        fi
    fi

    # --- unpack ------------------------------------------------------------
    # Into a scratch directory first, so a half-finished unpack can never be
    # mistaken for a usable dependency.
    local tmp="$destination/.unpack-$dep_dir"
    rm -rf "$tmp"
    mkdir -p "$tmp"

    echo "Unpacking into $dir"
    if ! unpack "$archive" "$tmp"; then
        rm -rf "$tmp"
        echo "error: could not unpack $archive." >&2
        echo "       /usr/bin/tar reads .7z on macOS 11 and later; on Linux install bsdtar" >&2
        echo "       (libarchive-tools) or 7-Zip (p7zip-full, 7zip), or unpack it by hand." >&2
        exit 1
    fi

    local need
    for need in "$@"; do
        if [[ ! -e "$tmp/$need" ]]; then
            rm -rf "$tmp"
            echo "error: unexpected archive layout in $archive_name: $need is missing." >&2
            exit 1
        fi
    done

    rm -rf "$dir"
    mv "$tmp" "$dir"
    printf '%s\n%s\n' "$version" "$url" > "$stamp"

    echo "$name $version ready in $dir"
}

fetch foobar2000_sdk \
    foobar2000/SDK/foobar2000.h \
    foobar2000/helpers-mac/fb2k-platform.h \
    foobar2000/shared/shared-nix.cpp \
    pfc/pfc.h

fetch lzma_sdk \
    C/LzmaDec.c \
    C/LzmaEnc.c \
    C/LzFind.c
