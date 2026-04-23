#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

namespace utx::common {
    /** A simple thread pool implementation that allows for concurrent execution of tasks.
     * The ThreadPool class manages a fixed number of worker threads that continuously
     * fetch and execute tasks from a shared task queue. Tasks can be enqueued using the
     * `enqueue` method, which returns a future that can be used to retrieve the result
     * of the task once it has been executed.
     *
     * The thread pool is designed to be simple and efficient, with proper synchronization
     * to ensure thread safety when accessing the task queue. It also provides a clean
     * shutdown mechanism to gracefully stop all worker threads when the pool is destroyed.
     */
    class ThreadPool {
    public:
        /** Constructor that initializes the thread pool with a specified number of worker threads.
         * @param threads The number of worker threads to create in the pool
         */
        explicit ThreadPool(const size_t threads)
            : stop_(false)
        {
            for (size_t i = 0; i < threads; ++i) {
                workers_.emplace_back([this]() {
                    while (true) {
                        std::function<void()> task;

                        {
                            std::unique_lock lock(mutex_);
                            cv_.wait(lock, [this]() {
                                return stop_ || !tasks_.empty();
                            });

                            if (stop_ && tasks_.empty()) return;

                            task = std::move(tasks_.front());
                            tasks_.pop();
                        }

                        task();
                    }
                });
            }
        }
        /** Destructor that stops all worker threads and cleans up resources.
         * It sets the stop flag, notifies all worker threads, and waits for them to finish.
         */
        ~ThreadPool() {
            {
                std::lock_guard lock(mutex_);
                stop_ = true;
            }
            cv_.notify_all();

            for (auto& t : workers_) {
                if (t.joinable()) t.join();
            }
        }
        /** Enqueue a task for execution in the thread pool.
         * @tparam F The type of the task function
         * @param f The task function to execute
         * @return A future that can be used to retrieve the result of the task
         */
        template<typename F>
        auto enqueue(F&& f) -> std::future<void> {
            auto task = std::make_shared<std::packaged_task<void()>>(std::forward<F>(f));
            std::future<void> res = task->get_future();

            {
                std::lock_guard lock(mutex_);
                tasks_.emplace([task]() { (*task)(); });
            }

            cv_.notify_one();
            return res;
        }

    private:
        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> tasks_;

        std::mutex mutex_;
        std::condition_variable cv_;
        bool stop_;
    };
}