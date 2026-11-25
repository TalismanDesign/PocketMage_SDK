#include "pocketmage_audio/playback_session.h"

#include <cassert>
#include <cstdio>

int main() {
  PlaybackSession session;
  auto first = session.request();
  assert(first != 0);
  assert(session.result(first) == PlaybackSession::Pending);
  session.start();
  session.finish(true);
  assert(session.result(first) == PlaybackSession::Ended);
  session.cancel();
  assert(session.result(first) == PlaybackSession::Ended);

  auto second = session.request();
  session.start();
  auto third = session.request();
  session.finish(true);
  assert(session.result(third) == PlaybackSession::Pending);
  assert(session.result(second) == PlaybackSession::Cancelled);
  session.start();
  session.cancel();
  session.finish(true);
  assert(session.result(third) == PlaybackSession::Cancelled);

  auto failed = session.request();
  session.start();
  session.finish(false);
  assert(session.result(failed) == PlaybackSession::Failed);
  session.cancel();
  assert(session.result(failed) == PlaybackSession::Failed);
  std::puts("audio playback session tests passed");
}
