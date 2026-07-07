/* ui.js — small shared UI helpers used by every page: DOM shortcuts, an inline
   icon set, toast notifications, a promise-based confirm dialog, the connection
   indicator, active-nav marking, and a one-time cleanup that unregisters the old
   service worker + caches (so stale code from the previous app can't linger).   */
(function (LOB) {
  "use strict";
  const fmt = LOB.fmt;
  const $ = (s, r = document) => r.querySelector(s);
  const $$ = (s, r = document) => Array.from(r.querySelectorAll(s));

  const P = {
    check: '<path d="M20 6L9 17l-5-5"/>',
    alert: '<path d="M12 9v4M12 17h.01"/><path d="M10.3 3.9l-8 14A2 2 0 004 21h16a2 2 0 001.7-3l-8-14a2 2 0 00-3.4 0z"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/>',
    x: '<path d="M18 6L6 18M6 6l12 12"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="M21 21l-4-4"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    minus: '<path d="M5 12h14"/>',
    trash: '<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/>',
    download: '<path d="M12 3v12"/><path d="M7 11l5 5 5-5"/><path d="M4 21h16"/>',
    chart: '<path d="M4 14l4-4 3 3 5-6 4 4"/><path d="M4 20h16"/>',
    list: '<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/>',
  };
  function icon(name, size = 18) {
    return `<svg viewBox="0 0 24 24" width="${size}" height="${size}" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${P[name] || ""}</svg>`;
  }

  /* -------------------------------------------------------------- toasts */
  let wrap;
  function toast({ type = "info", title = "", desc = "", timeout = 3600 } = {}) {
    if (!wrap) { wrap = document.createElement("div"); wrap.className = "toasts"; document.body.appendChild(wrap); }
    const ic = type === "ok" ? "check" : type === "err" ? "alert" : "info";
    const el = document.createElement("div");
    el.className = `toast ${type}`;
    el.innerHTML = `<span class="ic">${icon(ic, 15)}</span><div><div class="title">${fmt.escapeHtml(title)}</div>${desc ? `<div class="desc">${fmt.escapeHtml(desc)}</div>` : ""}</div>`;
    wrap.appendChild(el);
    const kill = () => { el.classList.add("out"); setTimeout(() => el.remove(), 180); };
    const t = setTimeout(kill, timeout);
    el.addEventListener("click", () => { clearTimeout(t); kill(); });
    return el;
  }

  /* ------------------------------------------------------------- confirm */
  function confirm(opts) {
    return new Promise((resolve) => {
      const el = document.createElement("div");
      el.className = "overlay show";
      const lines = (opts.lines || []).map(([k, v]) => `<div class="cline"><span class="dim">${fmt.escapeHtml(k)}</span><b class="mono">${fmt.escapeHtml(v)}</b></div>`).join("");
      el.innerHTML = `<div class="modal"><div class="modal-h"><h2>${fmt.escapeHtml(opts.title || "Confirm")}</h2></div>
        <div class="modal-b">${opts.body ? `<p class="dim" style="margin-bottom:12px">${fmt.escapeHtml(opts.body)}</p>` : ""}${lines}</div>
        <div class="modal-f"><button class="btn ghost" data-a="0">Cancel</button>
        <button class="btn ${opts.danger ? "danger" : "accent"}" data-a="1">${fmt.escapeHtml(opts.confirmText || "Confirm")}</button></div></div>`;
      document.body.appendChild(el);
      const done = (v) => { el.remove(); resolve(v); };
      el.addEventListener("click", (e) => {
        if (e.target === el) return done(false);
        const a = e.target.closest("[data-a]"); if (a) done(a.dataset.a === "1");
      });
      el.querySelector('[data-a="1"]').focus();
    });
  }

  /* ---------------------------------------------------------- connection */
  function setConn(ok) {
    const el = $("#conn"); if (!el) return;
    el.classList.toggle("down", !ok);
    const t = el.querySelector(".txt"); if (t) t.textContent = ok ? "Live" : "Reconnecting…";
  }

  function markActive() {
    const page = document.body.dataset.page;
    $$("[data-nav]").forEach((a) => a.classList.toggle("active", a.dataset.nav === page));
  }

  /* one-time cleanup: remove the previous app's service worker + caches */
  function cleanupSW() {
    try {
      if ("serviceWorker" in navigator)
        navigator.serviceWorker.getRegistrations().then((rs) => rs.forEach((r) => r.unregister())).catch(() => {});
      if (window.caches) caches.keys().then((ks) => ks.forEach((k) => caches.delete(k))).catch(() => {});
    } catch (_) {}
  }

  function init() { markActive(); cleanupSW(); }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();

  LOB.ui = { $, $$, icon, toast, confirm, setConn, markActive };
})(window.LOB = window.LOB || {});
