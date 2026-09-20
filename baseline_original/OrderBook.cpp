#include "OrderBook.h"

#include <algorithm>

void OrderBook::addOrder(Order order) {

    if (order.side == Side::BUY) {

        matchBuyOrder(order);

        if (order.quantity > 0) {

            auto [levelIt, inserted] =
                buyOrders.try_emplace(order.price);

            auto& orders = levelIt->second;

            orders.push_back(order);

            auto orderIt = std::prev(orders.end());

            orderLookup.insert_or_assign(
                order.id,
                OrderLocation{
                    Side::BUY,
                    levelIt,
                    orderIt
                }
            );
        }
    }
    else {

        matchSellOrder(order);

        if (order.quantity > 0) {

            auto [levelIt, inserted] =
                sellOrders.try_emplace(order.price);

            auto& orders = levelIt->second;

            orders.push_back(order);

            auto orderIt = std::prev(orders.end());

            orderLookup.insert_or_assign(
                order.id,
                OrderLocation{
                    Side::SELL,
                    levelIt,
                    orderIt
                }
            );
        }
    }
}

void OrderBook::matchBuyOrder(Order& order) {

    while (order.quantity > 0 && !sellOrders.empty()) {

        auto bestLevel = sellOrders.begin();

        int bestAsk = bestLevel->first;

        if (bestAsk > order.price)
            break;

        auto& orders = bestLevel->second;

        while (order.quantity > 0 && !orders.empty()) {

            Order& restingOrder = orders.front();

            int tradedQuantity =
                std::min(order.quantity, restingOrder.quantity);

            order.quantity -= tradedQuantity;
            restingOrder.quantity -= tradedQuantity;

            if (restingOrder.quantity == 0) {

                orderLookup.erase(restingOrder.id);

                orders.pop_front();
            }
        }

        if (orders.empty()) {
            sellOrders.erase(bestLevel);
        }
    }
}

void OrderBook::matchSellOrder(Order& order) {

    while (order.quantity > 0 && !buyOrders.empty()) {

        auto bestLevel = std::prev(buyOrders.end());

        int bestBid = bestLevel->first;

        if (bestBid < order.price)
            break;

        auto& orders = bestLevel->second;

        while (order.quantity > 0 && !orders.empty()) {

            Order& restingOrder = orders.front();

            int tradedQuantity =
                std::min(order.quantity, restingOrder.quantity);

            order.quantity -= tradedQuantity;
            restingOrder.quantity -= tradedQuantity;

            if (restingOrder.quantity == 0) {

                orderLookup.erase(restingOrder.id);

                orders.pop_front();
            }
        }

        if (orders.empty()) {
            buyOrders.erase(bestLevel);
        }
    }
}

int OrderBook::getBestBid() const {

    if (buyOrders.empty())
        return -1;

    return buyOrders.rbegin()->first;
}

int OrderBook::getBestAsk() const {

    if (sellOrders.empty())
        return -1;

    return sellOrders.begin()->first;
}

void OrderBook::cancelOrder(uint64_t orderId) {

    auto it = orderLookup.find(orderId);

    if (it == orderLookup.end())
        return;

    OrderLocation location = it->second;

    // Remove lookup entry first.
    // The stored iterators may become invalid when the price level is erased.
    orderLookup.erase(it);

    if (location.side == Side::BUY) {

        auto levelIt = location.priceLevel;

        levelIt->second.erase(location.order);

        if (levelIt->second.empty()) {
            buyOrders.erase(levelIt);
        }
    }
    else {

        auto levelIt = location.priceLevel;

        levelIt->second.erase(location.order);

        if (levelIt->second.empty()) {
            sellOrders.erase(levelIt);
        }
    }
}

int OrderBook::getOrderQuantity(uint64_t orderId) const {

    auto it = orderLookup.find(orderId);

    if (it == orderLookup.end())
        return 0;

    return it->second.order->quantity;
}