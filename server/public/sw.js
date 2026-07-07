/* sw.js — legacy service worker, now self-retiring.
   The rebuilt app does NOT use a service worker. If an old one is still
   registered from a previous version, this unregisters it and deletes every
   cache so no stale HTML/CSS/JS can ever be served. It then reloads open tabs. */
self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", (e) => {
  e.waitUntil((async () => {
    const keys = await caches.keys();
    await Promise.all(keys.map((k) => caches.delete(k)));
    try { await self.registration.unregister(); } catch (_) {}
    const clients = await self.clients.matchAll({ type: "window" });
    clients.forEach((c) => { try { c.navigate(c.url); } catch (_) {} });
  })());
});
self.addEventListener("fetch", () => {}); // network passthrough
