#include <stdio.h>
#include <unistd.h>

extern "C" int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  printf("hello from pocketmage sdk app\n");
  for (int i = 0; i < 3; ++i) {
    printf("tick %d\n", i);
    sleep(1);
  }
  printf("app done\n");
  return 0;
}