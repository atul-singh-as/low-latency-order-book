#include "OrderBook.h"

#include <algorithm>

//==========================================================================
//  Cold paths only.
//
//  Everything on the hot path (addOrder / cancelOrder / getOrderQuantity /
//  matching) is defined inline in OrderBook.h so it inlines straight into
//  the caller. What lives here is construction, container growth, sparse-id
//  fallback, and the bitmap scans that only run when a price level empties.
//==========================================================================

namespace {

constexpr uint64_t roundUpPow2(uint64_t v) noexcept {
    if (v < 2) return 2;
    --v;
    v |= v >> 1;  v |= v >> 2;  v |= v >> 4;
    v |= v >> 8;  v |= v >> 16; v |= v >> 32;
    return v + 1;
}

} // namespace


OrderBook::OrderBook(std::size_t expectedOrders) {
    bidLevels_.assign(NUM_LEVELS, Level{NIL, NIL, 0});
    askLevels_.assign(NUM_LEVELS, Level{NIL, NIL, 0});
    bidBase_ = bidLevels_.data();
    askBase_ = askLevels_.data();

    bidMask_.assign(MASK_WORDS, 0ull);
    askMask_.assign(MASK_WORDS, 0ull);
    bidMaskBase_ = bidMask_.data();
    askMaskBase_ = askMask_.data();

    const std::size_t n = std::max<std::size_t>(expectedOrders, 1024);
    pool_.resize(n);
    poolBase_ = pool_.data();

    idSlot_.assign(std::size_t(roundUpPow2(n)), NIL);
    idBase_     = idSlot_.data();
    idCapacity_ = idSlot_.size();
}


void OrderBook::reserve(std::size_t orders, uint64_t maxOrderId) {
    if (orders > pool_.size()) {
        pool_.resize(orders);
        poolBase_ = pool_.data();
    }

    const uint64_t wantIds =
        std::max<uint64_t>(uint64_t(orders), maxOrderId + 1);

    if (wantIds > idCapacity_ && wantIds <= DIRECT_ID_LIMIT) {
        idSlot_.resize(std::size_t(roundUpPow2(wantIds)), NIL);
        idBase_     = idSlot_.data();
        idCapacity_ = idSlot_.size();
    }
}


void OrderBook::clear() noexcept {
    std::fill(bidLevels_.begin(), bidLevels_.end(), Level{NIL, NIL, 0});
    std::fill(askLevels_.begin(), askLevels_.end(), Level{NIL, NIL, 0});
    std::fill(bidMask_.begin(), bidMask_.end(), 0ull);
    std::fill(askMask_.begin(), askMask_.end(), 0ull);
    std::fill(idSlot_.begin(), idSlot_.end(), NIL);

    idOverflow_.clear();

    poolUsed_   = 0;
    freeHead_   = NIL;
    bestBid_    = -1;
    bestAsk_    = int32_t(NUM_LEVELS);
    liveOrders_ = 0;
    tradedQty_  = 0;
}


void OrderBook::growPool() {
    const std::size_t oldSize = pool_.size();
    pool_.resize(oldSize ? oldSize * 2 : 1024);
    poolBase_ = pool_.data();
}


void OrderBook::growIdTable(uint64_t id) {
    uint64_t want = roundUpPow2(id + 1);
    if (want > DIRECT_ID_LIMIT) want = DIRECT_ID_LIMIT;

    idSlot_.resize(std::size_t(want), NIL);
    idBase_     = idSlot_.data();
    idCapacity_ = idSlot_.size();
}


uint32_t* OrderBook::overflowSlot(uint64_t id, bool create) {
    auto it = idOverflow_.find(id);
    if (it != idOverflow_.end()) return &it->second;
    if (!create) return nullptr;

    auto res = idOverflow_.emplace(id, NIL);
    return &res.first->second;
}


int32_t OrderBook::scanForward(const uint64_t* m, int32_t from) const noexcept {
    if (from < 0) from = 0;
    if (from >= int32_t(NUM_LEVELS)) return int32_t(NUM_LEVELS);

    uint32_t w   = uint32_t(from) >> 6;
    uint64_t cur = m[w] & (~0ull << (uint32_t(from) & 63u));

    while (true) {
        if (cur) return int32_t((w << 6) + ctz64(cur));
        if (++w >= MASK_WORDS) return int32_t(NUM_LEVELS);
        cur = m[w];
    }
}


int32_t OrderBook::scanBackward(const uint64_t* m, int32_t from) const noexcept {
    if (from < 0) return -1;
    if (from >= int32_t(NUM_LEVELS)) from = int32_t(NUM_LEVELS) - 1;

    uint32_t w   = uint32_t(from) >> 6;
    const uint32_t b = uint32_t(from) & 63u;
    uint64_t cur = m[w] & (b == 63u ? ~0ull : ((1ull << (b + 1)) - 1ull));

    while (true) {
        if (cur) return int32_t((w << 6) + (63u - clz64(cur)));
        if (w == 0) return -1;
        --w;
        cur = m[w];
    }
}