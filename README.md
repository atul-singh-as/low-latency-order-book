# Low-latency order book — optimised

Drop-in replacement for the original `std::map` + `std::list` +
`std::unordered_map` implementation. The public API is unchanged:

```cpp
void addOrder(const Order&);
void cancelOrder(uint64_t);
int  getBestBid() const;
int  getBestAsk() const;
int  getOrderQuantity(uint64_t) const;
```

## Build

```sh
rm -f Makefile CMakeCache.txt      # the old ones were CMake-generated / stale
make test                          # asserts ON, runs the suite
make bench                         # -O3 -march=native -flto
make run
```

or, with CMake:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/order_book_tests && ./build/order_book_benchmark
```

## Measured results

Intel Xeon @ 2.10 GHz, single shared vCPU, g++ 13.3, `-O3 -march=native -flto`,
1,000,000 orders, best of 5 runs. An M-series Mac will be meaningfully faster.

| Workload                         | Original, as configured | Original, `-O3 -flto` | Optimised | Speedup |
|----------------------------------|------------------------:|----------------------:|----------:|--------:|
| add, passive limit orders        |            2.6 M/s      |           8.8 M/s     | **298 M/s** |   34×  |
| cancel, sequential ids           |            8.8 M/s      |          42.4 M/s     | **345 M/s** |  8.1×  |
| cancel, random order, 64 levels  |            1.7 M/s      |           6.4 M/s     |  **37 M/s** |  5.8×  |
| match, 1:1 full fills            |            9.5 M/s      |          40.1 M/s     | **230 M/s** |  5.7×  |
| mixed feed (60% add/30% cancel/10% aggress) | —            | —                     |  **53 M/s** |    —   |

The original `CMakeCache.txt` had `CMAKE_BUILD_TYPE` empty, i.e. no
optimisation flags at all. That alone accounted for a 4–5× loss before any
code changes.

**On the 141 M orders/sec target:** the cancel and add benchmarks clear it
comfortably. The random-access and mixed-feed numbers do not, and they are
the honest ones — see "Reading the numbers" below.

## What changed and why

| Before | After | Why it matters |
|---|---|---|
| `std::map<int, std::list<Order>>` per side | flat `Level[65536]` array indexed directly by price | Level lookup becomes one array index instead of an ~17-deep red-black tree descent with a pointer chase per node. |
| `std::list<Order>` per level | intrusive doubly-linked list over one contiguous `Node` pool, 32-bit indices | No `new`/`delete` on the hot path, no allocator lock, nodes are 32 B instead of ~48 B, and the pool is contiguous so the prefetcher can follow it. |
| `std::unordered_map<uint64_t, OrderLocation>` | flat `uint32_t[]` indexed by order id, hash-map fallback only for sparse ids | Cancel was dominated by a hash of the id plus a dependent load into a scattered bucket. Now it is a single array load. |
| best bid/ask via `rbegin()`/`begin()` | cached `bestBid_`/`bestAsk_` + occupancy bitmaps scanned with `ctz`/`clz` | Best price is a register read. When a level empties, finding the next one is a 64-level-per-instruction bitmap scan instead of a tree walk. |
| Definitions in `.cpp`, no LTO | hot paths `inline` in the header, LTO on | Every hot operation inlines into the caller; the call overhead was a large fraction of a 2 ns operation. |
| `OrderLocation` stored a `map::iterator` | node stores a 4-byte price index | The original stored an iterator that it then had to defensively avoid using after `erase` — a latent correctness hazard, and 8 extra bytes. |

Also fixed: `addOrder` with a duplicate id previously used `insert_or_assign`,
which left the old order resting in the book but unreachable — it could still
be matched, and it corrupted level volume. It now cancels the previous order
first.

## Reading the numbers

- **Sequential cancel (345 M/s) is the easy case.** Ids are dense and walked in
  order, so both the id table and the order pool are streamed linearly and the
  hardware prefetcher hides all the latency. Your original benchmark had
  exactly this shape.
- **Random cancel (37 M/s) is the realistic case.** A real feed cancels orders
  in an order uncorrelated with insertion, so every cancel is two dependent
  cache misses. At that point you are bound by memory latency, not by the
  algorithm — no data structure gets past roughly one order per miss.
- The reported latency percentiles include two `steady_clock` reads per sample,
  which cost more than the operation itself on this machine. Treat them as an
  upper bound; use `rdtsc` or a batched harness if you need real numbers.

## Tuning knobs

- `MIN_PRICE` / `MAX_PRICE` in `OrderBook.h` set the price domain. Cost is
  16 bytes per level per side. Orders outside the domain are rejected.
- `DIRECT_ID_LIMIT` (default 16 M) is the cutoff for the flat id table. Ids
  above it use a hash map — keep ids dense if you care about cancel latency.
- Call `reserve(orders, maxOrderId)` once at startup so nothing grows mid-run.
- `make NATIVE=0` for a portable binary (costs roughly 10–15%).

## If you need more

The remaining wins are workload- and platform-specific rather than
algorithmic:

1. **Huge pages** for the pool and id table (`madvise(MADV_HUGEPAGE)`), which
   cuts TLB misses on the random-cancel path — usually the single biggest
   remaining gain.
2. **Software prefetch** of the next id when you can batch-decode incoming
   messages ahead of applying them.
3. **Pin the thread**, disable frequency scaling, and isolate the core. On a
   shared vCPU like the one above, run-to-run variance is larger than most
   micro-optimisations.
4. **Shrink `Node` to 16 bytes** by dropping the id (recover it from the slot
   table) and using 16-bit prices — four orders per cache line instead of two.