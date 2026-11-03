# FTL for iPad run by ftl.nro, the Switch homebrew port (see buildscripts/switch/): the
# port's Switch toolchain (host clang/lld, devkitPro's newlib and libnx) plus, as for
# Android and Linux, the port's libc++ with Apple's arm64 C++ ABI.
#   FTL_SWITCH_TOOLCHAIN: the port's switch/toolchain.cmake
#   FTL_LIBCXX: output of the port's `libcxx/build.sh switch`
foreach(var FTL_SWITCH_TOOLCHAIN FTL_LIBCXX)
    if(NOT DEFINED ENV{${var}})
        message(FATAL_ERROR "${var} is not set (see buildscripts/switch/README.md)")
    endif()
endforeach()
include("$ENV{FTL_SWITCH_TOOLCHAIN}")
if(NOT _FTL_SWITCH_CXX)
    set(_FTL_SWITCH_CXX 1)
    string(APPEND CMAKE_C_FLAGS " -fsigned-char")
    # libc++'s headers before the C library's (the toolchain's -isystem paths)
    set(CMAKE_CXX_FLAGS "-isystem $ENV{FTL_LIBCXX}/include/c++/v1 ${CMAKE_CXX_FLAGS}")
    string(APPEND CMAKE_CXX_FLAGS " -fsigned-char -fc++-abi=arm"
        " -D_LIBCPP_TYPEINFO_COMPARISON_IMPLEMENTATION=3 -D_LIBCPP_DARWIN_CTYPE_ABI -D_GNU_SOURCE=")
endif()
