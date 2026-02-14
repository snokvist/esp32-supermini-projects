# Task Log - t-display-s3-lvgl-demo

Use this file as a running implementation log.

## Entries

### 2026-02-14 - Project Created

- Bootstrapped from `templates/platformio-project`
- Board: `lilygo-t-display-s3`
- Env: `tdisplays3`
- Initial firmware: LED blink + serial output

### 2026-02-14 - LVGL Feasibility Demo

- Implemented LVGL 8.3 demo UI with `Arduino_GFX` flush bridge
- Added display init, draw buffer, flush callback, and LVGL driver registration
- Added button handling for `BOOT` (`GPIO0`) and `GPIO14`
- Added progress bar + uptime + refresh + CPU% metrics
- Added project docs for usage, expected behavior, and bring-up workflow

### 2026-02-14 - Rounded Tabs and Widgets Upgrade

- Reworked UI into rounded `lv_tabview` layout with three tabs: `Dash`, `Inputs`, `System`
- Added smoother card styling, rounded bars, and CPU arc widget
- Mapped `BOOT` button to tab cycling and `GPIO14` to accent theme toggling
- Added per-tab live status widgets (input LEDs/counters, refresh/cpu bars, uptime/fps labels)
- Updated README and hardware notes to document runtime interaction model

### 2026-02-14 - Layout Fit and Long-Press Controls

- Tightened card/widget geometry so all text/bars remain inside visible card area
- Reduced corner radii further and aligned tab container with margins from screen edges
- Disabled scrollability/scrollbars on tab pages and cards
- Added long-press handling (>1s):
  - `BOOT` long press = previous tab
  - `GPIO14` long press = reset counters + switch to `Inputs`
- Kept short-press behavior for existing quick actions

### 2026-02-14 - Card Area Expansion and System Counters

- Expanded tab content cards (wider and taller) to better use available screen area
- Repositioned labels/bars to keep all elements in bounds after expansion
- Added live button counters to the `System` tab for at-a-glance visibility
