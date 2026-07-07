/* orders.js — the Orders & Holdings page (orders.html).
   Holdings and P&L come STRAIGHT FROM THE C++ ENGINE (/api/report) so they are
   always the server's truth — the page can never show P&L for shares the engine
   doesn't think you own. The order-history table below is a local log of the
   orders you placed from this browser (handy, but not the source of P&L).        */
(function (LOB) {
  "use strict";
  const { store, fmt, api } = LOB, ui = LOB.ui, $ = ui.$, $$ = ui.$$;

  const state = { side: "ALL", q: "", rep: null };
  const pnlClass = (c) => (c > 0 ? "up" : c < 0 ? "down" : "flat");
  const heroClass = (c) => (c > 0 ? "up" : c < 0 ? "down" : "dim");
  const money = (c) => fmt.signedMoney(c);

  async function refreshReport() {
    try { state.rep = await api.report(); ui.setConn(true); }
    catch (_) { ui.setConn(false); }
  }

  function posCell(qty) {
    if (qty > 0) return `<span class="pos long">▲ ${fmt.int(qty)}</span>`;
    if (qty < 0) return `<span class="pos short">▼ ${fmt.int(-qty)}</span>`;
    return `<span class="pos flat">flat</span>`;
  }

  function filteredOrders() {
    let rows = store.all().slice().reverse();
    if (state.side !== "ALL") rows = rows.filter((r) => r.side === state.side);
    if (state.q) { const q = state.q.toUpperCase(); rows = rows.filter((r) => (r.symbol || "").includes(q)); }
    return rows;
  }

  function render() {
    const rep = state.rep || { totalPnl: 0, totalRealized: 0, totalUnrealized: 0, perSymbol: [] };
    const syms = rep.perSymbol || [];
    const open = syms.filter((s) => s.position !== 0).length;

    // headline P&L (server truth)
    $("#pnlHero").innerHTML = `
      <div class="cell main"><div class="k">Total P&L</div><div class="v ${heroClass(rep.totalPnl)}">${money(rep.totalPnl)}</div>
        <div class="foot">realised + unrealised, from the engine</div></div>
      <div class="cell"><div class="k">Realised</div><div class="v ${heroClass(rep.totalRealized)}">${money(rep.totalRealized)}</div>
        <div class="foot">booked by selling</div></div>
      <div class="cell"><div class="k">Unrealised</div><div class="v ${heroClass(rep.totalUnrealized)}">${money(rep.totalUnrealized)}</div>
        <div class="foot">open positions</div></div>
      <div class="cell"><div class="k">Open positions</div><div class="v dim">${fmt.int(open)}</div>
        <div class="foot">still held</div></div>`;

    // holdings (server truth)
    $("#holdings").innerHTML = syms.length ? syms.map((s) => `
      <tr>
        <td style="text-align:left"><b>${s.symbol}</b></td>
        <td>${posCell(s.position)}</td>
        <td class="mono">${s.position ? "$" + fmt.price(s.avgCost) : "—"}</td>
        <td class="mono">${s.mark ? "$" + fmt.price(s.mark) : "—"}</td>
        <td class="mono">${fmt.int(s.bought)}</td>
        <td class="mono">${fmt.int(s.sold)}</td>
        <td class="pnl ${pnlClass(s.realized)}">${money(s.realized)}</td>
        <td class="pnl ${pnlClass(s.unrealized)}">${s.position ? money(s.unrealized) : `<span class="tag-flat">closed</span>`}</td>
        <td class="pnl ${pnlClass(s.pnl)}">${money(s.pnl)}</td>
      </tr>`).join("")
      : `<tr class="t-empty"><td colspan="9">No holdings yet — buy a stock on the Trade screen, then sell it to book a profit.</td></tr>`;

    // order history (local log of what you placed here)
    const rows = filteredOrders();
    $("#orders").innerHTML = rows.length ? rows.map((r) => {
      const isCancel = r.kind === "cancel";
      return `<tr>
        <td class="tape-t mono">${fmt.clock(r.t)}</td>
        <td style="text-align:left">${r.symbol || "—"}</td>
        <td style="text-align:left">${r.side ? `<span class="badge ${r.side === "BUY" ? "buy" : "sell"}">${r.side}</span>` : "—"}</td>
        <td style="text-align:left">${isCancel ? "CANCEL" : r.type}</td>
        <td class="mono">${r.reqPrice ? "$" + fmt.price(r.reqPrice) : (isCancel ? "—" : "MKT")}</td>
        <td class="mono">${fmt.int(r.reqQty)}</td>
        <td class="mono">${r.filledQty ? fmt.int(r.filledQty) : "—"}</td>
        <td class="mono">${r.avgPx ? "$" + fmt.price(r.avgPx) : "—"}</td>
        <td class="mono">${r.notional ? "$" + fmt.price(r.notional) : "—"}</td>
        <td class="mono dim">${r.latencyMs ? fmt.ms(r.latencyMs) : "—"}</td>
        <td style="text-align:left"><span class="badge ${r.status}">${r.status}</span></td>
      </tr>`;
    }).join("") : `<tr class="t-empty"><td colspan="11">No orders yet — place one on the Trade screen.</td></tr>`;
  }

  function exportCsv() {
    const rows = filteredOrders();
    const head = ["time", "symbol", "side", "type", "order_px_cents", "req_qty", "filled_qty", "avg_fill_cents", "value_cents", "rtt_ms", "status"];
    const lines = [head.join(",")];
    for (const r of rows) lines.push([fmt.stamp(r.t), r.symbol, r.side, r.kind === "cancel" ? "CANCEL" : r.type,
      r.reqPrice, r.reqQty, r.filledQty, r.avgPx, r.notional, (r.latencyMs || 0).toFixed(1), r.status].map(fmt.csv).join(","));
    const blob = new Blob([lines.join("\n")], { type: "text/csv" });
    const a = document.createElement("a"); a.href = URL.createObjectURL(blob); a.download = "orders.csv";
    document.body.appendChild(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 2000);
  }

  async function clearAll() {
    const ok = await ui.confirm({ title: "Clear order history?", danger: true, confirmText: "Clear log",
      body: "Erases the local order log shown below. Your live holdings & P&L (from the engine) are unaffected." });
    if (!ok) return;
    store.clear(); render();
    ui.toast({ type: "ok", title: "Order log cleared" });
  }

  async function init() {
    $("#fSide").addEventListener("change", (e) => { state.side = e.target.value; render(); });
    $("#fSearch").addEventListener("input", (e) => { state.q = e.target.value; render(); });
    $("#btnCsv").addEventListener("click", exportCsv);
    $("#btnClear").addEventListener("click", clearAll);
    store.onChange(render);
    render();                      // paint the $0 hero + empty tables immediately
    await refreshReport(); render();
    setInterval(async () => { await refreshReport(); render(); }, 2000);
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})(window.LOB = window.LOB || {});
