extern "C" void pocketmage_missing_export(void);

extern "C" int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  pocketmage_missing_export();
  return 0;
}