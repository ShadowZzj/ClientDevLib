#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace zvpro
{

class ThreadPoolTask
{
  public:
    int id;
    std::function<void()> task;
};

class ThreadPool
{
  public:
    ThreadPool(size_t);
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>;

    template <class F, class... Args>
    auto enqueueWithTaskId(int id, F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>;
    void Stop();
    bool isTaskInQueue(int taskId);
    bool isTaskRunning(int taskId);
    ~ThreadPool();

  private:
    int taskDefaultId = -1;
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::vector<ThreadPoolTask> readyTasks;
    std::vector<ThreadPoolTask> runningTasks;
    // synchronization
    std::mutex defaultid_mutex;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads) : stop(false)
{
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back([this] {
            for (;;)
            {
                ThreadPoolTask task;

                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] { return this->stop || !this->readyTasks.empty(); });
                    if (stop && readyTasks.empty())
                        return;
                    task = std::move(this->readyTasks.front());
                    readyTasks.erase(readyTasks.begin());
                    runningTasks.push_back(task);
                }

                task.task();
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    auto iter = std::find_if(runningTasks.begin(), runningTasks.end(),
                                             [&task](auto runningTask) { return runningTask.id == task.id; });
                    runningTasks.erase(iter);
                }
            }
        });
}

// add new work item to the pool
template <class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
{
    std::unique_lock<std::mutex> lock(this->defaultid_mutex);
    return enqueueWithTaskId(taskDefaultId--, f, args...);
}

template <class F, class... Args>
auto ThreadPool::enqueueWithTaskId(int id, F &&f, Args &&...args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task =
        std::make_shared<std::packaged_task<return_type()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        ThreadPoolTask poolTask;
        poolTask.id = id;
        poolTask.task = [task]() { (*task)(); };
        readyTasks.push_back(poolTask);
    }
    condition.notify_one();
    return res;
}

inline void ThreadPool::Stop()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers)
        worker.join();
    workers.clear();
}
// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    Stop();
}

inline bool ThreadPool::isTaskInQueue(int taskId)
{
    std::unique_lock<std::mutex> lock(queue_mutex);
    if (std::find_if(readyTasks.begin(), readyTasks.end(),
                     [taskId](auto readyTask) { return readyTask.id == taskId; }) != readyTasks.end())
        return true;
    else
        return false;
}

inline bool ThreadPool::isTaskRunning(int taskId)
{
    std::unique_lock<std::mutex> lock(queue_mutex);
    if (std::find_if(runningTasks.begin(), runningTasks.end(),
                  [taskId](auto runningTask) { return runningTask.id == taskId; }) != runningTasks.end())
        return true;
    else
        return false;
}

} // namespace zvpro
