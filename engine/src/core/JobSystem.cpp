#include <remi/core/JobSystem.hpp>
#include <algorithm>
#include <exception>

namespace remi {
JobSystem::JobSystem(unsigned workers) {
    if (!workers) {
        const unsigned available = std::thread::hardware_concurrency();
        workers = std::clamp(available > 1 ? available - 1 : 1u, 1u, 8u);
    }
    try {
        workers_.reserve(workers);
        for (unsigned i = 0; i < workers; ++i) workers_.emplace_back([this] { Worker(); });
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_) worker.join();
        throw;
    }
}
JobSystem::~JobSystem() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_) worker.join();
}
void JobSystem::Worker() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        job(); // packaged_task stores exceptions in its future.
    }
}
void JobSystem::ParallelFor(std::size_t count, std::size_t grain,
                            const std::function<void(std::size_t, std::size_t)>& work) {
    if (!grain || !work) throw std::invalid_argument("ParallelFor needs work and a nonzero grain");
    if (!count) return;
    const std::size_t chunks = 1 + (count - 1) / grain;
    if (chunks == 1) { work(0,count); return; }
    std::vector<std::future<void>> pending;
    pending.reserve(chunks - 1);
    std::exception_ptr failure;
    try {
        for (std::size_t chunk = 1; chunk < chunks; ++chunk) {
            const auto begin = chunk * grain;
            pending.push_back(Submit([&,begin] { work(begin,std::min(count,begin+grain)); }));
        }
        work(0,std::min(count,grain));
    } catch (...) { failure = std::current_exception(); }
    for (auto& job : pending) {
        try { job.get(); } catch (...) { if (!failure) failure = std::current_exception(); }
    }
    if (failure) std::rethrow_exception(failure);
}
}
