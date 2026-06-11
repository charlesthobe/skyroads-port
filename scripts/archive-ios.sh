#!/bin/sh
# Produce an upload-ready iOS App archive (signed, icon, dSYM included).
# Usage: scripts/archive-ios.sh <TEAM_ID> [archive-path] [build-number]
set -e
TEAM="${1:?usage: archive-ios.sh <TEAM_ID> [archive-path] [build-number]}"
ARCHIVE="${2:-build-iosdev/skyroads.xcarchive}"
BUILD="${3:-$(date +%Y%m%d%H%M)}"    # monotonic by construction
echo "CFBundleVersion = $BUILD"
cmake -B build-iosdev -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
      -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DSR_IOS_TEAM="$TEAM" \
      -DSR_IOS_BUILD="$BUILD"
rm -rf "$ARCHIVE"
xcodebuild -project build-iosdev/skyroads.xcodeproj -scheme skyroads \
           -configuration Release -destination 'generic/platform=iOS' \
           -archivePath "$ARCHIVE" -allowProvisioningUpdates archive
# ensure the dSYM is inside the archive (CMake-generated projects don't
# always get it collected automatically)
mkdir -p "$ARCHIVE/dSYMs"
cp -R build-iosdev/Release-iphoneos/skyroads.app.dSYM "$ARCHIVE/dSYMs/"
APP_UUID=$(dwarfdump --uuid "$ARCHIVE/Products/Applications/skyroads.app/skyroads" | awk '{print $2}')
DSYM_UUID=$(dwarfdump --uuid "$ARCHIVE/dSYMs/skyroads.app.dSYM" | awk '{print $2}')
echo "app  UUID: $APP_UUID"
echo "dSYM UUID: $DSYM_UUID"
[ "$APP_UUID" = "$DSYM_UUID" ] || { echo "UUID MISMATCH — rebuild needed"; exit 1; }
# drop it into the Xcode Organizer
DEST="$HOME/Library/Developer/Xcode/Archives/$(date +%Y-%m-%d)"
mkdir -p "$DEST"
cp -R "$ARCHIVE" "$DEST/skyroads $(date +%H.%M).xcarchive"
echo "Archive ready in Xcode Organizer."
