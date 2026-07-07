// ---------------------------------------------------------------------------
// server.cpp  --  a small HTTP server that exposes the C++ matching engine as
// a live "exchange" web app. Built on cpp-httplib (single header, no deps).
//
//   GET  /                 -> the browser UI (server/public/index.html)
//   GET  /api/symbols      -> all tickers with last price + BBO
//   GET  /api/book         -> ?symbol=AAPL&depth=10  L2 ladder
//   GET  /api/trades       -> ?symbol=AAPL&limit=30  recent tape
//   GET  /api/myorders     -> the user's resting orders
//   GET  /api/report       -> session report (JSON)
//   GET  /api/report.csv   -> session report (CSV download)
//   POST /api/order        -> place an order (form: symbol,side,type,price,qty)
//   POST /api/cancel       -> cancel an order (form: id)
//
// Prices are integer CENTS everywhere (the UI converts to dollars).
// ---------------------------------------------------------------------------
#include "httplib.h"
#include "MarketSimulator.h"
#include <string>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <iostream>

namespace {

MarketSimulator g_market;

// ---- tiny JSON helpers (we control the shape, so hand-building is fine) ----
std::string jescape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}
std::string qstr(const std::string& s) { return "\"" + jescape(s) + "\""; }
const char* sideStr(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }

std::uint64_t paramU64(const httplib::Request& req, const char* key, std::uint64_t def = 0) {
    if (!req.has_param(key)) return def;
    try { return std::stoull(req.get_param_value(key)); } catch (...) { return def; }
}

bool parseSide(const std::string& s, Side& out) {
    if (s == "BUY")  { out = Side::Buy;  return true; }
    if (s == "SELL") { out = Side::Sell; return true; }
    return false;
}
bool parseType(const std::string& s, OrderType& out) {
    if (s == "GTC" || s == "LIMIT") { out = OrderType::GoodTillCancel; return true; }
    if (s == "IOC") { out = OrderType::FillAndKill; return true; }
    if (s == "FOK") { out = OrderType::FillOrKill;  return true; }
    if (s == "MKT" || s == "MARKET") { out = OrderType::Market; return true; }
    return false;
}

// ---- route handlers -------------------------------------------------------
void handleSymbols(const httplib::Request&, httplib::Response& res) {
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& q : g_market.quotes()) {
        if (!first) os << ",";
        first = false;
        const long long change = (long long)q.last - (long long)q.reference;
        char pct[32];
        std::snprintf(pct, sizeof(pct), "%.2f",
                      q.reference ? 100.0 * change / (double)q.reference : 0.0);
        os << "{" << "\"symbol\":" << qstr(q.symbol)
           << ",\"last\":" << q.last
           << ",\"reference\":" << q.reference
           << ",\"change\":" << change
           << ",\"changePct\":" << pct
           << ",\"bestBid\":" << (q.hasBid ? std::to_string(q.bestBid) : "null")
           << ",\"bestAsk\":" << (q.hasAsk ? std::to_string(q.bestAsk) : "null")
           << ",\"spread\":"
           << ((q.hasBid && q.hasAsk) ? std::to_string(q.bestAsk - q.bestBid) : "null")
           << ",\"held\":" << q.heldQty
           << ",\"sellable\":" << q.sellableQty
           << "}";
    }
    os << "]";
    res.set_content(os.str(), "application/json");
}

void handleBook(const httplib::Request& req, httplib::Response& res) {
    const std::string sym = req.get_param_value("symbol");
    const std::size_t depth = (std::size_t)paramU64(req, "depth", 10);
    std::vector<OrderBook::Level> bids, asks;
    MarketSimulator::Quote q;
    if (!g_market.book(sym, depth, bids, asks, q)) {
        res.status = 404;
        res.set_content("{\"error\":\"unknown symbol\"}", "application/json");
        return;
    }
    auto levels = [](std::ostringstream& os, const std::vector<OrderBook::Level>& v) {
        os << "[";
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) os << ",";
            os << "{\"price\":" << v[i].price << ",\"qty\":" << v[i].quantity
               << ",\"orders\":" << v[i].order_count << "}";
        }
        os << "]";
    };
    std::ostringstream os;
    os << "{\"symbol\":" << qstr(q.symbol)
       << ",\"last\":" << q.last
       << ",\"reference\":" << q.reference
       << ",\"bestBid\":" << (q.hasBid ? std::to_string(q.bestBid) : "null")
       << ",\"bestAsk\":" << (q.hasAsk ? std::to_string(q.bestAsk) : "null")
       << ",\"spread\":"
       << ((q.hasBid && q.hasAsk) ? std::to_string(q.bestAsk - q.bestBid) : "null")
       << ",\"held\":" << q.heldQty
       << ",\"sellable\":" << q.sellableQty
       << ",\"bids\":"; levels(os, bids);
    os << ",\"asks\":"; levels(os, asks);
    os << "}";
    res.set_content(os.str(), "application/json");
}

void handleTrades(const httplib::Request& req, httplib::Response& res) {
    const std::string sym = req.get_param_value("symbol");
    const std::size_t limit = (std::size_t)paramU64(req, "limit", 30);
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& t : g_market.recentTrades(sym, limit)) {
        if (!first) os << ",";
        first = false;
        os << "{\"seq\":" << t.seq << ",\"price\":" << t.price
           << ",\"qty\":" << t.quantity << ",\"side\":" << qstr(sideStr(t.aggressor)) << "}";
    }
    os << "]";
    res.set_content(os.str(), "application/json");
}

void handleOrder(const httplib::Request& req, httplib::Response& res) {
    const std::string sym = req.get_param_value("symbol");
    Side side; OrderType type;
    if (!parseSide(req.get_param_value("side"), side) ||
        !parseType(req.get_param_value("type"), type)) {
        res.status = 400;
        res.set_content("{\"accepted\":false,\"message\":\"bad side/type\"}", "application/json");
        return;
    }
    const std::uint64_t price = paramU64(req, "price", 0);
    const std::uint64_t qty   = paramU64(req, "qty", 0);

    auto r = g_market.placeUserOrder(sym, side, type, price, qty);
    std::uint64_t filled = 0;
    for (const auto& t : r.trades) filled += t.quantity;

    std::ostringstream os;
    os << "{\"accepted\":" << (r.accepted ? "true" : "false")
       << ",\"message\":" << qstr(r.message)
       << ",\"orderId\":" << r.orderId
       << ",\"filledQty\":" << filled
       << ",\"restingQty\":" << r.restingQty
       << ",\"fills\":[";
    for (std::size_t i = 0; i < r.trades.size(); ++i) {
        if (i) os << ",";
        os << "{\"price\":" << r.trades[i].price << ",\"qty\":" << r.trades[i].quantity << "}";
    }
    os << "]}";
    res.set_content(os.str(), "application/json");
}

void handleCancel(const httplib::Request& req, httplib::Response& res) {
    const bool ok = g_market.cancelUserOrder(paramU64(req, "id", 0));
    res.set_content(std::string("{\"ok\":") + (ok ? "true" : "false") + "}", "application/json");
}

void handleMyOrders(const httplib::Request&, httplib::Response& res) {
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& o : g_market.userOrders()) {
        if (!first) os << ",";
        first = false;
        os << "{\"id\":" << o.id << ",\"symbol\":" << qstr(o.symbol)
           << ",\"side\":" << qstr(sideStr(o.side))
           << ",\"price\":" << o.price << ",\"qty\":" << o.quantity << "}";
    }
    os << "]";
    res.set_content(os.str(), "application/json");
}

// Shared P&L math used by both the JSON and CSV report endpoints.
struct Row {
    std::string symbol;
    long long bought, sold, position, avgBuy, avgSell, avgCost, mark, cash;
    long long realized, unrealized, pnl;
};
std::vector<Row> buildRows(const MarketSimulator::Report& rep, long long& totalPnl,
                           long long& totalRealized) {
    std::vector<Row> rows;
    totalPnl = 0; totalRealized = 0;
    for (const auto& s : rep.perSymbol) {
        Row r;
        r.symbol  = s.symbol;
        r.bought  = (long long)s.bought;
        r.sold    = (long long)s.sold;
        r.position = r.bought - r.sold;
        r.avgBuy  = s.bought ? (long long)(s.buyNotional / s.bought) : 0;
        r.avgSell = s.sold ? (long long)(s.sellNotional / s.sold) : 0;
        r.avgCost = std::llround(s.avgCost);
        r.mark    = (long long)s.markPrice;
        r.cash    = (long long)s.sellNotional - (long long)s.buyNotional;
        r.pnl     = r.cash + r.position * r.mark;   // total mark-to-market P&L
        r.realized   = std::llround(s.realized);    // booked (avg-cost) P&L
        r.unrealized = r.pnl - r.realized;          // open P&L (total = realised + unrealised)
        totalPnl += r.pnl; totalRealized += r.realized;
        rows.push_back(r);
    }
    return rows;
}

void handleReport(const httplib::Request&, httplib::Response& res) {
    auto rep = g_market.report();
    long long totalPnl = 0, totalRealized = 0;
    auto rows = buildRows(rep, totalPnl, totalRealized);

    std::ostringstream os;
    os << "{\"ordersPlaced\":" << rep.ordersPlaced
       << ",\"ordersCancelled\":" << rep.ordersCancelled
       << ",\"openOrders\":" << rep.openOrders
       << ",\"userVolume\":" << rep.userVolume
       << ",\"totalPnl\":" << totalPnl
       << ",\"totalRealized\":" << totalRealized
       << ",\"totalUnrealized\":" << (totalPnl - totalRealized)
       << ",\"perSymbol\":[";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        if (i) os << ",";
        os << "{\"symbol\":" << qstr(r.symbol)
           << ",\"bought\":" << r.bought << ",\"sold\":" << r.sold
           << ",\"position\":" << r.position
           << ",\"avgBuy\":" << r.avgBuy << ",\"avgSell\":" << r.avgSell
           << ",\"avgCost\":" << r.avgCost
           << ",\"mark\":" << r.mark << ",\"cash\":" << r.cash
           << ",\"realized\":" << r.realized
           << ",\"unrealized\":" << r.unrealized
           << ",\"pnl\":" << r.pnl << "}";
    }
    os << "]}";
    res.set_content(os.str(), "application/json");
}

void handleReportCsv(const httplib::Request&, httplib::Response& res) {
    auto rep = g_market.report();
    long long totalPnl = 0, totalRealized = 0;
    auto rows = buildRows(rep, totalPnl, totalRealized);

    std::ostringstream os;
    os << "symbol,bought,sold,position,avg_cost_cents,mark_cents,realized_cents,unrealized_cents,pnl_cents\n";
    for (const auto& r : rows) {
        os << r.symbol << "," << r.bought << "," << r.sold << "," << r.position << ","
           << r.avgCost << "," << r.mark << "," << r.realized << "," << r.unrealized << "," << r.pnl << "\n";
    }
    os << "TOTAL,,,,,," << totalRealized << "," << (totalPnl - totalRealized) << "," << totalPnl << "\n";
    res.set_header("Content-Disposition", "attachment; filename=\"session_report.csv\"");
    res.set_content(os.str(), "text/csv");
}

} // namespace

int main(int argc, char** argv) {
    const int         port    = (argc > 1) ? std::atoi(argv[1]) : 8080;
    const std::string web_dir = (argc > 2) ? argv[2] : "server/public";

    g_market.start();   // launch the live market thread

    httplib::Server svr;
    svr.Get ("/api/symbols",    handleSymbols);
    svr.Get ("/api/book",       handleBook);
    svr.Get ("/api/trades",     handleTrades);
    svr.Get ("/api/myorders",   handleMyOrders);
    svr.Get ("/api/report",     handleReport);
    svr.Get ("/api/report.csv", handleReportCsv);
    svr.Post("/api/order",      handleOrder);
    svr.Post("/api/cancel",     handleCancel);
    svr.Get ("/api/health", [](const httplib::Request&, httplib::Response& r) {
        r.set_content("{\"ok\":true}", "application/json");
    });

    // Serve the browser UI from server/public (index.html at "/").
    if (!svr.set_mount_point("/", web_dir)) {
        std::cerr << "Warning: could not mount web dir '" << web_dir
                  << "'. The API still works; run from the LOB folder for the UI.\n";
    }

    std::cout << "LOB Exchange running:  http://localhost:" << port << "\n";
    std::cout << "(serving UI from '" << web_dir << "', Ctrl+C to stop)\n";
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Error: could not bind to port " << port << ".\n";
        g_market.stop();
        return 1;
    }
    g_market.stop();
    return 0;
}
