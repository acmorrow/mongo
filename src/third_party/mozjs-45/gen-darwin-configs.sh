#/bin/sh

SDKPATH="`xcrun --sdk macosx --show-sdk-path`"
CCFLAGS="-isysroot $SDKPATH -mmacosx-version-min=10.10"
LDFLAGS="-Wl,-syslibroot,$SDKPATH  -mmacosx-version-min=10.10"
CC="`xcrun -f --sdk macosx clang`"
CXX="`xcrun -f --sdk macosx clang++`"
CFLAGS="$CCFLAGS" CXXFLAGS="$CCFLAGS" LDFLAGS="$LDFLAGS" CC=$CC CXX=$CXX ./gen-config.sh x86_64 macOS

SDKPATH="`xcrun --sdk iphonesimulator --show-sdk-path`"
CCFLAGS="-isysroot $SDKPATH -miphoneos-version-min=10.2"
LDFLAGS="-Wl,-syslibroot,$SDKPATH  -miphoneos-version-min=10.2"
CC="`xcrun -f --sdk iphonesimulator clang`"
CXX="`xcrun -f --sdk iphonesimulator clang++`"
CFLAGS="$CCFLAGS" CXXFLAGS="$CCFLAGS" LDFLAGS="$LDFLAGS" CC=$CC CXX=$CXX ./gen-config.sh x86_64 iOS-sim --disable-trace-logging --disable-profiling

SDKPATH="`xcrun --sdk appletvsimulator --show-sdk-path`"
CCFLAGS="-isysroot $SDKPATH -mtvos-version-min=10.1"
LDFLAGS="-Wl,-syslibroot,$SDKPATH  -mtvos-version-min=10.1"
CC="`xcrun -f --sdk appletvsimulator clang`"
CXX="`xcrun -f --sdk appletvsimulator clang++`"
CFLAGS="$CCFLAGS" CXXFLAGS="$CCFLAGS" LDFLAGS="$LDFLAGS" CC=$CC CXX=$CXX ./gen-config.sh x86_64 tvOS-sim --disable-trace-logging --disable-profiling

SDKPATH="`xcrun --sdk iphoneos --show-sdk-path`"
CCFLAGS="-isysroot $SDKPATH -miphoneos-version-min=10.2 -arch arm64"
LDFLAGS="-Wl,-syslibroot,$SDKPATH  -miphoneos-version-min=10.2 -arch arm64"
CC="`xcrun -f --sdk iphoneos clang`"
CXX="`xcrun -f --sdk iphoneos clang++`"
CFLAGS="$CCFLAGS" CXXFLAGS="$CCFLAGS" LDFLAGS="$LDFLAGS" CC=$CC CXX=$CXX ./gen-config.sh aarch64 iOS --disable-trace-logging --disable-profiling

SDKPATH="`xcrun --sdk appletvos --show-sdk-path`"
CCFLAGS="-isysroot $SDKPATH -mtvos-version-min=10.1 -arch arm64"
LDFLAGS="-Wl,-syslibroot,$SDKPATH  -mtvos-version-min=10.1 -arch arm64"
CC="`xcrun -f --sdk appletvos clang`"
CXX="`xcrun -f --sdk appletvos clang++`"
CFLAGS="$CCFLAGS" CXXFLAGS="$CCFLAGS" LDFLAGS="$LDFLAGS" CC=$CC CXX=$CXX ./gen-config.sh aarch64 tvOS --disable-trace-logging --disable-profiling
