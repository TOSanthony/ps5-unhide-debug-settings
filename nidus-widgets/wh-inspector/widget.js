// wh-inspector v20: abandons the undocumented VshNMPsnGameIntentModule.LaunchApp
// call (it just hung forever with no effect, likely missing some backend-only
// piece). Tries a different, user-supplied, publicly documented PS5 URI scheme
// instead: "psgm:play?id=<titleId>" ("Launch an application with the given id"),
// by assigning it to location.href -- the normal way an embedded webview triggers
// a custom URL scheme handler without necessarily tearing down the whole page.
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
  try { already = root.localStorage && root.localStorage.getItem('wh-inspector-v20-done') === '1'; } catch (e) {}
  if (already) { return; }
  try { if (root.localStorage) root.localStorage.setItem('wh-inspector-v20-done', '1'); } catch (e) {}

  try {
    if (!wh) { queue('abort: wh missing'); return; }

    var target = 'psgm:play?id=PPSA99177';
    sendNow('about to navigate to ' + target);

    try {
      if (root.location) {
        root.location.href = target;
        sendNow('set location.href, no synchronous exception');
      } else {
        sendNow('no location object on globalThis');
      }
    } catch (e) {
      sendNow('location.href EXCEPTION: ' + (e && e.message ? e.message : e));
    }

    // A short delay before this last marker: if the page/context is torn down by a
    // real navigation, we will never see it, which itself is informative.
    root.setTimeout(function () {
      sendNow('still alive ' + 1500 + 'ms after the psgm: navigation attempt');
    }, 1500);

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v20 probe complete');
  } catch (e) {
    sendNow('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
