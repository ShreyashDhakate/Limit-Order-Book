/* api.js — thin wrappers over the C++ server's REST API (all prices in cents).
   Round-trip latency is timed on writes so the Engine report can show how fast
   the matching engine acknowledges orders. Endpoints (from server.cpp):
     GET  /api/symbols | /api/book | /api/trades | /api/myorders | /api/report
     POST /api/order   | /api/cancel     GET /api/health                       */
(function (LOB) {
  "use strict";

  async function getJSON(path) {
    const r = await fetch(path, { headers: { Accept: "application/json" } });
    if (!r.ok) throw new Error(`HTTP ${r.status} on ${path}`);
    return r.json();
  }

  async function postForm(path, obj) {
    const t0 = performance.now();
    const r = await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams(obj),
    });
    const latencyMs = performance.now() - t0;
    if (!r.ok && r.status !== 400) throw new Error(`HTTP ${r.status} on ${path}`);
    const data = await r.json();
    return { data, latencyMs };
  }

  LOB.api = {
    symbols: () => getJSON("/api/symbols"),
    book: (sym, depth = 12) => getJSON(`/api/book?symbol=${encodeURIComponent(sym)}&depth=${depth}`),
    trades: (sym, limit = 30) => getJSON(`/api/trades?symbol=${encodeURIComponent(sym)}&limit=${limit}`),
    myorders: () => getJSON("/api/myorders"),
    report: () => getJSON("/api/report"),
    health: () => getJSON("/api/health"),

    /** place an order; returns { data, latencyMs }. `price` in cents. */
    order: ({ symbol, side, type, price, qty }) =>
      postForm("/api/order", { symbol, side, type, price, qty }),

    /** cancel a resting order by id; returns { data:{ok}, latencyMs } */
    cancel: (id) => postForm("/api/cancel", { id }),
  };
})(window.LOB = window.LOB || {});
