#include <pm_sdk_app.h>

extern "C" {
/** Keep compiler-emitted hooks scoped to this image, never to firmware shutdown. */
int atexit(void (*callback)(void)) noexcept {
  return pm_app_at_exit(callback);
}

/** Register an Itanium ABI destructor against the OS app session.
 * See https://itanium-cxx-abi.github.io/cxx-abi/abi.html#dso-dtor-runtime-api
 */
int __cxa_atexit(void (*callback)(void *), void *argument, void *dso) noexcept {
  return pm_app_register_destructor(callback, argument, dso);
}

/** Run matching registered destructors once while this image is still resident. */
void __cxa_finalize(void *dso) noexcept {
  pm_app_finalize_destructors(dso);
}

/** Image-local identity for compiler-generated destructor registrations. */
void *__dso_handle = &__dso_handle;
}
