#include "../include/OrderBook.h"
#include <iostream>
#include <limits>

void OrderBook::addOrder(const Order& order) {
    Order incoming = order;
    if(incoming.order_type == OrderType::Market){
        if(incoming.side == Side::Buy){
            incoming.price = std::numeric_limits<std::uint64_t>::max();
        }else{
            incoming.price = 0;
        }
    }
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
            
            logTrade(incoming.trader_id, book_order.trader_id, ask_price, match_qty);
            std::cout << "Trade: " << match_qty << " @ " << ask_price << std::endl;
            
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

            logTrade(incoming.trader_id, book_order.trader_id, bid_price, match_qty);
            std::cout << "Trade: " << match_qty << " @ " << bid_price << std::endl;
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
void OrderBook::cancelOrder(std::uint64_t orderId) {
    for (auto it = bids_.begin(); it != bids_.end(); ++it) {
        auto& list = it->second;
        for (auto orderIt = list.begin(); orderIt != list.end(); ++orderIt) {
            if (orderIt->order_id == orderId) {
                list.erase(orderIt); 
                if (list.empty()) {
                    bids_.erase(it); 
                }
                return; 
            }
        }
    }

    for (auto it = asks_.begin(); it != asks_.end(); ++it) {
        auto& list = it->second;
        for (auto orderIt = list.begin(); orderIt != list.end(); ++orderIt) {
            if (orderIt->order_id == orderId) {
                list.erase(orderIt);
                
                if (list.empty()) {
                    asks_.erase(it);
                }
                return;
            }
        }
    }
    
    std::cout << "Order NOT FOUND" << std::endl;
}
void OrderBook::modifyOrder(OrderModification mod) {
    for (auto it = bids_.begin(); it != bids_.end(); ++it) {
        auto& list = it->second;
        for (auto orderIt = list.begin(); orderIt != list.end(); ++orderIt) {
            if (orderIt->order_id == mod.order_id) {
                Order newOrder = *orderIt;
                newOrder.price = mod.new_price;
                newOrder.quantity = mod.new_quantity;
                newOrder.side = mod.new_side;

                list.erase(orderIt);
                
                if (list.empty()) {
                    bids_.erase(it);
                }
                
                addOrder(newOrder);
                return;
            }
        }
    }

    for (auto it = asks_.begin(); it != asks_.end(); ++it) {
        auto& list = it->second;
        for (auto orderIt = list.begin(); orderIt != list.end(); ++orderIt) {
            if (orderIt->order_id == mod.order_id) {
                Order newOrder = *orderIt;
                newOrder.price = mod.new_price;
                newOrder.quantity = mod.new_quantity;
                newOrder.side = mod.new_side;

                list.erase(orderIt);
                if (list.empty()) {
                    asks_.erase(it);
                }
                addOrder(newOrder);
                return;
            }
        }
    }
}