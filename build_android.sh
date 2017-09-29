#! /bin/bash

scons TARGET_OS=android TARGET_ARCH=armeabi-v7a TARGET_TRANSPORT=IP,BLE LOGGING=yes VERBOSE=yes
sudo mv android/android_api/base/build/outputs/aar/iotivity-base-armeabi-v7a-release.aar ~/Desktop/vb_shared/iotivity-base-armeabi-v7a-release.aar
