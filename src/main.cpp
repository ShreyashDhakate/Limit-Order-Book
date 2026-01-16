#include <iostream>
#include "../include/OrderBook.h"
#include "../include/Order.h"

int main() {
    OrderBook book;

    // // 1. Setup the Book (Passive Orders)
    // Order o1 = {1, 1001, Side::Buy, 100, 100, OrderType::GoodTillCancel};
    // Order o2 = {2, 1002, Side::Sell, 105, 100, OrderType::GoodTillCancel};
    // Order o3 = {3, 1001, Side::Buy, 100, 50,  OrderType::GoodTillCancel}; 
    // Order o4 = {4, 1003, Side::Buy, 99,  200, OrderType::GoodTillCancel}; 

    // book.addOrder(o1);
    // book.addOrder(o2);
    // book.addOrder(o3);
    // book.addOrder(o4);

    // std::cout << "--- INITIAL STATE ---" << std::endl;
    // book.print();

    // Order o5 = {5, 1004, Side::Sell, 100, 100, OrderType::GoodTillCancel}; 
    // book.addOrder(o5);
    
    // Order o6 = {6, 1005, Side::Buy, 101, 150, OrderType::GoodTillCancel};
    // book.addOrder(o6);
    // book.print();
    // Order o7 = {7, 1006, Side::Sell, 10, 100, OrderType::Market};
    // Order o8 = {8, 1007, Side::Sell, 20, 102, OrderType::Market};
    // book.addOrder(o7);
    // book.addOrder(o8);
    // book.print();
    // ... setup orders ...
    Order o1 = {1, 1001, Side::Buy, 100, 100, OrderType::GoodTillCancel};
    book.addOrder(o1);

    std::cout << "--- BEFORE CANCEL ---" << std::endl;
    book.print();

    std::cout << "--- CANCELLING ORDER 1 ---" << std::endl;
    book.cancelOrder(1); // Should remove the order

    std::cout << "--- AFTER CANCEL ---" << std::endl;
    book.print();
    
}