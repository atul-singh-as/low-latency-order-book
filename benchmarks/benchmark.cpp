#include "OrderBook.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <cstdlib>
#include <vector>

//==========================================================================
//  Benchmarks
//
//  Each benchmark separates *setup* from the *timed region*, warms the
//  caches, runs several repetitions and reports the best one. Only the
//  operation under test is inside the clock.
//==========================================================================

namespace {

using Clock = std::chrono::steady_clock;

// Stop the optimiser from deleting work whose result is unused.
template <typename T>
inline void doNotOptimise(T const& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    volatile T sink = value; (void)sink;
#endif
}

inline void clobber() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#endif
}

struct Result {
    double nsPerOp;
    double opsPerSec;
};

void report(const char* name, std::size_t ops, double bestSeconds) {
    const double nsPerOp   = bestSeconds * 1e9 / double(ops);
    const double opsPerSec = double(ops) / bestSeconds;

    std::printf("  %-34s %10zu ops   %8.2f ns/op   %12.2f M ops/sec\n",
                name, ops, nsPerOp, opsPerSec / 1e6);
}

//----------------------------------------------------------------------
// 1. Cancellation: the original benchmark, same shape.
//----------------------------------------------------------------------
void benchmarkCancellation(int numOrders, int reps) {
    OrderBook book;
    book.reserve(std::size_t(numOrders), uint64_t(numOrders));

    double best = 1e30;

    for (int r = 0; r < reps; ++r) {
        book.clear();
        for (int i = 0; i < numOrders; ++i)
            book.addOrder({uint64_t(i), Side::BUY, 100, 10});

        clobber();
        const auto start = Clock::now();

        for (int i = 0; i < numOrders; ++i)
            book.cancelOrder(uint64_t(i));

        const auto end = Clock::now();
        clobber();

        doNotOptimise(book.size());
        best = std::min(best, std::chrono::duration<double>(end - start).count());
    }

    report("cancel (sequential ids)", std::size_t(numOrders), best);
}

//----------------------------------------------------------------------
// 2. Cancellation in random order: defeats the prefetcher, this is the
//    realistic figure for a cancel-heavy feed.
//----------------------------------------------------------------------
void benchmarkCancellationRandom(int numOrders, int reps) {
    OrderBook book;
    book.reserve(std::size_t(numOrders), uint64_t(numOrders));

    const std::size_t n = std::size_t(numOrders);
    std::vector<uint64_t> ids(n, 0ull);
    for (int i = 0; i < numOrders; ++i) ids[std::size_t(i)] = uint64_t(i);
    std::shuffle(ids.begin(), ids.end(), std::mt19937_64(12345));

    double best = 1e30;

    for (int r = 0; r < reps; ++r) {
        book.clear();
        for (int i = 0; i < numOrders; ++i)
            book.addOrder({uint64_t(i), Side::BUY, 100 + (i % 64), 10});

        clobber();
        const auto start = Clock::now();

        for (std::size_t i = 0; i < ids.size(); ++i)
            book.cancelOrder(ids[i]);

        const auto end = Clock::now();
        clobber();

        doNotOptimise(book.size());
        best = std::min(best, std::chrono::duration<double>(end - start).count());
    }

    report("cancel (random order, 64 levels)", std::size_t(numOrders), best);
}

//----------------------------------------------------------------------
// 3. Passive inserts: non-crossing limit orders spread over price levels.
//----------------------------------------------------------------------
void benchmarkAddPassive(int numOrders, int reps) {
    OrderBook book;
    book.reserve(std::size_t(numOrders), uint64_t(numOrders));

    double best = 1e30;

    for (int r = 0; r < reps; ++r) {
        book.clear();

        clobber();
        const auto start = Clock::now();

        for (int i = 0; i < numOrders; ++i)
            book.addOrder({uint64_t(i), Side::BUY, 1000 + (i & 255), 10});

        const auto end = Clock::now();
        clobber();

        doNotOptimise(book.size());
        best = std::min(best, std::chrono::duration<double>(end - start).count());
    }

    report("add (passive limit orders)", std::size_t(numOrders), best);
}

//----------------------------------------------------------------------
// 4. Aggressive orders that fully cross and remove a resting order each.
//----------------------------------------------------------------------
void benchmarkMatching(int numOrders, int reps) {
    OrderBook book;
    book.reserve(std::size_t(numOrders) * 2, uint64_t(numOrders) * 2);

    double best = 1e30;

    for (int r = 0; r < reps; ++r) {
        book.clear();
        for (int i = 0; i < numOrders; ++i)
            book.addOrder({uint64_t(i), Side::SELL, 1000, 10});

        clobber();
        const auto start = Clock::now();

        for (int i = 0; i < numOrders; ++i)
            book.addOrder({uint64_t(numOrders + i), Side::BUY, 1000, 10});

        const auto end = Clock::now();
        clobber();

        doNotOptimise(book.tradedQuantity());
        best = std::min(best, std::chrono::duration<double>(end - start).count());
    }

    report("match (1:1 full fills)", std::size_t(numOrders), best);
}

//----------------------------------------------------------------------
// 5. Realistic mixed feed: ~60% add, ~30% cancel, ~10% aggressive.
//    Ids and prices are precomputed so the RNG is not inside the clock.
//----------------------------------------------------------------------
struct Op {
    uint8_t  kind;    // 0 add passive, 1 cancel, 2 aggress
    uint64_t id;
    int32_t  price;
    int32_t  qty;
    uint8_t  side;
};

void benchmarkMixed(int numOps, int reps) {
    std::mt19937_64 rng(98765);
    std::vector<Op> ops;
    ops.reserve(std::size_t(numOps));

    std::vector<uint64_t> live;
    live.reserve(std::size_t(numOps));
    uint64_t nextId = 0;

    for (int i = 0; i < numOps; ++i) {
        const int roll = int(rng() % 100);
        if (roll < 60 || live.empty()) {
            const bool buy = (rng() & 1) != 0;
            Op o{0, nextId++, int32_t(buy ? 990 + int(rng() % 10)
                                          : 1001 + int(rng() % 10)),
                 int32_t(1 + rng() % 50), uint8_t(buy ? 0 : 1)};
            ops.push_back(o);
            live.push_back(o.id);
        } else if (roll < 90) {
            const std::size_t k = std::size_t(rng() % live.size());
            ops.push_back(Op{1, live[k], 0, 0, 0});
            live[k] = live.back();
            live.pop_back();
        } else {
            const bool buy = (rng() & 1) != 0;
            ops.push_back(Op{2, nextId++, int32_t(buy ? 1010 : 985),
                             int32_t(1 + rng() % 30), uint8_t(buy ? 0 : 1)});
        }
    }

    OrderBook book;
    book.reserve(std::size_t(numOps), nextId);

    double best = 1e30;

    for (int r = 0; r < reps; ++r) {
        book.clear();

        clobber();
        const auto start = Clock::now();

        for (std::size_t i = 0; i < ops.size(); ++i) {
            const Op& o = ops[i];
            if (o.kind == 1) {
                book.cancelOrder(o.id);
            } else {
                book.addOrder({o.id, Side(o.side), o.price, o.qty});
            }
        }

        const auto end = Clock::now();
        clobber();

        doNotOptimise(book.size());
        best = std::min(best, std::chrono::duration<double>(end - start).count());
    }

    report("mixed feed (60/30/10)", std::size_t(numOps), best);
}

//----------------------------------------------------------------------
// 6. Per-operation latency distribution for cancel.
//----------------------------------------------------------------------
void benchmarkCancelLatency(int numOrders) {
    OrderBook book;
    book.reserve(std::size_t(numOrders), uint64_t(numOrders));

    const std::size_t n = std::size_t(numOrders);
    std::vector<uint64_t> ids(n, 0ull);
    for (int i = 0; i < numOrders; ++i) ids[std::size_t(i)] = uint64_t(i);
    std::shuffle(ids.begin(), ids.end(), std::mt19937_64(4242));

    std::vector<uint32_t> samples(std::size_t(numOrders), 0u);

    book.clear();
    for (int i = 0; i < numOrders; ++i)
        book.addOrder({uint64_t(i), Side::BUY, 100 + (i % 64), 10});

    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto t0 = Clock::now();
        book.cancelOrder(ids[i]);
        const auto t1 = Clock::now();
        samples[i] = uint32_t(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    std::sort(samples.begin(), samples.end());
    const auto pct = [&](double p) {
        return samples[std::size_t(double(samples.size() - 1) * p)];
    };

    std::printf("  %-34s p50=%u ns  p99=%u ns  p99.9=%u ns  max=%u ns\n",
                "cancel latency (clock-inclusive)",
                pct(0.50), pct(0.99), pct(0.999), samples.back());
    std::printf("    (each sample includes ~2 clock reads; treat as an upper bound)\n");
}

} // namespace


int main(int argc, char** argv) {
    int numOrders = 1'000'000;
    int reps      = 5;

    if (argc > 1) numOrders = std::atoi(argv[1]);
    if (argc > 2) reps      = std::atoi(argv[2]);

    std::printf("\nLow-latency order book benchmark\n");
    std::printf("  orders per run: %d, repetitions: %d (best run reported)\n\n",
                numOrders, reps);

    benchmarkAddPassive(numOrders, reps);
    benchmarkCancellation(numOrders, reps);
    benchmarkCancellationRandom(numOrders, reps);
    benchmarkMatching(numOrders / 2, reps);
    benchmarkMixed(numOrders, reps);

    std::printf("\n");
    benchmarkCancelLatency(std::min(numOrders, 1'000'000));
    std::printf("\n");

    return 0;
}