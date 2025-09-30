// engine/core/input.h
#pragma once

#include "pch.h"


namespace Core {

class Window;

enum class MouseButton {
    Left = GLFW_MOUSE_BUTTON_LEFT,
    Right = GLFW_MOUSE_BUTTON_RIGHT,
    Middle = GLFW_MOUSE_BUTTON_MIDDLE
};

class Input {
public:
    using KeyCallback = std::function<void(int action)>;
    using MouseCallback = std::function<void(MouseButton button, int action)>;

    explicit Input(Window* window);
    ~Input();

    void Update();

    // === Keyboard (статические методы) ===
    static bool IsKeyPressed(int key);
    static bool IsKeyDown(int key);
    static bool IsKeyReleased(int key);
    static void RegisterKeyCallback(int key, KeyCallback callback);

    // === Mouse (статические методы) ===
    static bool IsMouseButtonPressed(MouseButton button);
    static bool IsMouseButtonDown(MouseButton button);
    static bool IsMouseButtonReleased(MouseButton button);

    static glm::vec2 GetMousePosition();
    static glm::vec2 GetMouseDelta();
    static float GetScrollDelta();

    static void SetCursorLocked(bool locked);
    static bool IsCursorLocked();

    static void RegisterMouseCallback(MouseButton button, MouseCallback callback);

private:
    // GLFW callbacks
    static void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void OnMouseButton(GLFWwindow* window, int button, int action, int mods);
    static void OnMouseMove(GLFWwindow* window, double xpos, double ypos);
    static void OnScroll(GLFWwindow* window, double xoffset, double yoffset);

    Window* m_window;

    // Keyboard state
    std::array<bool, GLFW_KEY_LAST + 1> m_keysCurrent{};
    std::array<bool, GLFW_KEY_LAST + 1> m_keysPrevious{};
    std::unordered_map<int, KeyCallback> m_keyCallbacks;

    // Mouse state
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_mouseButtonsCurrent{};
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_mouseButtonsPrevious{};
    std::unordered_map<int, MouseCallback> m_mouseCallbacks;

    glm::vec2 m_mousePosition{ 0.0f };
    glm::vec2 m_mousePrevious{ 0.0f };
    glm::vec2 m_mouseDelta{ 0.0f };
    float m_scrollDelta = 0.0f;
    bool m_cursorLocked = false;
    bool m_firstMouse = true;

    static Input* s_instance;
};
}