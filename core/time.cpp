// engine/core/time.cpp
#include "time.h"

namespace Core {

Time::Time() {
    m_startTime = Clock::now();
    m_lastFrame = m_startTime;
}

void Time::Update() {
    TimePoint currentFrame = Clock::now();
    
    // Calculate delta time in seconds
    auto duration = std::chrono::duration<float>(currentFrame - m_lastFrame);
    m_deltaTime = duration.count() * m_timeScale;
    m_frameTime = duration.count() * 1000.0f; // milliseconds
    
    // Update total time
    auto totalDuration = std::chrono::duration<float>(currentFrame - m_startTime);
    m_time = totalDuration.count();
    
    // Calculate FPS
    m_frameCount++;
    m_fpsAccumulator += duration.count();
    
    if (m_fpsAccumulator >= 1.0f) {
        m_fps = m_frameCount;
        m_frameCount = 0;
        m_fpsAccumulator = 0.0f;
    }
    
    m_lastFrame = currentFrame;
}

} // namespace Core