#include "pocketmage_sys/idle_timer.h"

#include <cassert>
#include <climits>
#include <cstdio>

int main() {
  pocketmage::IdleTimer timer;
  timer.activity(100);
  assert(!timer.poll(10099, 10, false));
  assert(timer.poll(10100, 10, false));
  timer.activity(10100);
  assert(!timer.poll(10101, 10, false));

  assert(!timer.poll(50000, 10, true));
  assert(!timer.poll(59999, 10, false));
  assert(timer.poll(60000, 10, false));

  timer.activity(UINT32_MAX - 499);
  assert(!timer.poll(499, 1, false));
  assert(timer.poll(500, 1, false));
  timer.activity(1001);
  assert(!timer.poll(1000, 1, false));
  assert(!timer.poll(50000, 0, false));
  assert(!timer.poll(50000, -1, false));
  assert(!timer.poll(50000, INT_MAX, false));
  std::puts("idle timer tests passed");
}
