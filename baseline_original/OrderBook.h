#pragma once

#include "Order.h"

#include <cstdint>
#include <map>
#include <list>
#include <unordered_map>

class OrderBook {
private:

    using OrderList = std::list<Order>;
    using PriceLevels = std::map<int, OrderList>;
    using PriceIterator = PriceLevels::iterator;
    using OrderIterator = OrderList::iterator;

    struct OrderLocation {
        Side side;
        PriceIterator priceLevel;
        OrderIterator order;
    };

    std::map<int, std::list<Order>> buyOrders;
    std::map<int, std::list<Order>> sellOrders;

    std::unordered_map<uint64_t, OrderLocation> orderLookup;

public:

    void addOrder(Order order);

    void cancelOrder(uint64_t orderId);

    int getBestBid() const;

    int getBestAsk() const;

    int getOrderQuantity(uint64_t orderId) const;

private:

    void matchBuyOrder(Order& order);
    void matchSellOrder(Order& order);
};