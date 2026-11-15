// wh-inspector v5: v4 found promising native modules — VshNMPsnAccountModule.IsSignedIn /
// .GetState, VshNMLoginMgr.GetSigninState / .GetAccountType, VshNMPsnShellUISigninUtility.Open.
// v5 only READS their current result (no patching, no side-effecting calls like
// StartEventListener/Cancel/Signout/Open) to see what they currently report.
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
  send('=== v5 run #' + runs + ' ===');

  try {
    if (!wh) { send('abort: wh missing'); return; }
    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) { send('base_dll missing'); return; }
    var ids = Object.getOwnPropertyNames(base.c), nm = null;
    for (var i = 0; i < ids.length && !nm; i++) {
      var ex; try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
      if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
    }
    if (!nm) { send('NativeModules not found'); return; }

    // Only read-only, no-arg calls. Never: Cancel, Signout, Open, StartEventListener (side
    // effects or leaves a dangling state).
    var SAFE = [
      ['VshNMPsnAccountModule', 'IsSignedIn'],
      ['VshNMPsnAccountModule', 'GetState'],
      ['VshNMLoginMgr', 'GetSigninState'],
      ['VshNMLoginMgr', 'GetAccountType'],
      ['VshNMLoginMgr', 'GetInternalUserStatus'],
      ['VshNMUserModule', 'GetForegroundUserId'],
      ['VshNMUserModule', 'GetLoginUserIdList'],
    ];

    function report(label, v) {
      var s;
      try { s = JSON.stringify(v); } catch (e) { s = String(v); }
      send('RESULT ' + label + ' = ' + String(s).slice(0, 260));
    }

    SAFE.forEach(function (pair) {
      var modName = pair[0], fnName = pair[1], label = modName + '.' + fnName;
      try {
        var mod = nm[modName];
        if (!mod) { send(label + ': module not present'); return; }
        var fn = mod[fnName];
        if (typeof fn !== 'function') { send(label + ': not a function (' + typeof fn + ')'); return; }
        var r;
        try {
          r = fn.call(mod);
        } catch (e) {
          send(label + ': sync call threw ' + (e && e.message ? e.message : e));
          return;
        }
        if (r && typeof r.then === 'function') {
          r.then(function (v) { report(label + ' (promise resolved)', v); },
                 function (e) { send(label + ' (promise rejected): ' + (e && e.message ? e.message : e)); });
        } else {
          report(label + ' (sync)', r);
        }
      } catch (e) {
        send(label + ': outer error ' + (e && e.message ? e.message : e));
      }
    });

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    send('v5 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
