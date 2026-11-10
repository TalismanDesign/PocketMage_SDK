#pragma once

#include <cstddef>

/** C-compatible no-argument app exit callback. */
using AppExitCallback = void (*)();
/** C++ ABI destructor callback; argument points into the live app. */
using AppDestructor = void (*)(void *);

/** Single-task callback registry; callbacks are removed before invocation. */
class ExitCallbacks {
public:
  /** C guarantees space for at least 32 atexit registrations. */
  static constexpr size_t Capacity = 32;

  /** Start an empty session; false preserves an already active session. */
  bool begin() {
    if (active_) return false;
    active_ = true;
    return true;
  }

  /** Register callback in LIFO order; return -1 for null, full or inactive sessions. */
  int add(AppExitCallback callback) {
    if (!active_ || !callback || count_ == Capacity) return -1;
    entries_[count_++] = {callback, nullptr, nullptr, nullptr};
    return 0;
  }

  /** Register destructor and its argument/DSO; return -1 on invalid/full/inactive input. */
  int addDestructor(AppDestructor callback, void *argument, void *dso) {
    if (!active_ || !callback || count_ == Capacity) return -1;
    entries_[count_++] = {nullptr, callback, argument, dso};
    return 0;
  }

  /** Run destructors for dso, or all destructors for null; leave atexit hooks pending. */
  void finalize(void *dso) { drain(false, dso); }

  /** Run all remaining callbacks once, including callbacks registered during exit. */
  void finish() {
    drain(true, nullptr);
    active_ = false;
  }

private:
  struct Entry {
    AppExitCallback callback;
    AppDestructor destructor;
    void *argument;
    void *dso;
  };

  void drain(bool all, void *dso) {
    while (active_) {
      size_t index = count_;
      while (index) {
        const Entry &entry = entries_[index - 1];
        if (all || (entry.destructor && (!dso || entry.dso == dso))) break;
        --index;
      }
      if (!index) return;
      const Entry entry = entries_[--index];
      for (size_t next = index + 1; next < count_; ++next) entries_[next - 1] = entries_[next];
      entries_[--count_] = {};
      if (entry.callback) entry.callback();
      else entry.destructor(entry.argument);
    }
  }

  Entry entries_[Capacity]{};
  size_t count_ = 0;
  bool active_ = false;
};
