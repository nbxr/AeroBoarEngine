#include "gfx/Engine.h"
#include "core/InputManager.h"
#include "core/Log.h"
#include "VkBootstrap.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
bool query_dwm_composition_on() {
    // dwmapi can be unhealthy after TDR ("desktop composition off"). Treat a
    // failed query as "on" so we do not force a present-mode change at init.
    using Fn = HRESULT(WINAPI*)(BOOL*);
    HMODULE mod = GetModuleHandleW(L"dwmapi.dll");
    if (!mod)
        mod = LoadLibraryW(L"dwmapi.dll");
    if (!mod)
        return true;
    auto fn = reinterpret_cast<Fn>(GetProcAddress(mod, "DwmIsCompositionEnabled"));
    if (!fn)
        return true;
    BOOL on = TRUE;
    if (FAILED(fn(&on)))
        return true;
    return on != FALSE;
}
#endif

} // namespace

void gfx::Engine::apply_present_modes(vkb::SwapchainBuilder& builder) {
    bool fifo = core::InputManager::get_instance().is_remote_session();
#ifdef _WIN32
    if (!query_dwm_composition_on())
        fifo = true;
#endif
    if (!fifo)
        return; // vk-bootstrap default: MAILBOX, FIFO fallback (local console)

    // Prefer FIFO on RDP / composition-off, but never FIFO-only — some remote
    // adapters omit FIFO from the reported list even though the spec requires it.
    builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
    builder.add_fallback_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);
    builder.add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR);
}

void gfx::Engine::poll_display_composition() {
#ifdef _WIN32
    const bool on = query_dwm_composition_on();
    if (!dwm_composition_known_) {
        dwm_composition_known_ = true;
        dwm_composition_on_ = on;
        if (!on) {
            LOG_INFO("[Vulkan] DWM composition is OFF (typical on RDP). "
                     "Preferring FIFO present.");
        }
        return;
    }
    if (on == dwm_composition_on_)
        return;
    dwm_composition_on_ = on;
    LOG_INFO("[Vulkan] DWM composition " << (on ? "restored" : "disabled")
             << " — recreating swapchain");
    if (!renderer.vk.device_lost)
        recreate_swapchain();
#endif
}
