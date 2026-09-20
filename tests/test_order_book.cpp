#include "OrderBook.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <list>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

//==========================================================================
//  Original test suite (unchanged expectations)
//==========================================================================

void testBasicOrders() {
    OrderBook book;
    book.addOrder({1, Side::BUY, 100, 10});
    book.addOrder({2, Side::SELL, 105, 20});

    assert(book.getBestBid() == 100);
    assert(book.getBestAsk() == 105);

    std::cout << "testBasicOrders PASSED\n";
}

void testFullMatch() {
    OrderBook book;
    book.addOrder({1, Side::SELL, 100, 10});
    book.addOrder({2, Side::BUY, 100, 10});

    assert(book.getBestBid() == -1);
    assert(book.getBestAsk() == -1);

    std::cout << "testFullMatch PASSED\n";
}

void testPartialMatch() {
    OrderBook book;
    book.addOrder({1, Side::SELL, 100, 10});
    book.addOrder({2, Side::BUY, 100, 6});

    assert(book.getBestAsk() == 100);
    assert(book.getOrderQuantity(1) == 4);

    std::cout << "testPartialMatch PASSED\n";
}

void testMultiplePriceLevels() {
    OrderBook book;
    book.addOrder({1, Side::SELL, 105, 10});
    book.addOrder({2, Side::SELL, 103, 20});
    book.addOrder({3, Side::SELL, 101, 30});

    assert(book.getBestAsk() == 101);

    std::cout << "testMultiplePriceLevels PASSED\n";
}

void testPriceTimePriority() {
    OrderBook book;
    book.addOrder({1, Side::SELL, 100, 10});
    book.addOrder({2, Side::SELL, 100, 20});
    book.addOrder({3, Side::BUY, 100, 15});

    assert(book.getOrderQuantity(2) == 15);

    std::cout << "testPriceTimePriority PASSED\n";
}

void testCancellation() {
    OrderBook book;
    book.addOrder({1, Side::BUY, 100, 10});
    assert(book.getBestBid() == 100);

    book.cancelOrder(1);
    assert(book.getBestBid() == -1);

    std::cout << "testCancellation PASSED\n";
}

void testNonCrossingOrders() {
    OrderBook book;
    book.addOrder({1, Side::BUY, 100, 10});
    book.addOrder({2, Side::SELL, 101, 10});

    assert(book.getBestBid() == 100);
    assert(book.getBestAsk() == 101);

    std::cout << "testNonCrossingOrders PASSED\n";
}

void testBuyConsumesMultipleLevels() {
    OrderBook book;
    book.addOrder({1, Side::SELL, 100, 10});
    book.addOrder({2, Side::SELL, 101, 20});
    book.addOrder({3, Side::SELL, 102, 30});

    book.addOrder({4, Side::BUY, 102, 35});

    assert(book.getOrderQuantity(2) == 0);
    assert(book.getOrderQuantity(3) == 25);
    assert(book.getBestAsk() == 102);

    std::cout << "testBuyConsumesMultipleLevels PASSED\n";
}

void testSellConsumesMultipleLevels() {
    OrderBook book;
    book.addOrder({1, Side::BUY, 102, 30});
    book.addOrder({2, Side::BUY, 101, 20});
    book.addOrder({3, Side::BUY, 100, 10});

    book.addOrder({4, Side::SELL, 100, 45});

    assert(book.getOrderQuantity(1) == 0);
    assert(book.getOrderQuantity(2) == 5);
    assert(book.getBestBid() == 101);

    std::cout << "testSellConsumesMultipleLevels PASSED\n";
}

void testCancelNonexistentOrder() {
    OrderBook book;
    book.cancelOrder(999);

    assert(book.getBestBid() == -1);
    assert(book.getBestAsk() == -1);

    std::cout << "testCancelNonexistentOrder PASSED\n";
}

//==========================================================================
//  Additional coverage for the new implementation
//==========================================================================

void testCancelMiddleOfQueue() {
    OrderBook book;
    book.addOrder({1, Side::BUY, 100, 10});
    book.addOrder({2, Side::BUY, 100, 20});
    book.addOrder({3, Side::BUY, 100, 30});

    book.cancelOrder(2);
    assert(book.getBidVolumeAt(100) == 40);
    assert(book.size() == 2);

    // FIFO order must survive the middle removal.
    book.addOrder({4, Side::SELL, 100, 10});
    assert(book.getOrderQuantity(1) == 0);
    assert(book.getOrderQuantity(3) == 30);

    book.addOrder({5, Side::SELL, 100, 5});
    assert(book.getOrderQuantity(3) == 25);

    std::cout << "testCancelMiddleOfQueue PASSED\n";
}

void testBestPriceRecoveryAfterCancel() {
    OrderBook book;
    book.addOrder({1, Side::BUY, 100, 10});
    book.addOrder({2, Side::BUY, 200, 10});
    book.addOrder({3, Side::BUY, 300, 10});

    assert(book.getBestBid() == 300);
    book.cancelOrder(3);
    assert(book.getBestBid() == 200);
    book.cancelOrder(2);
    assert(book.getBestBid() == 100);
    book.cancelOrder(1);
    assert(book.getBestBid() == -1);

    book.addOrder({4, Side::SELL, 300, 10});
    book.addOrder({5, Side::SELL, 200, 10});
    book.addOrder({6, Side::SELL, 100, 10});

    assert(book.getBestAsk() == 100);
    book.cancelOrder(6);
    assert(book.getBestAsk() == 200);
    book.cancelOrder(5);
    assert(book.getBestAsk() == 300);
    book.cancelOrder(4);
    assert(book.getBestAsk() == -1);

    std::cout << "testBestPriceRecoveryAfterCancel PASSED\n";
}

void testCancelAfterFullFillIsNoop() {
    OrderBook book;
    book.addOrder({1, Side::SELL, 100, 10});
    book.addOrder({2, Side::BUY, 100, 10});

    book.cancelOrder(1);   // already fully filled
    book.cancelOrder(2);   // never rested

    assert(book.size() == 0);
    assert(book.getBestBid() == -1);
    assert(book.getBestAsk() == -1);

    std::cout << "testCancelAfterFullFillIsNoop PASSED\n";
}

void testDoubleCancel() {
    OrderBook book;
    book.addOrder({7, Side::BUY, 100, 10});
    book.cancelOrder(7);
    book.cancelOrder(7);

    assert(book.size() == 0);
    assert(book.getBestBid() == -1);

    std::cout << "testDoubleCancel PASSED\n";
}

void testAggressorFullyConsumesBook() {
    OrderBook book;
    book.addOrder({1, Side::SELL, 100, 10});
    book.addOrder({2, Side::SELL, 101, 10});

    book.addOrder({3, Side::BUY, 105, 50});   // eats everything, rests 30

    assert(book.getBestAsk() == -1);
    assert(book.getBestBid() == 105);
    assert(book.getOrderQuantity(3) == 30);
    assert(book.tradedQuantity() == 20);

    std::cout << "testAggressorFullyConsumesBook PASSED\n";
}

void testWidePriceRangeAndSparseIds() {
    OrderBook book;

    book.addOrder({1'000'000'000'000ull, Side::BUY, 0, 5});
    book.addOrder({2'000'000'000'000ull, Side::SELL, OrderBook::MAX_PRICE, 5});

    assert(book.getBestBid() == 0);
    assert(book.getBestAsk() == OrderBook::MAX_PRICE);
    assert(book.getOrderQuantity(1'000'000'000'000ull) == 5);

    book.cancelOrder(1'000'000'000'000ull);
    assert(book.getBestBid() == -1);

    std::cout << "testWidePriceRangeAndSparseIds PASSED\n";
}

void testClearAndReuse() {
    OrderBook book;
    book.addOrder({1, Side::BUY, 100, 10});
    book.addOrder({2, Side::SELL, 200, 10});

    book.clear();
    assert(book.getBestBid() == -1);
    assert(book.getBestAsk() == -1);
    assert(book.size() == 0);
    assert(book.getOrderQuantity(1) == 0);

    book.addOrder({1, Side::BUY, 150, 7});
    assert(book.getBestBid() == 150);
    assert(book.getOrderQuantity(1) == 7);

    std::cout << "testClearAndReuse PASSED\n";
}

void testVolumeAccounting() {
    OrderBook book;
    book.addOrder({1, Side::BUY, 100, 10});
    book.addOrder({2, Side::BUY, 100, 15});
    assert(book.getBestBidVolume() == 25);

    book.addOrder({3, Side::SELL, 100, 12});
    assert(book.getBestBidVolume() == 13);
    assert(book.getOrderQuantity(2) == 13);

    book.cancelOrder(2);
    assert(book.getBestBidVolume() == 0);
    assert(book.getBestBid() == -1);

    std::cout << "testVolumeAccounting PASSED\n";
}

//==========================================================================
//  Randomised differential test against a straightforward reference model
//==========================================================================

// A straightforward map+list reference model. Deliberately simple and
// obviously correct; the fast book must agree with it on every operation.
class SimpleBook {
public:
    void addOrder(const Order& o) {
        int qty = o.quantity;
        if (qty <= 0) return;

        if (o.side == Side::BUY) {
            while (qty > 0 && !asks_.empty() && asks_.begin()->first <= o.price) {
                qty = consume(asks_, asks_.begin(), qty);
            }
            if (qty > 0) rest(bids_, Side::BUY, o, qty);
        } else {
            while (qty > 0 && !bids_.empty() && std::prev(bids_.end())->first >= o.price) {
                qty = consume(bids_, std::prev(bids_.end()), qty);
            }
            if (qty > 0) rest(asks_, Side::SELL, o, qty);
        }
    }

    void cancelOrder(uint64_t id) {
        auto it = loc_.find(id);
        if (it == loc_.end()) return;
        const Side side = std::get<0>(it->second);
        const int  px   = std::get<1>(it->second);
        auto       lit  = std::get<2>(it->second);

        auto& levels = (side == Side::BUY) ? bids_ : asks_;
        auto lvl = levels.find(px);
        lvl->second.erase(lit);
        if (lvl->second.empty()) levels.erase(lvl);
        loc_.erase(it);
    }

    int getBestBid() const { return bids_.empty() ? -1 : std::prev(bids_.end())->first; }
    int getBestAsk() const { return asks_.empty() ? -1 : asks_.begin()->first; }
    int getOrderQuantity(uint64_t id) const {
        auto it = loc_.find(id);
        return it == loc_.end() ? 0 : std::get<2>(it->second)->quantity;
    }
    std::size_t size() const { return loc_.size(); }

private:
    using OrderList = std::list<Order>;
    using Levels    = std::map<int, OrderList>;
    using Location  = std::tuple<Side, int, OrderList::iterator>;

    int consume(Levels& levels, Levels::iterator lvl, int qty) {
        auto& orders = lvl->second;
        while (qty > 0 && !orders.empty()) {
            Order& resting = orders.front();
            const int traded = std::min(qty, resting.quantity);
            qty -= traded;
            resting.quantity -= traded;
            if (resting.quantity == 0) {
                loc_.erase(resting.id);
                orders.pop_front();
            }
        }
        if (orders.empty()) levels.erase(lvl);
        return qty;
    }

    void rest(Levels& levels, Side side, const Order& o, int qty) {
        auto it = loc_.find(o.id);
        if (it != loc_.end()) cancelOrder(o.id);

        auto& orders = levels[o.price];
        Order stored = o;
        stored.quantity = qty;
        orders.push_back(stored);
        loc_.emplace(o.id, Location{side, o.price, std::prev(orders.end())});
    }

    Levels bids_, asks_;
    std::unordered_map<uint64_t, Location> loc_;
};

void testRandomisedAgainstReference() {
    constexpr int ITERATIONS = 200'000;
    constexpr int MIN_PX = 900, MAX_PX = 1100;

    std::mt19937_64 rng(0xC0FFEEu);
    std::uniform_int_distribution<int>  pxDist(MIN_PX, MAX_PX);
    std::uniform_int_distribution<int>  qtyDist(1, 50);
    std::uniform_int_distribution<int>  actionDist(0, 99);

    OrderBook  fast;
    SimpleBook ref;

    std::vector<uint64_t> live;
    uint64_t nextId = 1;

    for (int i = 0; i < ITERATIONS; ++i) {
        const int action = actionDist(rng);

        if (action < 70 || live.empty()) {
            Order o{nextId++,
                    (rng() & 1) ? Side::BUY : Side::SELL,
                    pxDist(rng),
                    qtyDist(rng)};
            fast.addOrder(o);
            ref.addOrder(o);
            live.push_back(o.id);
        } else {
            const std::size_t k = std::size_t(rng() % live.size());
            const uint64_t id = live[k];
            live[k] = live.back();
            live.pop_back();
            fast.cancelOrder(id);
            ref.cancelOrder(id);
        }

        if ((i & 0xFF) == 0) {
            assert(fast.getBestBid() == ref.getBestBid());
            assert(fast.getBestAsk() == ref.getBestAsk());
            assert(fast.size() == ref.size());
        }
    }

    assert(fast.getBestBid() == ref.getBestBid());
    assert(fast.getBestAsk() == ref.getBestAsk());
    assert(fast.size() == ref.size());

    for (uint64_t id = 1; id < nextId; ++id)
        assert(fast.getOrderQuantity(id) == ref.getOrderQuantity(id));

    std::cout << "testRandomisedAgainstReference PASSED ("
              << ITERATIONS << " ops)\n";
}


int main() {
    testBasicOrders();
    testFullMatch();
    testPartialMatch();
    testMultiplePriceLevels();
    testPriceTimePriority();
    testCancellation();
    testNonCrossingOrders();
    testBuyConsumesMultipleLevels();
    testSellConsumesMultipleLevels();
    testCancelNonexistentOrder();

    testCancelMiddleOfQueue();
    testBestPriceRecoveryAfterCancel();
    testCancelAfterFullFillIsNoop();
    testDoubleCancel();
    testAggressorFullyConsumesBook();
    testWidePriceRangeAndSparseIds();
    testClearAndReuse();
    testVolumeAccounting();

    testRandomisedAgainstReference();

    std::cout << "\nAll tests passed!\n";
}