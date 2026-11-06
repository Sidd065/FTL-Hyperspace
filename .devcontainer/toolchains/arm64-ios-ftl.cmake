if(NOT _FTL_TOOLCHAIN)
set(_FTL_TOOLCHAIN 1)

# FTL for iPad on iOS itself, on a device (sysroot iphoneos, triplet arm64-ios-ftl) or
# in the simulator (iphonesimulator, arm64-iossim-ftl); see buildscripts/ios/. Like
# arm64-darwin-ftl: the game uses the system's libc++, so no libc++ of our own.
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0)
if(VCPKG_OSX_SYSROOT)
    set(CMAKE_OSX_SYSROOT "${VCPKG_OSX_SYSROOT}")
endif()
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_DEBUG_INIT "-DDEBUG -g")
    set(CMAKE_${lang}_FLAGS_RELEASE_INIT "-DNDEBUG -O2")
    set(CMAKE_${lang}_FLAGS_MINSIZEREL_INIT "-DNDEBUG -Os")
    set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT "-DNDEBUG -O2 -g")
    string(APPEND CMAKE_${lang}_FLAGS " -fPIC ${VCPKG_${lang}_FLAGS}")  # the triplet's, for vcpkg's ports
endforeach()

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")

endif()
