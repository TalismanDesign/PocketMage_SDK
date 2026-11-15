#pragma once

#include "exit_callbacks.h"

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/** Runtime state for the one loaded ELF app; registrations belong to its entry task. */
class PocketMageApp {
public:
  /** Return the firmware-owned app runtime. */
  static PocketMageApp &getInstance();
  /** Register a callback before unload; return zero on success, -1 on rejection. */
  int atExit(AppExitCallback callback);
  /** Register a C++ destructor; return zero on success, -1 on rejection. */
  int registerDestructor(AppDestructor callback, void *argument, void *dso);
  /** Finalize matching C++ destructors once; null selects all DSOs in this app. */
  void finalizeDestructors(void *dso);
  /** Loader-only: bind a fresh session to the calling task before constructors run. */
  bool beginSession();
  /** Loader-only: invoke exit callbacks while code is resident, then reject new hooks. */
  void finishSession();

private:
  ExitCallbacks callbacks_;
  std::atomic<TaskHandle_t> owner_{nullptr};
};

/** Shared native runtime; apps use the generated pm_app_* functions. */
extern PocketMageApp &P_APP;
