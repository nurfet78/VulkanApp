// engine/core/window.cpp
#include "window.h"
#include <stdexcept>
#include <iostream>

namespace Core {

bool Window::s_glfwInitialized = false;

Window::Window(const std::string& title, uint32_t width, uint32_t height, bool fullscreen)
    : m_title(title), m_width(width), m_height(height), m_fullscreen(fullscreen) {
    
    // Initialize GLFW once
    if (!s_glfwInitialized) {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }
        s_glfwInitialized = true;
    }
    
    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    
    // Get primary monitor for fullscreen
    GLFWmonitor* monitor = fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    
    if (fullscreen && monitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        m_width = mode->width;
        m_height = mode->height;
    }
    
    // Create window
    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), monitor, nullptr);
    if (!m_window) {
        throw std::runtime_error("Failed to create GLFW window");
    }
    
    // Set user pointer for callbacks
    glfwSetWindowUserPointer(m_window, this);
    
    // Set callbacks
    glfwSetWindowCloseCallback(m_window, OnWindowClose);
    glfwSetWindowSizeCallback(m_window, OnWindowResize);
    glfwSetFramebufferSizeCallback(m_window, OnFramebufferResize);
    
    // Save initial window position and size for fullscreen toggle
    if (!fullscreen) {
        glfwGetWindowPos(m_window, &m_savedX, &m_savedY);
        glfwGetWindowSize(m_window, &m_savedWidth, &m_savedHeight);
    }
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    
    if (s_glfwInitialized) {
        glfwTerminate();
        s_glfwInitialized = false;
    }
}

void Window::PollEvents() {
    glfwPollEvents();
}

void Window::SwapBuffers() {
    glfwSwapBuffers(m_window);
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::ToggleFullscreen() {
    SetFullscreen(!m_fullscreen);
}

void Window::SetFullscreen(bool fullscreen) {
    if (m_fullscreen == fullscreen) return;
    
    if (fullscreen) {
        // Save current window state
        glfwGetWindowPos(m_window, &m_savedX, &m_savedY);
        glfwGetWindowSize(m_window, &m_savedWidth, &m_savedHeight);
        
        // Get primary monitor and its video mode
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        
        // Switch to fullscreen
        glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        
        m_width = mode->width;
        m_height = mode->height;
    } else {
        // Restore windowed mode
        glfwSetWindowMonitor(m_window, nullptr, m_savedX, m_savedY, m_savedWidth, m_savedHeight, 0);
        
        m_width = m_savedWidth;
        m_height = m_savedHeight;
    }
    
    m_fullscreen = fullscreen;
    
    // Trigger resize callback
    if (m_resizeCallback) {
        m_resizeCallback(m_width, m_height);
    }
}

const char** Window::GetRequiredInstanceExtensions(uint32_t* count) const {
    return glfwGetRequiredInstanceExtensions(count);
}

VkResult Window::CreateWindowSurface(VkInstance instance, VkSurfaceKHR* surface) const {
    return glfwCreateWindowSurface(instance, m_window, nullptr, surface);
}

void Window::OnWindowClose(GLFWwindow* window) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->m_closeCallback) {
        self->m_closeCallback();
    }
}

void Window::OnWindowResize(GLFWwindow* window, int width, int height) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->m_width = width;
        self->m_height = height;
        if (self->m_resizeCallback) {
            self->m_resizeCallback(width, height);
        }
    }
}

void Window::OnFramebufferResize(GLFWwindow* window, int width, int height) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self && self->m_resizeCallback) {
        self->m_resizeCallback(width, height);
    }
}

} // namespace Core