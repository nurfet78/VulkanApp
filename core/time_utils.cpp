// engine/core/time.cpp

#include "time_utils.h"

namespace Core {

Time::Time() {
    m_startTime = Clock::now();
    m_lastFrame = m_startTime;
}

void Time::Update() {
    TimePoint currentFrame = Clock::now();
    
    // Calculate delta time in seconds
    auto frameDuration = std::chrono::duration<float>(currentFrame - m_lastFrame);
    m_deltaTime = frameDuration.count() * m_timeScale;
    m_frameTime = frameDuration.count() * 1000.0f; // milliseconds
    
    // Update total time - ИСПРАВЛЕНО: убрано дублирование
    auto totalDuration = std::chrono::duration<float>(currentFrame - m_startTime);
    m_time = totalDuration.count();
    
    // Calculate FPS
    m_frameCount++;
    m_fpsAccumulator += frameDuration.count();
    
    if (m_fpsAccumulator >= 1.0f) {
        m_fps = m_frameCount;
        m_frameCount = 0;
        m_fpsAccumulator = 0.0f;
    }
    
    m_lastFrame = currentFrame;
}

} // namespace Core