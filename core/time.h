// engine/core/time.h

#include <chrono>  
#include <cstdint>

namespace Core {
	
class Time {
public:
    Time();
    
    void Update();
    
    float GetDeltaTime() const { return m_deltaTime; }
    float GetTime() const { return m_time; }
    uint32_t GetFPS() const { return m_fps; }
    float GetFrameTime() const { return m_frameTime; }
    
    void SetTimeScale(float scale) { m_timeScale = scale; }
    float GetTimeScale() const { return m_timeScale; }

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    
    TimePoint m_startTime;
    TimePoint m_lastFrame;
    
    float m_deltaTime = 0.0f;
    float m_time = 0.0f;
    float m_timeScale = 1.0f;
    
    // FPS calculation
    float m_frameTime = 0.0f;
    uint32_t m_fps = 0;
    uint32_t m_frameCount = 0;
    float m_fpsAccumulator = 0.0f;
};

} // namespace Core