#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>
#include <functional>
#include <vector>
#include <memory>

namespace aero_boar {

// Forward declarations
class InputManager;
class IWindow;

// Input action types for abstraction
enum class InputAction {
    // Head movement (mouse → VR head tracking)
    LOOK_X,
    LOOK_Y,
    
    // Player movement (keyboard → VR controller movement)
    MOVE_FORWARD,
    MOVE_BACKWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN,
    
    // System actions
    RESET_CAMERA,
    EXIT_APPLICATION,
    
    // Future VR actions
    VR_GRAB_LEFT,
    VR_GRAB_RIGHT,
    VR_TRIGGER_LEFT,
    VR_TRIGGER_RIGHT,
    VR_MENU_LEFT,
    VR_MENU_RIGHT
};

// Input device types
enum class InputDevice {
    MOUSE,
    KEYBOARD,
    VR_CONTROLLER_LEFT,
    VR_CONTROLLER_RIGHT,
    VR_HEAD_TRACKING
};

// Input binding structure
struct InputBinding {
    InputAction action;
    InputDevice device;
    int key;           // GLFW key code or VR button
    float sensitivity; // For analog inputs
    bool isAnalog;     // True for continuous inputs (mouse, VR tracking)
    bool isPressed;    // Current state for digital inputs
    float value;       // Current value for analog inputs
};

// Input state for actions
struct InputState {
    float value = 0.0f;        // Current value (0.0 to 1.0 for digital, -1.0 to 1.0 for analog)
    bool isPressed = false;    // Current pressed state
    bool wasPressed = false;   // Previous frame pressed state
    float delta = 0.0f;        // Change from previous frame
};

// Input event callback types
using InputEventCallback = std::function<void(InputAction action, float value, bool isPressed)>;

// Main input manager class
class InputManager {
public:
    InputManager();
    ~InputManager();

    // Initialize input system
    bool Initialize(IWindow* window);
    void Shutdown();

    // Update input state (call each frame)
    void Update(float deltaTime);

    // Input binding management
    void AddBinding(const InputBinding& binding);
    void RemoveBinding(InputAction action, InputDevice device, int key);
    void ClearBindings();

    // Input state queries
    bool IsActionPressed(InputAction action) const;
    bool IsActionJustPressed(InputAction action) const;
    bool IsActionJustReleased(InputAction action) const;
    float GetActionValue(InputAction action) const;
    float GetActionDelta(InputAction action) const;

    // Event callbacks
    void RegisterCallback(InputAction action, InputEventCallback callback);
    void UnregisterCallback(InputAction action);

    // Mouse input handling
    void OnMouseMove(double xpos, double ypos);
    void OnMouseButton(int button, int action, int mods);
    void OnScroll(double xoffset, double yoffset);

    // Keyboard input handling
    void OnKeyPress(int key, int scancode, int action, int mods);

    // VR input handling (for future implementation)
    void OnVRControllerInput(InputDevice device, int button, float value);
    void OnVRHeadTracking(const glm::mat4& headPose);

    // Default input setup
    void SetupDefaultDesktopBindings();
    void SetupDefaultVRBindings(); // For future VR implementation

    // Getters
    bool IsInitialized() const { return m_initialized; }

private:
    // Input state management
    void UpdateInputStates();
    void ProcessInputEvents();
    
    // Input bindings and states
    std::vector<InputBinding> m_bindings;
    std::unordered_map<InputAction, InputState> m_inputStates;
    std::unordered_map<InputAction, std::vector<InputEventCallback>> m_callbacks;
    
    // Device state tracking
    std::unordered_map<int, bool> m_keyStates;
    std::unordered_map<int, bool> m_mouseButtonStates;
    
    // System state
    bool m_initialized = false;
    IWindow* m_window = nullptr;
    
    // Mouse state
    bool m_firstMouse = true;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    float m_mouseSensitivity = 0.1f;
    
    // VR state (for future implementation)
    glm::mat4 m_vrHeadPose{1.0f};
    glm::vec3 m_vrLeftControllerPosition{0.0f};
    glm::vec3 m_vrRightControllerPosition{0.0f};
    bool m_vrLeftControllerActive = false;
    bool m_vrRightControllerActive = false;
};

} // namespace aero_boar
