#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include "../include/OrderBook.h"
#include "../include/Order.h"

int main() {
    OrderBook book;
    std::ifstream file("orders.txt"); 

    if (!file.is_open()) {
        std::cerr << "Error: Could not open orders.txt. Make sure it is in the project root." << std::endl;
        return 1;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string command;
        iss >> command;

        if (command == "NEW") {
            std::uint64_t id, trader_id, price, qty; 
            std::string sideStr;

            if (!(iss >> id >> trader_id >> sideStr >> price >> qty)) {
                std::cerr << "Error parsing NEW command: " << line << std::endl;
                continue;
            }

            // --- THE FIX IS HERE ---
            Side side;
            if (sideStr == "BUY") {
                side = Side::Buy;
            } else if (sideStr == "SELL") {
                side = Side::Sell;
            } else {
                std::cerr << "Error: Unknown Side '" << sideStr << "'. Skipping." << std::endl;
                continue;
            }
            // -----------------------

            Order order = {id, trader_id, side, price, qty, OrderType::GoodTillCancel};
            book.addOrder(order);
        }
        else if (command == "MOD") {
            // Format: MOD <id> <new_price> <new_qty>
            std::uint64_t id, new_price, new_qty;
            if (!(iss >> id >> new_price >> new_qty)) {
                std::cerr << "Error parsing MOD command" << std::endl;
                continue;
            }
            
            // Placeholder: Assuming Buy side for modification (Limit of V1.0)
            OrderModification mod = {id, new_price, new_qty, Side::Buy}; 
            book.modifyOrder(mod);
        }
        else if (command == "CXL") {
            // Format: CXL <id>
            std::uint64_t id;
            if (!(iss >> id)) {
                std::cerr << "Error parsing CXL command" << std::endl;
                continue;
            }
            book.cancelOrder(id);
        }
    }

    std::cout << "--- PROCESSING COMPLETE. FINAL BOOK STATE ---" << std::endl;
    book.print();

    return 0;
}