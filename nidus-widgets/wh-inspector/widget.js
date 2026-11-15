// wh-inspector v12: NEW DIRECTION. Instead of hunting for where the text is rendered,
// patch VshNMPsnAccountModule.IsSignedIn / .GetState directly. These promises NEVER
// resolve or reject on this console (confirmed by v5) -- they just hang forever, which
// is plausibly what drives the Hub's restart loop. v12 wraps each one: call the REAL
// function first; if it hasn't settled within ~1.5s, resolve with a guessed fallback
// value instead (logged clearly as "FALLBACK used", vs "REAL result" if the original
// ever does answer). This does not fake a real PSN session -- it only unblocks whatever
// JS code is waiting on these two specific promises, so the Hub can proceed (or hit a
// different, more informative stall further down).
(function (wh) {
  var root = globalThis;

  function hexOf(s) {
    var hex = '';
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      hex += (c < 16 ? '0' : '') + c.toString(16);
    }
    return hex;
  }
  function send(msg) {
    try {
      var url = 'http://127.0.0.1:8098/__log?w=776828696e73706563746f72&h=' + hexOf(String(msg).slice(0, 320));
      if (root.fetch) root.fetch(url).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('GET', url, true); x.send(); }
    } catch (e) {}
  }

  var runs = (root.__wh_insp_runs || 0) + 1;
  root.__wh_insp_runs = runs;
  send('=== v12 run #' + runs + ' ===');

  function raceWithTimeout(origFn, thisArg, fallbackValue, label, timeoutMs) {
    return function () {
      var args = arguments;
      var settled = false;
      return new Promise(function (resolve) {
        var timer = setTimeout(function () {
          if (settled) return;
          settled = true;
          send(label + ': FALLBACK used = ' + JSON.stringify(fallbackValue));
          resolve(fallbackValue);
        }, timeoutMs);

        var r;
        try {
          r = origFn.apply(thisArg, args);
        } catch (e) {
          if (!settled) {
            settled = true;
            clearTimeout(timer);
            send(label + ': original threw sync, using FALLBACK = ' + JSON.stringify(fallbackValue) + ' (err: ' + (e && e.message ? e.message : e) + ')');
            resolve(fallbackValue);
          }
          return;
        }

        if (r && typeof r.then === 'function') {
          r.then(function (v) {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            send(label + ': REAL result = ' + JSON.stringify(v));
            resolve(v);
          }, function (e) {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            send(label + ': real promise REJECTED (' + (e && e.message ? e.message : e) + '), using FALLBACK = ' + JSON.stringify(fallbackValue));
            resolve(fallbackValue);
          });
        } else {
          // not a promise at all -- pass the real synchronous value straight through
          if (!settled) {
            settled = true;
            clearTimeout(timer);
            send(label + ': REAL sync result = ' + JSON.stringify(r));
            resolve(r);
          }
        }
      });
    };
  }

  try {
    if (!wh) { send('abort: wh missing'); return; }
    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) { send('base_dll missing'); return; }

    if (root.__wh_insp12_patched) {
      send('already patched (persisted), run #' + runs);
    } else {
      var ids = Object.getOwnPropertyNames(base.c), nm = null;
      for (var i = 0; i < ids.length && !nm; i++) {
        var ex;
        try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
      }
      if (!nm) { send('NativeModules not found'); return; }

      var patched = 0;
      if (nm.VshNMPsnAccountModule) {
        var acc = nm.VshNMPsnAccountModule;
        if (typeof acc.IsSignedIn === 'function') {
          acc.IsSignedIn = raceWithTimeout(acc.IsSignedIn, acc, true, 'IsSignedIn', 1500);
          patched++;
        }
        if (typeof acc.GetState === 'function') {
          acc.GetState = raceWithTimeout(acc.GetState, acc, 'SIGNED_IN', 'GetState', 1500);
          patched++;
        }
      } else {
        send('VshNMPsnAccountModule not present on nm');
      }

      root.__wh_insp12_patched = true;
      send('patched ' + patched + ' PSN account function(s), timeout=1500ms');
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    send('v12 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
