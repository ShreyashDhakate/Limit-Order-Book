#include "MarketSimulator.h"
#include <algorithm>
#include <chrono>

namespace {
// The dummy universe: ticker + session reference price (in cents) + tick size.
struct Seed { const char* sym; std::uint64_t ref; std::uint64_t tick; };
const Seed kUniverse[] = {
    {"AAPL", 19035, 5},
    {"MSFT", 41520, 10},
    {"TSLA", 24875, 10},
    {"AMZN", 18760, 5},
    {"GOOGL",17890, 5},
    {"NVDA", 12640, 5},
};
constexpr int    kSeedLevels = 6;     // levels per side to seed/replenish
constexpr std::size_t kMaxSimOrders = 80;   // cap resting sim orders per symbol
constexpr std::size_t kTapeMax      = 100;  // recent trades kept per symbol
} // namespace

MarketSimulator::MarketSimulator() : rng_(0xC0FFEEULL) {
    for (const auto& s : kUniverse) {
        Market m;
        m.symbol    = s.sym;
        m.reference = s.ref;
        m.tick      = s.tick;
        seed(m);
        order_.push_back(s.sym);
        markets_.emplace(s.sym, std::move(m));
    }
}

MarketSimulator::~MarketSimulator() { stop(); }

void MarketSimulator::start() {
    if (running_.exchange(true)) return;     // already running
    worker_ = std::thread(&MarketSimulator::run, this);
}

void MarketSimulator::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
}

void MarketSimulator::run() {
    while (running_.load()) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            tickOnce();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
    }
}

// --------------------------------------------------------------------------
// Simulation
// --------------------------------------------------------------------------
std::uint64_t MarketSimulator::submitSim(Market& m, Side side, OrderType type,
                                         std::uint64_t price, std::uint64_t qty) {
    Order o{nextId(), kSimTrader, side, price, qty, type};
    auto trades = m.book.addOrder(o);
    recordTrades(m, trades, side);
    if (m.book.contains(o.order_id)) m.simIds.push_back(o.order_id);
    return o.order_id;
}

void MarketSimulator::seed(Market& m) {
    m.mid = m.reference;
    m.last = m.reference;
    for (int i = 1; i <= kSeedLevels; ++i) {
        submitSim(m, Side::Buy,  OrderType::GoodTillCancel, m.mid - i * m.tick, 50 + rng_() % 450);
        submitSim(m, Side::Sell, OrderType::GoodTillCancel, m.mid + i * m.tick, 50 + rng_() % 450);
    }
}

void MarketSimulator::tickOnce() {
    for (auto& [sym, m] : markets_) tickSymbol(m);
}

void MarketSimulator::tickSymbol(Market& m) {
    // 1) Drift the fair value as a small mean-reverting random walk.
    const std::uint64_t lo = m.reference * 9 / 10;     // soft band: +/-10%
    const std::uint64_t hi = m.reference * 11 / 10;
    const int r = static_cast<int>(rng_() % 100);
    if (r < 42 && m.mid - m.tick > lo)      m.mid -= m.tick;     // down
    else if (r < 84 && m.mid + m.tick < hi) m.mid += m.tick;     // up
    // else flat. A gentle pull back toward reference keeps it from wandering off.
    if (m.mid > m.reference && (rng_() % 100) < 15) m.mid -= m.tick;
    if (m.mid < m.reference && (rng_() % 100) < 15) m.mid += m.tick;

    // 2) Refresh resting liquidity around the new fair value (market makers).
    const std::uint64_t bidLvl = 1 + rng_() % kSeedLevels;
    const std::uint64_t askLvl = 1 + rng_() % kSeedLevels;
    submitSim(m, Side::Buy,  OrderType::GoodTillCancel, m.mid - bidLvl * m.tick, 40 + rng_() % 400);
    submitSim(m, Side::Sell, OrderType::GoodTillCancel, m.mid + askLvl * m.tick, 40 + rng_() % 400);

    // 3) A taker crosses the spread ~70% of ticks -> generates a trade, moves last.
    if (rng_() % 100 < 70) {
        const Side side = (rng_() % 2 == 0) ? Side::Buy : Side::Sell;
        const std::uint64_t qty = 20 + rng_() % 180;
        submitSim(m, side, OrderType::Market, 0, qty);
        // Nudge fair value toward where trading actually happened.
        m.mid = (m.mid + m.last) / 2;
    }

    // 4) Bound the book: cancel the oldest sim orders once we exceed the cap.
    while (m.simIds.size() > kMaxSimOrders) {
        m.book.cancelOrder(m.simIds.front());   // returns false if already filled (fine)
        m.simIds.pop_front();
    }
}

void MarketSimulator::recordTrades(Market& m, const std::vector<Trade>& trades, Side aggressor) {
    for (const auto& t : trades) {
        m.last = t.price;                               // last trade price
        m.tape.push_back({nextSeq(), t.price, t.quantity, aggressor});
        if (m.tape.size() > kTapeMax) m.tape.pop_front();

        // User accounting: did the human take part in this fill?
        if (t.buyer_id == kUserTrader) {
            auto& a = acct_[m.symbol];
            a.bought      += t.quantity;
            a.buyNotional += t.price * t.quantity;
        }
        if (t.seller_id == kUserTrader) {
            auto& a = acct_[m.symbol];
            a.sold         += t.quantity;
            a.sellNotional += t.price * t.quantity;
        }
    }
}

// --------------------------------------------------------------------------
// Read APIs
// --------------------------------------------------------------------------
bool MarketSimulator::hasSymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mu_);
    return markets_.count(symbol) != 0;
}

std::vector<MarketSimulator::Quote> MarketSimulator::quotes() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Quote> out;
    out.reserve(order_.size());
    for (const auto& sym : order_) {
        Market& m = markets_.at(sym);
        Quote q;
        q.symbol = sym; q.last = m.last; q.reference = m.reference;
        if (auto b = m.book.bestBid()) { q.hasBid = true; q.bestBid = *b; }
        if (auto a = m.book.bestAsk()) { q.hasAsk = true; q.bestAsk = *a; }
        out.push_back(std::move(q));
    }
    return out;
}

bool MarketSimulator::book(const std::string& symbol, std::size_t depth,
                           std::vector<OrderBook::Level>& bids,
                           std::vector<OrderBook::Level>& asks, Quote& quote) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = markets_.find(symbol);
    if (it == markets_.end()) return false;
    Market& m = it->second;
    bids = m.book.bidLevels(depth);
    asks = m.book.askLevels(depth);
    quote.symbol = symbol; quote.last = m.last; quote.reference = m.reference;
    if (auto b = m.book.bestBid()) { quote.hasBid = true; quote.bestBid = *b; }
    if (auto a = m.book.bestAsk()) { quote.hasAsk = true; quote.bestAsk = *a; }
    return true;
}

std::vector<MarketSimulator::TapeTrade>
MarketSimulator::recentTrades(const std::string& symbol, std::size_t limit) {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<TapeTrade> out;
    auto it = markets_.find(symbol);
    if (it == markets_.end()) return out;
    const auto& tape = it->second.tape;
    std::size_t n = (limit == 0 || limit > tape.size()) ? tape.size() : limit;
    out.assign(tape.end() - n, tape.end());      // newest n, oldest first
    std::reverse(out.begin(), out.end());        // newest first for display
    return out;
}

std::vector<MarketSimulator::UserOrderView> MarketSimulator::userOrders() {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<UserOrderView> out;
    for (auto it = userOrderSymbol_.begin(); it != userOrderSymbol_.end(); ) {
        const std::uint64_t id  = it->first;
        Market&             m   = markets_.at(it->second);
        if (auto o = m.book.getOrder(id)) {
            out.push_back({id, it->second, o->side, o->price, o->quantity});
            ++it;
        } else {
            it = userOrderSymbol_.erase(it);     // filled/cancelled -> stop tracking
        }
    }
    return out;
}

MarketSimulator::Report MarketSimulator::report() {
    std::lock_guard<std::mutex> lock(mu_);
    Report rep;
    rep.ordersPlaced    = userOrdersPlaced_;
    rep.ordersCancelled = userOrdersCancelled_;
    rep.openOrders      = userOrderSymbol_.size();
    for (const auto& sym : order_) {
        auto a = acct_.count(sym) ? acct_.at(sym) : Acct{};
        if (a.bought == 0 && a.sold == 0) continue;       // only symbols traded
        SymbolReport sr;
        sr.symbol = sym;
        sr.bought = a.bought; sr.sold = a.sold;
        sr.buyNotional = a.buyNotional; sr.sellNotional = a.sellNotional;
        sr.markPrice = markets_.at(sym).last;
        rep.perSymbol.push_back(sr);
        rep.userVolume += a.bought + a.sold;
    }
    return rep;
}

// --------------------------------------------------------------------------
// Write APIs
// --------------------------------------------------------------------------
MarketSimulator::PlaceResult
MarketSimulator::placeUserOrder(const std::string& symbol, Side side, OrderType type,
                                std::uint64_t price, std::uint64_t qty) {
    std::lock_guard<std::mutex> lock(mu_);
    PlaceResult res;
    auto it = markets_.find(symbol);
    if (it == markets_.end()) { res.message = "unknown symbol"; return res; }
    if (qty == 0)             { res.message = "quantity must be > 0"; return res; }
    if (type != OrderType::Market && price == 0) {
        res.message = "limit orders need a price > 0"; return res;
    }
    Market& m = it->second;

    const std::uint64_t id = nextId();
    Order o{id, kUserTrader, side, price, qty, type};
    res.trades = m.book.addOrder(o);
    recordTrades(m, res.trades, side);

    std::uint64_t filled = 0;
    for (const auto& t : res.trades) filled += t.quantity;

    res.accepted = true;
    res.orderId  = id;
    ++userOrdersPlaced_;

    if (m.book.contains(id)) {                  // remainder rested (GTC)
        userOrderSymbol_[id] = symbol;
        res.restingQty = qty - filled;
    }

    if (filled == 0 && res.restingQty == 0)
        res.message = "no fill (order did not rest)";
    else if (res.restingQty > 0 && filled > 0)
        res.message = "partially filled, remainder resting";
    else if (res.restingQty > 0)
        res.message = "resting on the book";
    else
        res.message = "filled";
    return res;
}

bool MarketSimulator::cancelUserOrder(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = userOrderSymbol_.find(id);
    if (it == userOrderSymbol_.end()) return false;
    markets_.at(it->second).book.cancelOrder(id);
    userOrderSymbol_.erase(it);
    ++userOrdersCancelled_;
    return true;
}
