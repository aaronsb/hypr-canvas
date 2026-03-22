#include "canvas.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    g_pCanvas = std::make_unique<CCanvas>();

    return {"hypr-canvas", "Infinite canvas desktop — pan and zoom like a map", "Aaron", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_pCanvas.reset();
}
