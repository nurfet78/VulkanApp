// engine/core/input.h
#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <functional>
#include <array>

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
    
    // Keyboard
    bool IsKeyPressed(int key) const;
    bool IsKeyDown(int key) const;
    bool IsKeyReleased(int key) const;
    void RegisterKeyCallback(int key, KeyCallback callback);
    
    // Mouse
    bool IsMouseButtonPressed(MouseButton button) const;
    bool IsMouseButtonDown(MouseButton button) const;
    bool IsMouseButtonReleased(MouseButton button) const;
    glm::vec2 GetMousePosition() const { return m_mousePosition; }
    glm::vec2 GetMouseDelta() const { return m_mouseDelta; }
    float GetScrollDelta() const { return m_scrollDelta; }
    
    void SetCursorLocked(bool locked);
    bool IsCursorLocked() const { return m_cursorLocked; }
    
    void RegisterMouseCallback(MouseButton button, MouseCallback callback);

private:
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
    
    glm::vec2 m_mousePosition{0.0f};
    glm::vec2 m_mousePrevious{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    float m_scrollDelta = 0.0f;
    bool m_cursorLocked = false;
    bool m_firstMouse = true;
    
    static Input* s_instance;
};