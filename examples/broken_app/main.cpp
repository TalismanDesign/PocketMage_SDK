// Negative fixture for CI: references a symbol no host exports. The app still
// links (a shared ELF leaves undefined symbols to the loader) and `pm check`
// must fail on it. Do not remove without replacing the negative gate in
// .github/workflows/sdk.yml.
extern "C" void pocketmage_missing_export(void);

extern "C" int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  pocketmage_missing_export();
  return 0;
}