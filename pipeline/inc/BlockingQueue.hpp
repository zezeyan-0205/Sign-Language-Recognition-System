#ifndef BLOCKING_QUEUE_HPP
#define BLOCKING_QUEUE_HPP

#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T>
class BlockingQueue {
public:
    // 将数据放入队列后唤醒一个等待中的消费者线程。
    void push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        condition_.notify_one();
    }

    // 队列为空时阻塞等待，直到生产者写入新的数据。
    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] {
            return !queue_.empty();
        });

        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable condition_;
};

#endif
