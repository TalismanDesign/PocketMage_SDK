---
type: guide
title: Rendering
description: "Which draw calls go where, the E-Ink buffered refresh model, and the scroll loop."
source: "https://talismandesign.github.io/PocketMage_SDK/docs/guides/rendering/"
path: /guides/rendering/
updated: 2026-10-10
okf:
  generated_by: "@docmd/plugin-okf"
  generated_at: "2026-10-10T16:00:43.167Z"
---
---
title: "Rendering"
description: "Which draw calls go where, the E-Ink buffered refresh model, and the scroll loop."
---

# Rendering

PocketMage drives two panels from one shared app API. Getting pixel messages to the right panel and knowing when a refresh actually happens is the core of any screen.

## Two targets, one `FontEngine`-dominated path

- The **OLED** (256x32 SSD1326) is a fast status text panel. Apps reach it through `pm_oled_send`, `pm_oled_sysmsg`, and `pm_oled_set_power_save`.
- The **E-Ink** (320x240 lambda) is the big document/list panel. Apps draw it with `pm_eink_pixel`, `pm_eink_rect`, and `pm_eink_refresh`, plus the [frames engine](../api/frames.md) and the [ui helpers](../api/ui.md).

Every `FontEngine` call takes an explicit `DisplayTarget`, so the same measure/draw code works for both panels and the metrics are per-target. The OLED draw color and the E-Ink draw color are independent ([font](../api/font.md)).

## The E-Ink refresh model

The E-Ink driver is **buffered**: draw calls only write to the current buffer; nothing reaches the panel until a `refresh()` (or a full `-ForceFull` update). The canonical app rhythm is:

```cpp
beginEinkScreen();                    // clear buffer, set full window
// ... FontEngine / EINK / ui draws ...
endEinkScreen("Status text");         // draw status band + refresh
```

`beginEinkScreen(preserveBg)` optionally keeps the previous buffer background (for scrolling windows) instead of clearing. `endEinkScreen` adds the status bar for you. If you manage the window yourself, call `pm_eink_refresh()` directly; `PM_REFRESH_FORCE_FULL` exists for content that must not ghost.

Status band ownership: the bottom `kEinkStatusH` (26) pixels belong to `drawStatusBar`. Content frames should stay within `kEinkContentH` (214) ([layout](../api/layout.md)).

## Text metrics and layout

- `y` in draw calls is the **baseline**, not the top.
- Fit text before drawing: `pm_text_width` + `sliceThatFits`, or `truncateWithEllipsis` for a single line; `wordWrap` for paragraphs ([layout](../api/layout.md)).
- Row pitch is `einkRowPitch(style)` = font height + line spacing; `Normal`-vs-`ForceFull` refresh choice rides on `pm_eink_get_line_spacing()`.
- Named constants (`kFrameTextPadX`, `kOledWordBaseline`, `kOledInfoBaseline`, `kGridLabelMaxW`, ...) are in [layout](../api/layout.md); use them instead of literals.

## The scroll loop

These three views share one gesture-sensing core ([touch](../api/touch.md)):

1. **Fixed window with offset**: `pm_touch_update_scroll_from_touch()`, then read `pm_touch_get_dynamic_scroll()` and draw the sliding window. Used by TXT.
2. **Step scroll**: `pm_touch_update_scroll(maxScroll, lineScroll, step)`, redraw only when it returns `true`. Used by lists.
3. **Gesture vector**: `pm_touch_get_scroll_vector()` for preview-band feedback (`t >= 1` scrolls by a page).

## OLED overlay patterns

For transient feedback use `pm_oled_sysmsg(text, ms)`; it draws a framed dialog over whatever the E-Ink is showing and dismisses itself. Long text that the OLED cannot hold goes wider than the widget: pass longer strings only when they fit the panel, or refresh a region for a running thumbnail of the E-Ink window.

## Full examples

`pm new myapp` gives the text-only skeleton. For draw-heavy apps, start from the
same template and call the E-Ink and OLED functions directly; see
[app-abi.md](../app-abi.md) for what the app process owns.
