#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

namespace std
{
#define MAX_THREAD_NUM 256
class threadpool
{
public:
    inline threadpool(unsigned short size = 4) : stoped(false)
    {
        idlThrNum = size < 1 ? 1 : size;
        for (size = 0; size < idlThrNum; ++size)
        {
            pool.emplace_back([this] {
                while (!stoped)
                {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(m_lock);
                        cv_task.wait(lock, [this] { return stoped.load() || !tasks.empty(); });
                        if (!stoped.load() && !tasks.empty())
                        {
                            task = std::move(tasks.front());
                            tasks.pop();
                        }else{
                            continue;
                        }
                    }
                    idlThrNum--;
                    task();
                    idlThrNum++;
                }
                idlThrNum--;
            });
        }
    }

    inline ~threadpool()
    {
        stoped.store(true);
        cv_task.notify_all();
        for (std::thread &t : pool)
        {
            if (t.joinable())
            {
                t.join();
            }
        }
    }

    template <typename F, typename... Args>
    auto commit(F &&f, Args &&... args) -> std::future<decltype(f(args...))>
    {
        if (stoped.load())
        {
            throw std::runtime_error("commit on Threadpool is stopped");
        }

        using RetType = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<RetType()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<RetType> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(m_lock);
            tasks.emplace([task] {
                (*task)();
            });
        }
        cv_task.notify_one();

        return future;
    }

    int idlCount() { return idlThrNum; }

private:
    using Task = std::function<void()>; //任务函数
    std::vector<std::thread> pool;      //线程池
    std::queue<Task> tasks;             //任务队列
    std::mutex m_lock;                  //互斥锁
    std::condition_variable cv_task;    //条件阻塞
    std::atomic<bool> stoped;           //是否关闭提交
    std::atomic<int> idlThrNum;         //空闲线程数量
};
} // namespace std