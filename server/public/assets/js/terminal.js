/* terminal.js — the live trading screen (index.html).
   Polls the C++ engine over /api/*, renders the watchlist, price chart, order
   book, holdings-aware order ticket, open orders and the tape. Enforces the
   real-world rule client-side too: you can only sell shares you actually own.  */
(function (LOB) {
  "use strict";
  const { api, store, fmt } = LOB, ui = LOB.ui, $ = ui.$, $$ = ui.$$;

  const REFRESH_MS = 900;
  const CONFIRM_NOTIONAL = 2000000;  // >= $20,000 asks to confirm
  const CONFIRM_QTY = 1000;
  const NAMES = { AAPL: "Apple Inc.", MSFT: "Microsoft Corp.", TSLA: "Tesla Inc.",
    AMZN: "Amazon.com Inc.", NVDA: "NVIDIA Corp.", GOOGL: "Alphabet Inc." };

  const state = {
    sel: null, side: "BUY", range: 600000, section: "chart",
    holdings: {}, prevLast: {}, book: null, quotes: [], filter: "",
  };
  const css = (v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim();

  /* --------------------------------------------------------- watchlist */
  function renderWatch(rows) {
    state.quotes = rows;
    rows.forEach((q) => (state.holdings[q.symbol] = { held: q.held || 0, sellable: q.sellable || 0 }));
    const f = state.filter.toUpperCase();
    const shown = rows.filter((q) => !f || q.symbol.includes(f) || (NAMES[q.symbol] || "").toUpperCase().includes(f));
    if (!state.sel && shown.length) state.sel = shown[0].symbol;
    $("#watchlist").innerHTML = shown.map((q) => {
      const prev = state.prevLast[q.symbol];
      const flash = prev == null || prev === q.last ? "" : q.last > prev ? "flash-up" : "flash-down";
      const held = (state.holdings[q.symbol] || {}).held || 0;
      return `<div class="wl-row ${q.symbol === state.sel ? "sel" : ""}" data-sym="${q.symbol}">
        <div><div class="wl-sym">${q.symbol}</div><div class="wl-name">${fmt.escapeHtml(NAMES[q.symbol] || "Simulated equity")}</div>
          ${held ? `<div class="wl-held">▲ ${fmt.int(held)} held</div>` : ""}</div>
        <div class="wl-right"><div class="wl-px ${flash}">$${fmt.price(q.last)}</div>
          <div class="wl-chg ${fmt.signClass(q.change)}">${fmt.pct(q.changePct)}</div></div>
      </div>`;
    }).join("") || `<div class="empty">No match for “${fmt.escapeHtml(state.filter)}”.</div>`;
    rows.forEach((q) => (state.prevLast[q.symbol] = q.last));

    const me = rows.find((q) => q.symbol === state.sel);
    if (me) {
      $("#symName").textContent = state.sel;
      $("#symDesc").textContent = NAMES[state.sel] || "Simulated equity";
      $("#symLast").textContent = "$" + fmt.price(me.last);
      $("#symChg").textContent = `${fmt.signedMoney(me.change)} (${fmt.pct(me.changePct)})`;
      $("#symChg").className = "c " + fmt.signClass(me.change);
      $("#kOpen").textContent = "$" + fmt.price(me.reference);
      submitLabel(); updateHoldings();
    }
  }

  /* ------------------------------------------------------------ book */
  // Top-5 depth: bids on the LEFT, asks on the RIGHT (5 rows), then a
  // buyers-vs-sellers imbalance bar built from the resting size on each side.
  function renderBook(b) {
    state.book = b;
    const bids = b.bids.slice(0, 5), asks = b.asks.slice(0, 5);
    const maxQty = Math.max(1, ...bids.map((l) => l.qty), ...asks.map((l) => l.qty));
    const cell = (l, cls) => {
      if (!l) return `<div class="half ${cls} empty"></div>`;
      const w = ((l.qty / maxQty) * 100).toFixed(0);
      return `<div class="half ${cls}" data-px="${l.price}" data-side="${cls === "buy" ? "SELL" : "BUY"}">
        <span class="bar" style="width:${w}%"></span>
        <span class="px">$${fmt.price(l.price)}</span><span class="sz">${fmt.int(l.qty)}</span></div>`;
    };
    let rows = "";
    for (let i = 0; i < 5; i++) rows += `<div class="dob-row">${cell(bids[i], "buy")}${cell(asks[i], "sell")}</div>`;
    $("#dobRows").innerHTML = rows;

    const mid = b.bestBid != null && b.bestAsk != null ? (b.bestBid + b.bestAsk) / 2 : b.last;
    $("#midPx").textContent = "$" + fmt.price(mid);
    $("#midSpread").textContent = b.spread == null ? "one-sided" : `spread $${fmt.price(b.spread)}`;
    $("#kBid").textContent = b.bestBid == null ? "—" : "$" + fmt.price(b.bestBid);
    $("#kAsk").textContent = b.bestAsk == null ? "—" : "$" + fmt.price(b.bestAsk);
    $("#kSpread").textContent = b.spread == null ? "—" : "$" + fmt.price(b.spread);

    // buyers vs sellers pressure from the visible top-5 resting size
    const bidQty = bids.reduce((a, l) => a + l.qty, 0), askQty = asks.reduce((a, l) => a + l.qty, 0);
    const tot = bidQty + askQty || 1;
    const buyPct = Math.round((bidQty / tot) * 100), sellPct = 100 - buyPct;
    $("#imbBuy").style.width = buyPct + "%";
    $("#imbSell").style.width = sellPct + "%";
    $("#imbBuyTxt").textContent = `Buyers ${buyPct}%`;
    $("#imbSellTxt").textContent = `Sellers ${sellPct}%`;
  }

  /* ------------------------------------------------------------ tape */
  function renderTape(rows) {
    $("#tape").innerHTML = rows.length ? rows.map((t) => {
      const c = t.side === "BUY" ? "tape-b" : "tape-s";
      return `<tr><td class="mono ${c}">$${fmt.price(t.price)}</td><td class="mono">${fmt.int(t.qty)}</td><td class="${c}">${fmt.arrow(t.side)}</td></tr>`;
    }).join("") : `<tr class="t-empty"><td colspan="3">no prints yet</td></tr>`;
  }

  /* -------------------------------------------------------- open orders */
  function renderOpen(rows) {
    $("#openOrders").innerHTML = rows.length ? rows.map((o) => `
      <tr><td>${o.symbol}</td><td><span class="badge ${o.side === "BUY" ? "buy" : "sell"}">${o.side}</span></td>
        <td class="mono">$${fmt.price(o.price)}</td><td class="mono">${fmt.int(o.qty)}</td>
        <td class="x" data-cancel="${o.id}" title="Cancel">${ui.icon("x", 14)}</td></tr>`).join("")
      : `<tr class="t-empty"><td colspan="5">no open orders</td></tr>`;
    $("#ordCount").textContent = rows.length;
  }

  /* ------------------------------------------------------------ chart */
  function drawChart() {
    const c = $("#chart"), ctx = c.getContext("2d");
    const dpr = window.devicePixelRatio || 1;
    const w = (c.width = Math.round(c.clientWidth * dpr));
    const h = (c.height = Math.round(c.clientHeight * dpr));
    ctx.clearRect(0, 0, w, h);
    const up = css("--up"), down = css("--down");
    const pts = store.series(state.sel, state.range);
    if (pts.length < 2) {
      ctx.fillStyle = css("--text-3"); ctx.font = `${12 * dpr}px ${css("--mono") || "monospace"}`;
      ctx.textAlign = "center"; ctx.fillText("collecting price history…", w / 2, h / 2);
      $("#chartMeta").textContent = ""; return;
    }
    const prices = pts.map((p) => p[1]);
    const min = Math.min(...prices), max = Math.max(...prices), rng = max - min || 1;
    const t0 = pts[0][0], t1 = pts[pts.length - 1][0], tr = t1 - t0 || 1;
    const rising = prices[prices.length - 1] >= prices[0];
    const padY = 14 * dpr;
    const x = (t) => ((t - t0) / tr) * w;
    const y = (p) => h - padY - ((p - min) / rng) * (h - 2 * padY);
    // dashed baseline at the window's opening price
    ctx.save(); ctx.strokeStyle = css("--border-2"); ctx.setLineDash([4 * dpr, 4 * dpr]); ctx.lineWidth = dpr;
    ctx.beginPath(); ctx.moveTo(0, y(prices[0])); ctx.lineTo(w, y(prices[0])); ctx.stroke(); ctx.restore();
    // area
    const grad = ctx.createLinearGradient(0, 0, 0, h);
    grad.addColorStop(0, rising ? "rgba(34,197,139,.22)" : "rgba(242,96,109,.22)");
    grad.addColorStop(1, "rgba(0,0,0,0)");
    ctx.beginPath(); ctx.moveTo(x(t0), h); pts.forEach((p) => ctx.lineTo(x(p[0]), y(p[1]))); ctx.lineTo(x(t1), h); ctx.closePath();
    ctx.fillStyle = grad; ctx.fill();
    // line
    ctx.beginPath(); pts.forEach((p, i) => (i ? ctx.lineTo(x(p[0]), y(p[1])) : ctx.moveTo(x(p[0]), y(p[1]))));
    ctx.strokeStyle = rising ? up : down; ctx.lineWidth = 1.8 * dpr; ctx.stroke();
    ctx.fillStyle = rising ? up : down;
    ctx.beginPath(); ctx.arc(x(t1), y(prices[prices.length - 1]), 2.8 * dpr, 0, 7); ctx.fill();
    $("#chartMeta").textContent = `H $${fmt.price(max)}  L $${fmt.price(min)}`;
  }

  /* ----------------------------------------------------------- ticket */
  function setSide(s) {
    state.side = s;
    $("#sideBuy").classList.toggle("on", s === "BUY");
    $("#sideSell").classList.toggle("on", s === "SELL");
    $("#placeBtn").className = "place " + (s === "BUY" ? "buy" : "sell");
    submitLabel(); updateEst(); updateHoldings();
  }
  function submitLabel() {
    $("#placeBtn").textContent = `${state.side} ${state.sel || ""}`.trim();
    const t = $("#tktSym"); if (t) t.textContent = state.sel || "—";
  }
  function updateHoldings() {
    const hd = state.holdings[state.sel] || { held: 0, sellable: 0 };
    $("#hHeld").textContent = fmt.int(hd.held);
    $("#hSell").textContent = fmt.int(hd.sellable);
    $("#hMax").hidden = !(state.side === "SELL" && hd.sellable > 0);
    const blocked = state.side === "SELL" && hd.sellable <= 0;
    $("#placeBtn").disabled = blocked;
    const msg = $("#msg");
    if (blocked) { msg.className = "msg warn"; msg.textContent = `You don't own ${state.sel} — buy it first to sell.`; }
    else if (msg.classList.contains("warn")) { msg.className = "msg"; msg.textContent = ""; }
  }
  function onType() {
    const isMkt = $("#oType").value === "MKT";
    $("#oPrice").disabled = isMkt;
    $("#oPrice").placeholder = isMkt ? "market" : "0.00";
    if (isMkt) $("#oPrice").value = "";
    updateEst();
  }
  function priceCents() {
    if ($("#oType").value === "MKT") { const me = state.quotes.find((q) => q.symbol === state.sel); return me ? me.last : 0; }
    return Math.round(parseFloat($("#oPrice").value || "0") * 100);
  }
  function updateEst() {
    const qty = parseInt($("#oQty").value || "0", 10) || 0;
    const est = priceCents() * qty;
    $("#estVal").textContent = est > 0 ? "$" + fmt.price(est) : "—";
  }
  function bump(d) { const el = $("#oQty"); el.value = Math.max(0, (parseInt(el.value || "0", 10) || 0) + d); updateEst(); }

  async function placeOrder() {
    const type = $("#oType").value, qty = parseInt($("#oQty").value || "0", 10) || 0;
    const px = priceCents(), msg = $("#msg");
    if (qty <= 0) { msg.className = "msg err"; msg.textContent = "Enter a quantity greater than 0."; return; }
    if (type !== "MKT" && px <= 0) { msg.className = "msg err"; msg.textContent = "Enter a limit price."; return; }
    if (state.side === "SELL") {
      const avail = (state.holdings[state.sel] || {}).sellable || 0;
      if (qty > avail) {
        msg.className = "msg err";
        msg.textContent = avail > 0 ? `You can only sell ${fmt.int(avail)} ${state.sel} you own.` : `You don't own any ${state.sel}. Buy some first.`;
        ui.toast({ type: "err", title: "Can't sell what you don't own", desc: `Sellable ${state.sel}: ${fmt.int(avail)}` });
        return;
      }
    }
    const notion = px * qty;
    if (notion >= CONFIRM_NOTIONAL || qty >= CONFIRM_QTY) {
      const ok = await ui.confirm({ title: "Confirm order", danger: state.side === "SELL", confirmText: `${state.side} ${state.sel}`,
        lines: [["Symbol", state.sel], ["Side", state.side], ["Type", $("#oType").options[$("#oType").selectedIndex].text],
          ["Quantity", fmt.int(qty) + " sh"], ["Price", type === "MKT" ? "Market" : "$" + fmt.price(px)], ["Est. value", "$" + fmt.price(notion)]] });
      if (!ok) { msg.className = "msg"; msg.textContent = "Order cancelled."; return; }
    }
    msg.className = "msg"; msg.textContent = "Working…";
    let res;
    try { res = await api.order({ symbol: state.sel, side: state.side, type, price: type === "MKT" ? 0 : px, qty }); }
    catch (_) { msg.className = "msg err"; msg.textContent = "Network error — order not sent."; return; }
    const d = res.data;
    store.logOrder({ symbol: state.sel, side: state.side, type, price: px, qty, accepted: d.accepted, message: d.message,
      orderId: d.orderId, restingQty: d.restingQty, fills: d.fills, latencyMs: res.latencyMs });
    if (!d.accepted) {
      msg.className = "msg err"; msg.textContent = "Rejected: " + d.message;
      ui.toast({ type: "err", title: "Order rejected", desc: d.message });
    } else {
      const filled = d.filledQty || 0;
      msg.className = "msg ok";
      msg.textContent = filled > 0 ? `Filled ${fmt.int(filled)}${d.restingQty > 0 ? ` (+${fmt.int(d.restingQty)} resting)` : ""}` : d.message;
      ui.toast({ type: "ok", title: filled > 0 ? `Filled ${fmt.int(filled)} ${state.sel}` : "Order accepted",
        desc: filled > 0 ? `avg $${fmt.price(d.fills.reduce((a, f) => a + f.price * f.qty, 0) / filled)} · ${res.latencyMs.toFixed(1)} ms`
                         : `${d.restingQty ? fmt.int(d.restingQty) + " resting" : d.message} · ${res.latencyMs.toFixed(1)} ms` });
    }
    refresh();
  }

  async function cancelOrder(id) {
    const o = (await api.myorders().catch(() => [])).find((x) => x.id === id) || {};
    let r; try { r = await api.cancel(id); } catch (_) { ui.toast({ type: "err", title: "Cancel failed" }); return; }
    store.logCancel({ orderId: id, symbol: o.symbol, side: o.side, price: o.price, qty: o.qty, ok: r.data.ok, latencyMs: r.latencyMs });
    ui.toast({ type: r.data.ok ? "ok" : "err", title: r.data.ok ? "Order cancelled" : "Order not found",
      desc: o.symbol ? `${o.side} ${o.symbol} @ $${fmt.price(o.price)}` : "" });
    refresh();
  }

  /* --------------------------------------------------------- selection */
  function selectSym(s) {
    if (!s || s === state.sel) return;
    state.sel = s;
    if (state.quotes.length) renderWatch(state.quotes);
    setSide(state.side);
    if (window.matchMedia("(max-width:860px)").matches) setSection("chart");
    refresh();
  }
  function setSection(sec) { state.section = sec; $("#terminal").dataset.sec = sec; $$("#mtabs button").forEach((b) => b.classList.toggle("on", b.dataset.sec === sec)); }
  function setRange(ms) { state.range = ms; $$("#chartRanges button").forEach((b) => b.classList.toggle("on", Number(b.dataset.range) === ms)); drawChart(); }

  /* ----------------------------------------------------------- polling */
  let pollN = 0;
  async function refresh() {
    if (!state.sel) { try { renderWatch(await api.symbols()); } catch (_) {} if (!state.sel) return; }
    try {
      const [syms, book, tape, mine] = await Promise.all([
        api.symbols(), api.book(state.sel, 5), api.trades(state.sel, 22), api.myorders(),
      ]);
      store.recordPrices(syms);
      store.seedSeries(state.sel, tape.map((t) => t.price).reverse());  // fill the chart instantly on first load
      renderWatch(syms); renderBook(book); renderTape(tape); renderOpen(mine);
      $("#tapeSym").textContent = state.sel;
      drawChart(); ui.setConn(true);
      if (pollN++ % 6 === 0) refreshPnl();
    } catch (_) { ui.setConn(false); }
  }
  async function refreshPnl() {
    try { const rep = await api.report(); const el = $("#pnlChip"); el.textContent = fmt.signedMoney(rep.totalPnl); el.className = "val mono " + fmt.signClass(rep.totalPnl); } catch (_) {}
  }

  /* --------------------------------------------------------- keyboard */
  function keys(e) {
    const tag = e.target.tagName;
    const typing = tag === "INPUT" || tag === "SELECT";
    if (typing) { if (e.key === "Enter" && (e.target.id === "oPrice" || e.target.id === "oQty")) { e.preventDefault(); placeOrder(); } return; }
    const k = e.key.toLowerCase();
    if (k === "b") setSide("BUY");
    else if (k === "s") setSide("SELL");
    else if (k === "m") { $("#oType").value = "MKT"; onType(); }
    else if (k === "l") { $("#oType").value = "GTC"; onType(); }
    else if (e.key === "Enter") placeOrder();
    else if (e.key === "/") { e.preventDefault(); $("#wlSearch").focus(); }
    else if (e.key === "+" || e.key === "=") bump(100);
    else if (e.key === "-") bump(-100);
  }

  /* ------------------------------------------------------------ init */
  function init() {
    $("#watchlist").addEventListener("click", (e) => { const r = e.target.closest("[data-sym]"); if (r) selectSym(r.dataset.sym); });
    document.addEventListener("click", (e) => {
      const b = e.target.closest(".half[data-px]");
      if (b) { setSide(b.dataset.side); $("#oType").value = "GTC"; onType(); $("#oPrice").value = (Number(b.dataset.px) / 100).toFixed(2); updateEst();
        if (window.matchMedia("(max-width:860px)").matches) setSection("trade"); return; }
      const cx = e.target.closest("[data-cancel]"); if (cx) cancelOrder(Number(cx.dataset.cancel));
    });
    $("#wlSearch").addEventListener("input", (e) => { state.filter = e.target.value; if (state.quotes.length) renderWatch(state.quotes); });
    $("#sideBuy").addEventListener("click", () => setSide("BUY"));
    $("#sideSell").addEventListener("click", () => setSide("SELL"));
    $("#oType").addEventListener("change", onType);
    $("#oPrice").addEventListener("input", updateEst);
    $("#oQty").addEventListener("input", updateEst);
    $("#qtyMinus").addEventListener("click", () => bump(-100));
    $("#qtyPlus").addEventListener("click", () => bump(100));
    $("#hMax").addEventListener("click", () => { $("#oQty").value = (state.holdings[state.sel] || {}).sellable || 0; updateEst(); });
    $("#placeBtn").addEventListener("click", placeOrder);
    $$("#mtabs button").forEach((b) => b.addEventListener("click", () => setSection(b.dataset.sec)));
    $$("#chartRanges button").forEach((b) => b.addEventListener("click", () => setRange(Number(b.dataset.range))));
    window.addEventListener("resize", drawChart);
    document.addEventListener("keydown", keys);

    setSide("BUY"); onType(); setSection("chart");
    refresh(); refreshPnl();
    setInterval(refresh, REFRESH_MS);
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})(window.LOB = window.LOB || {});
