# Hand-written Makefile for the low-latency order book.
#
# The Makefile you had was CMake-generated ("DO NOT EDIT"). This one is
# standalone: `make` works with no CMake at all. A CMakeLists.txt is also
# provided if you prefer that route.
#
#   make            build everything into bin/
#   make test       build and run the test suite (asserts ON)
#   make bench      build and run the benchmark (asserts OFF, -O3 -flto)
#   make run        run the demo
#   make clean

CXX      ?= c++
CXXSTD   ?= -std=c++20
INCLUDES := -Iinclude

# Portable-but-aggressive baseline.
OPT      := -O3 -fno-plt -fomit-frame-pointer -fno-semantic-interposition

# -march=native is the single biggest free win; disable with NATIVE=0 if you
# need a binary that runs on other machines.
NATIVE   ?= 1
ifeq ($(NATIVE),1)
  ARCHFLAGS := $(shell $(CXX) -march=native -E -x c++ /dev/null >/dev/null 2>&1 && echo -march=native)
  ifeq ($(ARCHFLAGS),)
    # Apple Silicon clang wants -mcpu instead of -march
    ARCHFLAGS := $(shell $(CXX) -mcpu=native -E -x c++ /dev/null >/dev/null 2>&1 && echo -mcpu=native)
  endif
endif

# Link-time optimisation so the cold .cpp can still be inlined where useful.
LTOFLAGS ?= -flto

WARN     := -Wall -Wextra -Wpedantic

BENCH_CXXFLAGS := $(CXXSTD) $(INCLUDES) $(OPT) $(ARCHFLAGS) $(LTOFLAGS) $(WARN) -DNDEBUG
TEST_CXXFLAGS  := $(CXXSTD) $(INCLUDES) -O2 $(ARCHFLAGS) $(WARN) -g
DEMO_CXXFLAGS  := $(BENCH_CXXFLAGS)

BIN := bin
SRC := src/OrderBook.cpp

.PHONY: all test bench run clean asm

all: $(BIN)/order_book $(BIN)/order_book_tests $(BIN)/order_book_benchmark

$(BIN):
	@mkdir -p $(BIN)

$(BIN)/order_book: src/main.cpp $(SRC) include/OrderBook.h include/Order.h | $(BIN)
	$(CXX) $(DEMO_CXXFLAGS) src/main.cpp $(SRC) -o $@

$(BIN)/order_book_tests: tests/test_order_book.cpp $(SRC) include/OrderBook.h include/Order.h | $(BIN)
	$(CXX) $(TEST_CXXFLAGS) tests/test_order_book.cpp $(SRC) -o $@

$(BIN)/order_book_benchmark: benchmarks/benchmark.cpp $(SRC) include/OrderBook.h include/Order.h | $(BIN)
	$(CXX) $(BENCH_CXXFLAGS) benchmarks/benchmark.cpp $(SRC) -o $@

test: $(BIN)/order_book_tests
	./$(BIN)/order_book_tests

bench: $(BIN)/order_book_benchmark
	./$(BIN)/order_book_benchmark

run: $(BIN)/order_book
	./$(BIN)/order_book

# Dump the generated code for the hot path, handy when tuning.
asm: | $(BIN)
	$(CXX) $(BENCH_CXXFLAGS) -fno-lto -S -masm=intel benchmarks/benchmark.cpp -o $(BIN)/benchmark.s
	@echo "wrote $(BIN)/benchmark.s"

clean:
	rm -rf $(BIN)