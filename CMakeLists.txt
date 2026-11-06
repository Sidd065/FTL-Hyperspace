cmake_minimum_required(VERSION 3.16)
project(Hyperspace CXX)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# compile_commands.json for editors and clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Rules for generated/: hook definitions, swig runtime, version header
include(GeneratedSources)

# FTL for iPad, run by ftl-android, ftl-linux or ftl.nro (Switch, static library without vcpkg)
option(HS_IPAD_LINUX "Build for FTL for iPad run by ftl-linux on aarch64 Linux" OFF)
option(HS_IPAD_SWITCH "Build for FTL for iPad run by ftl.nro on the Switch (static library)" OFF)
option(HS_IPAD_MACOS "Build for FTL for iPad run as a Mac Catalyst app on Apple Silicon (iOS-FTL-for-macOS)" OFF)
option(HS_IPAD_IOS "Build for FTL for iPad on iOS itself, a device or the simulator" OFF)
set(HS_DEPS "" CACHE PATH "Switch: include/ and lib/ with Boost headers, Lua 5.3, minizip and zlib")
if(ANDROID OR HS_IPAD_LINUX OR HS_IPAD_SWITCH OR HS_IPAD_MACOS OR HS_IPAD_IOS)
    set(HS_IPAD_PORT ON)
else()
    set(HS_IPAD_PORT OFF)
endif()

if(APPLE)
    # Prefer static libraries for dependencies on macOS
    set(BUILD_SHARED_LIBS OFF)
endif()

if(HS_IPAD_SWITCH)
    # Only Boost's header-only parts are used (filesystem is for the crash reporter, left out)
    set(Boost_INCLUDE_DIRS "${HS_DEPS}/include")
    set(LUA_INCLUDE_DIR "${HS_DEPS}/include")
    set(LUA_LIBRARIES "${HS_DEPS}/lib/liblua.a")
    add_library(minizip::minizip INTERFACE IMPORTED)
    set_target_properties(minizip::minizip PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${HS_DEPS}/include")
    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    add_library(Boost::filesystem INTERFACE IMPORTED)
    add_library(Threads::Threads INTERFACE IMPORTED)
else()
# Let vcpkg handle Boost automatically
find_package(Boost REQUIRED COMPONENTS filesystem system)

# Let vcpkg handle Lua automatically
find_package(Lua REQUIRED)

# Let vcpkg handle minizip automatically
find_package(minizip REQUIRED)
find_package(ZLIB REQUIRED)
endif()

# Let vcpkg handle SDL3 automatically
if(NOT HS_IPAD_PORT)
    find_package(SDL3 CONFIG REQUIRED)
endif()

if(WIN32)
    find_package(OpenGL REQUIRED)
endif()

# pthread (newlib's on the Switch, given above)
if(NOT HS_IPAD_SWITCH)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif()

# SWIG
find_package(SWIG REQUIRED COMPONENTS lua)
include(UseSWIG)

# Setup SWIG *.i files
set(
    swigfiles
        lua/modules/defines.i
        lua/modules/graphics.i
        lua/modules/hyperspace.i
        lua/modules/rapidxml.i
)
set_property(SOURCE ${swigfiles} PROPERTY USE_TARGET_INCLUDE_DIRECTORIES TRUE)
set_property(SOURCE ${swigfiles} PROPERTY CPLUSPLUS ON)
if(HS_IPAD_PORT)
    set_property(SOURCE ${swigfiles} APPEND PROPERTY COMPILE_DEFINITIONS HS_ANDROID)
endif()

# Name the binary as Hyperspace.dll instead of libHyperspace.dll
set(CMAKE_SHARED_LIBRARY_PREFIX_CXX "")

if(HS_IPAD_SWITCH)
    set(HS_LIBRARY_TYPE STATIC)  # linked into ftl.nro: Horizon has no dynamic loader
else()
    set(HS_LIBRARY_TYPE SHARED)
endif()
swig_add_library(
    Hyperspace
    TYPE ${HS_LIBRARY_TYPE}
    LANGUAGE lua
    SOURCES ${swigfiles}
)

# Swig runs as part of this target and includes the generated headers
add_dependencies(Hyperspace hs_generated)

target_compile_features(Hyperspace PRIVATE cxx_std_11)

# src/game mirrors FTL's source folders; its headers are included by name, so names must be unique
file(GLOB_RECURSE GAME_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/game/*.h")
set(GAME_INCLUDE_DIRS "")
set(GAME_HEADER_NAMES "")
foreach(header ${GAME_HEADERS})
    get_filename_component(header_dir ${header} DIRECTORY)
    get_filename_component(header_name ${header} NAME)
    if(header_name IN_LIST GAME_HEADER_NAMES)
        message(FATAL_ERROR "More than one header under src/game is named ${header_name}")
    endif()
    list(APPEND GAME_HEADER_NAMES ${header_name})
    list(APPEND GAME_INCLUDE_DIRS ${header_dir})
endforeach()
list(REMOVE_DUPLICATES GAME_INCLUDE_DIRS)

target_include_directories(
    Hyperspace PRIVATE

    ${Boost_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}

    # Include roots; include from a root and not with "../"
    ${GAME_INCLUDE_DIRS}   # "CustomCrew.h", from every src/game folder
    src/zhl                # "zhl.h", "SigScan.h"
    src                    # "Global.h", "helpers/PALMemoryProtection.h", "features/discord/DiscordIntegration.h"
    lua                    # "LuaScriptInit.h"
    generated              # "Version.autogen.hpp", FTLGame<Platform>.*
    ${HS_SWIG_RUNTIME_DIR}  # "swigluarun.h", generated per build folder
    third_party/detours    # "detours.h", "hde.h"

    # Note: Don't vcpkg-fy rapidxml. It's already more than a decade old and we've customized the code to avoid its bug
    #       that leads to a compilation failure. See https://sourceforge.net/p/rapidxml/bugs/16/ for more info.
    third_party/rapidxml   # "rapidxml.hpp"
)
target_link_libraries(
    Hyperspace PRIVATE
    ${LUA_LIBRARIES}
    minizip::minizip
    ZLIB::ZLIB
    Boost::filesystem
    Threads::Threads
)
if(NOT HS_IPAD_PORT)
    target_link_libraries(Hyperspace PRIVATE SDL3::SDL3)
endif()

# Sources belong in src/
file(GLOB ROOT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
if(ROOT_SOURCES)
    message(FATAL_ERROR "Move these files from the repository root into src/game: ${ROOT_SOURCES}")
endif()

# All src/ sources
file(GLOB_RECURSE HS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Placed explicitly below
list(FILTER HS_SOURCES EXCLUDE REGEX "/src/(zhl/|main\\.cpp$)")
if(HS_IPAD_PORT)
    list(FILTER HS_SOURCES EXCLUDE REGEX "/src/features/(sdl-test|crash-detection)/")
else()
    list(FILTER HS_SOURCES EXCLUDE REGEX "/src/android/")
endif()

# Hooking engine
file(GLOB ZHL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zhl/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/zhl/*.h"
)

# Generated hook definitions for this platform
if(WIN32)
    set(FTLGAME_PLATFORM Win32)
elseif(HS_IPAD_PORT)
    set(FTLGAME_PLATFORM iOSARM64)
elseif(APPLE)
    set(FTLGAME_PLATFORM MacOSAMD64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(FTLGAME_PLATFORM ELF64)
else()
    set(FTLGAME_PLATFORM ELF32)
endif()

# Include lua sources
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lua/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/lua/*.h"
)

# Enable testing - controlled by ENABLE_TESTING option
option(ENABLE_TESTING "Enable Hyperspace automated testing" OFF)

if(ENABLE_TESTING)
    # Automatically include all test files recursively
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    )
    target_compile_definitions(Hyperspace PRIVATE HYPERSPACE_TESTING)
    message(STATUS "Hyperspace Testing: ENABLED")
else()
    set(TEST_SOURCES "")
    message(STATUS "Hyperspace Testing: DISABLED")
endif()

target_sources(
    Hyperspace PRIVATE

    # Hook definitions for this platform
    generated/FTLGame${FTLGAME_PLATFORM}.h
    generated/FTLGame${FTLGAME_PLATFORM}.cpp

    # Test sources first (if enabled) - ensures TestScript hooks into the game loop first
    ${TEST_SOURCES}

    # All src/ sources
    ${HS_SOURCES}

    # all lua files
    ${LUA_SOURCES}

    # Generated files
    ${HS_SWIG_RUNTIME}
    ${CMAKE_CURRENT_SOURCE_DIR}/generated/Version.autogen.hpp

    ${ZHL_SOURCES}

    # Calls ZHL::Init() at load on Linux/macOS, so after every hook and definition
    src/main.cpp
)

if(NOT HS_IPAD_PORT)
    # Vendored sources
    target_sources(Hyperspace PRIVATE third_party/detours/hde.cpp)

    # Enable SSE2
    target_compile_options(Hyperspace PRIVATE -msse -msse2 -mfpmath=sse)
endif()

target_compile_options(
    Hyperspace PRIVATE

    # Error on narrowing conversion
    -Werror=narrowing

    # Error on missing return
    -Werror=return-type

    # No warning for this == nullptr (can be removed once ShipSystem::GetPowerCap workaround is replaced)
    -fno-delete-null-pointer-checks

    # Warn everything
    # TODO: Check if it's meaningful
    # -Wall
)

target_compile_options(Hyperspace PRIVATE -std=c++11)
if(APPLE)
    target_compile_options(Hyperspace PRIVATE -Wno-deprecated-declarations)
endif()

target_compile_definitions(Hyperspace PRIVATE _REENTRANT)
if(HS_IPAD_PORT)
    set_target_properties(Hyperspace PROPERTIES PREFIX "lib" OUTPUT_NAME "Hyperspace")
    # Not every file that checks these includes PALPlatform.h
    target_compile_definitions(Hyperspace PRIVATE HS_ANDROID=1 HS_APPLE_GAME=1)
    if(ANDROID)
        target_link_libraries(Hyperspace PRIVATE log)
    elseif(HS_IPAD_SWITCH)
        target_compile_definitions(Hyperspace PRIVATE HS_IPAD_SWITCH=1)
    elseif(HS_IPAD_MACOS)
        target_compile_definitions(Hyperspace PRIVATE HS_IPAD_MACOS=1)
    elseif(HS_IPAD_IOS)
        target_compile_definitions(Hyperspace PRIVATE HS_IPAD_IOS=1)
    else()
        target_compile_definitions(Hyperspace PRIVATE HS_IPAD_LINUX=1)
    endif()
    if(HS_IPAD_MACOS OR HS_IPAD_IOS)
        # The game is a real Mach-O image under dyld, so the system's libc++ and unwinder
        # serve it as they are. Export only the host interface.
        set_target_properties(Hyperspace PROPERTIES PREFIX "" OUTPUT_NAME "Hyperspace.1.6.21.arm64")
        target_link_options(Hyperspace PRIVATE "-Wl,-exported_symbol,_hs_android_*")
    else()
        target_link_options(Hyperspace PRIVATE
            # Use the port's libunwind
            -unwindlib=none "${FTL_LIBCXX}/lib/libunwind.a"
            # Keep static library symbols private
            -Wl,--exclude-libs,ALL
        )
    endif()
elseif(UNIX)
    if (APPLE)
        # SDL3 loads OpenGL dynamically on macOS. 
        target_link_libraries(Hyperspace PRIVATE "-framework OpenGL")
    endif()

    # Naming the binary
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(name_suffix "amd64")
    else()
        set(name_suffix "x86")
    endif()

    if(STEAM_1_6_13_BUILD)
        target_compile_definitions(Hyperspace PRIVATE STEAM_1_6_13_BUILD)
        set_target_properties(Hyperspace PROPERTIES OUTPUT_NAME "Hyperspace.1.6.13.${name_suffix}")
    else()
        set_target_properties(Hyperspace PROPERTIES OUTPUT_NAME "Hyperspace.1.6.12.${name_suffix}")
    endif()
elseif(WIN32)
    # OpenGL, just raw OpenGL goodness
    target_link_libraries(Hyperspace PRIVATE OpenGL::GL)

    target_compile_definitions(Hyperspace PRIVATE BUILD_DLL)
endif()

# Discord Rich Presence, on by default on Windows
option(USE_DISCORD "Enable the Discord Rich Presence integration" ${WIN32})

if(USE_DISCORD)
    find_package(DiscordRpc REQUIRED) # From vcpkg & our custom find module

    target_compile_definitions(Hyperspace PRIVATE USE_DISCORD)
    target_link_libraries(Hyperspace PRIVATE DiscordRpc::DiscordRpc)
endif()

