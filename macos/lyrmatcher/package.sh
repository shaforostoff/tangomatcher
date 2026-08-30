#!/usr/bin/env bash
#
# Builds LyrMatcher and wraps it in a tar.xz release archive.
#
#   ./package.sh                     universal (arm64 + x86_64), tests first
#   ./package.sh --arch native       only this machine's architecture, much faster
#   ./package.sh --version 1.2       override the version in the archive name
#   ./package.sh --skip-tests        skip the test run
#   ./package.sh --output ~/Desktop  write the archive somewhere else
#   ./package.sh --notarize          Developer ID signature, notarisation, and a .dmg
#
# --notarize replaces the ad-hoc signature with a Developer ID one, submits the app
# and the disk image to Apple's notary service, staples both, and leaves a signed
# .dmg next to the tar.xz. It needs an identity and notary credentials:
#
#   identity     the only "Developer ID Application" identity in the keychain is used
#                automatically; pass --identity NAME (or set CODESIGN_IDENTITY) when
#                there is more than one
#
#   credentials  either a notarytool keychain profile, via --keychain-profile NAME or
#                NOTARY_PROFILE=NAME, stored once with
#
#                  xcrun notarytool store-credentials NAME \
#                      --apple-id you@example.com --team-id TEAMID \
#                      --password <app-specific-password>
#
#                or NOTARY_APPLE_ID, NOTARY_TEAM_ID and NOTARY_PASSWORD in the
#                environment
#
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

APP_NAME="LyrMatcher"
BUNDLE_ID="ua.net.program.LyrMatcher"
ARCH_MODE="universal"
RUN_TESTS=1
VERSION=""
OUTPUT_DIR="dist"
NOTARIZE=0
IDENTITY="${CODESIGN_IDENTITY:-}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"

die() { printf 'package.sh: %s\n' "$1" >&2; exit 1; }
step() { printf '\n==> %s\n' "$1"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --arch)             ARCH_MODE="${2:-}"; shift 2 ;;
        --version)          VERSION="${2:-}"; shift 2 ;;
        --output)           OUTPUT_DIR="${2:-}"; shift 2 ;;
        --skip-tests)       RUN_TESTS=0; shift ;;
        --notarize)         NOTARIZE=1; shift ;;
        --identity)         IDENTITY="${2:-}"; shift 2 ;;
        --keychain-profile) NOTARY_PROFILE="${2:-}"; shift 2 ;;
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

# ---------------------------------------------------------- notarisation setup

# Everything --notarize needs is resolved before the build, so a missing identity or
# missing credentials fails in seconds instead of after a universal release build.
NOTARY_ARGS=()

if [ "$NOTARIZE" -eq 1 ]; then
    step "Checking the notarisation setup"

    xcrun --find notarytool >/dev/null 2>&1 || die "notarytool not found; Xcode 13 or later is required"
    command -v hdiutil >/dev/null || die "hdiutil not found"

    if [ -z "$IDENTITY" ]; then
        identities="$(security find-identity -v -p codesigning | grep 'Developer ID Application' || true)"
        [ -n "$identities" ] ||
            die "no 'Developer ID Application' identity in the keychain; install one or pass --identity"
        [ "$(printf '%s\n' "$identities" | wc -l | tr -d ' ')" -eq 1 ] ||
            die "more than one Developer ID identity; choose with --identity:
$identities"
        # Lines look like: 1) ABC123 "Developer ID Application: You (TEAMID)"
        IDENTITY="$(printf '%s' "$identities" | sed -n 's/.*"\(.*\)".*/\1/p')"
    fi
    [ -n "$IDENTITY" ] || die "could not determine the signing identity; pass --identity"

    if [ -n "$NOTARY_PROFILE" ]; then
        NOTARY_ARGS=(--keychain-profile "$NOTARY_PROFILE")
    elif [ -n "${NOTARY_APPLE_ID:-}" ] && [ -n "${NOTARY_TEAM_ID:-}" ] && [ -n "${NOTARY_PASSWORD:-}" ]; then
        NOTARY_ARGS=(--apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" --password "$NOTARY_PASSWORD")
    else
        die "no notary credentials; pass --keychain-profile NAME or set NOTARY_APPLE_ID, NOTARY_TEAM_ID and NOTARY_PASSWORD (see --help)"
    fi

    printf '    identity: %s\n' "$IDENTITY"
fi

# Submits one file, waits for the verdict, and prints Apple's log if it is rejected.
notarize_file() {
    local target="$1" output submission_id
    # `|| true` keeps a rejected submission on the reporting path below instead of
    # letting notarytool's exit code end the script through `set -e`.
    output="$(xcrun notarytool submit "$target" "${NOTARY_ARGS[@]}" --wait 2>&1 | tee /dev/stderr || true)"
    submission_id="$(printf '%s\n' "$output" | awk '/^ *id: / {print $2; exit}')"
    if ! printf '%s\n' "$output" | grep -q 'status: Accepted'; then
        if [ -n "$submission_id" ]; then
            printf '\n--- notary log for %s ---\n' "$submission_id" >&2
            xcrun notarytool log "$submission_id" "${NOTARY_ARGS[@]}" >&2 || true
        fi
        die "notarisation of $(basename "$target") was not accepted"
    fi
}

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
DMG="$APP_NAME-$VERSION-macos-$ARCH_LABEL.dmg"

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

if [ "$NOTARIZE" -eq 1 ]; then
    # The hardened runtime and a secure timestamp are both required by the notary service.
    step "Signing $APP with a Developer ID identity"
    SIGN_FLAGS=(--force --options runtime --timestamp --sign "$IDENTITY")
    if [ -f "Resources/$APP_NAME.entitlements" ]; then
        SIGN_FLAGS+=(--entitlements "Resources/$APP_NAME.entitlements")
        printf '    entitlements: Resources/%s.entitlements\n' "$APP_NAME"
    fi
    codesign "${SIGN_FLAGS[@]}" "$APP"
else
    # Ad-hoc signature. Not a Developer ID identity, so the archive is not notarised — see
    # the INSTALL note below for what that means for whoever unpacks it.
    codesign --force --sign - --timestamp=none "$APP"
fi
codesign --verify --strict "$APP" || die "codesign verification failed"

printf '    architectures: %s\n' "$(lipo -archs "$APP/Contents/MacOS/$APP_NAME")"

if [ "$NOTARIZE" -eq 1 ]; then
    # The app is notarised on its own, so the stapled ticket travels in the tar.xz too and
    # not only in the disk image.
    step "Notarising $APP (this waits on Apple and can take a few minutes)"
    APP_ZIP="$(mktemp -d)/$APP_NAME.zip"
    ditto -c -k --keepParent "$APP" "$APP_ZIP"
    notarize_file "$APP_ZIP"
    rm -rf "$(dirname "$APP_ZIP")"

    step "Stapling the ticket to $APP"
    xcrun stapler staple "$APP"
    spctl --assess --type exec -vv "$APP" || die "Gatekeeper rejected the notarised app"
fi

# ---------------------------------------------------------------- stage

step "Staging release contents"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

mkdir -p "$STAGE/$STAGE_NAME"
cp -R "$APP" "$STAGE/$STAGE_NAME/"
cp README.md "$STAGE/$STAGE_NAME/"
if [ -f ../../LICENSE ]; then cp ../../LICENSE "$STAGE/$STAGE_NAME/"; fi

if [ "$NOTARIZE" -eq 1 ]; then
    FIRST_LAUNCH="First launch
  This build is signed with a Developer ID identity and notarised by Apple, so it
  opens with a normal double-click."
else
    FIRST_LAUNCH="First launch
  This build is signed ad-hoc, not with a Developer ID, and it is not notarised, so
  Gatekeeper will refuse the first launch. Either:

    Right-click $APP -> Open, then confirm in the dialog, or

    xattr -dr com.apple.quarantine /Applications/$APP

  Only the first launch is affected."
fi

cat > "$STAGE/$STAGE_NAME/INSTALL.txt" <<EOF
$APP_NAME $VERSION ($ARCH_LABEL)

Install
  Drag $APP into /Applications (or anywhere else you like).

$FIRST_LAUNCH

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

# ---------------------------------------------------------------- disk image

if [ "$NOTARIZE" -eq 1 ]; then
    step "Building $DMG"
    DMG_ROOT="$STAGE/dmg"
    mkdir -p "$DMG_ROOT"
    cp -R "$APP" "$DMG_ROOT/"
    cp "$STAGE/$STAGE_NAME/README.md" "$STAGE/$STAGE_NAME/INSTALL.txt" "$DMG_ROOT/"
    if [ -f "$STAGE/$STAGE_NAME/LICENSE" ]; then cp "$STAGE/$STAGE_NAME/LICENSE" "$DMG_ROOT/"; fi
    ln -s /Applications "$DMG_ROOT/Applications"

    rm -f "$OUTPUT_DIR/$DMG"
    hdiutil create -quiet -volname "$APP_NAME $VERSION" -srcfolder "$DMG_ROOT" \
        -fs HFS+ -format UDZO -ov "$OUTPUT_DIR/$DMG"

    step "Signing and notarising $DMG"
    codesign --force --timestamp --sign "$IDENTITY" "$OUTPUT_DIR/$DMG"
    codesign --verify --strict "$OUTPUT_DIR/$DMG" || die "the disk image does not verify"
    notarize_file "$OUTPUT_DIR/$DMG"
    xcrun stapler staple "$OUTPUT_DIR/$DMG"

    # A disk image is assessed as something the user opens, against its own signature.
    spctl --assess --type open --context context:primary-signature -vv "$OUTPUT_DIR/$DMG" ||
        die "Gatekeeper rejected the notarised disk image"

    ( cd "$OUTPUT_DIR" && shasum -a 256 "$DMG" > "$DMG.sha256" )
fi

printf '\n%s\n' "Release package ready:"
printf '  %s (%s)\n' "$OUTPUT_DIR/$ARCHIVE" "$(du -h "$OUTPUT_DIR/$ARCHIVE" | cut -f1 | tr -d ' ')"
printf '  %s\n' "$(cat "$OUTPUT_DIR/$ARCHIVE.sha256")"
if [ "$NOTARIZE" -eq 1 ]; then
    printf '  %s (%s)\n' "$OUTPUT_DIR/$DMG" "$(du -h "$OUTPUT_DIR/$DMG" | cut -f1 | tr -d ' ')"
    printf '  %s\n' "$(cat "$OUTPUT_DIR/$DMG.sha256")"
fi
