/* store.js — client-side persistence for a single-user paper-trading account.
   The C++ engine is the source of truth for holdings & the live book; this just
   keeps a durable log of the orders YOU placed (so the Orders page can show full
   detail) plus a short rolling price series for the chart. All in localStorage,
   no build step. Prices are integer CENTS everywhere.                          */
(function (LOB) {
  "use strict";

  const K_ORDERS = "lob.orders.v2";     // flat list of your orders/cancels (oldest first)
  const K_PRICES = "lob.px.v2";         // { SYM: [[epochMs, cents], ...] }
  const MAX_ORDERS   = 2000;
  const PX_WINDOW_MS = 20 * 60 * 1000;  // keep ~20 min of ticks (light on storage)
  const PX_MIN_GAP   = 1500;            // don't store denser than this
  const PX_MAX        = 900;            // hard cap on points per symbol
  const PX_FLUSH_MS  = 6000;            // throttle localStorage writes

  const listeners = new Set();
  const notify = () => listeners.forEach((fn) => { try { fn(); } catch (_) {} });

  const mem = {};
  function read(key, def) {
    try { const v = localStorage.getItem(key); return v == null ? def : JSON.parse(v); }
    catch (_) { return key in mem ? mem[key] : def; }
  }
  function write(key, val) {
    try { localStorage.setItem(key, JSON.stringify(val)); }
    catch (_) { mem[key] = val; }
  }
  const now = () => Date.now();

  // ---- order log ------------------------------------------------------------
  function all() { return read(K_ORDERS, []); }
  function push(evt) {
    const list = read(K_ORDERS, []);
    list.push(evt);
    write(K_ORDERS, list.slice(-MAX_ORDERS));
    notify();
  }

  /** record a placed order together with the engine's fills + round-trip latency. */
  function logOrder(o) {
    const fills = o.fills || [];
    let filledQty = 0, notional = 0;
    for (const f of fills) { filledQty += Number(f.qty) || 0; notional += (Number(f.qty) || 0) * (Number(f.price) || 0); }
    const restingQty = Number(o.restingQty) || 0;
    let status;
    if (!o.accepted) status = "rejected";
    else if (filledQty > 0 && restingQty > 0) status = "partial";
    else if (filledQty > 0) status = "filled";
    else if (restingQty > 0) status = "open";
    else status = "unfilled";
    push({
      t: now(), kind: "order",
      symbol: o.symbol, side: o.side, type: o.type,
      reqQty: Number(o.qty) || 0, reqPrice: Number(o.price) || 0,
      orderId: Number(o.orderId) || 0,
      accepted: !!o.accepted, message: o.message || "",
      filledQty, restingQty, notional,
      avgPx: filledQty ? Math.round(notional / filledQty) : 0,
      latencyMs: o.latencyMs || 0, status,
    });
  }

  function logCancel(c) {
    push({
      t: now(), kind: "cancel",
      symbol: c.symbol || "", side: c.side || "", type: "",
      reqQty: Number(c.qty) || 0, reqPrice: Number(c.price) || 0,
      orderId: Number(c.orderId) || 0,
      accepted: !!c.ok, message: c.ok ? "cancelled" : "not found",
      filledQty: 0, restingQty: 0, notional: 0, avgPx: 0,
      latencyMs: c.latencyMs || 0, status: c.ok ? "cancelled" : "rejected",
    });
  }

  function clear() { write(K_ORDERS, []); notify(); }

  // ---- rolling price series (for the chart) ---------------------------------
  // NOTE: holdings & P&L are NOT derived here — they come straight from the C++
  // engine via /api/report, so the number you see is always the server's truth
  // (it can never show P&L for shares the engine doesn't think you hold).
  let pxCache = null, pxLastWrite = 0;
  function pxAll() { return pxCache || (pxCache = read(K_PRICES, {})); }
  function recordPrices(quotes, t) {
    t = t || now();
    const allpx = pxAll();
    for (const q of quotes || []) {
      const px = Number(q.last) || 0; if (!px) continue;
      const arr = allpx[q.symbol] || (allpx[q.symbol] = []);
      const last = arr[arr.length - 1];
      if (last && t - last[0] < PX_MIN_GAP) last[1] = px;
      else arr.push([t, px]);
      const cutoff = t - PX_WINDOW_MS;
      let drop = 0; while (drop < arr.length && arr[drop][0] < cutoff) drop++;
      if (drop) arr.splice(0, drop);
      if (arr.length > PX_MAX) arr.splice(0, arr.length - PX_MAX);
    }
    if (t - pxLastWrite > PX_FLUSH_MS) { write(K_PRICES, allpx); pxLastWrite = t; }
  }
  function series(symbol, windowMs) {
    const arr = pxAll()[symbol] || [];
    if (!windowMs) return arr.slice();
    const since = now() - windowMs;
    return arr.filter((p) => p[0] >= since);
  }
  /** prime a symbol's series from recent trade prices so the chart isn't empty
      on first load (oldest first). No-op once we already have live points. */
  function seedSeries(symbol, prices, t) {
    if (!prices || prices.length < 2) return;
    const allpx = pxAll();
    if ((allpx[symbol] || []).length >= 2) return;
    t = t || now();
    const n = prices.length;
    allpx[symbol] = prices.map((p, i) => [t - (n - 1 - i) * 2000, Number(p) || 0]).filter((p) => p[1]);
    write(K_PRICES, allpx);
  }

  LOB.store = {
    onChange(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    all, logOrder, logCancel, clear, recordPrices, series, seedSeries,
    PX_WINDOW_MS,
  };
})(window.LOB = window.LOB || {});
