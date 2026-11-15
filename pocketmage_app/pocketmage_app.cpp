#include "pocketmage_app.h"

PocketMageApp &PocketMageApp::getInstance() {
  static PocketMageApp instance;
  return instance;
}

PocketMageApp &P_APP = PocketMageApp::getInstance();

bool PocketMageApp::beginSession() {
  if (owner_ || !callbacks_.begin()) return false;
  owner_ = xTaskGetCurrentTaskHandle();
  return true;
}

int PocketMageApp::atExit(AppExitCallback callback) {
  if (owner_ != xTaskGetCurrentTaskHandle()) return -1;
  return callbacks_.add(callback);
}

int PocketMageApp::registerDestructor(AppDestructor callback, void *argument, void *dso) {
  if (owner_ != xTaskGetCurrentTaskHandle()) return -1;
  return callbacks_.addDestructor(callback, argument, dso);
}

void PocketMageApp::finalizeDestructors(void *dso) {
  if (owner_ == xTaskGetCurrentTaskHandle()) callbacks_.finalize(dso);
}

void PocketMageApp::finishSession() {
  if (owner_ != xTaskGetCurrentTaskHandle()) return;
  callbacks_.finish();
  owner_ = nullptr;
}
