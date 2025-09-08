// engine/core/job_system.h
#pragma once

#include "pch.h"


namespace Core {

class JobSystem {
public:
    using Job = std::function<void()>;
    
    explicit JobSystem(uint32_t numThreads = 0);
    ~JobSystem();
    
    // Submit a job to the queue
    void Execute(Job job);
    
    // Submit a job and get a future for its completion
    template<typename F, typename... Args>
    auto Submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using ReturnType = decltype(f(args...));
        
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<ReturnType> result = task->get_future();
        
        Execute([task]() { (*task)(); });
        
        return result;
    }
    
    // Execute jobs in parallel and wait for completion
    void Dispatch(uint32_t jobCount, uint32_t groupSize, const std::function<void(uint32_t)>& job);
    
    // Wait for all jobs to complete
    void Wait();
    
    // Check if all jobs are complete
    bool IsBusy() const { return m_jobCount > 0; }
    
    uint32_t GetThreadCount() const { return static_cast<uint32_t>(m_threads.size()); }

private:
    void WorkerThread();
    
    std::vector<std::thread> m_threads;
    std::queue<Job> m_jobQueue;
    
    mutable std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::condition_variable m_waitCondition;
    
    std::atomic<uint32_t> m_jobCount{0};
    std::atomic<bool> m_stop{false};
};

} // namespace Core