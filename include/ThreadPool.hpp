#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <queue>
#include <thread>
#include <mutex>

// 线程池
class ThreadPool
{
public:
    ThreadPool(int32_t _threadNumbers); // 构造函数

    // 添加任务到队列中
    template <typename Function, typename ...Arg>
    auto commit(Function&& _function, Arg&&... _args)
        -> std::future<decltype(_function(_args...))>;

    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 获取空闲线程数量
    int32_t getIdleThreadCount() const;

    ~ThreadPool(); // 析构函数
private:
    void functionTask(void); // 线程的执行内容
    std::atomic_bool m_Stop; // 停止标识，当前线程是否停止（true/false）
    std::atomic_int32_t m_ThreadNum; // 空闲线程的数量
    std::condition_variable m_ConditionVariable; // 条件变量
    std::mutex m_MutexLock; // 互斥锁
    std::vector<std::thread> m_Pool; // 线程集合，即线程池
    std::queue<std::function<void(void)>> m_TaskQueue; // 任务队列
};

ThreadPool::ThreadPool(int32_t _threadNumbers) : m_Stop(false)
{
    if (_threadNumbers < 1) { m_ThreadNum = 1; }
    else { m_ThreadNum = _threadNumbers; }
    for (size_t i = 0; i < m_ThreadNum; ++i)
    {
        m_Pool.emplace_back(
            [this] {
                this->functionTask();
            }
        );
    }
}

ThreadPool::~ThreadPool()
{
    m_Stop.store(true); // 更改停止标识

    m_ConditionVariable.notify_all(); // 通知所有阻塞中的线程

    for (std::thread& thread : m_Pool) // 将所有线程加入到主线程中
    {
        if (thread.joinable())
        {
            std::cout << "Join Thread " << thread.get_id() << std::endl;
            thread.join();
        }
    }
}

template <typename Function, typename ...Arg>
auto ThreadPool::commit(Function&& _function, Arg&&... _args)
-> std::future<decltype(_function(_args...))>
{
    using functionType = decltype(_function(_args...));

    if (m_Stop.load())
        return std::future<functionType>{};

    // 共享指针，指向一个被包装为functionType(void)的task
    std::shared_ptr<std::packaged_task<functionType(void)>>
        task = std::make_shared<std::packaged_task<functionType(void)>>(
            std::bind(std::forward<Function>(_function), std::forward<Arg>(_args)...)
        );

    std::future<functionType> resultFuture = task->get_future(); // 获取future

    // 将任务添加到队列中
    {
        std::lock_guard<std::mutex> lockGuard(this->m_MutexLock);
        if (m_Stop)
        {
            throw std::runtime_error("Error: Thread pool has stopped!");
        }

        m_TaskQueue.emplace([task] { // 添加任务至队列
            (*task)();
            });
    }

    m_ConditionVariable.notify_all(); // 通知所有线程去执行

    return resultFuture; // 返回结果
}

int32_t ThreadPool::getIdleThreadCount() const
{
    return m_ThreadNum;
}

void ThreadPool::functionTask(void)
{
    while (true)
    {
        std::function<void(void)> task; // 定义任务

        // 获取任务
        {
            std::unique_lock<std::mutex> localLock(m_MutexLock);
            // 如果停止标识为true或者任务队列为空，那么阻塞当前任务
            m_ConditionVariable.wait(localLock, [this] { return this->m_Stop.load()
                || !this->m_TaskQueue.empty(); });
            // 如果条件成立，则中止当前任务
            if (m_Stop.load() && m_TaskQueue.empty()) { return; }
            // 获取并弹出任务
            task = std::move(this->m_TaskQueue.front());
            this->m_TaskQueue.pop();
        }

        this->m_ThreadNum--;
        task(); // 执行
        this->m_ThreadNum++;
    }
}

#endif