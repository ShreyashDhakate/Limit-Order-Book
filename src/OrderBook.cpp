#include "../include/OrderBook.h"
#include <iostream>
#include <iomanip>
#include <limits>
#include <algorithm>

// ===========================================================================
// Matching core
// ===========================================================================

// Walk the best levels of the opposite book, generating fills, until `incoming`
// is exhausted or the best opposite price no longer crosses the limit price.
template <typename OppositeBook>
std::vector<Trade> OrderBook::matchAgainst(Order& incoming, OppositeBook& opposite) {
    std::vector<Trade> trades;
    const bool incoming_is_buy = (incoming.side == Side::Buy);

    while (incoming.quantity > 0 && !opposite.empty()) {
        auto  best_level  = opposite.begin();        // best price = front of map.
        const Price level_price = best_level->first;

        // A buy crosses an ask priced at or below its limit; a sell crosses a
        // bid priced at or above its limit. Market orders use a sentinel limit
        // (max for buys, 0 for sells) so they always cross.
        const bool crosses = incoming_is_buy ? (level_price <= incoming.price)
                                             : (level_price >= incoming.price);
        if (!crosses) break;

        OrderList& resting = best_level->second;     // FIFO queue at this price.
        while (incoming.quantity > 0 && !resting.empty()) {
            Order& maker = resting.front();          // oldest order has priority.
            const std::uint64_t fill = std::min(incoming.quantity, maker.quantity);

            Trade t;
            t.trade_id = next_trade_id_++;
            t.price    = level_price;                // fills happen at maker price.
            t.quantity = fill;
            if (incoming_is_buy) {
                t.buy_order_id  = incoming.order_id;  t.buyer_id  = incoming.trader_id;
                t.sell_order_id = maker.order_id;     t.seller_id = maker.trader_id;
            } else {
                t.sell_order_id = incoming.order_id;  t.seller_id = incoming.trader_id;
                t.buy_order_id  = maker.order_id;      t.buyer_id  = maker.trader_id;
            }
            trades.push_back(t);

            incoming.quantity   -= fill;
            maker.quantity      -= fill;
            total_volume_traded_ += fill;

            if (maker.quantity == 0) {               // maker fully consumed.
                index_.erase(maker.order_id);
                resting.pop_front();
            }
        }

        if (resting.empty()) opposite.erase(best_level);   // drop empty level.
    }
    return trades;
}

bool OrderBook::canFullyFill(const Order& order) const {
    std::uint64_t remaining = order.quantity;
    if (order.side == Side::Buy) {
        for (const auto& [price, list] : asks_) {
            if (price > order.price) break;          // no more crossable levels.
            for (const auto& o : list) {
                if (o.quantity >= remaining) return true;
                remaining -= o.quantity;
            }
        }
    } else {
        for (const auto& [price, list] : bids_) {
            if (price < order.price) break;
            for (const auto& o : list) {
                if (o.quantity >= remaining) return true;
                remaining -= o.quantity;
            }
        }
    }
    return false;
}

void OrderBook::rest(const Order& order) {
    if (order.side == Side::Buy) {
        OrderList& list = bids_[order.price];
        list.push_back(order);
        index_[order.order_id] = {Side::Buy, order.price, std::prev(list.end())};
    } else {
        OrderList& list = asks_[order.price];
        list.push_back(order);
        index_[order.order_id] = {Side::Sell, order.price, std::prev(list.end())};
    }
}

std::vector<Trade> OrderBook::addOrder(const Order& order) {
    // Reject bad input: zero quantity or a duplicate id already on the book.
    if (order.quantity == 0 || index_.count(order.order_id)) return {};

    Order incoming = order;

    // A market order is just a limit order with an unbeatable price.
    if (incoming.order_type == OrderType::Market) {
        incoming.price = (incoming.side == Side::Buy)
                       ? std::numeric_limits<Price>::max()
                       : 0;
    }

    // Fill-Or-Kill: abort entirely unless the whole size can trade right now.
    if (incoming.order_type == OrderType::FillOrKill && !canFullyFill(incoming)) {
        return {};
    }

    std::vector<Trade> trades = (incoming.side == Side::Buy)
                              ? matchAgainst(incoming, asks_)
                              : matchAgainst(incoming, bids_);

    // Only a GTC limit order rests; IOC / FOK / Market never sit on the book.
    if (incoming.quantity > 0 && incoming.order_type == OrderType::GoodTillCancel) {
        rest(incoming);
    }
    return trades;
}

// ===========================================================================
// Cancel / modify  --  both O(1) thanks to index_.
// ===========================================================================

bool OrderBook::cancelOrder(std::uint64_t order_id) {
    auto found = index_.find(order_id);
    if (found == index_.end()) return false;

    const OrderLocation loc = found->second;
    if (loc.side == Side::Buy) {
        auto level = bids_.find(loc.price);
        level->second.erase(loc.it);
        if (level->second.empty()) bids_.erase(level);
    } else {
        auto level = asks_.find(loc.price);
        level->second.erase(loc.it);
        if (level->second.empty()) asks_.erase(level);
    }
    index_.erase(found);
    return true;
}

std::vector<Trade> OrderBook::modifyOrder(const OrderModification& mod) {
    auto found = index_.find(mod.order_id);
    if (found == index_.end()) return {};

    // Preserve every field except the two the caller is changing. In particular
    // the side is kept from the original order (fixes the old "assume Buy" bug).
    Order updated      = *found->second.it;
    updated.price      = mod.new_price;
    updated.quantity   = mod.new_quantity;

    cancelOrder(mod.order_id);          // remove the stale resting order...
    return addOrder(updated);           // ...and resubmit (re-matches if it now crosses).
}

// ===========================================================================
// Market data
// ===========================================================================

std::optional<OrderBook::Price> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<OrderBook::Price> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<OrderBook::Price> OrderBook::spread() const {
    if (bids_.empty() || asks_.empty()) return std::nullopt;
    return asks_.begin()->first - bids_.begin()->first;
}

std::vector<OrderBook::Level> OrderBook::bidLevels(std::size_t depth) const {
    std::vector<Level> out;
    for (const auto& [price, list] : bids_) {
        if (depth != 0 && out.size() >= depth) break;
        std::uint64_t qty = 0;
        for (const auto& o : list) qty += o.quantity;
        out.push_back({price, qty, list.size()});
    }
    return out;
}

std::vector<OrderBook::Level> OrderBook::askLevels(std::size_t depth) const {
    std::vector<Level> out;
    for (const auto& [price, list] : asks_) {
        if (depth != 0 && out.size() >= depth) break;
        std::uint64_t qty = 0;
        for (const auto& o : list) qty += o.quantity;
        out.push_back({price, qty, list.size()});
    }
    return out;
}

// ===========================================================================
// Introspection
// ===========================================================================

bool          OrderBook::contains(std::uint64_t id) const { return index_.count(id) != 0; }
std::size_t   OrderBook::size() const                     { return index_.size(); }
bool          OrderBook::empty() const                    { return index_.empty(); }
std::uint64_t OrderBook::totalVolumeTraded() const        { return total_volume_traded_; }
std::size_t   OrderBook::tradeCount() const               { return next_trade_id_ - 1; }

void OrderBook::print() const {
    std::cout << "        ===== ORDER BOOK =====\n";
    std::cout << "   PRICE | TOTAL QTY | #ORDERS\n";
    std::cout << "   ----------------------------\n";

    // Asks printed from worst (top) down to best, so the spread sits in the
    // middle of the ladder like a real depth display.
    std::vector<Level> asks = askLevels();
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::cout << "A  " << std::setw(6) << it->price << " | "
                  << std::setw(9) << it->quantity << " | "
                  << std::setw(7) << it->order_count << "\n";
    }

    if (auto s = spread()) std::cout << "   ---- spread: " << *s << " ----\n";
    else                   std::cout << "   ---- (one-sided book) ----\n";

    for (const auto& lvl : bidLevels()) {
        std::cout << "B  " << std::setw(6) << lvl.price << " | "
                  << std::setw(9) << lvl.quantity << " | "
                  << std::setw(7) << lvl.order_count << "\n";
    }
    std::cout << std::endl;
}

// Explicit instantiations are unnecessary: matchAgainst is only ever called
// from addOrder (above, in this same translation unit) with asks_ and bids_,
// so the compiler instantiates both versions right here.
