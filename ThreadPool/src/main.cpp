#include <iostream>
#include "ThreadPool.hpp"

int main()
{
    ThreadPool pool(4);

    auto result = pool.enqueue([](int a, int b) {
        return a + b;
    }, 3, 7);

    std::cout << "Result: " << result.get() << std::endl;

    return 0;
}