#pragma once

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprutils/signal/Signal.hpp>

class CCanvas {
  public:
    CCanvas();
    ~CCanvas();

    // Viewport state
    double   zoom   = 1.0;
    Vector2D offset = {0, 0};

    // Coordinate transforms
    Vector2D screenToCanvas(const Vector2D& screen) const;
    Vector2D canvasToScreen(const Vector2D& canvas) const;
    bool     isTransformed() const;

    // Apply zoom with cursor anchoring
    void applyZoom(double newZoom, const Vector2D& anchorScreen);

  private:
    // Event listeners — must stay alive to keep hooks registered
    CHyprSignalListener m_renderStageListener;
    CHyprSignalListener m_mouseAxisListener;

    // Render hooks
    void onRenderStage(eRenderStage stage);

    // Input hooks
    void onMouseAxis(IPointer::SAxisEvent e, Event::SCallbackInfo& info);

    // Constants
    static constexpr double ZOOM_MIN  = 0.05;
    static constexpr double ZOOM_MAX  = 1.0;
    static constexpr double ZOOM_STEP = 1.15;
};

inline std::unique_ptr<CCanvas> g_pCanvas;
inline HANDLE                   PHANDLE = nullptr;
