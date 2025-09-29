#include "input/input_manager.hpp"
#include "core/window_interface.hpp"
#include <iostream>
#include <algorithm>

namespace aero_boar {

InputManager::InputManager() {
}

InputManager::~InputManager() {
    Shutdown();
}

bool InputManager::Initialize(IWindow* window) {
    if (m_initialized) {
        return true;
    }

    m_window = window;
    if (!m_window) {
        std::cerr << "InputManager: Invalid window handle" << std::endl;
        return false;
    }

    // Set up default input bindings
    SetupDefaultDesktopBindings();

    // Initialize input states for all actions
    m_inputStates[InputAction::LOOK_X] = InputState{};
    m_inputStates[InputAction::LOOK_Y] = InputState{};
    m_inputStates[InputAction::MOVE_FORWARD] = InputState{};
    m_inputStates[InputAction::MOVE_BACKWARD] = InputState{};
    m_inputStates[InputAction::MOVE_LEFT] = InputState{};
    m_inputStates[InputAction::MOVE_RIGHT] = InputState{};
    m_inputStates[InputAction::MOVE_UP] = InputState{};
    m_inputStates[InputAction::MOVE_DOWN] = InputState{};
    m_inputStates[InputAction::RESET_CAMERA] = InputState{};
    m_inputStates[InputAction::EXIT_APPLICATION] = InputState{};

    m_initialized = true;
    std::cout << "InputManager initialized successfully" << std::endl;
    return true;
}

void InputManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_bindings.clear();
    m_inputStates.clear();
    m_callbacks.clear();
    m_keyStates.clear();
    m_mouseButtonStates.clear();
    
    m_window = nullptr;
    m_initialized = false;
    
    std::cout << "InputManager shutdown completed" << std::endl;
}

void InputManager::Update(float deltaTime) {
    if (!m_initialized) {
        return;
    }

    // Update input states
    UpdateInputStates();
    
    // Process input events
    ProcessInputEvents();
    
    // Reset mouse movement values at the end of the frame as they are deltas
    m_inputStates[InputAction::LOOK_X].value = 0.0f;
    m_inputStates[InputAction::LOOK_Y].value = 0.0f;
}

void InputManager::AddBinding(const InputBinding& binding) {
    // Remove existing binding for this action/device/key combination
    m_bindings.erase(
        std::remove_if(m_bindings.begin(), m_bindings.end(),
            [&](const InputBinding& b) {
                return b.action == binding.action && 
                       b.device == binding.device && 
                       b.key == binding.key;
            }),
        m_bindings.end()
    );
    
    // Add new binding
    m_bindings.push_back(binding);
    
    // Initialize input state if not exists
    if (m_inputStates.find(binding.action) == m_inputStates.end()) {
        m_inputStates[binding.action] = InputState{};
    }
}

void InputManager::RemoveBinding(InputAction action, InputDevice device, int key) {
    m_bindings.erase(
        std::remove_if(m_bindings.begin(), m_bindings.end(),
            [&](const InputBinding& b) {
                return b.action == action && 
                       b.device == device && 
                       b.key == key;
            }),
        m_bindings.end()
    );
}

void InputManager::ClearBindings() {
    m_bindings.clear();
    m_inputStates.clear();
}

bool InputManager::IsActionPressed(InputAction action) const {
    auto it = m_inputStates.find(action);
    return it != m_inputStates.end() ? it->second.isPressed : false;
}

bool InputManager::IsActionJustPressed(InputAction action) const {
    auto it = m_inputStates.find(action);
    return it != m_inputStates.end() ? (it->second.isPressed && !it->second.wasPressed) : false;
}

bool InputManager::IsActionJustReleased(InputAction action) const {
    auto it = m_inputStates.find(action);
    return it != m_inputStates.end() ? (!it->second.isPressed && it->second.wasPressed) : false;
}

float InputManager::GetActionValue(InputAction action) const {
    auto it = m_inputStates.find(action);
    return it != m_inputStates.end() ? it->second.value : 0.0f;
}

float InputManager::GetActionDelta(InputAction action) const {
    auto it = m_inputStates.find(action);
    return it != m_inputStates.end() ? it->second.delta : 0.0f;
}

void InputManager::RegisterCallback(InputAction action, InputEventCallback callback) {
    m_callbacks[action].push_back(callback);
}

void InputManager::UnregisterCallback(InputAction action) {
    m_callbacks[action].clear();
}

void InputManager::OnMouseMove(double xpos, double ypos) {
    if (!m_initialized) {
        return;
    }

    if (m_firstMouse) {
        m_lastX = xpos;
        m_lastY = ypos;
        m_firstMouse = false;
    }

    double xoffset = xpos - m_lastX;
    double yoffset = m_lastY - ypos; // Reversed since Y-coordinates go from bottom to top

    m_lastX = xpos;
    m_lastY = ypos;

    // Apply sensitivity and update LOOK_X, LOOK_Y actions
    m_inputStates[InputAction::LOOK_X].value = static_cast<float>(xoffset * m_mouseSensitivity);
    m_inputStates[InputAction::LOOK_Y].value = static_cast<float>(yoffset * m_mouseSensitivity);
}

void InputManager::OnMouseButton(int button, int action, int mods) {
    if (!m_initialized) {
        return;
    }

    bool isPressed = (action == GLFW_PRESS);
    m_mouseButtonStates[button] = isPressed;
}

void InputManager::OnScroll(double xoffset, double yoffset) {
    // Scroll wheel handling can be added here for future features
    // For now, we don't use scroll wheel as per user requirements
}

void InputManager::OnKeyPress(int key, int scancode, int action, int mods) {
    if (!m_initialized) {
        return;
    }

    bool isPressed = (action == GLFW_PRESS || action == GLFW_REPEAT);
    m_keyStates[key] = isPressed;
}

void InputManager::OnVRControllerInput(InputDevice device, int button, float value) {
    // Future VR implementation
    // This will be implemented when OpenXR integration is added
}

void InputManager::OnVRHeadTracking(const glm::mat4& headPose) {
    // Future VR implementation
    // This will be implemented when OpenXR integration is added
    m_vrHeadPose = headPose;
}

void InputManager::SetupDefaultDesktopBindings() {
    // Mouse look bindings (head movement abstraction)
    AddBinding({InputAction::LOOK_X, InputDevice::MOUSE, 0, 0.1f, true, false, 0.0f});
    AddBinding({InputAction::LOOK_Y, InputDevice::MOUSE, 1, 0.1f, true, false, 0.0f});
    
    // Keyboard movement bindings (player pose movement abstraction)
    AddBinding({InputAction::MOVE_FORWARD, InputDevice::KEYBOARD, GLFW_KEY_W, 1.0f, false, false, 0.0f});
    AddBinding({InputAction::MOVE_BACKWARD, InputDevice::KEYBOARD, GLFW_KEY_S, 1.0f, false, false, 0.0f});
    AddBinding({InputAction::MOVE_LEFT, InputDevice::KEYBOARD, GLFW_KEY_A, 1.0f, false, false, 0.0f});
    AddBinding({InputAction::MOVE_RIGHT, InputDevice::KEYBOARD, GLFW_KEY_D, 1.0f, false, false, 0.0f});
    AddBinding({InputAction::MOVE_UP, InputDevice::KEYBOARD, GLFW_KEY_SPACE, 1.0f, false, false, 0.0f});
    AddBinding({InputAction::MOVE_DOWN, InputDevice::KEYBOARD, GLFW_KEY_LEFT_SHIFT, 1.0f, false, false, 0.0f});
    
    // System action bindings
    AddBinding({InputAction::RESET_CAMERA, InputDevice::KEYBOARD, GLFW_KEY_R, 1.0f, false, false, 0.0f});
    AddBinding({InputAction::EXIT_APPLICATION, InputDevice::KEYBOARD, GLFW_KEY_ESCAPE, 1.0f, false, false, 0.0f});
}

void InputManager::SetupDefaultVRBindings() {
    // Future VR implementation
    // This will be implemented when OpenXR integration is added
    
    // VR head tracking (replaces mouse look)
    AddBinding({InputAction::LOOK_X, InputDevice::VR_HEAD_TRACKING, 0, 1.0f, true, false, 0.0f});
    AddBinding({InputAction::LOOK_Y, InputDevice::VR_HEAD_TRACKING, 1, 1.0f, true, false, 0.0f});
    
    // VR controller movement (replaces keyboard movement)
    AddBinding({InputAction::MOVE_FORWARD, InputDevice::VR_CONTROLLER_LEFT, 0, 1.0f, true, false, 0.0f});
    AddBinding({InputAction::MOVE_BACKWARD, InputDevice::VR_CONTROLLER_LEFT, 1, 1.0f, true, false, 0.0f});
    AddBinding({InputAction::MOVE_LEFT, InputDevice::VR_CONTROLLER_LEFT, 2, 1.0f, true, false, 0.0f});
    AddBinding({InputAction::MOVE_RIGHT, InputDevice::VR_CONTROLLER_LEFT, 3, 1.0f, true, false, 0.0f});
    
    // VR controller actions
    AddBinding({InputAction::VR_GRAB_LEFT, InputDevice::VR_CONTROLLER_LEFT, 0, 1.0f, false, false, 0.0f});
    AddBinding({InputAction::VR_GRAB_RIGHT, InputDevice::VR_CONTROLLER_RIGHT, 0, 1.0f, false, false, 0.0f});
    AddBinding({InputAction::VR_TRIGGER_LEFT, InputDevice::VR_CONTROLLER_LEFT, 1, 1.0f, true, false, 0.0f});
    AddBinding({InputAction::VR_TRIGGER_RIGHT, InputDevice::VR_CONTROLLER_RIGHT, 1, 1.0f, true, false, 0.0f});
}

void InputManager::UpdateInputStates() {
    // Update previous frame states
    for (auto& [action, state] : m_inputStates) {
        state.wasPressed = state.isPressed;
        state.delta = 0.0f;
    }
    
    // Process all bindings
    for (const auto& binding : m_bindings) {
        InputState& state = m_inputStates[binding.action];
        float newValue = 0.0f;
        bool newPressed = false;
        
        switch (binding.device) {
            case InputDevice::MOUSE: {
                // Mouse movement is handled directly in OnMouseMove
                // Only handle mouse buttons here
                if (!binding.isAnalog) {
                    auto it = m_mouseButtonStates.find(binding.key);
                    newPressed = (it != m_mouseButtonStates.end()) ? it->second : false;
                    newValue = newPressed ? 1.0f : 0.0f;
                } else {
                    // For analog mouse movement, keep the current value (set in OnMouseMove)
                    newValue = state.value;
                    newPressed = (std::abs(newValue) > 0.001f);
                }
                break;
            }
            
            case InputDevice::KEYBOARD: {
                auto it = m_keyStates.find(binding.key);
                newPressed = (it != m_keyStates.end()) ? it->second : false;
                newValue = newPressed ? 1.0f : 0.0f;
                break;
            }
            
            case InputDevice::VR_CONTROLLER_LEFT:
            case InputDevice::VR_CONTROLLER_RIGHT:
            case InputDevice::VR_HEAD_TRACKING: {
                // Future VR implementation
                // These will be implemented when OpenXR integration is added
                break;
            }
        }
        
        // Update state
        state.delta = newValue - state.value;
        state.value = newValue;
        state.isPressed = newPressed;
    }
}

void InputManager::ProcessInputEvents() {
    // Process callbacks for actions that changed state
    for (const auto& [action, state] : m_inputStates) {
        if (state.delta != 0.0f || state.isPressed != state.wasPressed) {
            auto callbackIt = m_callbacks.find(action);
            if (callbackIt != m_callbacks.end()) {
                for (const auto& callback : callbackIt->second) {
                    callback(action, state.value, state.isPressed);
                }
            }
        }
    }
}

} // namespace aero_boar
