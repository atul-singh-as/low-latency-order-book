cmake_minimum_required(VERSION 3.16)
project(LowLatencyOrderBook CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Your CMakeCache.txt had CMAKE_BUILD_TYPE empty, which means *no optimisation
# flags at all*. That alone was costing you roughly an order of magnitude.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

include(CheckCXXCompilerFlag)

add_library(order_book_lib STATIC src/OrderBook.cpp)
target_include_directories(order_book_lib PUBLIC include)

# -march=native (gcc/clang on x86) or -mcpu=native (Apple clang on arm64).
check_cxx_compiler_flag("-march=native" HAVE_MARCH_NATIVE)
check_cxx_compiler_flag("-mcpu=native"  HAVE_MCPU_NATIVE)

if(HAVE_MARCH_NATIVE)
  target_compile_options(order_book_lib PUBLIC $<$<NOT:$<CONFIG:Debug>>:-march=native>)
elseif(HAVE_MCPU_NATIVE)
  target_compile_options(order_book_lib PUBLIC $<$<NOT:$<CONFIG:Debug>>:-mcpu=native>)
endif()

target_compile_options(order_book_lib PUBLIC
  $<$<NOT:$<CONFIG:Debug>>:-O3 -fomit-frame-pointer>
  -Wall -Wextra)

add_executable(order_book           src/main.cpp)
add_executable(order_book_benchmark benchmarks/benchmark.cpp)
add_executable(order_book_tests     tests/test_order_book.cpp)

target_link_libraries(order_book           PRIVATE order_book_lib)
target_link_libraries(order_book_benchmark PRIVATE order_book_lib)
target_link_libraries(order_book_tests     PRIVATE order_book_lib)

# The tests rely on assert(), so they must not be built with NDEBUG.
target_compile_options(order_book_tests PRIVATE -UNDEBUG)
set_target_properties(order_book_tests PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION FALSE)

enable_testing()
add_test(NAME order_book_tests COMMAND order_book_tests)