#pragma once

//==========================================================================
//  Low-latency order book
//
//  Design notes (why this is fast):
//
//   1. No std::map.            Price levels live in a flat array indexed
//                              directly by price.  Level lookup is one
//                              shift-free array index, not a red-black
//                              tree descent.
//
//   2. No std::list.           Orders live in one contiguous pool and are
//                              threaded together with 32-bit intrusive
//                              prev/next indices.  Zero allocation on the
//                              hot path, zero pointer chasing across the
//                              heap, half the pointer footprint on 64-bit.
//
//   3. No std::unordered_map.  Order-id -> pool-slot is a flat array
//                              indexed by id (dense ids), with a hash-map
//                              fallback only for sparse/huge ids.
//
//   4. Best bid / best ask     are cached and maintained incrementally.
//                              Finding the next best price when a level
//                              empties is a bitmap word scan using
//                              ctz/clz, not a tree walk.
//
//   5. Everything hot is       defined in this header so it inlines into
//      inline.                 the caller.  Only cold paths (growth,
//                              overflow ids, bitmap rescans) live in the
//                              .cpp.
//
//  Result: addOrder / cancelOrder / getOrderQuantity are all O(1) with no
//  allocation, no branching on container internals, and a sequential
//  memory access pattern that the hardware prefetcher can follow.
//==========================================================================

#include "Order.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <unordered_map>

#if defined(__GNUC__) || defined(__clang__)
  #define OB_ALWAYS_INLINE inline __attribute__((always_inline))
  #define OB_NOINLINE      __attribute__((noinline))
  #define OB_LIKELY(x)     __builtin_expect(!!(x), 1)
  #define OB_UNLIKELY(x)   __builtin_expect(!!(x), 0)
  #define OB_PREFETCH(p)   __builtin_prefetch((p), 1, 3)
#else
  #define OB_ALWAYS_INLINE inline
  #define OB_NOINLINE
  #define OB_LIKELY(x)     (x)
  #define OB_UNLIKELY(x)   (x)
  #define OB_PREFETCH(p)   ((void)0)
#endif

class OrderBook {
public:
    //------------------------------------------------------------------
    // Compile-time configuration
    //------------------------------------------------------------------

    // Price domain. Prices are integer ticks in [MIN_PRICE, MAX_PRICE].
    // Widen these if your instrument needs it; memory cost is
    // 16 bytes per level per side plus 1 bit per level per side.
    static constexpr int32_t  MIN_PRICE  = 0;
    static constexpr int32_t  MAX_PRICE  = 65535;

    static constexpr uint32_t NUM_LEVELS = uint32_t(MAX_PRICE - MIN_PRICE) + 1u;
    static constexpr uint32_t MASK_WORDS = (NUM_LEVELS + 63u) / 64u;

    // Sentinel for "no slot".
    static constexpr uint32_t NIL = 0xFFFFFFFFu;

    // Order ids below this get an O(1) direct-indexed lookup table.
    // Ids at or above it fall back to a hash map (cold path).
    static constexpr uint64_t DIRECT_ID_LIMIT = 1ull << 24;   // 16M ids -> 64 MB max

    //------------------------------------------------------------------
    // Lifetime
    //------------------------------------------------------------------

    explicit OrderBook(std::size_t expectedOrders = 1u << 16);

    // Pre-size the pool and id table so the hot path never grows.
    void reserve(std::size_t orders, uint64_t maxOrderId = 0);

    // Drop every resting order; keeps the memory for reuse.
    void clear() noexcept;

    //------------------------------------------------------------------
    // Public API (unchanged from the original project)
    //------------------------------------------------------------------

    OB_ALWAYS_INLINE void addOrder(const Order& order) noexcept;
    OB_ALWAYS_INLINE void cancelOrder(uint64_t orderId) noexcept;

    OB_ALWAYS_INLINE int  getBestBid() const noexcept {
        return bestBid_ < 0 ? -1 : int(bestBid_) + MIN_PRICE;
    }

    OB_ALWAYS_INLINE int  getBestAsk() const noexcept {
        return bestAsk_ >= int32_t(NUM_LEVELS) ? -1 : int(bestAsk_) + MIN_PRICE;
    }

    OB_ALWAYS_INLINE int  getOrderQuantity(uint64_t orderId) const noexcept;

    //------------------------------------------------------------------
    // Extra accessors (free, given the layout)
    //------------------------------------------------------------------

    // Total resting quantity at a price level. O(1).
    OB_ALWAYS_INLINE int64_t getBidVolumeAt(int price) const noexcept {
        const uint32_t p = priceIndex(price);
        return p < NUM_LEVELS ? int64_t(bidLevels_[p].qty) : 0;
    }
    OB_ALWAYS_INLINE int64_t getAskVolumeAt(int price) const noexcept {
        const uint32_t p = priceIndex(price);
        return p < NUM_LEVELS ? int64_t(askLevels_[p].qty) : 0;
    }

    OB_ALWAYS_INLINE int64_t getBestBidVolume() const noexcept {
        return bestBid_ < 0 ? 0 : int64_t(bidLevels_[bestBid_].qty);
    }
    OB_ALWAYS_INLINE int64_t getBestAskVolume() const noexcept {
        return bestAsk_ >= int32_t(NUM_LEVELS) ? 0 : int64_t(askLevels_[bestAsk_].qty);
    }

    // Number of orders currently resting in the book.
    OB_ALWAYS_INLINE std::size_t size() const noexcept { return liveOrders_; }

    // Cumulative traded quantity since construction / clear().
    OB_ALWAYS_INLINE uint64_t tradedQuantity() const noexcept { return tradedQty_; }

private:
    //------------------------------------------------------------------
    // Storage
    //------------------------------------------------------------------

    // 32 bytes: two nodes per cache line, naturally aligned.
    struct Node {
        uint64_t id;        // owning order id
        int32_t  priceIdx;  // level index (not raw price)
        int32_t  quantity;  // remaining quantity
        uint32_t prev;      // intrusive list links, pool indices
        uint32_t next;
        uint8_t  side;      // 0 = BUY, 1 = SELL
        uint8_t  pad0;
        uint16_t pad1;
        uint32_t pad2;
    };
    static_assert(sizeof(Node) == 32, "Node should stay 32 bytes");

    // 16 bytes: four levels per cache line.
    struct Level {
        uint32_t head;      // FIFO front (oldest, highest priority)
        uint32_t tail;      // FIFO back
        uint64_t qty;       // aggregate resting quantity at this level
    };
    static_assert(sizeof(Level) == 16, "Level should stay 16 bytes");

    std::vector<Level>    bidLevels_;
    std::vector<Level>    askLevels_;
    Level*                bidBase_ = nullptr;   // cached .data()
    Level*                askBase_ = nullptr;

    std::vector<uint64_t> bidMask_;             // occupancy bitmaps
    std::vector<uint64_t> askMask_;
    uint64_t*             bidMaskBase_ = nullptr;
    uint64_t*             askMaskBase_ = nullptr;

    std::vector<Node>     pool_;
    Node*                 poolBase_ = nullptr;
    uint32_t              poolUsed_ = 0;        // high-water mark
    uint32_t              freeHead_ = NIL;      // free list through Node::next

    std::vector<uint32_t> idSlot_;              // direct id -> pool index
    uint32_t*             idBase_ = nullptr;
    uint64_t              idCapacity_ = 0;

    std::unordered_map<uint64_t, uint32_t> idOverflow_;   // cold path only

    int32_t               bestBid_ = -1;                       // -1 == empty
    int32_t               bestAsk_ = int32_t(NUM_LEVELS);       // NUM_LEVELS == empty

    std::size_t           liveOrders_ = 0;
    uint64_t              tradedQty_  = 0;

    //------------------------------------------------------------------
    // Price mapping
    //------------------------------------------------------------------

    // Returns NUM_LEVELS (an out-of-range marker) for prices outside the
    // configured domain. The unsigned subtraction makes this a single
    // compare rather than two.
    static OB_ALWAYS_INLINE uint32_t priceIndex(int price) noexcept {
        return uint32_t(int64_t(price) - int64_t(MIN_PRICE));
    }

    //------------------------------------------------------------------
    // Bitmap helpers
    //------------------------------------------------------------------

    static OB_ALWAYS_INLINE void setBit(uint64_t* m, uint32_t i) noexcept {
        m[i >> 6] |= (1ull << (i & 63u));
    }
    static OB_ALWAYS_INLINE void clearBit(uint64_t* m, uint32_t i) noexcept {
        m[i >> 6] &= ~(1ull << (i & 63u));
    }

    static OB_ALWAYS_INLINE uint32_t ctz64(uint64_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
        return uint32_t(__builtin_ctzll(v));
#else
        uint32_t n = 0; while (!(v & 1ull)) { v >>= 1; ++n; } return n;
#endif
    }
    static OB_ALWAYS_INLINE uint32_t clz64(uint64_t v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
        return uint32_t(__builtin_clzll(v));
#else
        uint32_t n = 0; while (!(v & (1ull << 63))) { v <<= 1; ++n; } return n;
#endif
    }

    // First set bit at index >= from, or NUM_LEVELS if none.
    OB_NOINLINE int32_t scanForward(const uint64_t* m, int32_t from) const noexcept;
    // Last set bit at index <= from, or -1 if none.
    OB_NOINLINE int32_t scanBackward(const uint64_t* m, int32_t from) const noexcept;

    //------------------------------------------------------------------
    // Pool management
    //------------------------------------------------------------------

    OB_NOINLINE void growPool();

    OB_ALWAYS_INLINE uint32_t allocNode() noexcept {
        const uint32_t idx = freeHead_;
        if (OB_LIKELY(idx != NIL)) {
            freeHead_ = poolBase_[idx].next;
            return idx;
        }
        if (OB_UNLIKELY(poolUsed_ == pool_.size())) growPool();
        return poolUsed_++;
    }

    OB_ALWAYS_INLINE void freeNode(uint32_t idx) noexcept {
        poolBase_[idx].next = freeHead_;
        freeHead_ = idx;
    }

    //------------------------------------------------------------------
    // Id table
    //------------------------------------------------------------------

    OB_NOINLINE void      growIdTable(uint64_t id);
    OB_NOINLINE uint32_t* overflowSlot(uint64_t id, bool create);

    // Returns a writable slot for `id`, creating the entry if needed.
    OB_ALWAYS_INLINE uint32_t* slotForWrite(uint64_t id) noexcept {
        if (OB_LIKELY(id < idCapacity_)) return idBase_ + id;
        if (id < DIRECT_ID_LIMIT) { growIdTable(id); return idBase_ + id; }
        return overflowSlot(id, true);
    }

    // Returns the slot for `id`, or nullptr if it was never registered.
    OB_ALWAYS_INLINE uint32_t* slotForRead(uint64_t id) noexcept {
        if (OB_LIKELY(id < idCapacity_)) return idBase_ + id;
        return overflowSlot(id, false);
    }
    OB_ALWAYS_INLINE const uint32_t* slotForRead(uint64_t id) const noexcept {
        return const_cast<OrderBook*>(this)->slotForRead(id);
    }

    //------------------------------------------------------------------
    // Core operations
    //------------------------------------------------------------------

    // Unlink `idx` from its price level. Does not touch the id table or
    // the free list.
    OB_ALWAYS_INLINE void unlink(uint32_t idx) noexcept;

    // Append a residual order to its level and register the id.
    OB_ALWAYS_INLINE void rest(uint64_t id, uint32_t priceIdx,
                               int32_t qty, Side side) noexcept;

    // Match an incoming buy of `qty` at limit `limitIdx` against the ask
    // side. Returns unfilled quantity.
    OB_ALWAYS_INLINE int32_t matchAgainstAsks(uint32_t limitIdx, int32_t qty) noexcept;

    // Symmetric, for an incoming sell against the bid side.
    OB_ALWAYS_INLINE int32_t matchAgainstBids(uint32_t limitIdx, int32_t qty) noexcept;
};


//==========================================================================
//  Inline definitions
//==========================================================================

OB_ALWAYS_INLINE void OrderBook::unlink(uint32_t idx) noexcept {
    Node& n = poolBase_[idx];

    Level* const levels = (n.side == uint8_t(Side::BUY)) ? bidBase_ : askBase_;
    Level& lv = levels[uint32_t(n.priceIdx)];

    const uint32_t prev = n.prev;
    const uint32_t next = n.next;

    if (prev != NIL) poolBase_[prev].next = next; else lv.head = next;
    if (next != NIL) poolBase_[next].prev = prev; else lv.tail = prev;

    lv.qty -= uint64_t(uint32_t(n.quantity));
    --liveOrders_;

    if (OB_UNLIKELY(lv.head == NIL)) {
        const int32_t p = n.priceIdx;
        if (n.side == uint8_t(Side::BUY)) {
            clearBit(bidMaskBase_, uint32_t(p));
            if (bestBid_ == p) bestBid_ = scanBackward(bidMaskBase_, p - 1);
        } else {
            clearBit(askMaskBase_, uint32_t(p));
            if (bestAsk_ == p) bestAsk_ = scanForward(askMaskBase_, p + 1);
        }
    }
}


OB_ALWAYS_INLINE void OrderBook::rest(uint64_t id, uint32_t priceIdx,
                                      int32_t qty, Side side) noexcept {
    uint32_t* slot = slotForWrite(id);

    // Replace semantics: an id that is already resting is cancelled first,
    // matching the original insert_or_assign behaviour but without leaking
    // the previous order into the book.
    if (OB_UNLIKELY(*slot != NIL)) {
        const uint32_t old = *slot;
        unlink(old);
        freeNode(old);
    }

    const uint32_t idx = allocNode();
    *slot = idx;

    Node& n   = poolBase_[idx];
    n.id       = id;
    n.priceIdx = int32_t(priceIdx);
    n.quantity = qty;
    n.next     = NIL;
    n.side     = uint8_t(side);

    Level* const levels = (side == Side::BUY) ? bidBase_ : askBase_;
    Level& lv = levels[priceIdx];

    const uint32_t tail = lv.tail;
    n.prev = tail;

    if (tail != NIL) {
        poolBase_[tail].next = idx;
    } else {
        lv.head = idx;
        if (side == Side::BUY) {
            setBit(bidMaskBase_, priceIdx);
            if (int32_t(priceIdx) > bestBid_) bestBid_ = int32_t(priceIdx);
        } else {
            setBit(askMaskBase_, priceIdx);
            if (int32_t(priceIdx) < bestAsk_) bestAsk_ = int32_t(priceIdx);
        }
    }
    lv.tail = idx;
    lv.qty += uint64_t(uint32_t(qty));

    ++liveOrders_;
}


OB_ALWAYS_INLINE int32_t OrderBook::matchAgainstAsks(uint32_t limitIdx,
                                                     int32_t qty) noexcept {
    // Fast rejection: nothing crosses. One compare, perfectly predicted
    // for the common "passive order" case.
    if (OB_LIKELY(bestAsk_ > int32_t(limitIdx))) return qty;

    while (qty > 0 && bestAsk_ <= int32_t(limitIdx)) {

        Level& lv = askBase_[bestAsk_];
        uint32_t cur = lv.head;
        uint64_t levelTraded = 0;

        while (cur != NIL) {
            Node& n = poolBase_[cur];

            if (n.quantity > qty) {              // partial fill, we are done
                n.quantity -= qty;
                levelTraded += uint64_t(uint32_t(qty));
                tradedQty_  += uint64_t(uint32_t(qty));
                qty = 0;
                break;
            }

            // full fill of the resting order
            const int32_t  filled = n.quantity;
            const uint32_t nxt    = n.next;

            qty         -= filled;
            levelTraded += uint64_t(uint32_t(filled));
            tradedQty_  += uint64_t(uint32_t(filled));

            uint32_t* slot = slotForRead(n.id);
            if (OB_LIKELY(slot != nullptr)) *slot = NIL;

            freeNode(cur);
            --liveOrders_;

            cur = nxt;
            if (qty == 0) break;
        }

        lv.qty -= levelTraded;
        lv.head = cur;

        if (cur != NIL) {
            poolBase_[cur].prev = NIL;
        } else {
            lv.tail = NIL;
            clearBit(askMaskBase_, uint32_t(bestAsk_));
            bestAsk_ = scanForward(askMaskBase_, bestAsk_ + 1);
        }
    }
    return qty;
}


OB_ALWAYS_INLINE int32_t OrderBook::matchAgainstBids(uint32_t limitIdx,
                                                     int32_t qty) noexcept {
    if (OB_LIKELY(bestBid_ < int32_t(limitIdx))) return qty;

    while (qty > 0 && bestBid_ >= int32_t(limitIdx)) {

        Level& lv = bidBase_[bestBid_];
        uint32_t cur = lv.head;
        uint64_t levelTraded = 0;

        while (cur != NIL) {
            Node& n = poolBase_[cur];

            if (n.quantity > qty) {
                n.quantity -= qty;
                levelTraded += uint64_t(uint32_t(qty));
                tradedQty_  += uint64_t(uint32_t(qty));
                qty = 0;
                break;
            }

            const int32_t  filled = n.quantity;
            const uint32_t nxt    = n.next;

            qty         -= filled;
            levelTraded += uint64_t(uint32_t(filled));
            tradedQty_  += uint64_t(uint32_t(filled));

            uint32_t* slot = slotForRead(n.id);
            if (OB_LIKELY(slot != nullptr)) *slot = NIL;

            freeNode(cur);
            --liveOrders_;

            cur = nxt;
            if (qty == 0) break;
        }

        lv.qty -= levelTraded;
        lv.head = cur;

        if (cur != NIL) {
            poolBase_[cur].prev = NIL;
        } else {
            lv.tail = NIL;
            clearBit(bidMaskBase_, uint32_t(bestBid_));
            bestBid_ = scanBackward(bidMaskBase_, bestBid_ - 1);
        }
    }
    return qty;
}


OB_ALWAYS_INLINE void OrderBook::addOrder(const Order& order) noexcept {
    int32_t qty = int32_t(order.quantity);
    if (OB_UNLIKELY(qty <= 0)) return;

    const uint32_t p = priceIndex(order.price);
    if (OB_UNLIKELY(p >= NUM_LEVELS)) return;      // outside the price domain

    if (order.side == Side::BUY) {
        qty = matchAgainstAsks(p, qty);
        if (qty > 0) rest(order.id, p, qty, Side::BUY);
    } else {
        qty = matchAgainstBids(p, qty);
        if (qty > 0) rest(order.id, p, qty, Side::SELL);
    }
}


OB_ALWAYS_INLINE void OrderBook::cancelOrder(uint64_t orderId) noexcept {
    uint32_t* slot = slotForRead(orderId);
    if (OB_UNLIKELY(slot == nullptr)) return;

    const uint32_t idx = *slot;
    if (OB_UNLIKELY(idx == NIL)) return;

    *slot = NIL;
    unlink(idx);
    freeNode(idx);
}


OB_ALWAYS_INLINE int OrderBook::getOrderQuantity(uint64_t orderId) const noexcept {
    const uint32_t* slot = slotForRead(orderId);
    if (OB_UNLIKELY(slot == nullptr)) return 0;

    const uint32_t idx = *slot;
    if (OB_UNLIKELY(idx == NIL)) return 0;

    return int(poolBase_[idx].quantity);
}