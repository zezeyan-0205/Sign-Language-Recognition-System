#ifndef _ThreadPool_hpp
#define _ThreadPool_hpp

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // 提交任意可调用任务到线程池，并通过 future 取回任务返回值。
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

private:
    // workers 持有真正执行任务的后台线程。
    std::vector<std::thread> workers;
    // tasks 保存等待执行的任务闭包。
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    // stop 为 true 时，线程池不再接受新任务，并让工作线程在队列清空后退出。
    bool stop;
};

// 模板函数必须写在头文件，否则不同调用类型无法在编译期实例化。
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    // packaged_task 把普通函数包装成可以返回 future 的异步任务。
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        // 统一擦除成 void()，便于放入同一个任务队列。
        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}

#endif
