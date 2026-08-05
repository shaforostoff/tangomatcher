#!/usr/bin/env bash
#
# Builds LyrMatcher and wraps it in a tar.xz release archive.
#
#   ./package.sh                     universal (arm64 + x86_64), tests first
#   ./package.sh --arch native       only this machine's architecture, much faster
#   ./package.sh --version 1.2       override the version in the archive name
#   ./package.sh --skip-tests        skip the test run
#   ./package.sh --output ~/Desktop  write the archive somewhere else
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

APP_NAME="LyrMatcher"
BUNDLE_ID="ua.net.program.LyrMatcher"
ARCH_MODE="universal"
RUN_TESTS=1
VERSION=""
OUTPUT_DIR="dist"

die() { printf 'package.sh: %s\n' "$1" >&2; exit 1; }
step() { printf '\n==> %s\n' "$1"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)       ARCH_MODE="${2:-}"; shift 2 ;;
        --version)    VERSION="${2:-}"; shift 2 ;;
        --output)     OUTPUT_DIR="${2:-}"; shift 2 ;;
        --skip-tests) RUN_TESTS=0; shift ;;
        # Print the header comment block, minus the shebang and the leading "# ".
        -h|--help)    awk 'NR==1 {next} /^#/ {sub(/^# ?/, ""); print; next} {exit}' "$0"; exit 0 ;;
        *)            die "unknown option: $1" ;;
    esac
done

case "$ARCH_MODE" in
    universal|native) ;;
    *) die "--arch must be 'universal' or 'native', got '$ARCH_MODE'" ;;
esac

command -v swift >/dev/null || die "swift not found; install the Xcode command line tools"
# `tar --help` does not list --xz on macOS even though bsdtar supports it, so probe for real.
tar --xz -cf /dev/null -T /dev/null 2>/dev/null || die "this tar cannot write .xz archives"

# ---------------------------------------------------------------- version

## Prefer a git tag (v1.2 / lyrmatcher-1.2), fall back to the bundle's own version.
plist_version() {
    /usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" Resources/Info.plist
}

if [ -z "$VERSION" ]; then
    if git_tag=$(git describe --tags --match 'v[0-9]*' --dirty 2>/dev/null); then
        VERSION="${git_tag#v}"
    else
        VERSION="$(plist_version)"
        if commit=$(git rev-parse --short HEAD 2>/dev/null); then
            VERSION="$VERSION+$commit"
        fi
    fi
fi

# Kept non-empty so it expands cleanly under `set -u` on the bash 3.2 macOS ships.
BUILD_FLAGS=(-c release)

if [ "$ARCH_MODE" = universal ]; then
    ARCH_LABEL="universal"
    BUILD_FLAGS+=(--arch arm64 --arch x86_64)
    BINARY=".build/apple/Products/Release/$APP_NAME"
else
    ARCH_LABEL="$(uname -m)"
    BINARY=".build/release/$APP_NAME"
fi

STAGE_NAME="$APP_NAME-$VERSION"
ARCHIVE="$APP_NAME-$VERSION-macos-$ARCH_LABEL.tar.xz"

# ---------------------------------------------------------------- build

if [ "$RUN_TESTS" -eq 1 ]; then
    step "Running tests"
    swift test
fi

step "Building $ARCH_LABEL release"
swift build "${BUILD_FLAGS[@]}"
[ -f "$BINARY" ] || die "expected binary at $BINARY"

step "Assembling $APP_NAME.app"
APP="$APP_NAME.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BINARY" "$APP/Contents/MacOS/$APP_NAME"
cp Resources/Info.plist "$APP/Contents/Info.plist"

# Stamp the archive's version into the bundle so About and Finder agree with the filename.
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$APP/Contents/Info.plist"

# Ad-hoc signature. Not a Developer ID identity, so the archive is not notarised — see the
# INSTALL note below for what that means for whoever unpacks it.
codesign --force --sign - --timestamp=none "$APP"
codesign --verify --strict "$APP" || die "codesign verification failed"

printf '    architectures: %s\n' "$(lipo -archs "$APP/Contents/MacOS/$APP_NAME")"

# ---------------------------------------------------------------- stage

step "Staging release contents"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/$STAGE_NAME"
cp -R "$APP" "$STAGE/$STAGE_NAME/"
cp README.md "$STAGE/$STAGE_NAME/"
if [ -f ../../LICENSE ]; then cp ../../LICENSE "$STAGE/$STAGE_NAME/"; fi

cat > "$STAGE/$STAGE_NAME/INSTALL.txt" <<EOF
$APP_NAME $VERSION ($ARCH_LABEL)

Install
  Drag $APP into /Applications (or anywhere else you like).

First launch
  This build is signed ad-hoc, not with a Developer ID, and it is not notarised, so
  Gatekeeper will refuse the first launch. Either:

    Right-click $APP -> Open, then confirm in the dialog, or

    xattr -dr com.apple.quarantine /Applications/$APP

  Only the first launch is affected.

Requirements
  macOS 13 or later.

Usage and build instructions are in README.md.
EOF

# ---------------------------------------------------------------- archive

step "Writing $ARCHIVE"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
rm -f "$OUTPUT_DIR/$ARCHIVE"

# Max compression: the archive is a few MB and only built on release.
tar --xz --options 'xz:compression-level=9' \
    -cf "$OUTPUT_DIR/$ARCHIVE" -C "$STAGE" "$STAGE_NAME"

( cd "$OUTPUT_DIR" && shasum -a 256 "$ARCHIVE" > "$ARCHIVE.sha256" )

# Unpack into a scratch directory and check the app still verifies — catches a tar that
# mangled the bundle or dropped the code signature.
step "Verifying the archive"
VERIFY="$(mktemp -d)"
tar -xf "$OUTPUT_DIR/$ARCHIVE" -C "$VERIFY"
codesign --verify --strict "$VERIFY/$STAGE_NAME/$APP" || die "the unpacked app does not verify"
[ -x "$VERIFY/$STAGE_NAME/$APP/Contents/MacOS/$APP_NAME" ] || die "the unpacked binary is not executable"
rm -rf "$VERIFY"

printf '\n%s\n' "Release package ready:"
printf '  %s (%s)\n' "$OUTPUT_DIR/$ARCHIVE" "$(du -h "$OUTPUT_DIR/$ARCHIVE" | cut -f1 | tr -d ' ')"
printf '  %s\n' "$(cat "$OUTPUT_DIR/$ARCHIVE.sha256")"
