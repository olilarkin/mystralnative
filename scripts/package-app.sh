#!/usr/bin/env bash
#
# package-app.sh — Create a macOS .app bundle from a compiled MystralNative binary
#
# Usage:
#   ./scripts/package-app.sh --binary build/sponza --name Sponza --output dist
#   ./scripts/package-app.sh --binary build/sponza --name Sponza --team-id ABC123XYZ
#   APPLE_TEAM_ID=ABC123XYZ ./scripts/package-app.sh --binary build/sponza --name Sponza
#
# Code Signing:
#   By default, uses ad-hoc signing (works on your own machine, triggers Gatekeeper).
#   For distribution, provide an Apple Developer Team ID:
#     --team-id <id>           Apple Developer Team ID (or set APPLE_TEAM_ID env var)
#     --sign-identity <id>     Full signing identity (or set APPLE_SIGNING_IDENTITY env var)
#                              If only --team-id is given, identity is auto-detected from keychain.
#     --entitlements <plist>   Optional entitlements file
#     --no-sign                Skip code signing entirely
#
set -euo pipefail

# Defaults
BINARY_PATH=""
APP_NAME=""
OUTPUT_DIR="."
BUNDLE_ID=""
VERSION="1.0.0"
ICON_PATH=""
TEAM_ID="${APPLE_TEAM_ID:-}"
SIGN_IDENTITY="${APPLE_SIGNING_IDENTITY:-}"
ENTITLEMENTS=""
DO_SIGN="true"

usage() {
    echo "Usage: $0 --binary <path> --name <name> [options]"
    echo ""
    echo "Required:"
    echo "  --binary <path>         Path to compiled MystralNative binary"
    echo "  --name <name>           App display name (e.g. 'Sponza')"
    echo ""
    echo "Options:"
    echo "  --output <dir>          Output directory (default: .)"
    echo "  --bundle-id <id>        Bundle identifier (default: com.mystralengine.<name>)"
    echo "  --version <ver>         Version string (default: 1.0.0)"
    echo "  --icon <icns>           Path to .icns icon file"
    echo ""
    echo "Code Signing:"
    echo "  --team-id <id>          Apple Developer Team ID (or APPLE_TEAM_ID env var)"
    echo "  --sign-identity <id>    Signing identity (or APPLE_SIGNING_IDENTITY env var)"
    echo "  --entitlements <plist>  Entitlements file for signing"
    echo "  --no-sign               Skip code signing"
    echo ""
    echo "Environment Variables:"
    echo "  APPLE_TEAM_ID           Apple Developer Team ID"
    echo "  APPLE_SIGNING_IDENTITY  Full signing identity string"
    exit 1
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --binary)       BINARY_PATH="$2"; shift 2 ;;
        --name)         APP_NAME="$2"; shift 2 ;;
        --output)       OUTPUT_DIR="$2"; shift 2 ;;
        --bundle-id)    BUNDLE_ID="$2"; shift 2 ;;
        --version)      VERSION="$2"; shift 2 ;;
        --icon)         ICON_PATH="$2"; shift 2 ;;
        --team-id)      TEAM_ID="$2"; shift 2 ;;
        --sign-identity) SIGN_IDENTITY="$2"; shift 2 ;;
        --entitlements) ENTITLEMENTS="$2"; shift 2 ;;
        --no-sign)      DO_SIGN="false"; shift ;;
        --help|-h)      usage ;;
        *)              echo "Unknown option: $1"; usage ;;
    esac
done

# Validate required args
if [ -z "$BINARY_PATH" ] || [ -z "$APP_NAME" ]; then
    echo "Error: --binary and --name are required"
    usage
fi

if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Binary not found: $BINARY_PATH"
    exit 1
fi

# Derive lowercase name for executable and bundle ID
APP_NAME_LOWER=$(echo "$APP_NAME" | tr '[:upper:]' '[:lower:]')

if [ -z "$BUNDLE_ID" ]; then
    BUNDLE_ID="com.mystralengine.${APP_NAME_LOWER}"
fi

# Resolve signing identity
resolve_signing_identity() {
    # If explicit identity is set, use it
    if [ -n "$SIGN_IDENTITY" ]; then
        echo "$SIGN_IDENTITY"
        return
    fi

    # If team ID is set, find the Developer ID Application certificate
    if [ -n "$TEAM_ID" ]; then
        # Search keychain for Developer ID Application certificate matching team ID
        local found_identity
        found_identity=$(security find-identity -v -p codesigning 2>/dev/null | \
            grep "Developer ID Application" | \
            grep "$TEAM_ID" | \
            head -1 | \
            sed 's/.*"\(.*\)".*/\1/' || true)

        if [ -n "$found_identity" ]; then
            echo "$found_identity"
            return
        fi

        # Try Apple Development certificate (for local testing)
        found_identity=$(security find-identity -v -p codesigning 2>/dev/null | \
            grep "Apple Development" | \
            grep "$TEAM_ID" | \
            head -1 | \
            sed 's/.*"\(.*\)".*/\1/' || true)

        if [ -n "$found_identity" ]; then
            echo "$found_identity"
            return
        fi

        echo "Warning: No signing certificate found for team ID: $TEAM_ID" >&2
        echo "Falling back to ad-hoc signing" >&2
    fi

    # Fallback: ad-hoc
    echo "-"
}

# Create .app structure
APP_DIR="${OUTPUT_DIR}/${APP_NAME}.app"
rm -rf "$APP_DIR"
mkdir -p "${APP_DIR}/Contents/MacOS"
mkdir -p "${APP_DIR}/Contents/Resources"

echo "Creating ${APP_NAME}.app..."

# Copy binary
cp "$BINARY_PATH" "${APP_DIR}/Contents/MacOS/${APP_NAME_LOWER}"
chmod +x "${APP_DIR}/Contents/MacOS/${APP_NAME_LOWER}"

# Generate Info.plist
ICON_ENTRY=""
if [ -n "$ICON_PATH" ] && [ -f "$ICON_PATH" ]; then
    ICON_ENTRY="    <key>CFBundleIconFile</key>
    <string>AppIcon</string>"
fi

cat > "${APP_DIR}/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME_LOWER}</string>
    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleDisplayName</key>
    <string>${APP_NAME}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleSignature</key>
    <string>????</string>
${ICON_ENTRY}
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>NSSupportsAutomaticGraphicsSwitching</key>
    <true/>
</dict>
</plist>
PLIST

echo "  Info.plist generated (bundle-id: ${BUNDLE_ID})"

# Copy icon if provided
if [ -n "$ICON_PATH" ] && [ -f "$ICON_PATH" ]; then
    cp "$ICON_PATH" "${APP_DIR}/Contents/Resources/AppIcon.icns"
    echo "  Icon: ${ICON_PATH}"
fi

# Check for dynamic library dependencies that need bundling
# (SDL3 is typically statically linked, but handle dynamic libs if present)
DYLIBS=$(otool -L "${APP_DIR}/Contents/MacOS/${APP_NAME_LOWER}" 2>/dev/null | \
    tail -n +2 | \
    awk '{print $1}' | \
    grep -v '/usr/lib/' | \
    grep -v '/System/' | \
    grep -v '@rpath' | \
    grep -v '@executable_path' || true)

if [ -n "$DYLIBS" ]; then
    mkdir -p "${APP_DIR}/Contents/Frameworks"
    echo "  Bundling dynamic libraries:"
    while IFS= read -r dylib; do
        if [ -f "$dylib" ]; then
            echo "    - $(basename "$dylib")"
            cp "$dylib" "${APP_DIR}/Contents/Frameworks/"
            install_name_tool -change "$dylib" \
                "@executable_path/../Frameworks/$(basename "$dylib")" \
                "${APP_DIR}/Contents/MacOS/${APP_NAME_LOWER}"
        fi
    done <<< "$DYLIBS"
fi

# Code signing
if [ "$DO_SIGN" = "true" ]; then
    IDENTITY=$(resolve_signing_identity)

    if [ "$IDENTITY" = "-" ]; then
        echo "  Signing: ad-hoc"
        codesign --force --deep --sign - "$APP_DIR"
    else
        echo "  Signing: ${IDENTITY}"
        SIGN_ARGS=(--force --deep --sign "$IDENTITY" --options runtime)

        if [ -n "$ENTITLEMENTS" ] && [ -f "$ENTITLEMENTS" ]; then
            SIGN_ARGS+=(--entitlements "$ENTITLEMENTS")
            echo "  Entitlements: ${ENTITLEMENTS}"
        fi

        codesign "${SIGN_ARGS[@]}" "$APP_DIR"
    fi

    # Verify signature
    codesign --verify --deep --strict "$APP_DIR" 2>/dev/null && \
        echo "  Signature verified" || \
        echo "  Warning: Signature verification failed"
else
    echo "  Signing: skipped"
fi

# Print summary
APP_SIZE=$(du -sh "$APP_DIR" | awk '{print $1}')
echo ""
echo "Created: ${APP_DIR} (${APP_SIZE})"
