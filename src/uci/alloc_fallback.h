// Header-only halve-and-retry allocation policy shared by the UCI layer's
// persistent transposition table. Kept separate from uci.cpp so the policy
// can be unit-tested with an injected failing allocator instead of having
// to exhaust real memory.
#ifndef NIGHTWING_UCI_ALLOC_FALLBACK_H
#define NIGHTWING_UCI_ALLOC_FALLBACK_H

#include <cstddef>
#include <new>

namespace nightwing::uci {

/// Calls `attempt(size_mb)` with `requested_mb` (raised to at least 1) and,
/// whenever it throws std::bad_alloc, retries with half the size (never
/// below 1 MB). Returns the size in MB for which `attempt` finally
/// succeeded, or 0 if even a 1 MB attempt failed (the caller must then treat
/// whatever `attempt` was filling as unusable). Any exception other than
/// std::bad_alloc propagates unchanged. Never throws std::bad_alloc itself:
/// the former contract (rethrow on the last attempt) left callers holding
/// an empty std::optional after std::optional::emplace() had already
/// destroyed the previous table (external review, 2026-10-22).
template <typename Attempt>
[[nodiscard]] std::size_t allocate_with_halving(std::size_t requested_mb, Attempt&& attempt) {
    std::size_t size_mb = requested_mb < 1 ? 1 : requested_mb;
    while (true) {
        try {
            attempt(size_mb);
            return size_mb;
        } catch (const std::bad_alloc&) {
            if (size_mb <= 1) {
                return 0; // Nothing smaller left to try.
            }
            size_mb /= 2;
            if (size_mb < 1) {
                size_mb = 1;
            }
        }
    }
}

} // namespace nightwing::uci

#endif // NIGHTWING_UCI_ALLOC_FALLBACK_H
