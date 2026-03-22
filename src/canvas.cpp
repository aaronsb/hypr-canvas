#include "canvas.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <linux/input-event-codes.h>

static void logf(const char* fmt, ...) {
    FILE* f = fopen("/tmp/hypr-canvas.log", "a");
    if (!f)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

static void scheduleFrame() {
    auto mon = Desktop::focusState()->monitor();
    if (mon)
        g_pCompositor->scheduleFrameForMonitor(mon);
}

// --- Forward typedefs ---
using PHLMONITOR   = SP<CMonitor>;
using PHLWORKSPACE = SP<CWorkspace>;
using PHLWINDOW    = SP<Desktop::View::CWindow>;
using steady_tp    = std::chrono::steady_clock::time_point;
typedef Vector2D (*positionFn)(CPointerManager*);
typedef PHLMONITOR (*getMonitorFromCursorFn)(CCompositor*);

// --- Scroll/zoom hook ---

typedef void (*onMouseWheelFn)(CInputManager*, IPointer::SAxisEvent, SP<IPointer>);

static void hkOnMouseWheel(CInputManager* self, IPointer::SAxisEvent e, SP<IPointer> pointer) {
    const uint32_t mods = g_pInputManager->getModsFromAllKBs();

    if ((mods & HL_MODIFIER_META) && g_pCanvas && e.axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        const double scrollDelta = (e.deltaDiscrete != 0) ? (double)e.deltaDiscrete : e.delta;
        if (scrollDelta != 0) {
            double newZoom = g_pCanvas->zoom;
            if (scrollDelta < 0)
                newZoom *= CCanvas::ZOOM_STEP;
            else
                newZoom /= CCanvas::ZOOM_STEP;

            // Get raw screen coords (bypass our canvas-space hook)
            auto rawPos = (positionFn)g_pCanvas->m_positionHook->m_original;
            const auto cursorScreen = rawPos(g_pPointerManager.get());
            g_pCanvas->applyZoom(newZoom, cursorScreen);

            logf("[hypr-canvas] zoom=%.3f offset=(%.1f, %.1f)\n",
                 g_pCanvas->zoom, g_pCanvas->offset.x, g_pCanvas->offset.y);

            scheduleFrame();
            return;
        }
    }

    auto original = (onMouseWheelFn)g_pCanvas->m_mouseWheelHook->m_original;
    original(self, e, pointer);
}

// --- Mouse button hook (pan start/stop) ---

typedef void (*onMouseButtonFn)(CInputManager*, IPointer::SButtonEvent);

static void hkOnMouseButton(CInputManager* self, IPointer::SButtonEvent e) {
    if (g_pCanvas && g_pCanvas->isTransformed() && e.button == BTN_LEFT) {
        const uint32_t mods = g_pInputManager->getModsFromAllKBs();
        if (mods & HL_MODIFIER_META) {
            if (e.state == WL_POINTER_BUTTON_STATE_PRESSED) {
                // Only pan if clicking on empty desktop, not on a window
                const auto coords = g_pInputManager->getMouseCoordsInternal();
                using namespace Desktop::View;
                auto windowUnder = g_pCompositor->vectorToWindowUnified(coords, RESERVED_EXTENTS | INPUT_EXTENTS | ALLOW_FLOATING);
                if (!windowUnder) {
                    g_pCanvas->m_panning = true;
                    logf("[hypr-canvas] pan start\n");
                    return;
                }
            }
        }
    }

    // Release panning on button release
    if (g_pCanvas && g_pCanvas->m_panning && e.button == BTN_LEFT && e.state == WL_POINTER_BUTTON_STATE_RELEASED) {
        g_pCanvas->m_panning = false;
        logf("[hypr-canvas] pan stop\n");
        return;
    }

    auto original = (onMouseButtonFn)g_pCanvas->m_mouseButtonHook->m_original;
    original(self, e);
}

// --- Mouse move hook (pan drag) ---

typedef void (*onMouseMovedFn)(CInputManager*, IPointer::SMotionEvent);

static void hkOnMouseMoved(CInputManager* self, IPointer::SMotionEvent e) {
    if (g_pCanvas && g_pCanvas->m_panning) {
        g_pCanvas->offset.x -= e.delta.x / g_pCanvas->zoom;
        g_pCanvas->offset.y -= e.delta.y / g_pCanvas->zoom;
        scheduleFrame();
        return;
    }

    auto original = (onMouseMovedFn)g_pCanvas->m_mouseMovedHook->m_original;
    original(self, e);
}



// --- Cursor coordinate hook ---
// position() has 16 call sites including mouseMoveUnified (window finding + surface-local coords)

static Vector2D hkPosition(CPointerManager* self) {
    auto original = (positionFn)g_pCanvas->m_positionHook->m_original;
    Vector2D raw = original(self);

    if (g_pCanvas && g_pCanvas->isTransformed() && !g_pCanvas->m_panning) {
        return g_pCanvas->screenToCanvas(raw);
    }
    return raw;
}

// --- Pointer clamping hook ---
// closestValid() clamps m_pointerPos to monitor layout, preventing the cursor
// from reaching canvas positions outside the physical monitor. When zoomed,
// disable clamping so m_pointerPos can hold any canvas-space position.

typedef Vector2D (*closestValidFn)(CPointerManager*, const Vector2D&);

static Vector2D hkClosestValid(CPointerManager* self, const Vector2D& pos) {
    if (g_pCanvas && g_pCanvas->isTransformed())
        return pos;

    auto original = (closestValidFn)g_pCanvas->m_closestValidHook->m_original;
    return original(self, pos);
}

// --- Monitor detection hook ---
// getMonitorFromCursor uses position() which now returns canvas coords.
// Canvas coords may be outside all monitors, so we bypass and use physical coords.

static PHLMONITOR hkGetMonitorFromCursor(CCompositor* self) {
    if (g_pCanvas && g_pCanvas->isTransformed()) {
        // Just return the focused monitor — cursor is always physically on it
        return Desktop::focusState()->monitor();
    }

    auto original = (getMonitorFromCursorFn)g_pCanvas->m_monitorFromCursorHook->m_original;
    return original(self);
}

// --- Visibility hook ---

typedef bool (*shouldRenderFn)(CHyprRenderer*, PHLWINDOW, PHLMONITOR);

static bool hkShouldRenderWindow(CHyprRenderer* self, PHLWINDOW pWindow, PHLMONITOR pMonitor) {
    auto original = (shouldRenderFn)g_pCanvas->m_shouldRenderHook->m_original;

    // When zoomed out, render all windows — the zoom transform will place them correctly
    if (g_pCanvas && g_pCanvas->isTransformed())
        return true;

    return original(self, pWindow, pMonitor);
}

// --- Render pass damage hook ---

typedef CRegion (*renderPassRenderFn)(CRenderPass*, const CRegion&);

static CRegion hkRenderPassRender(CRenderPass* self, const CRegion& damage) {
    auto original = (renderPassRenderFn)g_pCanvas->m_renderPassHook->m_original;

    if (g_pCanvas && g_pCanvas->isTransformed()) {
        // Expand damage to cover the full virtual viewport so no elements get culled
        auto mon = Desktop::focusState()->monitor();
        if (mon) {
            const auto monSize = mon->m_transformedSize;
            CRegion expanded;
            expanded.add(CBox{
                g_pCanvas->offset.x, g_pCanvas->offset.y,
                monSize.x / g_pCanvas->zoom, monSize.y / g_pCanvas->zoom
            });
            return original(self, expanded);
        }
    }

    return original(self, damage);
}

// --- Render hook ---

typedef void (*renderAllClientsFn)(CHyprRenderer*, PHLMONITOR, PHLWORKSPACE, const steady_tp&, const Vector2D&, const float&);

static void hkRenderAllClientsForWorkspace(CHyprRenderer* self, PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace,
                                           const steady_tp& now, const Vector2D& translate, const float& scale) {
    auto original = (renderAllClientsFn)g_pCanvas->m_renderHook->m_original;

    if (g_pCanvas && g_pCanvas->isTransformed()) {
        g_pHyprRenderer->damageMonitor(pMonitor);

        // Clear the full framebuffer so areas outside the wallpaper don't show stale pixels
        g_pHyprOpenGL->clear(CHyprColor(0.1, 0.1, 0.1, 1.0));

        // Disable render pass simplification and expand damage/clip to full virtual viewport
        const auto monSize = pMonitor->m_transformedSize;
        CBox virtualViewport = {
            g_pCanvas->offset.x, g_pCanvas->offset.y,
            monSize.x / g_pCanvas->zoom, monSize.y / g_pCanvas->zoom
        };
        g_pHyprOpenGL->m_renderData.damage.add(virtualViewport);
        g_pHyprOpenGL->m_renderData.clipBox = {};  // no clip
        g_pHyprOpenGL->m_renderData.noSimplify = true;

        // SRenderModifData applies translate then scale: (W + translate) * scale
        // We want (W - offset) * zoom, so translate = -offset
        Vector2D canvasTranslate = {-g_pCanvas->offset.x, -g_pCanvas->offset.y};
        float    canvasScale     = (float)g_pCanvas->zoom;
        original(self, pMonitor, pWorkspace, now, canvasTranslate, canvasScale);
    } else {
        original(self, pMonitor, pWorkspace, now, translate, scale);
    }
}

// --- Monitor-from-vector hook ---
// getMonitorFromVector is used by vectorToWindowUnified and many others.
// Canvas coords outside the physical monitor return null, blocking window lookup.

typedef PHLMONITOR (*getMonitorFromVectorFn)(CCompositor*, const Vector2D&);

static PHLMONITOR hkGetMonitorFromVector(CCompositor* self, const Vector2D& pos) {
    auto original = (getMonitorFromVectorFn)g_pCanvas->m_monitorFromVectorHook->m_original;

    if (g_pCanvas && g_pCanvas->isTransformed()) {
        auto result = original(self, pos);
        if (!result)
            return Desktop::focusState()->monitor();
        return result;
    }

    return original(self, pos);
}

// --- Popup positioning hook ---
// When zoomed, expand the constraint box so popups aren't clamped to the physical monitor

typedef void (*applyPositioningFn)(CXDGPopupResource*, const CBox&, const Vector2D&);

static void hkApplyPositioning(CXDGPopupResource* self, const CBox& availableBox, const Vector2D& t1coord) {
    auto original = (applyPositioningFn)g_pCanvas->m_popupPositionHook->m_original;

    if (g_pCanvas && g_pCanvas->isTransformed()) {
        // Use a very large constraint box so popups aren't clamped
        CBox expanded = {-100000, -100000, 200000, 200000};
        original(self, expanded, t1coord);
        return;
    }

    original(self, availableBox, t1coord);
}

// --- Constructor / Destructor ---

static CFunctionHook* hookByName(const std::string& name, void* dest) {
    auto fns = HyprlandAPI::findFunctionsByName(PHANDLE, name);
    logf("[hypr-canvas] %s: %zu matches\n", name.c_str(), fns.size());
    if (fns.empty())
        return nullptr;
    auto hook = HyprlandAPI::createFunctionHook(PHANDLE, fns[0].address, dest);
    if (hook && hook->hook())
        logf("[hypr-canvas] hooked %s\n", name.c_str());
    return hook;
}

CCanvas::CCanvas() {
    m_mouseWheelHook  = hookByName("onMouseWheel", (void*)&hkOnMouseWheel);
    m_mouseButtonHook = hookByName("onMouseButton", (void*)&hkOnMouseButton);
    m_mouseMovedHook  = hookByName("onMouseMoved", (void*)&hkOnMouseMoved);
    // Hook position() — 16 call sites including mouseMoveUnified for window finding + surface-local
    {
        auto fns = HyprlandAPI::findFunctionsByName(PHANDLE, std::string("position"));
        for (auto& fn : fns) {
            if (fn.demangled.find("CPointerManager") != std::string::npos) {
                logf("[hypr-canvas] found CPointerManager::position() @ %p\n", fn.address);
                m_positionHook = HyprlandAPI::createFunctionHook(PHANDLE, fn.address, (void*)&hkPosition);
                if (m_positionHook) m_positionHook->hook();
                break;
            }
        }
    }
    m_closestValidHook      = hookByName("closestValid", (void*)&hkClosestValid);
    m_monitorFromCursorHook = hookByName("getMonitorFromCursor", (void*)&hkGetMonitorFromCursor);
    m_monitorFromVectorHook = hookByName("getMonitorFromVector", (void*)&hkGetMonitorFromVector);
    m_popupPositionHook     = hookByName("applyPositioning", (void*)&hkApplyPositioning);
    // Hook shouldRenderWindow to disable culling when zoomed out
    {
        auto fns = HyprlandAPI::findFunctionsByName(PHANDLE, std::string("shouldRenderWindow"));
        for (auto& fn : fns) {
            // Match the 2-arg overload (PHLWINDOW, PHLMONITOR)
            if (fn.demangled.find("CMonitor") != std::string::npos) {
                logf("[hypr-canvas] found shouldRenderWindow(window,monitor) @ %p\n", fn.address);
                m_shouldRenderHook = HyprlandAPI::createFunctionHook(PHANDLE, fn.address, (void*)&hkShouldRenderWindow);
                if (m_shouldRenderHook) m_shouldRenderHook->hook();
                break;
            }
        }
    }
    // Hook render pass to expand damage region when zoomed
    {
        auto fns = HyprlandAPI::findFunctionsByName(PHANDLE, std::string("render"));
        for (auto& fn : fns) {
            if (fn.demangled.find("CRenderPass") != std::string::npos) {
                logf("[hypr-canvas] found CRenderPass::render @ %p\n", fn.address);
                m_renderPassHook = HyprlandAPI::createFunctionHook(PHANDLE, fn.address, (void*)&hkRenderPassRender);
                if (m_renderPassHook) m_renderPassHook->hook();
                break;
            }
        }
    }
    m_renderHook      = hookByName("renderAllClientsForWorkspace", (void*)&hkRenderAllClientsForWorkspace);
    logf("[hypr-canvas] initialized\n");
}

CCanvas::~CCanvas() {
    if (m_mouseWheelHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_mouseWheelHook);
    if (m_mouseButtonHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_mouseButtonHook);
    if (m_mouseMovedHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_mouseMovedHook);
    if (m_positionHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_positionHook);
    if (m_closestValidHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_closestValidHook);
    if (m_monitorFromCursorHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_monitorFromCursorHook);
    if (m_monitorFromVectorHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_monitorFromVectorHook);
    if (m_popupPositionHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_popupPositionHook);
    if (m_shouldRenderHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_shouldRenderHook);
    if (m_renderPassHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_renderPassHook);
    if (m_renderHook)
        HyprlandAPI::removeFunctionHook(PHANDLE, m_renderHook);
}

// --- Coordinate transforms ---

Vector2D CCanvas::screenToCanvas(const Vector2D& screen) const {
    return offset + screen / zoom;
}

Vector2D CCanvas::canvasToScreen(const Vector2D& canvas) const {
    return (canvas - offset) * zoom;
}

bool CCanvas::isTransformed() const {
    return std::abs(zoom - 1.0) > 0.001
        || std::abs(offset.x) > 0.5
        || std::abs(offset.y) > 0.5;
}

void CCanvas::applyZoom(double newZoom, const Vector2D& anchorScreen) {
    const Vector2D anchorCanvas = screenToCanvas(anchorScreen);
    zoom = std::clamp(newZoom, ZOOM_MIN, ZOOM_MAX);
    offset = anchorCanvas - anchorScreen / zoom;
}
