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

// �̳߳�
class ThreadPool
{
public:
    ThreadPool(int32_t _threadNumbers); // ���캯��

    // ������񵽶�����
    template <typename Function, typename ...Arg>
    auto commit(Function&& _function, Arg&&... _args)
        -> std::future<decltype(_function(_args...))>;

    // ��ֹ����
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // ��ȡ�����߳�����
    int32_t getIdleThreadCount() const;

    ~ThreadPool(); // ��������
private:
    void functionTask(void); // �̵߳�ִ������
    std::atomic_bool m_Stop; // ֹͣ��ʶ����ǰ�߳��Ƿ�ֹͣ��true/false��
    std::atomic_int32_t m_ThreadNum; // �����̵߳�����
    std::condition_variable m_ConditionVariable; // ��������
    std::mutex m_MutexLock; // ������
    std::vector<std::thread> m_Pool; // �̼߳��ϣ����̳߳�
    std::queue<std::function<void(void)>> m_TaskQueue; // �������
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
    m_Stop.store(true); // ����ֹͣ��ʶ

    m_ConditionVariable.notify_all(); // ֪ͨ���������е��߳�

    for (std::thread& thread : m_Pool) // �������̼߳��뵽���߳���
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

    // ����ָ�룬ָ��һ������װΪfunctionType(void)��task
    std::shared_ptr<std::packaged_task<functionType(void)>>
        task = std::make_shared<std::packaged_task<functionType(void)>>(
            std::bind(std::forward<Function>(_function), std::forward<Arg>(_args)...)
        );

    std::future<functionType> resultFuture = task->get_future(); // ��ȡfuture

    // ��������ӵ�������
    {
        std::lock_guard<std::mutex> lockGuard(this->m_MutexLock);
        if (m_Stop)
        {
            throw std::runtime_error("Error: Thread pool has stopped!");
        }

        m_TaskQueue.emplace([task] { // �������������
            (*task)();
            });
    }

    m_ConditionVariable.notify_all(); // ֪ͨ�����߳�ȥִ��

    return resultFuture; // ���ؽ��
}

int32_t ThreadPool::getIdleThreadCount() const
{
    return m_ThreadNum;
}

void ThreadPool::functionTask(void)
{
    while (true)
    {
        std::function<void(void)> task; // ��������

        // ��ȡ����
        {
            std::unique_lock<std::mutex> localLock(m_MutexLock);
            // ���ֹͣ��ʶΪtrue�����������Ϊ�գ���ô������ǰ����
            m_ConditionVariable.wait(localLock, [this] { return this->m_Stop.load()
                || !this->m_TaskQueue.empty(); });
            // �����������������ֹ��ǰ����
            if (m_Stop.load() && m_TaskQueue.empty()) { return; }
            // ��ȡ����������
            task = std::move(this->m_TaskQueue.front());
            this->m_TaskQueue.pop();
        }

        this->m_ThreadNum--;
        task(); // ִ��
        this->m_ThreadNum++;
    }
}

#endif