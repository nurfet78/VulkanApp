// engine/core/input.cpp
#include "input.h"
#include "window.h"


namespace Core {

    Input* Input::s_instance = nullptr;

    Input::Input(Window* window) : m_window(window) {
        s_instance = this;

        GLFWwindow* handle = window->GetHandle();
        glfwSetKeyCallback(handle, OnKey);
        glfwSetMouseButtonCallback(handle, OnMouseButton);
        glfwSetCursorPosCallback(handle, OnMouseMove);
        glfwSetScrollCallback(handle, OnScroll);
    }

    Input::~Input() {
        s_instance = nullptr;
    }

    void Input::Update() {
        if (!s_instance) return;

        // Update previous states
        s_instance->m_keysPrevious = s_instance->m_keysCurrent;
        s_instance->m_mouseButtonsPrevious = s_instance->m_mouseButtonsCurrent;

        // Update mouse delta
        s_instance->m_mouseDelta = s_instance->m_mousePosition - s_instance->m_mousePrevious;
        s_instance->m_mousePrevious = s_instance->m_mousePosition;

        // Reset scroll
        s_instance->m_scrollDelta = 0.0f;
    }

    // === Keyboard ===
    bool Input::IsKeyPressed(int key) {
        return s_instance && s_instance->m_keysCurrent[key] && !s_instance->m_keysPrevious[key];
    }

    bool Input::IsKeyDown(int key) {
        return s_instance && s_instance->m_keysCurrent[key];
    }

    bool Input::IsKeyReleased(int key) {
        return s_instance && !s_instance->m_keysCurrent[key] && s_instance->m_keysPrevious[key];
    }

    void Input::RegisterKeyCallback(int key, KeyCallback callback) {
        if (s_instance) {
            s_instance->m_keyCallbacks[key] = callback;
        }
    }

    // === Mouse ===
    bool Input::IsMouseButtonPressed(MouseButton button) {
        if (!s_instance) return false;
        int btn = static_cast<int>(button);
        return s_instance->m_mouseButtonsCurrent[btn] && !s_instance->m_mouseButtonsPrevious[btn];
    }

    bool Input::IsMouseButtonDown(MouseButton button) {
        if (!s_instance) return false;
        return s_instance->m_mouseButtonsCurrent[static_cast<int>(button)];
    }

    bool Input::IsMouseButtonReleased(MouseButton button) {
        if (!s_instance) return false;
        int btn = static_cast<int>(button);
        return !s_instance->m_mouseButtonsCurrent[btn] && s_instance->m_mouseButtonsPrevious[btn];
    }

    glm::vec2 Input::GetMousePosition() {
        return s_instance ? s_instance->m_mousePosition : glm::vec2(0.0f);
    }

    glm::vec2 Input::GetMouseDelta() {
        return s_instance ? s_instance->m_mouseDelta : glm::vec2(0.0f);
    }

    float Input::GetScrollDelta() {
        return s_instance ? s_instance->m_scrollDelta : 0.0f;
    }

    void Input::SetCursorLocked(bool locked) {
        if (!s_instance) return;

        s_instance->m_cursorLocked = locked;
        GLFWwindow* handle = s_instance->m_window->GetHandle();

        if (locked) {
            glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported()) {
                glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            }
        }
        else {
            glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        }

        s_instance->m_firstMouse = true;
    }

    bool Input::IsCursorLocked() {
        return s_instance && s_instance->m_cursorLocked;
    }

    void Input::RegisterMouseCallback(MouseButton button, MouseCallback callback) {
        if (s_instance) {
            s_instance->m_mouseCallbacks[static_cast<int>(button)] = callback;
        }
    }

    // === GLFW callbacks ===
    void Input::OnKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (!s_instance || key < 0 || key > GLFW_KEY_LAST) return;

        s_instance->m_keysCurrent[key] = (action != GLFW_RELEASE);

        auto it = s_instance->m_keyCallbacks.find(key);
        if (it != s_instance->m_keyCallbacks.end()) {
            it->second(action);
        }
    }

    void Input::OnMouseButton(GLFWwindow* window, int button, int action, int mods) {
        if (!s_instance || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;

        s_instance->m_mouseButtonsCurrent[button] = (action != GLFW_RELEASE);

        auto it = s_instance->m_mouseCallbacks.find(button);
        if (it != s_instance->m_mouseCallbacks.end()) {
            it->second(static_cast<MouseButton>(button), action);
        }
    }

    void Input::OnMouseMove(GLFWwindow* window, double xpos, double ypos) {
        if (!s_instance) return;

        glm::vec2 newPos(static_cast<float>(xpos), static_cast<float>(ypos));

        if (s_instance->m_firstMouse) {
            s_instance->m_mousePrevious = newPos;
            s_instance->m_firstMouse = false;
        }

        s_instance->m_mousePosition = newPos;
    }

    void Input::OnScroll(GLFWwindow* window, double xoffset, double yoffset) {
        if (!s_instance) return;
        s_instance->m_scrollDelta = static_cast<float>(yoffset);
    }
}