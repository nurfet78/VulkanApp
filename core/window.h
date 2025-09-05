// engine/core/window.h
#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <functional>
#include <vector>

namespace Core {

class Window {
public:
    using CloseCallback = std::function<void()>;
    using ResizeCallback = std::function<void(uint32_t, uint32_t)>;
    
    Window(const std::string& title, uint32_t width, uint32_t height, bool fullscreen = false);
    ~Window();
    
    void PollEvents();
    void SwapBuffers();
    bool ShouldClose() const;
    
    void ToggleFullscreen();
    void SetFullscreen(bool fullscreen);
    bool IsFullscreen() const { return m_fullscreen; }
    
    void SetCloseCallback(CloseCallback callback) { m_closeCallback = callback; }
    void SetResizeCallback(ResizeCallback callback) { m_resizeCallback = callback; }
    
    GLFWwindow* GetHandle() const { return m_window; }
    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }
    float GetAspectRatio() const { return static_cast<float>(m_width) / m_height; }
    
    // For Vulkan surface creation
    const char** GetRequiredInstanceExtensions(uint32_t* count) const;
    VkResult CreateWindowSurface(VkInstance instance, VkSurfaceKHR* surface) const;

private:
    static void OnWindowClose(GLFWwindow* window);
    static void OnWindowResize(GLFWwindow* window, int width, int height);
    static void OnFramebufferResize(GLFWwindow* window, int width, int height);
    
    GLFWwindow* m_window = nullptr;
    std::string m_title;
    uint32_t m_width;
    uint32_t m_height;
    bool m_fullscreen = false;
    
    // Saved window state for fullscreen toggle
    int m_savedX = 0;
    int m_savedY = 0;
    int m_savedWidth = 0;
    int m_savedHeight = 0;
    
    CloseCallback m_closeCallback;
    ResizeCallback m_resizeCallback;
    
    static bool s_glfwInitialized;
};

} // namespace Core