
#include "pm_app_api.h"

static int leaf_a(int x) {
  return x + pm_text_width(PM_TARGET_EINK, "x", PM_STYLE_BODY);
}

static int pong(int n);

static int ping(int n) {
  return n <= 0 ? 0 : pong(n - 1) + 1;
}

static int pong(int n) {
  return n <= 0 ? 0 : ping(n - 1) + 2;
}

static int chain3(int x) {
  return x + 3;
}

static int chain2(int x) {
  return chain3(x) * 2;
}

static int chain1(int x) {
  return chain2(x) + 1;
}

static int hub(int x) {
  return leaf_a(x) + chain1(x);
}

extern "C" int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  volatile int r = hub(4) + ping(3) + chain1(1);
  return r & 0x7f;
}
