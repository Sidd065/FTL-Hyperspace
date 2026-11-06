/* Included first in every C file vcpkg builds for iOS (arm64-ios-ftl, arm64-iossim-ftl):
 * iOS has no system(), which Lua's os.execute calls. Without a shell it fails, as
 * system() does where there is none. */
#include <stdlib.h>
#define system(cmd) ((void)(cmd), -1)
