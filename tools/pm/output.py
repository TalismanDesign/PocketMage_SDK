"""Terminal output for pm.

Backed by tuiro when installed, plain text otherwise.
"""

from __future__ import annotations

import sys

try:
    from tuiro import TUI as _TUI
except ImportError:
    _TUI = None


class UI:
    """Minimal output surface used by pm, delegating to tuiro when available."""

    def __init__(self, ci_mode: bool = False, theme: str = "default") -> None:
        self.ci_mode = ci_mode
        self._tui = None
        if _TUI is not None:
            tui = _TUI(ci_mode=ci_mode or not sys.stdout.isatty(), theme=theme)
            if ci_mode:
                # tuiro's Colors.disable() does not reset palette fields
                # (they copy escape strings at import time). Strip them here.
                for attr in ("info", "success", "warning", "error", "accent", "dim", "text"):
                    setattr(tui.palette, attr, "")
            self._tui = tui

    def section(self, title: str) -> None:
        if self._tui:
            self._tui.section(title)
        else:
            print(title)
            print("-" * min(len(title), 40))

    def info(self, message: str) -> None:
        if self._tui:
            self._tui.info(message)
        else:
            print(f"[*] {message}")

    def success(self, message: str) -> None:
        if self._tui:
            self._tui.success(message)
        else:
            print(f"[OK] {message}")

    def warning(self, message: str) -> None:
        if self._tui:
            self._tui.warning(message)
        else:
            print(f"[!] {message}")

    def error(self, message: str) -> None:
        if self._tui:
            self._tui.error(message)
        else:
            print(f"[ERROR] {message}")

    def command(self, cmd: list[str]) -> None:
        if self._tui:
            self._tui.command(cmd)
        else:
            print(f"$ {' '.join(cmd)}")

    def result(self, label: str, value: str) -> None:
        if self._tui:
            self._tui.result(label, value)
        else:
            print(f"{label}: {value}")

    def table(self, rows: list[tuple[str, str]]) -> None:
        if not rows:
            return
        left = max(len(label) for label, _ in rows)
        for label, value in rows:
            print(f"{label.ljust(left)}  {value}")