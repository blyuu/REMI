#include <remi/core/JobSystem.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    try {
        remi::JobSystem jobs(2);
        Check(jobs.WorkerCount() == 2,"Worker count mismatch");
        std::promise<void> release;
        auto gate = release.get_future().share();
        std::atomic<unsigned> entered{0};
        auto first = jobs.Submit([&] { ++entered; gate.wait(); return std::this_thread::get_id(); });
        auto second = jobs.Submit([&] { ++entered; gate.wait(); return std::this_thread::get_id(); });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (entered.load() != 2 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        const bool concurrent = entered.load() == 2;
        release.set_value();
        Check(concurrent && first.get() != second.get(),"Jobs did not run on separate workers");

        auto failed = jobs.Submit([]() -> int { throw std::runtime_error("expected"); });
        bool propagated = false;
        try { (void)failed.get(); } catch (const std::runtime_error&) { propagated = true; }
        Check(propagated,"Job exception was not delivered through the future");

        std::vector<std::atomic<unsigned>> visits(41);
        jobs.ParallelFor(visits.size(),3,[&](std::size_t begin,std::size_t end) {
            for (auto i = begin; i < end; ++i) ++visits[i];
        });
        for (const auto& visit : visits) Check(visit.load() == 1,"ParallelFor skipped or duplicated work");
        propagated = false;
        try {
            jobs.ParallelFor(12,1,[&](std::size_t begin,std::size_t) {
                ++visits[begin];
                if (begin == 5) throw std::runtime_error("expected");
            });
        } catch (const std::runtime_error&) { propagated = true; }
        Check(propagated,"ParallelFor lost a worker exception");
        for (std::size_t i = 0; i < 12; ++i)
            Check(visits[i].load() == 2,"ParallelFor returned before all ranges completed");

        std::atomic<unsigned> drained{0};
        {
            remi::JobSystem temporary(1);
            for (unsigned i = 0; i < 32; ++i) (void)temporary.Submit([&] { ++drained; });
        }
        Check(drained.load() == 32,"Pool destruction dropped queued jobs");
        std::cout << "Worker concurrency, exceptions, ranges and shutdown passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
