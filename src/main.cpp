#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "../include/OrderBook.h"
#include "../include/Order.h"

// ---------------------------------------------------------------------------
// main.cpp  --  a tiny command-driven driver for the OrderBook.
//
// It reads commands from a file (default: data/orders.txt, override with argv[1])
// and is the ONLY component that touches I/O. The engine itself stays pure.
//
// Command grammar (one per line, '#' starts a comment):
//   NEW  <id> <trader> <BUY|SELL> <price> <qty> [GTC|IOC|FOK|MKT]
//   MOD  <id> <new_price> <new_qty>
//   CXL  <id>
//   PRINT                 -> dump the L2 ladder
//   BBO                   -> print best bid / best ask / spread
//   DEPTH <n>             -> print the top n levels of each side
// ---------------------------------------------------------------------------

namespace {

bool parseSide(const std::string& s, Side& out) {
    if (s == "BUY")  { out = Side::Buy;  return true; }
    if (s == "SELL") { out = Side::Sell; return true; }
    return false;
}

bool parseType(const std::string& s, OrderType& out) {
    if (s == "GTC") { out = OrderType::GoodTillCancel; return true; }
    if (s == "IOC") { out = OrderType::FillAndKill;    return true; }
    if (s == "FOK") { out = OrderType::FillOrKill;     return true; }
    if (s == "MKT") { out = OrderType::Market;         return true; }
    return false;
}

// Append every fill to trades.csv and echo it to the console.
void reportTrades(const std::vector<Trade>& trades, std::ostream& csv) {
    for (const auto& t : trades) {
        csv << t.trade_id << ',' << t.buy_order_id << ',' << t.sell_order_id << ','
            << t.buyer_id << ',' << t.seller_id << ',' << t.price << ',' << t.quantity << '\n';
        std::cout << "  TRADE #" << t.trade_id << ": " << t.quantity
                  << " @ " << t.price
                  << "  (buy " << t.buy_order_id << " x sell " << t.sell_order_id << ")\n";
    }
}

void printBBO(const OrderBook& book) {
    auto bid = book.bestBid();
    auto ask = book.bestAsk();
    std::cout << "  BBO  bid=" << (bid ? std::to_string(*bid) : "-")
              << "  ask="      << (ask ? std::to_string(*ask) : "-");
    if (auto s = book.spread()) std::cout << "  spread=" << *s;
    std::cout << '\n';
}

void printDepth(const OrderBook& book, std::size_t n) {
    std::cout << "  ASKS (best first):\n";
    for (const auto& l : book.askLevels(n))
        std::cout << "    " << l.price << " x " << l.quantity
                  << " (" << l.order_count << " orders)\n";
    std::cout << "  BIDS (best first):\n";
    for (const auto& l : book.bidLevels(n))
        std::cout << "    " << l.price << " x " << l.quantity
                  << " (" << l.order_count << " orders)\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::string input_path = (argc > 1) ? argv[1] : "data/orders.txt";

    std::ifstream file(input_path);
    if (!file.is_open()) {
        std::cerr << "Error: could not open '" << input_path
                  << "'. Pass a path as the first argument, or run from the project root.\n";
        return 1;
    }

    // Open the trade log once and write a header so the CSV is self-describing.
    std::ofstream csv("trades.csv");
    csv << "trade_id,buy_order_id,sell_order_id,buyer_id,seller_id,price,quantity\n";

    OrderBook book;
    std::string line;
    std::size_t line_no = 0;

    while (std::getline(file, line)) {
        ++line_no;
        // Strip inline comments: everything from the first '#' to end of line.
        if (auto hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;  // blank.

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "NEW") {
            std::uint64_t id, trader, price, qty;
            std::string side_str, type_str;
            if (!(iss >> id >> trader >> side_str >> price >> qty)) {
                std::cerr << "[line " << line_no << "] bad NEW: " << line << '\n';
                continue;
            }
            Side side;
            if (!parseSide(side_str, side)) {
                std::cerr << "[line " << line_no << "] unknown side '" << side_str << "'\n";
                continue;
            }
            OrderType type = OrderType::GoodTillCancel;   // default time-in-force.
            if ((iss >> type_str) && !parseType(type_str, type)) {
                std::cerr << "[line " << line_no << "] unknown type '" << type_str << "'\n";
                continue;
            }
            std::cout << "NEW #" << id << ' ' << side_str << ' ' << qty << " @ " << price
                      << " (" << (type_str.empty() ? "GTC" : type_str) << ")\n";
            reportTrades(book.addOrder({id, trader, side, price, qty, type}), csv);
        }
        else if (cmd == "MOD") {
            std::uint64_t id, new_price, new_qty;
            if (!(iss >> id >> new_price >> new_qty)) {
                std::cerr << "[line " << line_no << "] bad MOD: " << line << '\n';
                continue;
            }
            std::cout << "MOD #" << id << " -> " << new_qty << " @ " << new_price << '\n';
            reportTrades(book.modifyOrder({id, new_price, new_qty}), csv);
        }
        else if (cmd == "CXL") {
            std::uint64_t id;
            if (!(iss >> id)) {
                std::cerr << "[line " << line_no << "] bad CXL: " << line << '\n';
                continue;
            }
            std::cout << "CXL #" << id << (book.cancelOrder(id) ? " ok\n" : " not found\n");
        }
        else if (cmd == "PRINT") {
            book.print();
        }
        else if (cmd == "BBO") {
            printBBO(book);
        }
        else if (cmd == "DEPTH") {
            std::size_t n = 0;
            iss >> n;                          // 0 -> all levels.
            printDepth(book, n);
        }
        else {
            std::cerr << "[line " << line_no << "] unknown command '" << cmd << "'\n";
        }
    }

    std::cout << "\n========== FINAL BOOK STATE ==========\n";
    book.print();
    std::cout << "Resting orders : " << book.size() << '\n';
    std::cout << "Trades executed: " << book.tradeCount() << '\n';
    std::cout << "Volume traded  : " << book.totalVolumeTraded() << '\n';
    std::cout << "Trade log      : trades.csv\n";
    return 0;
}
