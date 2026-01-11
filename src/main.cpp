#include <iostream>
#include "../include/OrderBook.h"
#include "../include/Order.h"

int main() {
    OrderBook book;

    // Order(id, trader, side, price, qty, type)
    Order o1 = {1, 1001, Side::Buy, 100, 100, OrderType::GoodTillCancel};
    Order o2 = {2, 1002, Side::Sell, 105, 100, OrderType::GoodTillCancel};
    Order o3 = {3, 1001, Side::Buy, 100, 50,  OrderType::GoodTillCancel}; // Same price as o1
    Order o4 = {4, 1003, Side::Buy, 99,  200, OrderType::GoodTillCancel}; // Lower price
    
    book.addOrder(o1);
    book.addOrder(o2);
    book.addOrder(o3);
    book.addOrder(o4);

    book.print();
}