#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <future>
#include <condition_variable>
#include <functional>
#include <queue>

// �̳߳�
class ThreadPool
{
public:
    ThreadPool(int32_t _threadNumbers); // ���캯��

    template <typename Function, typename ...Arg>
    auto addTask(Function&& _function, Arg&&... _arg) // ������񵽶�����
        -> std::future<typename std::result_of<Function(Arg...)>::type>;

    ~ThreadPool(); // ��������
private:
    void functionTask(void); // �̵߳�ִ������
    bool m_IsStop; // ֹͣ��ʶ����ǰ�߳��Ƿ�ֹͣ��true/false��
    std::condition_variable m_ConditionVariable; // ��������
    std::mutex m_MutexLock; // ������
    std::vector<std::thread> m_Threads; // �̼߳��ϣ����̳߳�
    std::queue<std::function<void(void)>> m_TaskQueue; // �������
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
    // ����ֹͣ��ʶ
    {
        std::unique_lock<std::mutex>(m_MutexLock);
        m_IsStop = true;
    }

    m_ConditionVariable.notify_all(); // ֪ͨ���������е��߳�

    for (std::thread& thread : m_Threads) // �������̼߳��뵽���߳���
    {
        thread.join();
    }
}

template <typename Function, typename ...Arg>
auto ThreadPool::addTask(Function&& _function, Arg&&... _arg)
-> std::future<typename std::result_of<Function(Arg...)>::type>
{
    using functionType = typename std::result_of<Function(Arg...)>::type;

    // ����ָ�룬ָ��һ������װΪfunctionType(void)��task
    std::shared_ptr<std::packaged_task<functionType(void)>>
        task = std::make_shared<std::packaged_task<functionType(void)>>(
            std::bind(std::forward<Function>(_function), std::forward<Arg>(_arg)...)
        );

    std::future<functionType> resultFuture = task->get_future(); // ��ȡfuture

    // ��������ӵ�������
    {
        std::lock_guard<std::mutex> lockGuard(this->m_MutexLock);
        if (m_IsStop)
        {
            throw std::runtime_error("�����̳߳��Ѿ�ֹͣ��");
        }

        m_TaskQueue.emplace([task] { // �������������
            (*task)();
            });
    }

    m_ConditionVariable.notify_all(); // ֪ͨ�����߳�ȥִ��

    return resultFuture; // ���ؽ��
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
            m_ConditionVariable.wait(localLock, [this] { return this->m_IsStop || !this->m_TaskQueue.empty(); });
            // �����������������ֹ��ǰ����
            if (m_IsStop && m_TaskQueue.empty()) { return; }
            // ��ȡ����������
            task = std::move(this->m_TaskQueue.front());
            this->m_TaskQueue.pop();
        }

        task(); // ִ��
    }
}

#endif