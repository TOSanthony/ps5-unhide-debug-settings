// wh-inspector v19: v18 confirmed GetPermittedIntentTypes({titleId:"PPSA99177"})
// resolves to {"intentTypes":["launchActivity"]} -- Orbit Store is permitted to
// receive a launchActivity intent, matching its own param.json declaration. This is
// the first version that actually calls LaunchApp, with the one intent type we now
// know is permitted. Still a single, narrowly-scoped call -- not LaunchApp without
// first having confirmed permission for this exact titleId/intentType pair.
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
  try { already = root.localStorage && root.localStorage.getItem('wh-inspector-v19-done') === '1'; } catch (e) {}
  if (already) { return; }
  try { if (root.localStorage) root.localStorage.setItem('wh-inspector-v19-done', '1'); } catch (e) {}

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

    var a = { titleId: 'PPSA99177', intentType: 'launchActivity' };
    sendNow('about to call LaunchApp(' + safeStr(a) + ')');
    try {
      var r = gi.LaunchApp(a);
      if (r && typeof r.then === 'function') {
        sendNow('LaunchApp(' + safeStr(a) + '): returned a promise, awaiting...');
        r.then(
          function (ok) { sendNow('LaunchApp resolved: ' + safeStr(ok)); },
          function (err) { sendNow('LaunchApp rejected: ' + safeStr(err)); }
        );
      } else {
        sendNow('LaunchApp(' + safeStr(a) + ') -> ' + safeStr(r));
      }
    } catch (e) {
      sendNow('LaunchApp(' + safeStr(a) + ') EXCEPTION: ' + (e && e.message ? e.message : e));
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v19 probe complete');
  } catch (e) {
    sendNow('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
