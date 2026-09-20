#include "OrderBook.h"

#include <cstdio>

static void printTop(const OrderBook& book) {
    std::printf("  best bid: %6d (vol %lld)   best ask: %6d (vol %lld)\n",
                book.getBestBid(), (long long)book.getBestBidVolume(),
                book.getBestAsk(), (long long)book.getBestAskVolume());
}

int main() {
    OrderBook book;
    book.reserve(1 << 16);

    std::printf("Seeding the book\n");
    book.addOrder({1, Side::SELL, 105, 100});
    book.addOrder({2, Side::SELL, 103, 50});
    book.addOrder({3, Side::BUY, 100, 80});
    printTop(book);

    std::printf("Aggressive buy 70 @ 103\n");
    book.addOrder({4, Side::BUY, 103, 70});
    printTop(book);
    std::printf("  order 2 remaining: %d, traded so far: %llu\n",
                book.getOrderQuantity(2),
                (unsigned long long)book.tradedQuantity());

    std::printf("Cancel order 3\n");
    book.cancelOrder(3);
    printTop(book);

    std::printf("Resting orders: %zu\n", book.size());
    return 0;
}