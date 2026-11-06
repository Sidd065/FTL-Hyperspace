if(NOT _FTL_TOOLCHAIN)
set(_FTL_TOOLCHAIN 1)

# FTL for iPad run as a Mac Catalyst app on Apple Silicon (iOS-FTL-for-macOS, see
# buildscripts/macos/). The game uses the system's libc++, which already has the
# ABI it was built for, so unlike Linux and Android no libc++ of our own is needed.
# A plain macOS dylib loads into the Catalyst process.
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(_ftl_flags " -target arm64-apple-macos11 -fPIC")
foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_DEBUG_INIT "-DDEBUG -g")
    set(CMAKE_${lang}_FLAGS_RELEASE_INIT "-DNDEBUG -O2")
    set(CMAKE_${lang}_FLAGS_MINSIZEREL_INIT "-DNDEBUG -Os")
    set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT "-DNDEBUG -O2 -g")
    string(APPEND CMAKE_${lang}_FLAGS "${_ftl_flags}")
endforeach()

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")

endif()
