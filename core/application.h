// engine/core/application.h
#pragma once

#include <memory>
#include <string>
#include <functional>
#include <chrono>

namespace Core {

class Window;
class Input;
class Time;
class JobSystem;

struct ApplicationConfig {
    std::string title = "Meadow World";
    uint32_t width = 1920;
    uint32_t height = 1080;
    bool vsync = true;
    bool fullscreen = false;
    uint32_t workerThreads = 4;
};

class Application {
public:
    Application(const ApplicationConfig& config);
    ~Application();

    void Run();
    void RequestExit() { m_running = false; }
    
    Window* GetWindow() const { return m_window.get(); }
    Input* GetInput() const { return m_input.get(); }
    Time* GetTime() const { return m_time.get(); }
    JobSystem* GetJobSystem() const { return m_jobSystem.get(); }
    
    static Application* Get() { return s_instance; }

protected:
    virtual void OnInitialize() = 0;
    virtual void OnShutdown() = 0;
    virtual void OnUpdate(float deltaTime) = 0;
    virtual void OnRender() = 0;
    virtual void OnImGui() {}

private:
    void Initialize();
    void Shutdown();
    void PollEvents();
    void Update(float deltaTime);
    void Render();

    ApplicationConfig m_config;
    bool m_running = true;
    
    std::unique_ptr<Window> m_window;
    std::unique_ptr<Input> m_input;
    std::unique_ptr<Time> m_time;
    std::unique_ptr<JobSystem> m_jobSystem;
    
    static Application* s_instance;
};

} // namespace Core