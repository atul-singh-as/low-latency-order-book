#include "OrderBook.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <random>

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    int N = argc > 1 ? std::atoi(argv[1]) : 1000000;
    {
        OrderBook b;
        auto s = Clock::now();
        for (int i = 0; i < N; ++i) b.addOrder({(uint64_t)i, Side::BUY, 1000 + (i & 255), 10});
        auto e = Clock::now();
        double sec = std::chrono::duration<double>(e - s).count();
        std::printf("  add (passive)                  %8.2f ns/op  %10.2f M ops/sec\n", sec * 1e9 / N, N / sec / 1e6);
    }
    {
        OrderBook b;
        for (int i = 0; i < N; ++i) b.addOrder({(uint64_t)i, Side::BUY, 100, 10});
        auto s = Clock::now();
        for (int i = 0; i < N; ++i) b.cancelOrder(i);
        auto e = Clock::now();
        double sec = std::chrono::duration<double>(e - s).count();
        std::printf("  cancel (sequential ids)        %8.2f ns/op  %10.2f M ops/sec\n", sec * 1e9 / N, N / sec / 1e6);
    }
    {
        OrderBook b;
        std::vector<uint64_t> ids(N, 0ull);
        for (int i = 0; i < N; ++i) { ids[i] = i; b.addOrder({(uint64_t)i, Side::BUY, 100 + (i % 64), 10}); }
        std::shuffle(ids.begin(), ids.end(), std::mt19937_64(12345));
        auto s = Clock::now();
        for (size_t i = 0; i < ids.size(); ++i) b.cancelOrder(ids[i]);
        auto e = Clock::now();
        double sec = std::chrono::duration<double>(e - s).count();
        std::printf("  cancel (random, 64 levels)     %8.2f ns/op  %10.2f M ops/sec\n", sec * 1e9 / N, N / sec / 1e6);
    }
    {
        OrderBook b;
        int M = N / 2;
        for (int i = 0; i < M; ++i) b.addOrder({(uint64_t)i, Side::SELL, 1000, 10});
        auto s = Clock::now();
        for (int i = 0; i < M; ++i) b.addOrder({(uint64_t)(M + i), Side::BUY, 1000, 10});
        auto e = Clock::now();
        double sec = std::chrono::duration<double>(e - s).count();
        std::printf("  match (1:1 full fills)         %8.2f ns/op  %10.2f M ops/sec\n", sec * 1e9 / M, M / sec / 1e6);
    }
}