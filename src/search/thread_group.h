// Exception-safe owner for the Lazy SMP helper threads.
#ifndef NIGHTWING_SEARCH_THREAD_GROUP_H
#define NIGHTWING_SEARCH_THREAD_GROUP_H

#include <atomic>
#include <cstddef>
#include <new>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace nightwing::search {

/// Owns a set of worker threads that all watch one shared stop flag, and
/// guarantees they are stopped and joined on EVERY exit path, including a
/// C++ exception unwinding through the owning scope. A plain
/// `std::vector<std::thread>` does not: destroying a still-joinable
/// std::thread calls std::terminate(), so any exception thrown on the main
/// search thread while helpers were running aborted the whole engine
/// (external review, 2026-10-22). Thread creation can also fail at run time
/// (resource limits), which try_spawn() reports as `false` instead of
/// throwing, so the search can carry on with fewer helpers.
///
/// `ThreadT` is std::thread in production; tests substitute a fake type to
/// inject creation failures. The owning scope must declare this object
/// AFTER every object the workers reference, so it is destroyed (and the
/// workers joined) first.
template <typename ThreadT = std::thread>
class BasicJoiningThreadGroup {
  public:
    /// `stop_flag` is set by stop_and_join(); workers must poll it.
    explicit BasicJoiningThreadGroup(std::atomic<bool>& stop_flag) noexcept
        : stop_flag_(stop_flag) {}

    BasicJoiningThreadGroup(const BasicJoiningThreadGroup&) = delete;
    BasicJoiningThreadGroup& operator=(const BasicJoiningThreadGroup&) = delete;

    ~BasicJoiningThreadGroup() { stop_and_join(); }

    /// Starts one worker constructed from `args`. Returns false, leaving the
    /// group unchanged, if the operating system refused to create the thread
    /// (std::system_error) or memory ran out (std::bad_alloc).
    template <typename... Args>
    [[nodiscard]] bool try_spawn(Args&&... args) {
        try {
            threads_.emplace_back(std::forward<Args>(args)...);
            return true;
        } catch (const std::system_error&) {
            return false;
        } catch (const std::bad_alloc&) {
            return false;
        }
    }

    /// Raises the stop flag and joins every worker. Safe to call repeatedly
    /// and from the destructor; never throws.
    void stop_and_join() noexcept {
        stop_flag_.store(true, std::memory_order_relaxed);
        for (ThreadT& t : threads_) {
            if (t.joinable()) {
                try {
                    t.join();
                } catch (...) {
                    // join() failing means the thread is unjoinable in
                    // practice; nothing safe is left to do.
                }
            }
        }
    }

    /// Number of workers successfully started.
    [[nodiscard]] std::size_t size() const noexcept { return threads_.size(); }

  private:
    std::atomic<bool>& stop_flag_;
    std::vector<ThreadT> threads_;
};

using JoiningThreadGroup = BasicJoiningThreadGroup<std::thread>;

} // namespace nightwing::search

#endif // NIGHTWING_SEARCH_THREAD_GROUP_H
