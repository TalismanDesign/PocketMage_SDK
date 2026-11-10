#pragma once

#include <cstdint>

/** Tracks request completion independently of sampled decoder states.
 * The audio service mutex must protect every access across tasks.
 */
class PlaybackSession {
public:
  /** Stable app ABI values for the result of a playback request. */
  enum Result { Pending = 0, Ended = 1, Failed = 2, Cancelled = 3 };

  /** Begin a request and return its nonzero identifier, superseding the old one. */
  uint32_t request() {
    if (++requested_ == 0)
      ++requested_;
    result_ = Pending;
    return requested_;
  }

  /** Associate the decoder with the latest request. Called under the path lock. */
  void start() { active_ = requested_; }

  /** Complete the active decoder; stale or cancelled decoders cannot advance playback. */
  void finish(bool ended) {
    if (active_ == requested_ && result_ == Pending)
      result_ = ended ? Ended : Failed;
  }

  /** Reject the pending request without associating it with an older decoder. */
  void fail() {
    if (result_ == Pending)
      result_ = Failed;
  }

  /** Cancel pending playback before signalling the decoder to stop. */
  void cancel() {
    if (result_ == Pending)
      result_ = Cancelled;
  }

  /** Return the latest request identifier, or zero before the first request. */
  uint32_t id() const { return requested_; }

  /** Return a request's result; zero or superseded identifiers are cancelled. */
  Result result(uint32_t id) const {
    return id && id == requested_ ? result_ : Cancelled;
  }

private:
  uint32_t requested_ = 0;
  uint32_t active_ = 0;
  Result result_ = Cancelled;
};
