#include "concurrency/inc/ThreadPool.hpp"

ThreadPool::ThreadPool(size_t num_threads) : stop(false)
{
    // 创建固定数量的工作线程，每个线程持续从任务队列里取任务执行。
    for (size_t i = 0; i < num_threads; ++i)
    {
        workers.emplace_back([this] {
            while (true)
            {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    // 没有任务时阻塞等待；收到 stop 或新任务时被唤醒。
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });

                    // stop 后仍会处理队列里剩余任务，全部处理完再退出线程。
                    if (this->stop && this->tasks.empty())
                        return;

                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }

                task();
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // 通知所有工作线程进入收尾状态。
        stop = true;
    }

    condition.notify_all();

    // 等待后台线程退出，避免析构后线程继续访问已释放的对象成员。
    for (std::thread &worker : workers)
        worker.join();
}
