// wh-inspector v16: still read-only for everything except the two explicit probe
// calls below. v15 found VshNMPsnGameIntentModule exposed to Welcome Hub widgets,
// with exactly: LaunchApp, LaunchAppForLegacySession, GetPermittedIntentTypes,
// getConstants. This version does NOT call LaunchApp/LaunchAppForLegacySession --
// those are the ones that would actually try to launch something, and we don't yet
// know what argument shape they expect. It only calls the two side-effect-free
// query methods (getConstants/GetPermittedIntentTypes), which by Sony's own naming
// and by RN NativeModule convention (getConstants) just return data, to learn the
// intent schema before risking a real launch call.
// Logged to dashboard.elf's own HTTP server (port 8096), which appends to
// /data/wh-inspector.log.
(function (wh) {
  var root = globalThis;
  var q = [];

  function queue(msg) { q.push(String(msg)); }

  function safeStr(v) {
    try {
      if (v === undefined) return 'undefined';
      if (v === null) return 'null';
      if (typeof v === 'function') return '<function>';
      return JSON.stringify(v);
    } catch (e) { return 'unstringifiable(' + typeof v + '): ' + String(v); }
  }

  // The message goes in the POST body, not the URL: NiDUS's internal HTTP client
  // truncates the request LINE (method+path+version) at ~256 bytes. A body has no
  // such limit. The widget id stays in the query string (it's tiny).
  function send(msg) {
    try {
      var url = 'http://127.0.0.1:8096/__log?w=77682d696e73706563746f72';
      var text = String(msg).slice(0, 1200);
      if (root.fetch) root.fetch(url, { method: 'POST', body: text }).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('POST', url, true); x.send(text); }
    } catch (e) {}
  }

  var pumpN = 0;
  var pump = root.setInterval(function () {
    if (!q.length) { if (pumpN > 200) root.clearInterval(pump); pumpN++; return; }
    send(q.shift());
  }, 700);

  // Avoids re-running (and re-flooding the log) every time the Hub's JS session
  // restarts, which it does every few seconds -- best effort, falls through to
  // running every time if localStorage isn't available in this webview.
  var already = false;
  try { already = root.localStorage && root.localStorage.getItem('wh-inspector-v16-done') === '1'; } catch (e) {}
  if (already) { return; }
  try { if (root.localStorage) root.localStorage.setItem('wh-inspector-v16-done', '1'); } catch (e) {}

  try {
    if (!wh) { queue('abort: wh missing'); return; }

    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) { queue('base_dll missing'); return; }

    var ids = Object.getOwnPropertyNames(base.c), nm = null;
    for (var i = 0; i < ids.length && !nm; i++) {
      var ex; try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
      if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
    }
    if (!nm) { queue('NativeModules object not found in any base_dll export'); return; }

    var gi = nm['VshNMPsnGameIntentModule'];
    if (!gi) { queue('VshNMPsnGameIntentModule not present this run'); return; }
    queue('VshNMPsnGameIntentModule found, methods: ' + safeStr(Object.keys(gi)));

    ['getConstants', 'GetPermittedIntentTypes'].forEach(function (method) {
      try {
        if (typeof gi[method] !== 'function') { queue(method + ': not a function'); return; }
        var r = gi[method]();
        if (r && typeof r.then === 'function') {
          queue(method + ': returned a promise, awaiting...');
          r.then(
            function (ok) { queue(method + ' resolved: ' + safeStr(ok)); },
            function (err) { queue(method + ' rejected: ' + safeStr(err)); }
          );
        } else {
          queue(method + ' -> ' + safeStr(r));
        }
      } catch (e) {
        queue(method + ' EXCEPTION: ' + (e && e.message ? e.message : e));
      }
    });

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v16 probe complete');
  } catch (e) {
    queue('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
