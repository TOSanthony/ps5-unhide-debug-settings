// wh-inspector v21: v20 found there's no browser-style location object in this JS
// environment (it's a React-Native-like bridge, not a real webview DOM) -- but the
// full NativeModules dump turned up VshNMApplicationLinkingPSModule, with exactly
// the standard RN "Linking" module shape: GetInitialURL, CanOpenURL, OpenURL,
// OpenURLArg, OpenTask, StartEventListener. This is almost certainly the correct,
// intended way to trigger a custom URL scheme like "psgm:play?id=<titleId>" from
// this bridge. v21 calls CanOpenURL first (pure read-only check), and only calls
// OpenURL if that reports true.
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

  function sendNow(msg) {
    try {
      var url = 'http://127.0.0.1:8096/__log?w=77682d696e73706563746f72';
      var text = String(msg).slice(0, 1200);
      if (root.fetch) root.fetch(url, { method: 'POST', body: text }).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('POST', url, true); x.send(text); }
    } catch (e) {}
  }

  function send(msg) { sendNow(msg); }

  var pumpN = 0;
  var pump = root.setInterval(function () {
    if (!q.length) { if (pumpN > 200) root.clearInterval(pump); pumpN++; return; }
    send(q.shift());
  }, 700);

  var already = false;
  try { already = root.localStorage && root.localStorage.getItem('wh-inspector-v21-done') === '1'; } catch (e) {}
  if (already) { return; }
  try { if (root.localStorage) root.localStorage.setItem('wh-inspector-v21-done', '1'); } catch (e) {}

  function callMaybePromise(label, fn) {
    try {
      var r = fn();
      if (r && typeof r.then === 'function') {
        sendNow(label + ': returned a promise, awaiting...');
        return r.then(
          function (ok) { sendNow(label + ' resolved: ' + safeStr(ok)); return ok; },
          function (err) { sendNow(label + ' rejected: ' + safeStr(err)); throw err; }
        );
      }
      sendNow(label + ' -> ' + safeStr(r));
      return Promise.resolve(r);
    } catch (e) {
      sendNow(label + ' EXCEPTION: ' + (e && e.message ? e.message : e));
      return Promise.reject(e);
    }
  }

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

    var link = nm['VshNMApplicationLinkingPSModule'];
    if (!link) { queue('VshNMApplicationLinkingPSModule not present this run'); return; }

    var target = 'psgm:play?id=PPSA99177';
    callMaybePromise('CanOpenURL(' + target + ')', function () { return link.CanOpenURL(target); })
      .then(function (can) {
        if (can) {
          return callMaybePromise('OpenURL(' + target + ')', function () { return link.OpenURL(target); });
        }
        sendNow('CanOpenURL said false/falsy -- not calling OpenURL');
      })
      .catch(function () { /* already logged above */ });

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v21 probe complete');
  } catch (e) {
    sendNow('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
