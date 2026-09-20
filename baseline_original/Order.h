#pragma once

#include <cstdint>

enum class Side {
    BUY,
    SELL
};

struct Order {
    uint64_t id;
    Side side;
    int price;
    int quantity;
};