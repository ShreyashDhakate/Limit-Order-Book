/* format.js — tiny formatting helpers shared by every page.
   Prices from the API are integer CENTS everywhere; convert only for display. */
(function (LOB) {
  "use strict";

  const nf2 = new Intl.NumberFormat(undefined, { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const nf0 = new Intl.NumberFormat(undefined);

  const fmt = {
    /** cents -> "1,234.56" (no currency symbol) */
    price(cents) { return nf2.format((Number(cents) || 0) / 100); },
    /** cents -> "$1,234.56" */
    money(cents) {
      const c = Number(cents) || 0;
      return (c < 0 ? "-$" : "$") + nf2.format(Math.abs(c) / 100);
    },
    /** integer with thousands separators */
    int(n) { return nf0.format(Math.round(Number(n) || 0)); },
    /** signed percentage, already-a-percent input, e.g. 1.2 -> "+1.20%" */
    pct(x) { const n = Number(x) || 0; return (n > 0 ? "+" : "") + n.toFixed(2) + "%"; },
    /** signed money from cents, e.g. -430 -> "-$4.30" */
    signedMoney(cents) { const c = Number(cents) || 0; return (c > 0 ? "+" : "") + fmt.money(c); },
    signed(n) { const x = Number(n) || 0; return (x > 0 ? "+" : "") + nf0.format(x); },
    /** ms latency -> "12.3 ms" */
    ms(x) { const n = Number(x) || 0; return (n >= 100 ? n.toFixed(0) : n.toFixed(1)) + " ms"; },

    signClass(n) { const x = Number(n) || 0; return x > 0 ? "up" : x < 0 ? "down" : "dim"; },
    arrow(side) { return side === "BUY" ? "▲" : "▼"; },

    /** epoch ms -> "14:02:37" */
    clock(ms) { return new Date(ms).toLocaleTimeString(undefined, { hour12: false }); },
    /** epoch ms -> "14:02" */
    hhmm(ms) { const d = new Date(ms); return d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit", hour12: false }); },
    /** epoch ms -> "2026-07-01 14:02:37" */
    stamp(ms) {
      const d = new Date(ms), p = (x) => String(x).padStart(2, "0");
      return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
    },
    /** duration ms -> "01:23:45" */
    dur(ms) {
      let s = Math.max(0, Math.floor(ms / 1000));
      const h = Math.floor(s / 3600); s -= h * 3600;
      const m = Math.floor(s / 60); s -= m * 60;
      const p = (x) => String(x).padStart(2, "0");
      return `${p(h)}:${p(m)}:${p(s)}`;
    },

    escapeHtml(s) {
      return String(s == null ? "" : s).replace(/[&<>"']/g, (c) =>
        ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
    },
    /** wrap a CSV cell if it contains separators/quotes */
    csv(v) {
      const s = String(v == null ? "" : v);
      return /[",\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
    },
  };

  LOB.fmt = fmt;
})(window.LOB = window.LOB || {});
