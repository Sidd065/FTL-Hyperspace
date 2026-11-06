set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME iOS)
set(VCPKG_OSX_SYSROOT iphonesimulator)
set(VCPKG_OSX_DEPLOYMENT_TARGET 14.0)
# Libraries only: iOS can't install command-line programs (lua's interpreter and compiler)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DSKIP_INSTALL_TOOLS=ON)
# iOS has no system() (Lua's os.execute); see ios-no-system.h
set(VCPKG_C_FLAGS "-include ${CMAKE_CURRENT_LIST_DIR}/ios-no-system.h")
set(VCPKG_CXX_FLAGS "-fPIC")  # vcpkg wants both or neither
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../scripts/toolchains/arm64-ios-ftl.cmake")
