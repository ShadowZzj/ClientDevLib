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

template <typename T> class ThreadPoolTask
{
  public:
    T id;
    static std::mutex mutex;
    std::function<void()> task;
    static T GetDefaultId();
};

template <> inline int ThreadPoolTask<int>::GetDefaultId()
{
    std::lock_guard<std::mutex> lock(mutex);
    static int id = 0;
    return id++;
}
template <> inline std::string ThreadPoolTask<std::string>::GetDefaultId()
{
    std::lock_guard<std::mutex> lock(mutex);
    static int id      = 0;
    std::string prefix = "zzj_threadpool_task_";

    return prefix + std::to_string(id++);
}

template <typename T> class ThreadPool
{
  public:
    ThreadPool(size_t threads);
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>;

    template <class F, class... Args>
    auto enqueueWithTaskId(const T &id, F &&f, Args &&...args)
        -> std::future<typename std::result_of<F(Args...)>::type>;
    void Stop();
    bool isTaskInQueue(const T &taskId);
    bool isTaskRunning(const T &taskId);
    ~ThreadPool();

  private:
    int taskDefaultId = -1;
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::vector<ThreadPoolTask<T>> readyTasks;
    std::vector<ThreadPoolTask<T>> runningTasks;
    // synchronization
    std::mutex defaultid_mutex;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// the constructor just launches some amount of workers
template <typename T> ThreadPool<T>::ThreadPool(size_t threads) : stop(false)
{
    for (size_t i = 0; i < threads; ++i)
        workers.emplace_back([this] {
            for (;;)
            {
                ThreadPoolTask<T> task;

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
template <typename T>
template <class F, class... Args>
auto ThreadPool<T>::enqueue(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
{
    std::unique_lock<std::mutex> lock(this->defaultid_mutex);
    return enqueueWithTaskId(ThreadPoolTask<T>::GetDefaultId(), f, args...);
}

template <typename T>
template <class F, class... Args>
auto ThreadPool<T>::enqueueWithTaskId(const T &id, F &&f, Args &&...args)
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

        ThreadPoolTask<T> poolTask;
        poolTask.id   = id;
        poolTask.task = [task]() { (*task)(); };
        readyTasks.push_back(poolTask);
    }
    condition.notify_one();
    return res;
}
template <typename T> void ThreadPool<T>::Stop()
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
template <typename T> ThreadPool<T>::~ThreadPool()
{
    Stop();
}

template <typename T> bool ThreadPool<T>::isTaskInQueue(const T &taskId)
{
    std::unique_lock<std::mutex> lock(queue_mutex);
    if (std::find_if(readyTasks.begin(), readyTasks.end(),
                     [taskId](auto readyTask) { return readyTask.id == taskId; }) != readyTasks.end())
        return true;
    else
        return false;
}
template <typename T> bool ThreadPool<T>::isTaskRunning(const T &taskId)
{
    std::unique_lock<std::mutex> lock(queue_mutex);
    if (std::find_if(runningTasks.begin(), runningTasks.end(),
                     [taskId](auto runningTask) { return runningTask.id == taskId; }) != runningTasks.end())
        return true;
    else
        return false;
}

} // namespace zvpro
