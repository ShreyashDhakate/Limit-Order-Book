// ---------------------------------------------------------------------------
// test_orderbook.cpp  --  a dependency-free unit-test suite for OrderBook.
//
// No external framework: a couple of macros keep score and the program exits
// non-zero if anything fails (so `make test` / CI can gate on it). Each test is
// a small, self-contained scenario that pins down one behaviour of the engine.
// ---------------------------------------------------------------------------
#include "../include/OrderBook.h"
#include <iostream>
#include <string>

namespace {

int g_checks = 0;
int g_failed = 0;

// CHECK(cond): record a pass/fail and print failures with file:line context.
#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failed;                                                        \
            std::cout << "  FAIL  " << __FILE__ << ':' << __LINE__             \
                      << "  (" #cond ")\n";                                    \
        }                                                                      \
    } while (0)

// Convenience builders so tests read like order tickets.
Order limit(std::uint64_t id, Side s, std::uint64_t px, std::uint64_t qty,
            OrderType t = OrderType::GoodTillCancel, std::uint64_t trader = 1) {
    return Order{id, trader, s, px, qty, t};
}

void test_simple_cross() {
    std::cout << "test_simple_cross\n";
    OrderBook b;
    CHECK(b.addOrder(limit(1, Side::Sell, 100, 10)).empty());   // rests, no trade.
    auto trades = b.addOrder(limit(2, Side::Buy, 100, 10));      // crosses fully.
    CHECK(trades.size() == 1);
    CHECK(trades[0].price == 100);
    CHECK(trades[0].quantity == 10);
    CHECK(trades[0].buy_order_id == 2 && trades[0].sell_order_id == 1);
    CHECK(b.empty());                                           // both consumed.
}

void test_price_improvement_for_taker() {
    std::cout << "test_price_improvement_for_taker\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Sell, 100, 10));
    // Buyer is willing to pay 105 but should trade at the resting 100.
    auto trades = b.addOrder(limit(2, Side::Buy, 105, 10));
    CHECK(trades.size() == 1);
    CHECK(trades[0].price == 100);
}

void test_time_priority() {
    std::cout << "test_time_priority\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Buy, 100, 10));   // first in queue at 100.
    b.addOrder(limit(2, Side::Buy, 100, 10));   // second in queue at 100.
    auto trades = b.addOrder(limit(3, Side::Sell, 100, 5));
    CHECK(trades.size() == 1);
    CHECK(trades[0].buy_order_id == 1);          // oldest order fills first.
}

void test_partial_fill_rests() {
    std::cout << "test_partial_fill_rests\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Sell, 100, 10));
    auto trades = b.addOrder(limit(2, Side::Buy, 100, 25));   // 10 fill, 15 rest.
    CHECK(trades.size() == 1 && trades[0].quantity == 10);
    CHECK(b.bestBid() && *b.bestBid() == 100);                // remainder rests.
    CHECK(b.size() == 1);
}

void test_cancel() {
    std::cout << "test_cancel\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Buy, 100, 10));
    CHECK(b.contains(1));
    CHECK(b.cancelOrder(1));            // returns true...
    CHECK(!b.contains(1));             // ...and the order is gone.
    CHECK(b.empty());
    CHECK(!b.cancelOrder(999));        // cancelling a missing id is a no-op.
}

void test_modify_reprices_and_requeues() {
    std::cout << "test_modify_reprices_and_requeues\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Buy, 100, 10));
    b.addOrder(limit(2, Side::Buy, 100, 10));   // behind #1.
    b.modifyOrder({1, 100, 10});                // reprice #1 (same px) -> goes to back.
    auto trades = b.addOrder(limit(3, Side::Sell, 100, 5));
    CHECK(trades.size() == 1);
    CHECK(trades[0].buy_order_id == 2);          // #2 now has priority over re-queued #1.
}

void test_modify_can_cross() {
    std::cout << "test_modify_can_cross\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Sell, 100, 10));
    b.addOrder(limit(2, Side::Buy, 95, 10));     // rests below the ask.
    auto trades = b.modifyOrder({2, 100, 10});   // bump bid up to 100 -> crosses.
    CHECK(trades.size() == 1);
    CHECK(trades[0].price == 100);
}

void test_ioc_discards_remainder() {
    std::cout << "test_ioc_discards_remainder\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Sell, 100, 10));
    auto trades = b.addOrder(limit(2, Side::Buy, 100, 25, OrderType::FillAndKill));
    CHECK(trades.size() == 1 && trades[0].quantity == 10);
    CHECK(b.empty());                            // the 15 leftover never rests.
}

void test_fok_all_or_nothing() {
    std::cout << "test_fok_all_or_nothing\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Sell, 100, 10));
    // Not enough liquidity for 25 -> FOK must do nothing.
    auto none = b.addOrder(limit(2, Side::Buy, 100, 25, OrderType::FillOrKill));
    CHECK(none.empty());
    CHECK(b.contains(1));                         // resting ask untouched.

    b.addOrder(limit(3, Side::Sell, 100, 20));    // now 30 available across levels.
    auto full = b.addOrder(limit(4, Side::Buy, 100, 25, OrderType::FillOrKill));
    CHECK(!full.empty());
    std::uint64_t filled = 0;
    for (auto& t : full) filled += t.quantity;
    CHECK(filled == 25);                          // exactly the requested size.
}

void test_market_order() {
    std::cout << "test_market_order\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Sell, 100, 10));
    b.addOrder(limit(2, Side::Sell, 101, 10));
    // Market buy sweeps best price first, never rests.
    auto trades = b.addOrder(limit(3, Side::Buy, 0, 15, OrderType::Market));
    CHECK(trades.size() == 2);
    CHECK(trades[0].price == 100 && trades[0].quantity == 10);
    CHECK(trades[1].price == 101 && trades[1].quantity == 5);
    CHECK(!b.contains(3));                         // market order does not rest.
    CHECK(b.bestAsk() && *b.bestAsk() == 101);     // 5 left at 101.
}

void test_market_data_and_stats() {
    std::cout << "test_market_data_and_stats\n";
    OrderBook b;
    b.addOrder(limit(1, Side::Buy, 99, 50));
    b.addOrder(limit(2, Side::Buy, 99, 20));   // same level -> aggregates.
    b.addOrder(limit(3, Side::Buy, 98, 30));
    b.addOrder(limit(4, Side::Sell, 101, 40));

    CHECK(b.bestBid() && *b.bestBid() == 99);
    CHECK(b.bestAsk() && *b.bestAsk() == 101);
    CHECK(b.spread() && *b.spread() == 2);

    auto bids = b.bidLevels();
    CHECK(bids.size() == 2);
    CHECK(bids[0].price == 99 && bids[0].quantity == 70 && bids[0].order_count == 2);
    CHECK(bids[1].price == 98 && bids[1].quantity == 30);

    CHECK(b.bidLevels(1).size() == 1);          // depth limit honoured.

    // Sell 60 @ 98 crosses every bid >= 98; best price (99) fills first:
    // 50 from #1 then 10 from #2 -> 60 total, all at price 99.
    auto trades = b.addOrder(limit(5, Side::Sell, 98, 60));
    std::uint64_t vol = 0;
    for (auto& t : trades) vol += t.quantity;
    CHECK(vol == 60);
    CHECK(b.totalVolumeTraded() == 60);
    CHECK(b.tradeCount() == trades.size());
}

void test_rejections() {
    std::cout << "test_rejections\n";
    OrderBook b;
    CHECK(b.addOrder(limit(1, Side::Buy, 100, 0)).empty());   // zero qty rejected.
    CHECK(!b.contains(1));
    b.addOrder(limit(2, Side::Buy, 100, 10));
    CHECK(b.addOrder(limit(2, Side::Buy, 50, 5)).empty());     // duplicate id rejected.
    CHECK(b.bidLevels()[0].quantity == 10);                    // original untouched.
}

} // namespace

int main() {
    test_simple_cross();
    test_price_improvement_for_taker();
    test_time_priority();
    test_partial_fill_rests();
    test_cancel();
    test_modify_reprices_and_requeues();
    test_modify_can_cross();
    test_ioc_discards_remainder();
    test_fok_all_or_nothing();
    test_market_order();
    test_market_data_and_stats();
    test_rejections();

    std::cout << "\n" << (g_checks - g_failed) << "/" << g_checks << " checks passed.\n";
    if (g_failed) {
        std::cout << g_failed << " CHECK(s) FAILED\n";
        return 1;
    }
    std::cout << "All tests passed.\n";
    return 0;
}
