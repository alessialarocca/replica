// Content script: bridge between the webapp page and the extension background.
// The page cannot reliably reach the service worker via externally_connectable
// (sleep/wake race conditions), so we relay through this content script, which
// uses internal chrome.runtime messaging — this reliably wakes the SW.

(function () {
  // 1. expose the extension id (kept for backwards-compat / display)
  try {
    const el = document.createElement('div');
    el.id = 'replica-ext-id';
    el.dataset.id = chrome.runtime.id;
    el.style.display = 'none';
    (document.documentElement || document.body).appendChild(el);
    console.log('[Replica] bridge ready, ext-id:', chrome.runtime.id);
  } catch (e) {
    console.error('[Replica] inject-id failed:', e);
  }

  // 2. relay messages: page → background → page
  window.addEventListener('message', (event) => {
    if (event.source !== window) return;
    const data = event.data;
    if (!data || data.__replica !== 'req') return;

    chrome.runtime.sendMessage(data.payload, (response) => {
      const err = chrome.runtime.lastError;
      window.postMessage({
        __replica: 'res',
        reqId: data.reqId,
        response: err ? null : response,
        error: err ? err.message : null,
      }, '*');
    });
  });
})();
