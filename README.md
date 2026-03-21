# hypr-canvas

Infinite canvas plugin for Hyprland. Turns the desktop into a pannable, zoomable 2D surface — like Google Maps for your windows.

## Status

Not yet started. This project continues work from `../kwin-map/` which prototyped the concept as a KWin effect.

## Prior Work

### kwin-map (KWin effect prototype)

Proved the concept works. Key outcomes:

- **Zoom/pan with cursor anchoring** — Math works, feels natural. Meta+Scroll to zoom, Meta+drag to pan.
- **Per-window transforms** — `dx = wPos*(zoom-1) - offset*zoom`, `dy` same. KWin applies scale-then-translate, with window position as a separate matrix multiply.
- **Found and patched KWin renderer bug** — `Region::infinite()` was being silently clipped to output bounds in `itemrenderer_opengl.cpp`. One-line fix. Patch at `../kwin-build/fix-infinite-clip.patch`.
- **Designed input transform API** — `Effect::inputTransform(pos)` virtual method for remapping input coordinates through effects. Patch at `../kwin-build/fix-input-transform.patch`.
- **settleWindows()** — At zoom=1.0, physically move all windows by -offset so real positions match screen positions and input routing works.
- **Desktop wallpaper scrolling** — Track accumulated pan offset, apply as visual transform to desktop window.

### driftwm (existing infinite canvas compositor)

Discovered `malbiruk/driftwm` — a Rust/smithay compositor implementing the same concept. Forked to `../driftwm/`. Issues found:

- Can't drive 7680x2160@120Hz (missing DSC support in smithay)
- No HDR support (smithay HDR is still a draft PR)
- Color mapping broken when display was left in HDR mode by KDE
- "Primarily built with AI" — rendering quality is rough

### Why Hyprland

- **Mature DRM/KMS backend** — handles DSC, 10-bit, HDR metadata, weird monitors
- **C++ plugin API** — hooks into rendering and input pipelines
- **Large community** — 34.6k stars, 609 contributors, active development
- **We already know C++ compositor plugins** — KWin work transfers directly

## Architecture

The plugin needs to:

1. **Hook the rendering pipeline** — apply zoom/pan transforms to all windows
2. **Hook the input pipeline** — remap screen coordinates to canvas coordinates
3. **Manage viewport state** — zoom level, offset, animation
4. **Handle gestures** — Meta+Scroll zoom, Meta+drag pan, edge panning

## Key Files from kwin-map

| File | What to reference |
|------|-------------------|
| `../kwin-map/mapeffect.cpp` | Transform math, zoom anchoring, pan logic, settleWindows |
| `../kwin-map/DESIGN.md` | Canvas model, home space concept, coordinate system |
| `../kwin-map/BUGS.md` | KWin upstream issues (some may apply to Hyprland too) |
| `../kwin-build/fix-infinite-clip.patch` | Renderer clipping fix (check if Hyprland has same issue) |
| `../kwin-build/fix-input-transform.patch` | Input remap API design |

## Display

Samsung Odyssey G95NC — 7680x2160@120Hz, HDR, 10-bit capable. This is the target hardware.
