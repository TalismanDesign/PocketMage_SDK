"""pm: the PocketMage SDK app developer CLI.

Commands: new, build, check, pack, version, exports, release. `check` gates
the app's undefined-symbol list against the host export surface; `make check`
only prints diagnostics, `pm check` enforces. `exports` reconciles the SDK's
curated export list against a host firmware's export table, and `release`
bumps the SDK version across VERSION, pocketmage_globals.cpp, library.json,
and CHANGELOG.md.
"""