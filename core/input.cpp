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
    // Update previous states
    m_keysPrevious = m_keysCurrent;
    m_mouseButtonsPrevious = m_mouseButtonsCurrent;
    
    // Update mouse delta
    m_mouseDelta = m_mousePosition - m_mousePrevious;
    m_mousePrevious = m_mousePosition;
    
    // Reset scroll
    m_scrollDelta = 0.0f;
}

bool Input::IsKeyPressed(int key) const {
    return m_keysCurrent[key] && !m_keysPrevious[key];
}

bool Input::IsKeyDown(int key) const {
    return m_keysCurrent[key];
}

bool Input::IsKeyReleased(int key) const {
    return !m_keysCurrent[key] && m_keysPrevious[key];
}

void Input::RegisterKeyCallback(int key, KeyCallback callback) {
    m_keyCallbacks[key] = callback;
}

bool Input::IsMouseButtonPressed(MouseButton button) const {
    int btn = static_cast<int>(button);
    return m_mouseButtonsCurrent[btn] && !m_mouseButtonsPrevious[btn];
}

bool Input::IsMouseButtonDown(MouseButton button) const {
    int btn = static_cast<int>(button);
    return m_mouseButtonsCurrent[btn];
}

bool Input::IsMouseButtonReleased(MouseButton button) const {
    int btn = static_cast<int>(button);
    return !m_mouseButtonsCurrent[btn] && m_mouseButtonsPrevious[btn];
}

void Input::SetCursorLocked(bool locked) {
    m_cursorLocked = locked;
    GLFWwindow* handle = m_window->GetHandle();
    
    if (locked) {
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    } else {
        glfwSetInputMode(handle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
    
    m_firstMouse = true;
}

void Input::RegisterMouseCallback(MouseButton button, MouseCallback callback) {
    m_mouseCallbacks[static_cast<int>(button)] = callback;
}

void Input::OnKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (!s_instance || key < 0 || key > GLFW_KEY_LAST) return;
    
    s_instance->m_keysCurrent[key] = (action != GLFW_RELEASE);
    
    // Trigger callback
    auto it = s_instance->m_keyCallbacks.find(key);
    if (it != s_instance->m_keyCallbacks.end()) {
        it->second(action);
    }
}

void Input::OnMouseButton(GLFWwindow* window, int button, int action, int mods) {
    if (!s_instance || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
    
    s_instance->m_mouseButtonsCurrent[button] = (action != GLFW_RELEASE);
    
    // Trigger callback
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