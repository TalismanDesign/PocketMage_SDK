#pragma once

#include <atomic>
#include <cstdint>

namespace pocketmage {

/** Shared inactivity clock; input producers may report activity from other tasks. */
class IdleTimer {
public:
  /** Record user activity at the supplied monotonic millisecond timestamp. */
  void activity(uint32_t now) { lastActivity_.store(now); }

  /** Return true after timeoutSeconds of inactivity. Inhibition resets the interval.
   * Nonpositive timeouts disable sleep. Poll at least once per 2^31 milliseconds.
   */
  bool poll(uint32_t now, int timeoutSeconds, bool inhibited) {
    if (inhibited || timeoutSeconds <= 0) {
      activity(now);
      return false;
    }
    uint32_t elapsed = now - lastActivity_.load();
    // A concurrent input can have a timestamp newer than this poll's sample.
    constexpr uint32_t kHalfClockRange = uint32_t{1} << 31;
    constexpr uint32_t kMillisPerSecond = 1000;
    return elapsed < kHalfClockRange &&
           elapsed >= static_cast<uint64_t>(timeoutSeconds) * kMillisPerSecond;
  }

private:
  std::atomic<uint32_t> lastActivity_{0};
};

} // namespace pocketmage
