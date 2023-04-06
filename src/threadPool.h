#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <future>
#include <memory>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include <functional>
#include <condition_variable>

namespace UT
{
    class ThreadPool final
    {
    public:
        explicit ThreadPool(size_t thread_num = std::thread::hardware_concurrency()) : stop(false)
        {
            if (!thread_num)
                throw std::invalid_argument("thread num should be more than zero");

            this->workers.reserve(thread_num);

            for (size_t i = 0; i < thread_num; ++i)
            {
                workers.emplace_back(
                    [this]
                    {
                        while (true)
                        {
                            std::function<void()> task;
                            {
                                std::unique_lock<std::mutex> lock(this->queue_mutex);
                                this->condition.wait(lock, [this]
                                                     { return this->stop || !this->tasks.empty(); });
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

        ~ThreadPool() noexcept
        {
            {
                std::unique_lock<std::mutex> lock(this->queue_mutex);
                stop = true;
            }
            condition.notify_all();
            for (auto &worker : workers)
                worker.join();
        };

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;
        ThreadPool(ThreadPool &&) = delete;
        ThreadPool &operator=(ThreadPool &&) = delete;

        template <class F, class... Args>
        auto enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
        {
            using return_type = typename std::result_of<F(Args...)>::type;
            using packaged_task_t = std::packaged_task<return_type()>;

            std::shared_ptr<packaged_task_t> task(new packaged_task_t(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)));

            auto res = task->get_future();

            {
                std::unique_lock<std::mutex> lock(this->queue_mutex);

                if(stop)
                    throw std::runtime_error("not allow to enqueue on stopped threadpool")

                this->tasks.emplace([task]()
                                    { (*task)(); });
            }
            this->condition.notify_one();
            return res;
        };

    private:
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;

        std::mutex queue_mutex;
        std::condition_variable condition;
        std::atomic_bool stop;
    };
}