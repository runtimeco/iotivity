#!/bin/sh
#===============================================================================
# Author:    Pete Goodliffe
# Copyright: (c) Copyright 2009 Pete Goodliffe
# Licence:   Please feel free to use this, with attribution
#===============================================================================

#VERSION_IOS="${MAJOR_VERSION}.${MINOR_VERSION}.${RELEASE_NUMBER}.${BUILD_NUMBER}"
VERSION_IOS="0.9.0.1"


OUTDIR=$PWD/out/ios
BUILD=debug
LIBCOAP=libcoap
LIB_OCTBSTACK=liboctbstack
LIB_ROUTING=libroutingmanager
LIB_OCSRM=libocsrm
LIB_CONNECTIVITY=libconnectivity_abstraction
LIB_LOGGER=liblogger
LIBC_COMMON=libc_common

LIPO="xcrun -sdk iphoneos lipo"


VERSION_TYPE=Alpha
FRAMEWORK_NAME=iotivity-csdk
FRAMEWORK_VERSION=A
FRAMEWORK_CURRENT_VERSION=${VERSION_IOS}
FRAMEWORK_COMPATIBILITY_VERSION=${VERSION_IOS}
FRAMEWORKDIR=out/ios

FRAMEWORK_BUNDLE=$FRAMEWORKDIR/$FRAMEWORK_NAME.framework
rm -rf $FRAMEWORK_BUNDLE


# Read first command line arg - either missing (defaults to debug), debug, or release
debugarg="$1"
if [ -z "$debugarg" ] || [ "$debugarg" = "debug" ] || [ "$debugarg" = "Debug" ] || [ "$debugarg" == "DEBUG" ]; then
    BUILD=debug
elif [ "$debugarg" = "release" ] || [ "$debugarg" = "Release" ] || [ "$debugarg" = "RELEASE" ]; then
    BUILD=release
else
    echo "Invalid argument: '${debugarg}'."
    echo "Expected debug or release."
    exit 1
fi


echo "Framework: Setting up directories..."
mkdir -p $FRAMEWORK_BUNDLE
mkdir -p $FRAMEWORK_BUNDLE/Versions
mkdir -p $FRAMEWORK_BUNDLE/Versions/$FRAMEWORK_VERSION
mkdir -p $FRAMEWORK_BUNDLE/Versions/$FRAMEWORK_VERSION/Resources
mkdir -p $FRAMEWORK_BUNDLE/Versions/$FRAMEWORK_VERSION/Headers
mkdir -p $FRAMEWORK_BUNDLE/Versions/$FRAMEWORK_VERSION/Documentation

echo "Framework: Creating symlinks..."
ln  -s $FRAMEWORK_VERSION               $FRAMEWORK_BUNDLE/Versions/Current
ln  -s Versions/Current/Headers         $FRAMEWORK_BUNDLE/Headers
ln  -s Versions/Current/Resources       $FRAMEWORK_BUNDLE/Resources
ln  -s Versions/Current/Documentation   $FRAMEWORK_BUNDLE/Documentation
ln  -s Versions/Current/$FRAMEWORK_NAME $FRAMEWORK_BUNDLE/$FRAMEWORK_NAME
FRAMEWORK_INSTALL_NAME=$FRAMEWORK_BUNDLE/Versions/$FRAMEWORK_VERSION/$FRAMEWORK_NAME

lipolite()
{
   PREV="$PWD"
   cd "$1"
   ar -x "$2"
   cd "$PREV"
}


echo "Extracting libraries..."
mkdir -p $OUTDIR/objs

ARCHS="armv7 armv7s arm64 i386 x86_64"
FATFILE=""

for ARCH in $ARCHS
do
    echo "extracting $ARCH"
	mkdir -p $OUTDIR/objs/$ARCH
	lipolite $OUTDIR/objs/$ARCH "$OUTDIR/$ARCH/$BUILD/$LIBCOAP.a"
	lipolite $OUTDIR/objs/$ARCH "$OUTDIR/$ARCH/$BUILD/$LIB_OCTBSTACK.a"
    lipolite $OUTDIR/objs/$ARCH "$OUTDIR/$ARCH/$BUILD/$LIB_ROUTING.a"
    lipolite $OUTDIR/objs/$ARCH "$OUTDIR/$ARCH/$BUILD/$LIB_OCSRM.a"
    lipolite $OUTDIR/objs/$ARCH "$OUTDIR/$ARCH/$BUILD/$LIB_CONNECTIVITY.a"
    lipolite $OUTDIR/objs/$ARCH "$OUTDIR/$ARCH/$BUILD/$LIB_LOGGER.a"
	lipolite $OUTDIR/objs/$ARCH "$OUTDIR/$ARCH/$BUILD/$LIBC_COMMON.a"
	ar -r $OUTDIR/objs/$ARCH.a $OUTDIR/objs/$ARCH/*.o
done


echo "Lipoing library into $FRAMEWORK_INSTALL_NAME..."
$LIPO \
	-create \
        -arch armv7 "$OUTDIR/objs/armv7.a" \
        -arch armv7s "$OUTDIR/objs/armv7s.a" \
        -arch arm64 "$OUTDIR/objs/arm64.a" \
        -arch i386 "$OUTDIR/objs/i386.a" \
        -arch x86_64  "$OUTDIR/objs/x86_64.a" \
        -output "$FRAMEWORK_INSTALL_NAME" \
    || abort "Lipo $1 failed"

echo rm -rf objs
find $OUTDIR/objs -name "*.o" | xargs rm

echo "Framework: Copying includes..."
cp -r  resource/csdk/connectivity/api/*.h  $FRAMEWORK_BUNDLE/Headers
cp -r  resource/csdk/connectivity/common/inc/*.h  $FRAMEWORK_BUNDLE/Headers
cp -r  resource/csdk/stack/include/*.h  $FRAMEWORK_BUNDLE/Headers
cp -r  resource/csdk/logger/include/*.h  $FRAMEWORK_BUNDLE/Headers
cp -r  resource/c_common/ocrandom/include/*.h  $FRAMEWORK_BUNDLE/Headers
cp -r  resource/c_common/oic_malloc/include/*.h  $FRAMEWORK_BUNDLE/Headers
cp -r  resource/c_common/platform_features.h $FRAMEWORK_BUNDLE/Headers
cp extlibs/tinycbor/tinycbor/src/cbor.h $FRAMEWORK_BUNDLE/Headers
cp extlibs/cjson/cJSON.h $FRAMEWORK_BUNDLE/Headers

echo "Framework: Creating plist..."
cat > $FRAMEWORK_BUNDLE/Resources/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
        <key>CFBundleDevelopmentRegion</key>
        <string>English</string>
        <key>CFBundleExecutable</key>
        <string>${FRAMEWORK_NAME}</string>
        <key>CFBundleIdentifier</key>
        <string>org.iotivity</string>
        <key>CFBundleInfoDictionaryVersion</key>
        <string>6.0</string>
        <key>CFBundlePackageType</key>
        <string>FMWK</string>
        <key>CFBundleSignature</key>
        <string>????</string>
        <key>CFBundleVersion</key>
        <string>${FRAMEWORK_CURRENT_VERSION}</string>
	<key>CFBundleSupportedPlatforms</key>
	<array>
	  <string>iPhoneOS</string>
	</array>
</dict>
</plist>
EOF

echo
echo "    ================================================================="
echo "    Done"
echo
