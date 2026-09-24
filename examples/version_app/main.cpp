// ABI version example: prints the SDK version this app was compiled against
// and the version the host reports at load time. It uses only libc plus the
// exported pocketmage_sdk_version symbol (symbols.list) so it builds in a bare
// toolchain without the PlatformIO include set.

#include <stdio.h>

#include "pocketmage_app_version.h"
extern "C" const char pocketmage_sdk_version[];

extern "C" int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("app built against sdk %s (major %d, minor %d, patch %d)\n", POCKETMAGE_SDK_VERSION_STRING,
         POCKETMAGE_SDK_VERSION_MAJOR, POCKETMAGE_SDK_VERSION_MINOR, POCKETMAGE_SDK_VERSION_PATCH);
  printf("host firmware reports sdk %s\n", pocketmage_sdk_version);
  return 0;
}