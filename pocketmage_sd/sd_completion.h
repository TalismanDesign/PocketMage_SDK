#pragma once

#include <cstdint>

namespace pm_sd_gateway {

/** Task notification slot for SD request completion. Slot 0 stays owned by caller wakeups. */
constexpr unsigned kCompletionSlot = 1;

/** Completion signaling over two independent notification slots.
 * Notify must provide clear(slot), take(slot) and give(slot).
 */
template <typename Notify> class CompletionWait {
 public:
  explicit CompletionWait(Notify &notify) : notify_(notify) {}

  /** Discard a completion left behind by an earlier request. */
  void reset() { notify_.clear(kCompletionSlot); }

  /** Consume the completion, blocking until one arrives. */
  void wait() { notify_.take(kCompletionSlot); }

  /** Mark the request complete. */
  void signal() { notify_.give(kCompletionSlot); }

 private:
  Notify &notify_;
};

} // namespace pm_sd_gateway
