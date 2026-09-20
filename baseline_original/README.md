# Baseline (original, unoptimised) implementation

This is the original `std::map` + `std::list` + `std::unordered_map` order
book, kept here only so the "Measured results" table in the top-level
README can be regenerated on any machine for an honest same-machine
comparison. It is not part of the optimised library or its build.

```sh
cd baseline_original
c++ -std=c++20 -I. -O3 -mcpu=native -flto -DNDEBUG bench_orig.cpp OrderBook.cpp -o bench_orig
./bench_orig 1000000
```

On Apple Silicon, use `-mcpu=native` instead of `-march=native`.