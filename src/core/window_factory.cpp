#include "core/window_interface.hpp"
#include "platforms/desktop/desktop_window.hpp"
#include <memory>
#include <stdexcept>

namespace aero_boar {

std::unique_ptr<IWindow> WindowFactory::CreateWindow(Type type) {
    switch (type) {
        case Type::Desktop:
            return std::make_unique<DesktopWindow>();
        case Type::VR:
            // TODO: Implement VR window for Quest
            // return std::make_unique<VRWindow>();
            throw std::runtime_error("VR window not yet implemented");
        default:
            throw std::runtime_error("Unknown window type");
    }
}

} // namespace aero_boar
