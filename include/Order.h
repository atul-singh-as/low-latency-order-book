#pragma once

#include <cstdint>

enum class Side : uint8_t {
    BUY  = 0,
    SELL = 1
};

// Public, caller-facing order description.
// Kept layout-compatible with the original project so existing
// aggregate initialisation `{id, side, price, quantity}` still works.
struct Order {
    uint64_t id;
    Side     side;
    int      price;
    int      quantity;
};