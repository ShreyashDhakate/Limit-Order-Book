#pragma once
#include <cstdint>
#include <map>
#include <list>
#include <functional>
#include "Order.h"
#include <fstream>

class OrderBook{
public:
    using Price = std::uint64_t;
    using OrderList = std::list<Order>;
    void addOrder(const Order& order);
    void print() const;
    void cancelOrder(std::uint64_t orderId);
    void modifyOrder(OrderModification mod); 
private:
    std::map<Price, OrderList, std::greater<Price>> bids_;
    std::map<Price, OrderList, std::less<Price>> asks_;
    void logTrade(uint64_t buyer_id, uint64_t seller_id, uint64_t price, uint64_t quantity) {
        std::ofstream file("trades.csv", std::ios::app);
        if (file.is_open()) {
            file << buyer_id << "," << seller_id << "," << price << "," << quantity << "\n";
        }
    }

};