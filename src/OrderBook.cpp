#include "../include/OrderBook.h"
#include <iostream>
void OrderBook::addOrder(const Order& order) {
    if (order.side == Side::Buy) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
}
void OrderBook::print() const {
    std::cout << "BIDS:" << std::endl;
    for (const auto& it : bids_) {
        std::cout << "Price: " << it.first << "\t| Orders: ";
        for (const auto& order : it.second) {
            std::cout << order.quantity << " ";
        }
        std::cout << std::endl;
    }

    std::cout << "\nASKS:" << std::endl;
    for (const auto& it : asks_) {
        std::cout << "Price: " << it.first << "\t| Orders: ";
        for (const auto& order : it.second) {
            std::cout << order.quantity << " ";
        }
        std::cout << std::endl;
    }
}