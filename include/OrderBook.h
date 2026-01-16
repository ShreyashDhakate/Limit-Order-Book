#pragma once
#include <cstdint>
#include <map>
#include <list>
#include <functional>
#include "Order.h"

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
    
};