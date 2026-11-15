// hub-signin-patch (quiet production version of wh-inspector's v14 experiment):
// this console's PSN session cache can go stale after a backup restore, which leaves
// VshNMPsnAccountModule.IsSignedIn/GetState genuinely reporting "not signed in", which
// is what keeps the Welcome Hub stuck on its "Se connecter" screen. This is a cosmetic,
// 100% local client-side patch: it forces those two calls to report signedIn so the Hub
// renders past that screen. It does NOT create a real PSN session -- anything that needs
// a real authenticated network call (friends list, online trophy sync, etc.) may still
// fail or stay empty. Deployed/removed by dashboard.elf's "Welcome Hub" toggle.
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
      var url = 'http://127.0.0.1:8098/__log?w=6875622d7369676e696e2d7061746368&h=' + hexOf(String(msg).slice(0, 320));
      if (root.fetch) root.fetch(url).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('GET', url, true); x.send(); }
    } catch (e) {}
  }

  function forceFakeQuiet(origFn, thisArg, fakeValue) {
    return function () {
      try { origFn.apply(thisArg, arguments); } catch (e) {}
      return Promise.resolve(fakeValue);
    };
  }

  try {
    if (!wh) return;
    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) return;
    if (root.__hub_signin_patched) return;

    var ids = Object.getOwnPropertyNames(base.c), nm = null;
    for (var i = 0; i < ids.length && !nm; i++) {
      var ex;
      try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
      if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
    }
    if (!nm || !nm.VshNMPsnAccountModule) return;

    var acc = nm.VshNMPsnAccountModule;
    if (typeof acc.IsSignedIn === 'function') acc.IsSignedIn = forceFakeQuiet(acc.IsSignedIn, acc, true);
    if (typeof acc.GetState === 'function') acc.GetState = forceFakeQuiet(acc.GetState, acc, { state: 'signedIn' });

    root.__hub_signin_patched = true;
    send('hub-signin-patch: applied');

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
  } catch (e) {
    send('hub-signin-patch EXCEPTION: ' + (e && e.message ? e.message : e));
  }
})(globalThis.__wh);
