#include "../include/OrderBook.h"
#include <iostream>
void OrderBook::addOrder(const Order& order) {
    Order incoming = order;
    if (incoming.side == Side::Buy) {
        while(incoming.quantity>0 && !asks_.empty()){
            auto best_ask_iter = asks_.begin();
            Price ask_price = best_ask_iter->first;
            OrderList& orders_at_level = best_ask_iter->second;

            if(ask_price > incoming.price) {
                break;
            }
            Order& book_order = orders_at_level.front();
            std::uint64_t match_qty = std::min(incoming.quantity, book_order.quantity);
            std::cout<<"Trade Executed! "<< match_qty <<" @ "<< ask_price <<std::endl;
            incoming.quantity-=match_qty;
            book_order.quantity -=match_qty;
            if(book_order.quantity == 0){
                orders_at_level.pop_front();
                if(orders_at_level.empty()){
                    asks_.erase(best_ask_iter);
                }
            }
        }
        if (incoming.quantity > 0) {
            if(incoming.order_type==OrderType::GoodTillCancel){
                bids_[incoming.price].push_back(incoming);
            }
        }
    } else {
        while (incoming.quantity > 0 && !bids_.empty()) {
            auto best_bid_iter = bids_.begin(); 
            Price bid_price = best_bid_iter->first;
            OrderList& orders_at_level = best_bid_iter->second;

            if (bid_price < incoming.price) {
                break;
            }

            Order& book_order = orders_at_level.front();
            std::uint64_t match_qty = std::min(incoming.quantity, book_order.quantity);

            std::cout << "Trade Executed! " << match_qty << " @ " << bid_price << std::endl;

            incoming.quantity -= match_qty;
            book_order.quantity -= match_qty;

            if (book_order.quantity == 0) {
                orders_at_level.pop_front();
                if (orders_at_level.empty()) {
                    bids_.erase(best_bid_iter);
                }
                
            }
            
        }

        if (incoming.quantity > 0) {
            if(incoming.order_type==OrderType::GoodTillCancel){
                    asks_[incoming.price].push_back(incoming);
            }
            
        }
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