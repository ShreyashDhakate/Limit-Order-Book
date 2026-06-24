#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// Order.h  --  Core value types used throughout the order book.
// These are plain data structs (no behaviour) so they are cheap to copy and
// easy to reason about.
// ---------------------------------------------------------------------------

// Which side of the book an order sits on.
enum class Side : std::uint8_t {
    Buy,
    Sell
};

// Time-in-force / execution instruction for an order.
enum class OrderType : std::uint8_t {
    GoodTillCancel,  // GTC : rest on the book until filled or cancelled.
    FillAndKill,     // IOC : match whatever is available now, discard the rest.
    FillOrKill,      // FOK : match the WHOLE quantity now, or do nothing at all.
    Market           // MKT : cross any price to fill now, never rests.
};

// A single order as submitted by a trader.
struct Order {
    std::uint64_t order_id  = 0;   // unique id assigned by the client/gateway.
    std::uint64_t trader_id = 0;   // who placed it (used for trade reporting).
    Side          side      = Side::Buy;
    std::uint64_t price     = 0;   // limit price (ignored for Market orders).
    std::uint64_t quantity  = 0;   // number of units to trade.
    OrderType     order_type = OrderType::GoodTillCancel;
};

// A request to change a resting order. The side is intentionally NOT modifiable:
// flipping a buy into a sell is a cancel + new order in every real venue.
// A modify always loses time priority (it is re-queued at the back of its level).
struct OrderModification {
    std::uint64_t order_id     = 0;
    std::uint64_t new_price    = 0;
    std::uint64_t new_quantity = 0;
};

// The result of two orders crossing. addOrder() returns a list of these so the
// engine stays pure (no I/O) and is trivially unit-testable.
struct Trade {
    std::uint64_t trade_id      = 0;
    std::uint64_t buy_order_id  = 0;
    std::uint64_t sell_order_id = 0;
    std::uint64_t buyer_id      = 0;   // trader_id on the buy side.
    std::uint64_t seller_id     = 0;   // trader_id on the sell side.
    std::uint64_t price         = 0;   // execution price (always the resting price).
    std::uint64_t quantity      = 0;   // units exchanged in this fill.
};
