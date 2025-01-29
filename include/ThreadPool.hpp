#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <future>
#include <condition_variable>
#include <functional>
#include <queue>

// 线程池
class ThreadPool
{
public:
    ThreadPool(int32_t _threadNumbers); // 构造函数

    template <typename Function, typename ...Arg>
    auto addTask(Function&& _function, Arg&&... _arg) // 添加任务到队列中
        -> std::future<typename std::result_of<Function(Arg...)>::type>;

    ~ThreadPool(); // 析构函数
private:
    void functionTask(void); // 线程的执行内容
    bool m_IsStop; // 停止标识，当前线程是否停止（true/false）
    std::condition_variable m_ConditionVariable; // 条件变量
    std::mutex m_MutexLock; // 互斥锁
    std::vector<std::thread> m_Threads; // 线程集合，即线程池
    std::queue<std::function<void(void)>> m_TaskQueue; // 任务队列
};

ThreadPool::ThreadPool(int32_t _threadNumbers) : m_IsStop(false)
{
    for (size_t i = 0; i < _threadNumbers; ++i)
    {
        m_Threads.emplace_back(
            [this] {
                this->functionTask();
            }
        );
    }
}

ThreadPool::~ThreadPool()
{
    // 更改停止标识
    {
        std::unique_lock<std::mutex>(m_MutexLock);
        m_IsStop = true;
    }

    m_ConditionVariable.notify_all(); // 通知所有阻塞中的线程

    for (std::thread& thread : m_Threads) // 将所有线程加入到主线程中
    {
        thread.join();
    }
}

template <typename Function, typename ...Arg>
auto ThreadPool::addTask(Function&& _function, Arg&&... _arg)
-> std::future<typename std::result_of<Function(Arg...)>::type>
{
    using functionType = typename std::result_of<Function(Arg...)>::type;

    // 共享指针，指向一个被包装为functionType(void)的task
    std::shared_ptr<std::packaged_task<functionType(void)>>
        task = std::make_shared<std::packaged_task<functionType(void)>>(
            std::bind(std::forward<Function>(_function), std::forward<Arg>(_arg)...)
        );

    std::future<functionType> resultFuture = task->get_future(); // 获取future

    // 将任务添加到队列中
    {
        std::lock_guard<std::mutex> lockGuard(this->m_MutexLock);
        if (m_IsStop)
        {
            throw std::runtime_error("出错：线程池已经停止了");
        }

        m_TaskQueue.emplace([task] { // 添加任务至队列
            (*task)();
            });
    }

    m_ConditionVariable.notify_all(); // 通知所有线程去执行

    return resultFuture; // 返回结果
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
            m_ConditionVariable.wait(localLock, [this] { return this->m_IsStop || !this->m_TaskQueue.empty(); });
            // 如果条件成立，则中止当前任务
            if (m_IsStop && m_TaskQueue.empty()) { return; }
            // 获取并弹出任务
            task = std::move(this->m_TaskQueue.front());
            this->m_TaskQueue.pop();
        }

        task(); // 执行
    }
}

#endif