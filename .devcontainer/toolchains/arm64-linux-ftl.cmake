if(NOT _FTL_TOOLCHAIN)
set(_FTL_TOOLCHAIN 1)

# FTL for iPad run natively on aarch64 Linux by ftl-linux (see buildscripts/linux/):
# like arm64-android-ftl.cmake, with the host's clang and glibc instead of the NDK.
# Hyperspace is compiled against the port's libc++ (Apple's arm64 C++ ABI) so
# std::string and friends match the game's.
#
# FTL_LIBCXX: output of the port's `libcxx/build.sh linux` (include/ and lib/)
if(NOT DEFINED ENV{FTL_LIBCXX})
    message(FATAL_ERROR "FTL_LIBCXX is not set (see buildscripts/linux/README.md)")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(FTL_LIBCXX "$ENV{FTL_LIBCXX}" CACHE PATH "The port's libc++ build (include/ and lib/)")
set(_ftl_libcxx "${FTL_LIBCXX}")

# As on Android: char is signed on Apple; this-returning constructors and ARM array
# cookies (-fc++-abi=arm); Apple RTTI comparison and ctype tables.
string(CONCAT _ftl_c_flags
    " -fsigned-char"
    " -fPIC"
)
string(CONCAT _ftl_cxx_flags
    " -nostdinc++ -isystem ${_ftl_libcxx}/include/c++/v1"
    " -fc++-abi=arm"
    " -D_LIBCPP_TYPEINFO_COMPARISON_IMPLEMENTATION=3"
    " -D_LIBCPP_DARWIN_CTYPE_ABI"
)

foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_DEBUG_INIT "-DDEBUG -g")
    set(CMAKE_${lang}_FLAGS_RELEASE_INIT "-DNDEBUG -O2")
    set(CMAKE_${lang}_FLAGS_MINSIZEREL_INIT "-DNDEBUG -Os")
    set(CMAKE_${lang}_FLAGS_RELWITHDEBINFO_INIT "-DNDEBUG -O2 -g")
    string(APPEND CMAKE_${lang}_FLAGS "${_ftl_c_flags}")
endforeach()
string(APPEND CMAKE_CXX_FLAGS "${_ftl_cxx_flags}")

# The port's libc++ instead of the system's libstdc++; lld; any page size (4K, 16K, 64K).
foreach(kind EXE SHARED MODULE)
    string(APPEND CMAKE_${kind}_LINKER_FLAGS
        " -fuse-ld=lld -Wl,-z,max-page-size=65536 -nostdlib++ ${_ftl_libcxx}/lib/libc++.a ${_ftl_libcxx}/lib/libc++abi.a")
endforeach()
# Programs CMake and vcpkg build to check the compiler unwind with the system's
# libgcc_s; Hyperspace links the port's Mach-O aware libunwind (CMakeLists.txt),
# whose __ftl_find_macho_unwind only ftl-linux provides.
string(APPEND CMAKE_EXE_LINKER_FLAGS " -lpthread")

list(APPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")

endif()
