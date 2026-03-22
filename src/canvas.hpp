#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/devices/IPointer.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

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

    // Constants
    static constexpr double ZOOM_MIN  = 0.05;
    static constexpr double ZOOM_MAX  = 1.0;
    static constexpr double ZOOM_STEP = 1.15;

    // Panning state
    bool m_panning = false;

    // Function hooks (public for hook fn access)
    CFunctionHook* m_mouseWheelHook        = nullptr;
    CFunctionHook* m_mouseButtonHook       = nullptr;
    CFunctionHook* m_mouseMovedHook        = nullptr;
    CFunctionHook* m_positionHook          = nullptr;
    CFunctionHook* m_closestValidHook     = nullptr;
    CFunctionHook* m_monitorFromCursorHook  = nullptr;
    CFunctionHook* m_monitorFromVectorHook  = nullptr;
    CFunctionHook* m_popupPositionHook      = nullptr;
    CFunctionHook* m_shouldRenderHook      = nullptr;
    CFunctionHook* m_renderPassHook        = nullptr;
    CFunctionHook* m_renderHook            = nullptr;
};

inline std::unique_ptr<CCanvas> g_pCanvas;
inline HANDLE                   PHANDLE = nullptr;
