#!/bin/bash

TARGET_OS=darwin
RELEASE=false
WITH_TCP=false

ARCHS="x86_64"

# Read first command line arg - either missing (defaults to debug), debug, or release
debugarg="$1"

if [ -z "$debugarg" ] || [ "$debugarg" = "debug" ] || [ "$debugarg" = "Debug" ] || [ "$debugarg" == "DEBUG" ]; then
	RELEASE=false
elif [ "$debugarg" = "release" ] || [ "$debugarg" = "Release" ] || [ "$debugarg" = "RELEASE" ]; then
	RELEASE=true
else
	echo "Invalid argument: '${debugarg}'."
	echo "Expected debug or release."
	exit 1
fi


for ARCH in $ARCHS
do
	scons TARGET_OS=$TARGET_OS TARGET_ARCH=$ARCH RELEASE=$RELEASE WITH_TCP=$WITH_TCP

	if [ $? -ne 0 ]
	then
		echo "Failed building TARGET_OS=$TARGET_OS TARGET_ARCH=$ARCH" >&2
		exit 1
	fi
done
