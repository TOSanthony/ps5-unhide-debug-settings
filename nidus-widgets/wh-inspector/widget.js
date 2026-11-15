// wh-inspector v13: v12 proved GetState() actually answers fine and fast with the REAL
// value {"state":"signedOut"} -- not a hang. So the Hub is correctly reporting that this
// console has no active PSN session; it isn't broken. v13 goes further and FORCES a fake
// local answer regardless of the real one: IsSignedIn() -> true, GetState() -> {"state":
// "signedIn"}. This stays 100% local (no contact with Sony's servers, no bypass of any
// server-side check) -- it only changes what THIS screen's own UI gate believes. Expect
// partial results: cards that need a real authenticated network call (friends, online
// trophies, activity) will likely still fail or hang, since this patch can't fabricate
// an actual PSN session server-side.
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
  send('=== v13 run #' + runs + ' ===');

  function forceFake(origFn, thisArg, fakeValue, label) {
    return function () {
      // call the real one anyway, purely so we can log what it would have said,
      // but never let its result reach the caller -- always resolve with fakeValue.
      try {
        var r = origFn.apply(thisArg, arguments);
        if (r && typeof r.then === 'function') {
          r.then(function (v) { send(label + ': real would have said ' + JSON.stringify(v) + ' (ignored)'); },
                 function (e) { send(label + ': real would have rejected ' + (e && e.message ? e.message : e) + ' (ignored)'); });
        }
      } catch (e) {
        send(label + ': real threw sync ' + (e && e.message ? e.message : e) + ' (ignored)');
      }
      send(label + ': FORCED fake = ' + JSON.stringify(fakeValue));
      return Promise.resolve(fakeValue);
    };
  }

  try {
    if (!wh) { send('abort: wh missing'); return; }
    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) { send('base_dll missing'); return; }

    if (root.__wh_insp13_patched) {
      send('already force-patched (persisted), run #' + runs);
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
          acc.IsSignedIn = forceFake(acc.IsSignedIn, acc, true, 'IsSignedIn');
          patched++;
        }
        if (typeof acc.GetState === 'function') {
          acc.GetState = forceFake(acc.GetState, acc, { state: 'signedIn' }, 'GetState');
          patched++;
        }
      } else {
        send('VshNMPsnAccountModule not present on nm');
      }

      root.__wh_insp13_patched = true;
      send('force-patched ' + patched + ' PSN account function(s)');
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    send('v13 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
