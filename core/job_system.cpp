// engine/core/job_system.cpp
#include <windows.h>

#include "job_system.h"
#include <algorithm>

namespace Core {

JobSystem::JobSystem(uint32_t numThreads) {
    if (numThreads == 0) {
        numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }
    
    m_threads.reserve(numThreads);
    for (uint32_t i = 0; i < numThreads; ++i) {
        m_threads.emplace_back(&JobSystem::WorkerThread, this);
    }
}

JobSystem::~JobSystem() {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    
    m_condition.notify_all();
    
    for (auto& thread : m_threads) {
        thread.join();
    }
}

void JobSystem::Execute(Job job) {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_jobQueue.push(std::move(job));
        m_jobCount++;
    }
    
    m_condition.notify_one();
}

void JobSystem::Dispatch(uint32_t jobCount, uint32_t groupSize, const std::function<void(uint32_t)>& job) {
    if (jobCount == 0 || groupSize == 0) return;
    
    const uint32_t groupCount = (jobCount + groupSize - 1) / groupSize;
    
    m_jobCount += groupCount;
    
    for (uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        Execute([this, groupIndex, groupSize, jobCount, job]() {
            const uint32_t start = groupIndex * groupSize;
            const uint32_t end = std::min(start + groupSize, jobCount);
            
            for (uint32_t i = start; i < end; ++i) {
                job(i);
            }
        });
    }
}

void JobSystem::Wait() {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_waitCondition.wait(lock, [this] { return m_jobCount == 0; });
}

void JobSystem::WorkerThread() {
    while (true) {
        Job job;
        
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            m_condition.wait(lock, [this] {
                return m_stop || !m_jobQueue.empty();
            });
            
            if (m_stop && m_jobQueue.empty()) {
                break;
            }
            
            if (!m_jobQueue.empty()) {
                job = std::move(m_jobQueue.front());
                m_jobQueue.pop();
            }
        }
        
        if (job) {
            job();
            
            m_jobCount--;
            
            if (m_jobCount == 0) {
                m_waitCondition.notify_all();
            }
        }
    }
}

} // namespace Core