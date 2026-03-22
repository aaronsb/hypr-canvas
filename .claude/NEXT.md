# Continuation Notes — Hypr-Canvas Plugin

## What We're Building
An infinite canvas Hyprland plugin. Pan and zoom the desktop like Google Maps. Windows live at arbitrary coordinates on an infinite 2D surface.

## Current State
- Plugin scaffolded at `/home/aaron/Projects/system/hypr-canvas/`
- Builds with `make`, loads with `make reload` (hot-reload into running Hyprland)
- Plugin loads successfully — confirmed via `hyprctl plugin list`
- Zoom math ported from KWin prototype (cursor-anchored zoom, coordinate transforms)

## Immediate Blocker
**Axis events don't reach our listener.** We hook `Event::bus()->m_events.input.mouse.axis.listen(...)` but the callback never fires. libinput sees the scroll events (confirmed in Hyprland log). Possible causes:
1. Hyprland's keybind system (`mod+wheel-scroll = zoom` in hyprland.conf) consumes axis events before the event bus dispatches them
2. The input manager processes and dispatches axis events differently than button/move events
3. May need to use `createFunctionHook()` to hook the input manager's axis handler directly, like hyprexpo does with `renderWorkspace`

**Next step:** Look at how Hyprland's `InputManager` processes axis events and where in that pipeline the event bus emits. Check `src/managers/input/InputManager.cpp` in the Hyprland source. Also check how hyprexpo handles input — it hooks functions directly rather than using the event bus.

## Hyprland Config
- Config at `~/.config/hypr/hyprland.conf`
- Monitor: Samsung Odyssey G95NC, DP-7 (DP 2.1), 7680x2160@120Hz, 10-bit
- All windows float by default (windowrule block)
- hyprbars title bar plugin is broken — dragging doesn't work, disabled
- Super+left-drag moves windows, Super+right-drag resizes

## Build/Test Cycle
```bash
cd /home/aaron/Projects/system/hypr-canvas
make reload    # builds .so and hot-reloads into Hyprland
```

## Prior Work (reference only)
- KWin effect prototype: `../kwin-map/` — transform math in `mapeffect.cpp`
- KWin patches: `../kwin-build/fix-infinite-clip.patch` and `fix-input-transform.patch`
- driftwm fork: `../driftwm/` — existing infinite canvas compositor, can't drive this monitor (missing DSC in smithay)
- Design doc: `../kwin-map/DESIGN.md` — canvas model, home space concept

## Display Hardware
Samsung Odyssey G95NC — 7680x2160@120Hz, HDR capable, 10-bit. DP 2.1 via RX 7900 XTX. Connector is DP-7 (not DP-3, that was the 1.4 port).
