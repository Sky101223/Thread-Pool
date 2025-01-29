#include <iostream>
#include "ThreadPool.hpp"

int main(void)
{
    ThreadPool myThreadPool(4);
    for (size_t i = 0; i < 20; ++i)
    {
        auto resultFutrue = myThreadPool.addTask([](const int32_t& _x, const int32_t& _y) -> int32_t
            {
                std::cout << "The current thread ID is " << std::this_thread::get_id() << "." << std::endl;
                return _x + _y;
            }, 10 * i, 10 * i);
        std::cout << "Thread Result: " << resultFutrue.get() << std::endl;
    }

    return 0;
}