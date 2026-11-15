// wh-inspector v14 "quiet": confirmed by the user that forcing IsSignedIn/GetState to
// report signedIn is what actually unblocks the Welcome Hub -- the real PSN session on
// this console is still absent (sceNpManager genuinely has nothing), so this is a
// cosmetic client-side bypass, not a real sign-in. Keeping it installed means it runs
// on every Hub load, so v14 strips all the per-call debug logging from v12/v13 (which
// was flooding NiDUS's local log server during the Hub's retry bursts) and only logs
// once, at patch-install time. Same patch as v13: IsSignedIn() -> true, GetState() ->
// {"state":"signedIn"}, both 100% local, no network/server involved.
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

  function forceFakeQuiet(origFn, thisArg, fakeValue) {
    return function () {
      // still call the real one so any of its own internal side effects/telemetry still
      // happen normally -- we just never let its actual result reach the caller, and we
      // don't log per-call (that's what was flooding the log server before).
      try { origFn.apply(thisArg, arguments); } catch (e) {}
      return Promise.resolve(fakeValue);
    };
  }

  try {
    if (!wh) return;
    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) return;

    if (root.__wh_insp14_patched) return; // already done for this runtime instance

    var ids = Object.getOwnPropertyNames(base.c), nm = null;
    for (var i = 0; i < ids.length && !nm; i++) {
      var ex;
      try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
      if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
    }
    if (!nm || !nm.VshNMPsnAccountModule) { send('wh-inspector v14: NativeModules/VshNMPsnAccountModule not found, no patch applied'); return; }

    var acc = nm.VshNMPsnAccountModule;
    var patched = 0;
    if (typeof acc.IsSignedIn === 'function') { acc.IsSignedIn = forceFakeQuiet(acc.IsSignedIn, acc, true); patched++; }
    if (typeof acc.GetState === 'function') { acc.GetState = forceFakeQuiet(acc.GetState, acc, { state: 'signedIn' }); patched++; }

    root.__wh_insp14_patched = true;
    send('wh-inspector v14: patched ' + patched + ' PSN account function(s) (quiet mode)');

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
  } catch (e) {
    send('wh-inspector v14 EXCEPTION: ' + (e && e.message ? e.message : e));
  }
})(globalThis.__wh);
