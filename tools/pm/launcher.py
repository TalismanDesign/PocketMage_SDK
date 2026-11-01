"""Resilient console-script entry point for pm."""
from __future__ import annotations

import sys

from tools.pm import env as env_mod


def _non_interactive() -> bool:
    import os

    if os.environ.get("CI") or os.environ.get("NO_COLOR"):
        return True
    return not (sys.stdin.isatty() and sys.stdout.isatty())


def _repair(missing_root_package: str) -> int:
    """Diagnose and, with consent, fix an orphaned editable install."""
    sys.stderr.write(
        f"pm cannot import its own code: the '{missing_root_package}' package is not "
        "available to this Python.\n"
    )

    info = env_mod.inspect_install()
    if info.editable and info.editable_target and info.target_exists is False:
        sys.stderr.write(
            f"the pocketmage-sdk install is editable against a path that no longer "
            f"exists:\n    {info.editable_target}\n"
        )

    sdk_root = env_mod.discover_sdk_root()
    if not sdk_root:
        sys.stderr.write(
            "\nno live SDK checkout found to reinstall from. Set PM_SDK_ROOT to a "
            "PocketMage_SDK checkout, or run pm from inside one, then retry.\n"
            "Manual fix:\n"
            f"    {sys.executable} -m pip install -e /path/to/PocketMage_SDK\n"
        )
        return 1

    cmd = env_mod.pip_install_command(sdk_root, editable=True)
    printable = " ".join(cmd)
    sys.stderr.write(f"\nfound a live SDK checkout at:\n    {sdk_root}\n")
    sys.stderr.write(f"fix it by running:\n    {printable}\n")

    if _non_interactive():
        sys.stderr.write(
            "\nnon-interactive shell; not reinstalling automatically. Run the command "
            "above, then retry pm.\n"
        )
        return 1

    try:
        answer = input("\nreinstall now? [y/N] ").strip().lower()
    except (EOFError, KeyboardInterrupt):
        sys.stderr.write("\naborted.\n")
        return 1
    if answer not in ("y", "yes"):
        sys.stderr.write("aborted; run the command above when ready.\n")
        return 1

    code = env_mod.run_pip_install(sdk_root, editable=True)
    if code != 0:
        sys.stderr.write(f"\npip install failed (exit {code}); fix the error above and retry.\n")
        return code

    sys.stderr.write(
        "\nreinstalled. The current process still holds the stale import state; "
        "re-run your original pm command.\n"
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        from tools.pm.cli import main as cli_main
    except ModuleNotFoundError as exc:
        root = (exc.name or "").split(".", 1)[0]
        if root == env_mod._ROOT_PACKAGE:
            return _repair(root)
        raise
    return cli_main(argv)


if __name__ == "__main__":
    sys.exit(main())
