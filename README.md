# PocketMage SDK

PocketMage apps are small programs you drop onto the SD card. No firmware reflash, no wiring, no waiting on a full OS build. Write one against this SDK, pack it, and install it from APPLOADER.

@Ashtf 2026

## [Project Summary]

This repo is the SDK for standalone PocketMage apps. An app is an ELF file the OS loads from the SD card while the device is running. You build it on your computer, copy one archive over, and install it from the app loader. That is the whole loop.

The SDK holds the parts an app needs to fit in: the headers, the build tools, the exported symbols, the translation engine, the overlay format, and the `pm` CLI. It is deliberately small and a little strict, because an app and the OS have to agree on where the edges are. If you only want to know what PocketMage is, start with the [PDA README](https://github.com/TalismanDesign/PocketMage_PDA#readme).

The goal is to let apps feel like they belong on the device without becoming part of the firmware. That means people can write, share, and remove apps without asking me to rebuild the OS. It is still a work in progress, and the rough edges are mostly in setup and packaging rather than in the apps themselves.

## [Getting Started]

Start with the OS README if you have not used the device yet. It explains the hardware, the built-in apps, and the docs. Then come back here.

- [OS README](https://github.com/TalismanDesign/PocketMage_PDA#readme)
- [OS Docs](https://talismandesign.github.io/PocketMage_PDA/docs/)
- [SDK Docs](https://talismandesign.github.io/PocketMage_SDK/docs/)
- [Get Started](https://talismandesign.github.io/PocketMage_SDK/docs/get-started/)
- [App API Reference](https://talismandesign.github.io/PocketMage_SDK/docs/api/)
- [App walkthrough video](https://www.youtube.com/watch?v=3Ytc-3-BbMM)

Install the CLI and make a first app:

```sh
pip install .
pm new hello
cd hello
pm build --pack
pm check
```

Drop `build/hello.tar` into `/apps/` on the SD card and install it through APPLOADER. The toolchain setup is in the [toolchain guide](https://talismandesign.github.io/PocketMage_SDK/docs/get-started/toolchain/) if the build complains.

### Build and test

With Python 3 and a C11 compiler installed, run from the SDK root:

```sh
python3 -m unittest discover -s tools/pm/tests -t .
```

The tests cover the CLI, the generated exports, and the packaging checks. See the [docs](https://talismandesign.github.io/PocketMage_SDK/docs/) for the rest.

## [Where things live]

- `pocketmage_*` - the app-facing SDK modules
- `pocketmage.h` - the header an app includes first
- `tools/` - build rules, symbol generation, and the `pm` CLI
- `Docs/` - the Docs site
- `PocketMage_App` - the app template `pm new` clones, in its own repo
- `elf_overlay` - the overlay tools, also in their own repo
- `PocketMage_PDA` - the OS that loads the finished app

## [Native apps]

Not every app needs to be an ELF. If an app belongs inside the firmware, start from [APP_TEMPLATE.cpp](https://github.com/TalismanDesign/PocketMage_PDA/blob/main/Code/PocketMageOS/src/APP_TEMPLATE.cpp) and the [App API Reference](https://talismandesign.github.io/PocketMage_PDA/docs/reference/app-api). The [PDA README](https://github.com/TalismanDesign/PocketMage_PDA#readme) explains the difference between native and ELF apps.

## [Planned Features]

- More example apps
- A gentler toolchain setup
- Fewer sharp edges in packaging and install
- More APIs exposed to standalone apps

## License

All files are distributed under the Apache-2.0 license:

PocketMage SDK - the build tools and app-facing libraries for PocketMage apps.

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
