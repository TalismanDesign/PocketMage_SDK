
from __future__ import annotations

import argparse
import os
import sys

from tools import symbols as symbols_mod
from tools.pm import app as app_mod
from tools.pm import gate as gate_mod
from tools.pm import overlay as overlay_mod
from tools.pm import release as release_mod
from tools.pm.app import PmError, sdk_version
from tools.pm.output import UI
from tools.pm.scaffold import scaffold

_PROG = "pm"


def _ci_mode(args) -> bool:
    if args.ci or args.no_color:
        return True
    return bool(os.environ.get("CI") or os.environ.get("NO_COLOR"))


def cmd_new(ui: UI, args) -> int:
    path = scaffold(args.name, args.dest_dir)
    ui.success(f"scaffolded app in {path}")
    ui.info(
        "replace assets/icon.png with your own 40x40 artwork to change the "
        "APPLOADER card icon"
    )
    ui.info(
        "next: cd into the app, run `pm build --pack`, then `pm check` "
        "(add --host-elf <firmware.elf> for an exact gate)"
    )
    return 0


def cmd_build(ui: UI, args) -> int:
    app_info = app_mod.query_app(args.app_dir)
    app_mod.require_toolchain(app_info.readelf)
    targets = (["clean"] if args.clean else []) + ["all"]
    if args.pack:
        targets.append("pack")
    ui.section(f"building {app_info.name}")
    code = app_mod.run_make(ui, app_info.path, targets)
    if code != 0:
        ui.error(f"build failed (make exit {code})")
        return 1
    ui.success(f"built {app_info.out}")
    return 0


def cmd_check(ui: UI, args) -> int:
    app_info = app_mod.query_app(args.app_dir)
    if not os.path.isfile(app_info.out):
        ui.error(f"{app_info.out} does not exist; run `pm build` first")
        return 1

    ui.section(f"checking {app_info.name}")
    try:
        result = gate_mod.check_app(app_info, args.readelf, args.host_elf)
    except RuntimeError as exc:
        ui.error(str(exc))
        return 1

    if result.app_main_stripped:
        ui.warning(
            "app_main is not in the shipped ELF's symbol table; entry is trusted "
            "as linker-pinned (-e app_main)"
        )
    if result.init_array_size:
        ui.warning(
            f"app has static constructors (.init_array of 0x{result.init_array_size} "
            "bytes); the host's esp_elf loader does not run them"
        )
    ui.result("entry", result.entry or "n/a")
    ui.result(
        "undefined",
        str(len(result.ok) + len(result.unconfirmed) + len(result.broken)),
    )
    if result.broken:
        ui.section("not resolvable by the host (load will fail)")
        for name in result.broken:
            ui.error(name)
    if result.unconfirmed:
        ui.section("unconfirmed (outside the curated surface; needs --host-elf)")
        for name in result.unconfirmed:
            ui.warning(name)
        if not args.host_elf:
            ui.warning("run again with --host-elf <firmware.elf> for an exact result")

    icon_issue = gate_mod.validate_icon(app_info.icon)
    if icon_issue:
        ui.error(icon_issue)
        return 1

    if result.passed:
        ui.success("every undefined symbol resolves to a host export")
        return 0
    if result.broken:
        ui.error("app dependencies are NOT satisfied by the host firmware")
        return 1
    ui.warning("app dependencies are unconfirmed without a host ELF")
    return 1


def cmd_pack(ui: UI, args) -> int:
    app_info = app_mod.query_app(args.app_dir)
    app_mod.require_toolchain(app_info.readelf)
    code = app_mod.run_make(ui, app_info.path, ["pack"])
    if code != 0:
        ui.error(f"pack failed (make exit {code})")
        return 1
    ui.success(f"packed {app_info.tar}")
    return 0


def cmd_version(ui: UI, args) -> int:
    ui.result("pocketmage_sdk_version", sdk_version())
    return 0


def cmd_overlay(ui: UI, args) -> int:
    app_info = app_mod.query_app(args.app_dir)
    if not os.path.isfile(app_info.tool):
        ui.error(f"{app_info.tool or 'the .app.tool artifact'} does not exist; "
                 "run `pm build` first")
        return 1
    app_mod.require_toolchain(app_info.readelf)

    try:
        analysis = overlay_mod.analyze(app_info.tool, app_info.readelf)
    except overlay_mod.OverlayError as exc:
        ui.error(str(exc))
        return 1

    ui.section(f"{app_info.name}: {len(analysis.functions)} functions")
    for func in analysis.functions:
        ui.result(func.name, f"0x{func.address:x} ({func.size} bytes, {func.section})")

    edges = analysis.edges()
    if edges:
        ui.section(f"{len(edges)} in-app calls")
        for caller, callee in edges:
            ui.result(caller, f"-> {callee}")

    unresolved = [c for c in analysis.calls
                  if c.via == "indirect" and c.target is None and c.symbol is None]
    if unresolved:
        ui.warning(f"{len(unresolved)} indirect call(s) with no recoverable target")

    plan = overlay_mod.cluster(analysis, frame_size=args.frame_size)
    ui.section(f"{len(plan.overlays)} overlay(s), frame {args.frame_size} B, "
               f"depth {plan.max_call_depth} (pool needs >= {plan.max_call_depth})")
    for overlay in plan.overlays:
        names = ", ".join(f.name for f in overlay.functions)
        ui.result(f"overlay {overlay.overlay_id}",
                  f"{overlay.size} B: {names}")

    if plan.oversized:
        ui.error(f"{len(plan.oversized)} unit(s) larger than one frame cannot be paged:")
        for unit in plan.oversized:
            ui.result("too big", ", ".join(unit))
        return 1

    if plan.max_call_depth > args.frame_count:
        ui.warning(f"pool depth {plan.max_call_depth} exceeds --frame-count "
                   f"{args.frame_count}; raise the pool to avoid evicting live frames")
    return 0


def cmd_exports(ui: UI, args) -> int:
    root = app_mod.sdk_root()
    sdk_symbols = os.path.join(root, "symbols.list")
    host = args.host or next(
        (c for c in gate_mod.host_exports_candidates(root) if os.path.isfile(c)),
        None,
    )
    if not host:
        ui.error(
            "no host export table found; pass --host pointing at a PocketMageOS "
            "checkout's src/ELF_SYMS/host_exports.list"
        )
        return 1

    missing, extra = gate_mod.reconcile_exports(sdk_symbols, host)
    curated = symbols_mod.read_curated_list(sdk_symbols)
    ui.section(f"curated {len(curated)} symbols vs {host}")
    if missing:
        ui.section("curated symbols the host does not export")
        for name in missing:
            ui.error(name)
        ui.error("the host must export every curated symbol; fix the OS side, not symbols.list")
        return 1
    ui.success("every curated symbol is exported by the host")
    if args.all and extra:
        ui.section("host exports outside the curated surface (advisory)")
        for name in extra:
            ui.info(name)
    return 0


def cmd_release(ui: UI, args) -> int:
    new_version = release_mod.release(app_mod.sdk_root(), args.part, args.message)
    ui.result("released", new_version)
    ui.info("updated VERSION, pocketmage_globals.cpp, library.json, and CHANGELOG.md")
    ui.info("commit the bump and tag the release when ready")
    return 0


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog=_PROG,
        description="PocketMage SDK app developer tool",
    )
    parser.add_argument("--ci", action="store_true", help="plain output, no ANSI")
    parser.add_argument("--no-color", action="store_true", help="alias for --ci")
    parser.add_argument("--theme", default="default", help="tuiro theme: default | mono | pastel")

    sub = parser.add_subparsers(dest="command", required=True)

    p_new = sub.add_parser(
        "new", help="scaffold an app from the PocketMage_App template"
    )
    p_new.add_argument("name")
    p_new.add_argument(
        "dest_dir", nargs="?", default=".",
        help="destination directory (default: cwd)",
    )

    p_build = sub.add_parser("build", help="build via make; add --pack for the delivery tar")
    p_build.add_argument("app_dir", nargs="?", default=".", help="app directory (default: cwd)")
    p_build.add_argument("--clean", action="store_true", help="make clean first")
    p_build.add_argument("--pack", action="store_true", help="also run make pack")

    p_check = sub.add_parser(
        "check", help="gate the app's undefined symbols against the host surface"
    )
    p_check.add_argument("app_dir", nargs="?", default=".", help="app directory (default: cwd)")
    p_check.add_argument("--host-elf", default=None, help="exact gate against a host firmware ELF")
    p_check.add_argument(
        "--readelf", default=None,
        help="readelf binary to use (default: app.mk probe)",
    )

    p_pack = sub.add_parser("pack", help="build the delivery tar via make pack")
    p_pack.add_argument("app_dir", nargs="?", default=".", help="app directory (default: cwd)")

    p_overlay = sub.add_parser(
        "overlay", help="plan the app's overlays (function set, call graph, frames)"
    )
    p_overlay.add_argument("app_dir", nargs="?", default=".", help="app directory (default: cwd)")
    p_overlay.add_argument(
        "--frame-size", type=int, default=overlay_mod.DEFAULT_FRAME_SIZE,
        help=f"overlay frame size in bytes (default: {overlay_mod.DEFAULT_FRAME_SIZE})",
    )
    p_overlay.add_argument(
        "--frame-count", type=int, default=3,
        help="number of frames in the pool; used to warn when the call depth "
        "exceeds it",
    )

    p_exports = sub.add_parser("exports", help="diff symbols.list against the host export table")
    p_exports.add_argument(
        "--host", default=None,
        help="path to a PocketMageOS src/ELF_SYMS/host_exports.list (default: "
        "sibling checkout)",
    )
    p_exports.add_argument(
        "--all", action="store_true",
        help="also list host exports outside the curated surface (advisory)",
    )

    p_release = sub.add_parser("release", help="bump the SDK release version")
    p_release.add_argument("part", choices=["major", "minor", "patch"])
    p_release.add_argument(
        "--message", default=None,
        help="feature line to file under the new version in CHANGELOG.md",
    )

    sub.add_parser("version", help="print pocketmage_sdk_version")

    return parser


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    ui = UI(ci_mode=_ci_mode(args), theme=args.theme)

    try:
        if args.command == "new":
            return cmd_new(ui, args)
        if args.command == "build":
            return cmd_build(ui, args)
        if args.command == "check":
            return cmd_check(ui, args)
        if args.command == "pack":
            return cmd_pack(ui, args)
        if args.command == "overlay":
            return cmd_overlay(ui, args)
        if args.command == "exports":
            return cmd_exports(ui, args)
        if args.command == "release":
            return cmd_release(ui, args)
        if args.command == "version":
            return cmd_version(ui, args)
    except PmError as exc:
        ui.error(str(exc))
        sys.stderr.write(f"\nhint: {_PROG} --help\n")
        return 1
    return 0