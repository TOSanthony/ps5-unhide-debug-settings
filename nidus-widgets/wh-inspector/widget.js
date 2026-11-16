// wh-inspector v17: v16 called VshNMPsnGameIntentModule.getConstants() (returned {},
// no schema) then GetPermittedIntentTypes() with zero arguments -- that second call
// produced no log line at all, not even a caught exception, meaning it most likely
// crashed the Hub's JS session at the native bridge level rather than throwing a
// catchable JS error. v17 retries GetPermittedIntentTypes with a plausible argument
// ({} then [], one at a time) instead of zero args, in case the native glue expects
// an argument object and faults on arguments[0] being undefined. Still does NOT call
// LaunchApp/LaunchAppForLegacySession.
//
// Because a native-level crash can kill the JS context before the normal queued/
// throttled send() pump gets a chance to flush, the before/after markers around each
// risky call are sent immediately (fire-and-forget fetch), not queued, so we have the
// best chance of seeing how far it got even if the session dies mid-call.
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
  try { already = root.localStorage && root.localStorage.getItem('wh-inspector-v17-done') === '1'; } catch (e) {}
  if (already) { return; }
  try { if (root.localStorage) root.localStorage.setItem('wh-inspector-v17-done', '1'); } catch (e) {}

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

    var args = [{}, []];
    for (var j = 0; j < args.length; j++) {
      var a = args[j];
      sendNow('about to call GetPermittedIntentTypes(' + safeStr(a) + ')');
      try {
        var r = gi.GetPermittedIntentTypes(a);
        if (r && typeof r.then === 'function') {
          sendNow('GetPermittedIntentTypes(' + safeStr(a) + '): returned a promise, awaiting...');
          r.then(
            function (ok) { sendNow('GetPermittedIntentTypes resolved: ' + safeStr(ok)); },
            function (err) { sendNow('GetPermittedIntentTypes rejected: ' + safeStr(err)); }
          );
        } else {
          sendNow('GetPermittedIntentTypes(' + safeStr(a) + ') -> ' + safeStr(r));
        }
        // If we got here without crashing, don't also try the next argument shape --
        // we already have a usable answer.
        break;
      } catch (e) {
        sendNow('GetPermittedIntentTypes(' + safeStr(a) + ') EXCEPTION: ' + (e && e.message ? e.message : e));
      }
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v17 probe complete');
  } catch (e) {
    sendNow('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
