// App-side access to the SDK version this app was linked against.
//
// tools/app.mk reads VERSION at the repo root and defines PM_SDK_VERSION (with
// major/minor/patch parts) for every app compile. The host exports the same
// string as the pocketmage_sdk_version symbol, so an app can compare its
// build-time SDK with the running host. App builds only; host builds must not
// pull it in.

#ifndef POCKETMAGE_APP_VERSION_H
#define POCKETMAGE_APP_VERSION_H

#ifndef PM_SDK_VERSION
#error "PM_SDK_VERSION is not defined; apps must be built with tools/app.mk"
#endif

#define POCKETMAGE_SDK_VERSION_STRING PM_SDK_VERSION
#define POCKETMAGE_SDK_VERSION_MAJOR PM_SDK_VERSION_MAJOR
#define POCKETMAGE_SDK_VERSION_MINOR PM_SDK_VERSION_MINOR
#define POCKETMAGE_SDK_VERSION_PATCH PM_SDK_VERSION_PATCH

#endif // POCKETMAGE_APP_VERSION_H