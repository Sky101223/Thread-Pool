#ifndef __THREAD_POOL_
#define __THREAD_POOL_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <queue>
#include <thread>
#include <mutex>

class ThreadPool
{
public:
    ThreadPool(const uint32_t& _threadCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename Function, typename ...Arg>
    auto commit(Function&& _function, Arg&&... _args)
        -> std::future<decltype(_function(_args...))>;

    uint32_t GetIdleThreadCount(void) const;
private:
    void Task(void);
    std::atomic<uint32_t> m_idleThreadCount;
    std::atomic<bool> m_stop;
    std::condition_variable m_conditionVariable;
    std::mutex m_mutex;
    std::vector<std::thread> m_pool;
    std::queue<std::function<void(void)>> m_taskQueue;
};

ThreadPool::ThreadPool(const uint32_t& _threadCount) : m_stop(false)
{
    if (m_idleThreadCount < 1)
        m_idleThreadCount = 1;
    else
        m_idleThreadCount = _threadCount;

    for (size_t i = 0; i < m_idleThreadCount; ++i)
    {
        m_pool.emplace_back(
            [this] {
                this->Task();
            }
        );
    }
}

ThreadPool::~ThreadPool()
{
    m_stop.store(true);

    m_conditionVariable.notify_all();

    for (std::thread& thread : m_pool)
    {
        if (thread.joinable())
        {
#ifdef JOIN_MESSAGE
            std::cout << "Join Thread " << thread.get_id() << std::endl;
#endif
            thread.join();
        }
    }
}

template <typename Function, typename ...Arg>
auto ThreadPool::commit(Function&& _function, Arg&&... _args) -> std::future<decltype(_function(_args...))>
{
    using functionType = decltype(_function(_args...));

    if (m_stop.load())
        return std::future<functionType>{};

    std::shared_ptr<std::packaged_task<functionType(void)>>
        task = std::make_shared<std::packaged_task<functionType(void)>>(
            std::bind(std::forward<Function>(_function), std::forward<Arg>(_args)...)
        );

    std::future<functionType> result = task->get_future();

    {
        std::lock_guard<std::mutex> localLock(this->m_mutex);
        if (m_stop.load())
            throw std::runtime_error("Error: Thread pool has stopped!");

        m_taskQueue.emplace([task] { (*task)(); });
    }

    m_conditionVariable.notify_all();

    return result;
}

uint32_t ThreadPool::GetIdleThreadCount(void) const
{
    return m_idleThreadCount.load();
}

void ThreadPool::Task(void)
{
    while (true)
    {
        std::function<void(void)> task;

        {
            std::unique_lock<std::mutex> localLock(this->m_mutex);
            this->m_conditionVariable.wait(localLock, [this] {
                return this->m_stop.load()
                    || !this->m_taskQueue.empty();
                });
            if (this->m_stop.load() && this->m_taskQueue.empty())
                return;

            task = std::move(this->m_taskQueue.front());
            this->m_taskQueue.pop();
        }

        this->m_idleThreadCount--;
        task();
        this->m_idleThreadCount++;
    }
}

#endif