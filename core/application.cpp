// engine/core/application.cpp
#include <iostream>  // ПЕРЕМЕЩЕНО: до локальных заголовков
#include "application.h"
#include "window.h"
#include "input.h"
#include "time.h"
#include "job_system.h"


namespace Core {

Application* Application::s_instance = nullptr;

Application::Application(const ApplicationConfig& config)
    : m_config(config) {
    s_instance = this;
}

Application::~Application() {
    s_instance = nullptr;
}

void Application::Run() {
    Initialize();
    
    while (m_running) {
        m_time->Update();
        
        PollEvents();
        Update(m_time->GetDeltaTime());
        Render();
        
        m_jobSystem->Wait();
    }
    
    Shutdown();
}

void Application::Initialize() {
    // Initialize subsystems
    m_window = std::make_unique<Window>(m_config.title, m_config.width, m_config.height, m_config.fullscreen);
    m_input = std::make_unique<Input>(m_window.get());
    m_time = std::make_unique<Time>();
    m_jobSystem = std::make_unique<JobSystem>(m_config.workerThreads);
    
    // Set window callbacks
    m_window->SetCloseCallback([this]() { RequestExit(); });
    
    // Fullscreen toggle on F11
    m_input->RegisterKeyCallback(GLFW_KEY_F11, [this](int action) {
        if (action == GLFW_PRESS) {
            m_window->ToggleFullscreen();
        }
    });
    
    OnInitialize();
}

void Application::Shutdown() {
    OnShutdown();
    
    m_jobSystem.reset();
    m_time.reset();
    m_input.reset();
    m_window.reset();
}

void Application::PollEvents() {
    m_window->PollEvents();
    m_input->Update();
}

void Application::Update(float deltaTime) {
    OnUpdate(deltaTime);
}

void Application::Render() {
    OnRender();
}

} // namespace Core