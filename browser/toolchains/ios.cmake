set(IOS 1)
set(IOS_PLATFORM "iphoneos" CACHE STRING "iOS platform (iphoneos or iphonesimulator)")
set(CMAKE_OSX_SYSROOT "${IOS_PLATFORM}")
set(CMAKE_MACOSX_BUNDLE YES)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO")
