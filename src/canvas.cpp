#include "canvas.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>

#include <hyprland/src/debug/log/Logger.hpp>

#include <algorithm>
#include <cmath>

CCanvas::CCanvas() {
    // Hook render stages — we'll use this for transforms later
    m_renderStageListener = Event::bus()->m_events.render.stage.listen([this](eRenderStage stage) {
        onRenderStage(stage);
    });

    // Hook scroll wheel for zoom
    m_mouseAxisListener = Event::bus()->m_events.input.mouse.axis.listen([this](IPointer::SAxisEvent e, Event::SCallbackInfo& info) {
        onMouseAxis(e, info);
    });
}

CCanvas::~CCanvas() {}

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
    zoom   = std::clamp(newZoom, ZOOM_MIN, ZOOM_MAX);
    offset = anchorCanvas - anchorScreen / zoom;
}

// --- Render ---

void CCanvas::onRenderStage(eRenderStage stage) {
    if (!isTransformed())
        return;

    // TODO: apply GL projection transform here
    // Hyprland uses GLES — need to modify the projection matrix
    // via the renderer's current transform, not legacy glPushMatrix
}

// --- Input ---

void CCanvas::onMouseAxis(IPointer::SAxisEvent e, Event::SCallbackInfo& info) {
    if (e.delta == 0)
        return;

    double newZoom = zoom;
    if (e.delta < 0)
        newZoom *= ZOOM_STEP;
    else
        newZoom /= ZOOM_STEP;

    const auto cursorPos = g_pInputManager->getMouseCoordsInternal();
    applyZoom(newZoom, cursorPos);

    Log::logger->log(Log::INFO, "[hypr-canvas] zoom={:.3f} offset=({:.1f}, {:.1f}) cursor=({:.1f}, {:.1f})",
                     zoom, offset.x, offset.y, cursorPos.x, cursorPos.y);

    info.cancelled = true;

    auto mon = Desktop::focusState()->monitor();
    if (mon)
        g_pCompositor->scheduleFrameForMonitor(mon);
}
