#pragma once
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>
#include <cstdint>
#include "../include/OrderBook.h"

// ---------------------------------------------------------------------------
// MarketSimulator  --  turns the single-symbol OrderBook into a tiny live
// "exchange". It owns one OrderBook per ticker, runs a background thread that
// injects random market-maker + taker flow (so prices move on their own), and
// tracks the human user's orders/fills for reporting.
//
// Concurrency model: ONE mutex guards everything. The background tick thread
// and every HTTP request lock it briefly. Simple and correct for a demo; in
// production you'd shard per symbol or feed orders through a lock-free queue.
//
// All prices are integer CENTS (e.g. 19035 == $190.35).
// ---------------------------------------------------------------------------
class MarketSimulator {
public:
    static constexpr std::uint64_t kUserTrader = 1;   // the human's trader id
    static constexpr std::uint64_t kSimTrader  = 2;   // the simulated crowd

    // --- snapshots returned to the API layer -----------------------------
    struct Quote {
        std::string   symbol;
        std::uint64_t last      = 0;   // last trade price
        std::uint64_t reference = 0;   // session open ("prev close") for % change
        bool          hasBid = false, hasAsk = false;
        std::uint64_t bestBid = 0, bestAsk = 0;
    };

    struct TapeTrade {
        std::uint64_t seq      = 0;    // global event number, doubles as a clock
        std::uint64_t price    = 0;
        std::uint64_t quantity = 0;
        Side          aggressor = Side::Buy;   // side that initiated the trade
    };

    struct UserOrderView {
        std::uint64_t id    = 0;
        std::string   symbol;
        Side          side  = Side::Buy;
        std::uint64_t price = 0;
        std::uint64_t quantity = 0;    // remaining (live) quantity
    };

    struct PlaceResult {
        bool               accepted = false;
        std::string        message;
        std::vector<Trade> trades;
        std::uint64_t      orderId    = 0;
        std::uint64_t      restingQty = 0;
    };

    struct SymbolReport {
        std::string   symbol;
        std::uint64_t bought = 0, sold = 0;            // shares
        std::uint64_t buyNotional = 0, sellNotional = 0; // cents
        std::uint64_t markPrice = 0;                   // last price
    };
    struct Report {
        std::vector<SymbolReport> perSymbol;
        std::uint64_t ordersPlaced    = 0;
        std::uint64_t ordersCancelled = 0;
        std::uint64_t openOrders      = 0;   // user orders still resting
        std::uint64_t userVolume      = 0;   // total shares the user traded
    };

    MarketSimulator();
    ~MarketSimulator();

    void start();   // launch the background market thread
    void stop();

    // --- read APIs (thread-safe snapshots) -------------------------------
    std::vector<Quote> quotes();
    bool               book(const std::string& symbol, std::size_t depth,
                            std::vector<OrderBook::Level>& bids,
                            std::vector<OrderBook::Level>& asks,
                            Quote& quote);
    std::vector<TapeTrade>     recentTrades(const std::string& symbol, std::size_t limit);
    std::vector<UserOrderView> userOrders();
    Report                     report();
    bool                       hasSymbol(const std::string& symbol);

    // --- write APIs ------------------------------------------------------
    PlaceResult placeUserOrder(const std::string& symbol, Side side, OrderType type,
                               std::uint64_t price, std::uint64_t qty);
    bool        cancelUserOrder(std::uint64_t id);

private:
    struct Market {
        std::string                symbol;
        OrderBook                  book;
        std::uint64_t              reference = 0;  // session open
        std::uint64_t              last      = 0;  // last trade price
        std::uint64_t              mid       = 0;  // simulator "fair value"
        std::uint64_t              tick      = 5;  // price increment (cents)
        std::deque<TapeTrade>      tape;           // newest at the back
        std::deque<std::uint64_t>  simIds;         // resting sim order ids (for churn/cap)
    };

    // helpers below all assume mu_ is already held -------------------------
    void          seed(Market& m);
    void          tickOnce();
    void          tickSymbol(Market& m);
    void          recordTrades(Market& m, const std::vector<Trade>& trades, Side aggressor);
    // Submit one simulated (crowd) order and book-keep the result. Returns its id.
    std::uint64_t submitSim(Market& m, Side side, OrderType type,
                            std::uint64_t price, std::uint64_t qty);
    std::uint64_t nextId()  { return next_order_id_++; }
    std::uint64_t nextSeq() { return next_event_seq_++; }
    void          run();    // background loop

    mutable std::mutex                         mu_;
    std::vector<std::string>                   order_;     // symbol display order
    std::unordered_map<std::string, Market>    markets_;
    std::unordered_map<std::uint64_t, std::string> userOrderSymbol_;  // id -> symbol

    // user accounting (per symbol)
    struct Acct { std::uint64_t bought=0, sold=0, buyNotional=0, sellNotional=0; };
    std::unordered_map<std::string, Acct>      acct_;
    std::uint64_t userOrdersPlaced_    = 0;
    std::uint64_t userOrdersCancelled_ = 0;

    std::uint64_t next_order_id_ = 1;
    std::uint64_t next_event_seq_ = 1;
    std::mt19937_64 rng_;

    std::atomic<bool> running_{false};
    std::thread       worker_;
};
