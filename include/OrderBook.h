#pragma once
#include <cstdint>
#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>
#include "Order.h"

// ---------------------------------------------------------------------------
// OrderBook  --  a price/time-priority limit order book + matching engine.
//
// Data structures (the heart of the design):
//
//   bids_  : map<Price, list<Order>, greater>  -> best (highest) bid first.
//   asks_  : map<Price, list<Order>, less>     -> best (lowest)  ask first.
//            A std::map keeps price levels sorted; a std::list per level keeps
//            FIFO time priority. std::list iterators are STABLE, which is what
//            makes the O(1) index below possible.
//
//   index_ : unordered_map<order_id, OrderLocation>
//            Maps an order id straight to its (side, price, list iterator) so
//            cancel and modify are O(1) instead of scanning the whole book.
//
// The engine is pure: matching returns a vector<Trade> and performs no I/O,
// which makes it deterministic and easy to unit-test.
// ---------------------------------------------------------------------------
class OrderBook {
public:
    using Price     = std::uint64_t;
    using OrderList = std::list<Order>;

    // One aggregated price level, as seen in L2 ("market-by-price") data.
    struct Level {
        Price         price       = 0;
        std::uint64_t quantity    = 0;  // total resting quantity at this price.
        std::size_t   order_count = 0;  // number of distinct orders at this price.
    };

    // ---- Core API --------------------------------------------------------
    // Submit a new order. Returns every trade generated while matching it.
    std::vector<Trade> addOrder(const Order& order);

    // Remove a resting order. Returns true if the order existed. O(1).
    bool cancelOrder(std::uint64_t order_id);

    // Change a resting order's price/quantity. Implemented as cancel + re-add,
    // so the order loses time priority. Returns any trades the re-add produces.
    std::vector<Trade> modifyOrder(const OrderModification& mod);

    // ---- Market data -----------------------------------------------------
    std::optional<Price> bestBid() const;   // highest price someone will buy at.
    std::optional<Price> bestAsk() const;    // lowest price someone will sell at.
    std::optional<Price> spread() const;     // bestAsk - bestBid (if both exist).

    // L2 depth, best level first. depth == 0 means "all levels".
    std::vector<Level> bidLevels(std::size_t depth = 0) const;
    std::vector<Level> askLevels(std::size_t depth = 0) const;

    // ---- Introspection / statistics -------------------------------------
    bool          contains(std::uint64_t order_id) const;
    // Fetch a live copy of a resting order (with its current remaining qty),
    // or nullopt if it isn't on the book. O(1).
    std::optional<Order> getOrder(std::uint64_t order_id) const;
    std::size_t   size() const;                 // number of resting orders.
    bool          empty() const;
    std::uint64_t totalVolumeTraded() const;    // cumulative units matched.
    std::size_t   tradeCount() const;           // number of fills so far.

    // Human-readable dump of the book (L2 ladder).
    void print() const;

private:
    // Where a resting order lives, so we can reach it in O(1).
    struct OrderLocation {
        Side                side;
        Price               price;
        OrderList::iterator it;
    };

    std::map<Price, OrderList, std::greater<Price>> bids_;  // best bid = begin()
    std::map<Price, OrderList, std::less<Price>>    asks_;  // best ask = begin()
    std::unordered_map<std::uint64_t, OrderLocation> index_;

    std::uint64_t next_trade_id_       = 1;
    std::uint64_t total_volume_traded_ = 0;

    // Match `incoming` against the opposite book until it can no longer cross
    // or it is exhausted. Templated because bids_ and asks_ are different types
    // (different comparators) yet share identical matching logic.
    template <typename OppositeBook>
    std::vector<Trade> matchAgainst(Order& incoming, OppositeBook& opposite);

    // True if `order` can be filled in full right now (used for Fill-Or-Kill).
    bool canFullyFill(const Order& order) const;

    // Insert a (partially filled) order's remainder onto the book + index it.
    void rest(const Order& order);
};
