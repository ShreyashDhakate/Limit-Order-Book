# Limit Order Book (LOB) & Matching Engine

A clean, well-tested **limit order book** with a **price/time-priority matching engine**, written in modern C++17. It is the data structure that sits at the heart of every electronic exchange (NASDAQ, NYSE, Binance, NSE…): it stores resting buy/sell orders and matches incoming orders against them.

The design favours the things a trading-systems interviewer actually asks about — **O(1) cancel/modify**, correct **price-time priority**, and proper **order-type semantics** (GTC / IOC / FOK / Market) — while keeping the code at a readable, idiomatic level (no lock-free wizardry, no template soup).

> **It also ships as a live web app.** The same C++ engine powers a browser **trading app** — a live order-book ladder, a holdings-aware order ticket, ~6 simulated stocks that trade on their own, a time-&-sales tape, and a live **holdings & P&L** view. It behaves like a real cash account: **you can only sell shares you actually own** (no naked short selling). The web server is C++ too (cpp-httplib). See **[Live web app](#live-web-app-exchange-ui)**.

```
        ===== ORDER BOOK =====
   PRICE | TOTAL QTY | #ORDERS
   ----------------------------
A     102 |        60 |       1     <- asks (sellers), worst price on top
A     101 |        15 |       1
   ---- spread: 2 ----              <- gap between best bid and best ask
B      99 |        70 |       2     <- bids (buyers), best price on top
B      98 |        30 |       1
```

---

## Table of contents
- [What it does](#what-it-does)
- [Why these data structures](#why-these-data-structures)
- [Complexity](#complexity)
- [Order types](#order-types)
- [How matching works](#how-matching-works-the-flow)
- [Build & run](#build--run)
- [Command grammar](#command-grammar)
- [Live web app](#live-web-app-exchange-ui)
  - [How the price moves](#how-the-price-moves)
- [Public API](#public-api)
- [Project layout](#project-layout)
- [Tests](#tests)
- [Design notes & interview talking points](#design-notes--interview-talking-points)
- [Possible extensions](#possible-extensions)

---

## What it does

- Accepts **limit, market, IOC and FOK** orders on a buy or sell side.
- **Matches** crossing orders following **price priority** (best price first) then **time priority** (FIFO within a price level).
- Supports **cancel** and **modify** of resting orders in **O(1)**.
- Exposes **market data**: best bid/ask, spread, and full **L2 depth** (aggregated quantity per price level).
- Returns a list of `Trade`s from every order so the engine is **pure and unit-testable** (no printing or file I/O inside the engine).
- Ships with a **command-driven CLI** and a **47-check test suite**.

---

## Why these data structures

The whole project is really a story about three containers working together:

```
bids_ : std::map<Price, std::list<Order>, std::greater>     asks_ : std::map<Price, std::list<Order>, std::less>
  99 -> [ #1:50 ] -> [ #3:20 ]   (FIFO queue)                101 -> [ #4:15 ]
  98 -> [ #2:30 ]                                            102 -> [ #5:60 ]
   ^ best bid = begin() (highest price)                       ^ best ask = begin() (lowest price)

index_ : std::unordered_map<order_id, {side, price, list_iterator}>
  #1 -> {Buy, 99, it} ... #5 -> {Sell, 102, it}      <- jump straight to any order in O(1)
```

| Container | Role | Why this one |
|-----------|------|--------------|
| `std::map<Price, list, Compare>` | one entry per **price level**, kept sorted | `begin()` is always the **best** price; custom comparator makes bids descending and asks ascending |
| `std::list<Order>` (per level) | the **FIFO queue** at a price | preserves **time priority**; crucially its **iterators stay valid** when other elements are inserted/erased |
| `std::unordered_map<id, location>` | the **index** from order id to where it lives | turns cancel/modify from an **O(n)** book scan into an **O(1)** lookup |

> The key insight is that **`std::list` iterators are stable**. We can store an iterator into the index and still erase the element later in O(1), even after the book has changed all around it. That is exactly why the book uses `list` and not `vector` for each level.

---

## Complexity

`n` = total resting orders, `L` = number of price levels touched while matching.

| Operation | Complexity | Notes |
|-----------|------------|-------|
| `addOrder` (no match) | **O(log L)** | one `map` insert |
| `addOrder` (matching) | **O(log L + k)** | `k` = resting orders consumed |
| `cancelOrder` | **O(1)** avg | index lookup + `list::erase` |
| `modifyOrder` | **O(1)** avg + re-add | cancel, then resubmit |
| `bestBid` / `bestAsk` / `spread` | **O(1)** | `map::begin()` |
| `bidLevels(d)` / `askLevels(d)` | **O(d + orders shown)** | aggregates qty per level |
| `canFullyFill` (FOK pre-check) | **O(L + k)** | walks crossable levels once |

The naive version scanned the entire book for cancel/modify; the index makes those **O(1)**, which is the single most important upgrade in this repo.

---

## Order types

| Keyword | Name | Behaviour |
|---------|------|-----------|
| `GTC` | Good-Till-Cancel (default) | Match what it can, then **rest** the remainder on the book until filled or cancelled. |
| `IOC` | Fill-And-Kill / Immediate-Or-Cancel | Match what it can **right now**, then **discard** the remainder (never rests). |
| `FOK` | Fill-Or-Kill | Either fill the **entire** quantity immediately, or do **nothing at all**. |
| `MKT` | Market | Cross **any** price to fill now (uses a sentinel limit), never rests. |

---

## How matching works (the flow)

```
addOrder(order)
   │
   ├─ reject if quantity == 0 or order_id already on the book
   │
   ├─ if Market  -> set price to a sentinel (MAX for buy, 0 for sell) so it always crosses
   │
   ├─ if FOK and !canFullyFill(order) -> return {} (do nothing)
   │
   ├─ matchAgainst(opposite book):
   │      while order has qty AND best opposite level crosses the limit:
   │          fill against the FRONT (oldest) order at that level   <- time priority
   │          emit a Trade at the resting (maker) price             <- price improvement for taker
   │          if a resting order hits 0 -> pop it + erase from index_
   │          if a level empties        -> erase the level from the map
   │
   └─ if leftover qty AND type == GTC -> rest() it (push to back of its level, add to index_)
      (IOC / FOK / Market leftovers are discarded)
```

Every fill is reported as a `Trade { trade_id, buy_order_id, sell_order_id, buyer_id, seller_id, price, quantity }`. Trades always execute at the **resting (maker) order's price**, which is why an aggressive buyer willing to pay 105 still trades at a resting 100.

---

## Build & run

Requires a **C++17** compiler. Pick whichever you have:

```bash
# Option A — one script, no build tools needed
./build.sh            # builds the CLI and runs it on data/orders.txt
./build.sh test       # builds and runs the unit tests

# Option B — GNU make
make run              # build + run the demo
make test             # build + run the tests

# Option C — CMake
cmake -B build && cmake --build build
./build/lob data/orders.txt
ctest --test-dir build

# Option D — straight g++ (what CI / the script does under the hood)
g++ -std=c++17 -O2 -Iinclude src/main.cpp src/OrderBook.cpp -o lob
./lob data/orders.txt
```

> **Windows (PowerShell)** with MSYS2/MinGW g++:
> ```powershell
> g++ -std=c++17 -O2 -Iinclude src/main.cpp src/OrderBook.cpp -o lob.exe
> .\lob.exe data\orders.txt
> ```

The CLI reads a command file (default `data/orders.txt`, or pass your own path), prints each action and the trades it generated, and writes a self-describing `trades.csv`.

---

## Command grammar

One command per line; `#` starts a comment (inline comments are stripped).

```
NEW <id> <trader> <BUY|SELL> <price> <qty> [GTC|IOC|FOK|MKT]   # submit an order
MOD <id> <new_price> <new_qty>                                 # reprice/resize (loses time priority)
CXL <id>                                                       # cancel a resting order
PRINT                                                          # dump the L2 ladder
BBO                                                            # best bid / best ask / spread
DEPTH <n>                                                      # top n levels per side (0 = all)
```

Example (`data/orders.txt`):
```
NEW 1 100 BUY  99  50
NEW 2 100 BUY  98  30
NEW 4 200 SELL 101 40
NEW 6 300 BUY  101 25     # crosses -> trades 25 @ 101
BBO
```

`trades.csv` output:
```
trade_id,buy_order_id,sell_order_id,buyer_id,seller_id,price,quantity
1,6,4,300,200,101,25
```

---

## Live web app (exchange UI)

The same engine also runs as a **browser trading app**, with a **C++ HTTP server** (cpp-httplib, a single vendored header) in front of it. There is no separate backend language and no frontend build step — your C++ matching engine *is* the backend. The UI is a clean, dark **"fintech"** theme (Zerodha-Kite / Robinhood flavour), plain HTML/CSS/JS.

```
            Browser UI  (server/public/ — 2 static pages, plain HTML/CSS/JS, polls ~0.9s)
                  |   REST over HTTP
                  v
        C++ HTTP server  (server/server.cpp, cpp-httplib)
                  |
        MarketSimulator  (server/MarketSimulator.cpp)
          • one OrderBook per symbol  (the engine above)
          • background thread injects market-maker + taker flow -> prices move live
          • tracks YOUR fills -> holdings, avg cost, realised & unrealised P&L
          • enforces "no naked shorts": you cannot sell shares you don't hold
          • one std::mutex guards all shared state
```

**Two pages:**

- **Trade** (`index.html`) — a **watchlist** of ~6 simulated stocks (live price, % change, and how many
  you hold); a **price chart** with 5M / 10M / session ranges; a live **order-book ladder** (click a level
  to load its price into the ticket); a **holdings-aware order ticket** (BUY/SELL, Limit / Market / IOC /
  FOK, a live *sellable* count, a *Max* button, large-order confirm — and the **SELL button stays disabled
  until you own the stock**); an **open orders** panel (one-click cancel); a **time & sales** tape; and a
  live **P&L** chip.
- **Orders & Holdings** (`orders.html`) — your **holdings** marked to the live price with **realised +
  unrealised P&L per symbol**, headline **Total / Realised / Unrealised** cards, and a full **order
  history** (time, side, type, price, filled, avg fill, value, status) with symbol/side filters and **CSV
  export**.

> **Holdings and P&L come straight from the C++ engine** (`/api/report`), so the numbers are always the
> server's truth — the app can never show P&L for shares the engine doesn't think you hold. Because you
> can't short, P&L is a clean **average-cost** split where `realised + unrealised == total`.

### How the price moves

Prices are **not** a scripted feed — they *emerge* from real matching in the same order book. Each symbol
carries three numbers (all integer **cents**): a fixed **reference** (the session "open", used for the %
change), a simulator **fair value** (`mid`, starts at the reference), and the **last traded price** (`last`,
which is what the UI shows).

A background thread ticks every symbol **~every 700 ms** (`MarketSimulator::tickSymbol`):

1. **Random-walk the fair value.** ~42% of ticks it steps one tick down, ~42% one tick up, ~16% flat —
   clamped to a **±10% band** around the open. A gentle **mean-reversion** pull nudges it back toward the
   open, so it wanders without running away.
2. **Market makers refresh liquidity.** A fresh **bid** a few ticks below and a fresh **ask** a few ticks
   above the fair value (random sizes) are posted, keeping a realistic two-sided book and spread.
3. **A taker crosses the spread (~70% of ticks).** A random-side **market order** (random size) hits the
   book → a **real trade prints** → `last` updates, and the fair value is nudged toward where it actually
   traded.
4. **The book is bounded** — the oldest simulated orders are cancelled once a per-symbol cap is hit.

Because every price change is an actual fill in the engine, **your own orders move the market too**: a
market buy lifts the offer and prints a new `last`, exactly like a real venue. Tick size is 5–10 cents per
symbol; the six seeds open around AAPL $190.35, MSFT $415.20, TSLA $248.75, AMZN $187.60, GOOGL $178.90,
NVDA $126.40.

**Build & run** (from the project root):

```bash
# Windows: just double-click server/run.bat, or:
./server/build.sh                 # compiles server/exchange.exe
./server/exchange.exe 8080        # then open http://localhost:8080
```

```powershell
# Windows / PowerShell, one line:
g++ -std=c++17 -O2 -Iinclude -Iserver server/server.cpp server/MarketSimulator.cpp src/OrderBook.cpp -o server/exchange.exe -lws2_32 -lwsock32 -pthread
.\server\exchange.exe 8080
```

> Linux/macOS: drop `-lws2_32 -lwsock32`, keep `-pthread`. `exchange.exe` takes an optional `<port>` (default 8080) and `<web_dir>` (default `server/public`).

**REST API** (all prices are integer **cents**):

| Method & path | Purpose |
|---------------|---------|
| `GET /api/symbols` | every ticker: last, % change, BBO, spread, **your holdings** (`held` / `sellable`) |
| `GET /api/book?symbol=AAPL&depth=10` | L2 ladder for a symbol (+ your holdings) |
| `GET /api/trades?symbol=AAPL&limit=30` | recent trade tape |
| `POST /api/order` | place an order (`symbol, side, type, price, qty`) → fills. **A sell beyond your holdings is rejected.** |
| `POST /api/cancel` | cancel a resting order (`id`) |
| `GET /api/myorders` | your resting orders |
| `GET /api/report` / `GET /api/report.csv` | live **holdings & P&L**: per-symbol position, avg cost, mark, realised, unrealised, total |
| `GET /api/health` | liveness probe |

**Interview angle:** this shows you can take a systems-level C++ component and expose it as a real service — a background simulation thread, a mutex-guarded shared book, order-management rules (no naked shorts), average-cost P&L, a REST layer, and a client — without leaving C++. The honest scope: single process, in-memory, polling (not WebSocket), one global lock. The natural upgrades (lock-free input queue, per-symbol sharding, WebSocket push) are in [Possible extensions](#possible-extensions).

---

## Public API

From [`include/OrderBook.h`](include/OrderBook.h):

```cpp
std::vector<Trade>   addOrder(const Order& order);        // submit; returns fills
bool                 cancelOrder(std::uint64_t id);       // O(1); false if not found
std::vector<Trade>   modifyOrder(const OrderModification& mod);

std::optional<Price> bestBid() const;                     // highest buy price
std::optional<Price> bestAsk() const;                     // lowest sell price
std::optional<Price> spread() const;                      // bestAsk - bestBid

std::vector<Level>   bidLevels(std::size_t depth = 0) const;  // L2 depth, best first
std::vector<Level>   askLevels(std::size_t depth = 0) const;

bool          contains(std::uint64_t id) const;
std::size_t   size() const;            // resting orders
std::uint64_t totalVolumeTraded() const;
std::size_t   tradeCount() const;
void          print() const;           // human-readable ladder
```

---

## Project layout

```
LOB/
├── include/
│   ├── Order.h          # value types: Side, OrderType, Order, OrderModification, Trade
│   └── OrderBook.h      # the OrderBook class (data structures + API, fully commented)
├── src/
│   ├── OrderBook.cpp    # the matching engine (addOrder/cancel/modify/market-data)
│   └── main.cpp         # CLI driver — the ONLY file that does I/O
├── server/              # the live web app (uses the same engine)
│   ├── httplib.h            # vendored cpp-httplib (single-header HTTP server)
│   ├── MarketSimulator.h/.cpp  # per-symbol books + background market thread + holdings/P&L
│   ├── server.cpp          # REST routes (the HTTP layer)
│   ├── build.sh            # build the server
│   ├── run.bat             # Windows: build + launch + open browser
│   └── public/             # the browser front-end (static, no build step)
│       ├── index.html          # Trade terminal
│       ├── orders.html         # Orders & Holdings (positions, P&L, order history)
│       └── assets/
│           ├── css/    # app (dark design system), terminal, orders
│           ├── js/     # format, api, store, ui, terminal, orders
│           └── icons/  # app icon (svg)
├── tests/
│   └── test_orderbook.cpp   # 47 assert-based checks, no framework needed
├── data/
│   └── orders.txt       # sample order flow exercising every feature
├── CMakeLists.txt       # CMake build (+ ctest)
├── Makefile             # `make run`, `make test`
├── build.sh             # zero-dependency build script
└── README.md
```

---

## Tests

```bash
./build.sh test     # or: make test
```

The suite pins down each behaviour independently:

- simple cross, price improvement for the taker, FIFO time priority
- partial fill rests the remainder; full fill empties the book
- cancel (and cancelling a missing id); modify re-queues / can cross
- IOC discards the remainder; FOK is all-or-nothing
- market order sweeps levels and never rests
- best bid/ask/spread, L2 aggregation, depth limits, running statistics
- rejection of zero-quantity and duplicate-id orders

```
47/47 checks passed.
All tests passed.
```

---

## Design notes & interview talking points

- **Engine is pure.** Matching returns `std::vector<Trade>` and performs no I/O. Determinism makes it trivially testable and keeps logging/printing in `main.cpp`.
- **Iterator stability is the trick.** Storing `std::list::iterator` in the index is only safe because list iterators survive insertions/erasures elsewhere — a `vector` would invalidate them.
- **One templated matcher, two sides.** `bids_` and `asks_` are different types (different comparators), so `matchAgainst` is a small template instead of duplicated buy/sell loops. The crossing test collapses to `buy ? ask<=limit : bid>=limit`.
- **Market orders reuse the limit path** via a sentinel price (no special-case matching loop).
- **Modify = cancel + re-add**, so it intentionally loses time priority — the standard rule when price or visible quantity changes.
- **Maker-price execution.** Fills print at the resting order's price, giving the aggressor price improvement, exactly like a real venue.

## Possible extensions

Natural next steps if you want to push it further (kept out to respect a readable scope):

Engine:
- **Self-trade prevention** (cancel-newest / cancel-oldest / decrement-both when `trader_id` matches).
- **Iceberg / hidden orders** (display vs. total quantity).
- **Stop / stop-limit** orders triggered by last trade price.
- A **memory pool / arena allocator** for `Order` nodes to cut allocation overhead on the hot path.
- A **replay/benchmark harness** measuring orders-per-second throughput.
- **Sequence numbers + an event log** for deterministic replay and crash recovery.

Web app:
- **WebSocket push** instead of polling, so the UI updates the instant the book changes.
- **Per-symbol locks** (or a lock-free SPSC queue feeding the engine) to replace the single global mutex.
- **Server-side order history + persistent multi-user accounts.** Holdings & P&L are already server-authoritative; the per-order *history list* is still a local browser log, so moving it into the engine (and keying accounts by user) is the natural next step.
