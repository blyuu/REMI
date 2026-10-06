#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace remi {
// CPU-only worker pool. The owner must wait for jobs that capture external state
// before that state is destroyed. Destruction drains queued jobs and joins workers.
class JobSystem {
public:
    explicit JobSystem(unsigned workers = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] std::size_t WorkerCount() const noexcept { return workers_.size(); }

    template<class Function>
    auto Submit(Function&& function) -> std::future<std::invoke_result_t<Function>> {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
        auto result = task->get_future();
        {
            std::lock_guard lock(mutex_);
            if (stopping_) throw std::logic_error("JobSystem is stopping");
            queue_.emplace_back([task] { (*task)(); });
        }
        ready_.notify_one();
        return result;
    }

    // Call from the owning/main thread. Jobs must not wait on other jobs from
    // the same pool; all submitted ranges finish before an error is rethrown.
    void ParallelFor(std::size_t count, std::size_t grain,
                     const std::function<void(std::size_t, std::size_t)>& work);
private:
    void Worker();
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
};
}
