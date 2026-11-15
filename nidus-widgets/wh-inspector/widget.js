// wh-inspector v7: v6 found generic HOC chains (withPartialFlowScreen(Connect(t)), etc.)
// but never caught the literal "Se connecter" text via createElement text props --
// it's resolved through an i18n/label system (I18nPS/Label/Glyph), not a literal string.
// react-redux's Connect(...) wrapper tells us state is centralized in a Redux store.
// v7 looks for that store (duck-typed: has .getState/.dispatch/.subscribe as functions),
// calls ONLY store.getState() (read-only, no dispatch, nothing patched), and sends back
// just the parts of the state tree whose keys look sign-in/auth/account related.
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
  send('=== v7 run #' + runs + ' ===');

  try {
    if (!wh) { send('abort: wh missing'); return; }
    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) { send('base_dll missing'); return; }

    function looksLikeStore(o) {
      return !!o && typeof o === 'object'
        && typeof o.getState === 'function'
        && typeof o.dispatch === 'function'
        && typeof o.subscribe === 'function';
    }

    function safeKeys(o) {
      try { return Object.keys(o); } catch (e) { return []; }
    }

    var store = null, foundId = null;
    var ids = Object.getOwnPropertyNames(base.c);
    for (var i = 0; i < ids.length && !store; i++) {
      var ex;
      try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
      if (!ex) continue;
      if (looksLikeStore(ex)) { store = ex; foundId = String(ids[i]); break; }
      if (typeof ex === 'object') {
        var keys = safeKeys(ex);
        for (var j = 0; j < keys.length; j++) {
          var k = keys[j], v;
          try { v = ex[k]; } catch (e) { continue; }
          if (looksLikeStore(v)) { store = v; foundId = ids[i] + '.' + k; break; }
        }
      }
      if (store) break;
    }
    if (!store) {
      ['store', '__store', '__REDUX_STORE__'].forEach(function (name) {
        if (!store && looksLikeStore(root[name])) { store = root[name]; foundId = 'global.' + name; }
      });
    }

    if (!store) { send('no redux-like store found (scanned ' + ids.length + ' modules)'); return; }
    send('store found at ' + foundId);

    var state;
    try { state = store.getState(); } catch (e) { send('getState threw: ' + (e && e.message ? e.message : e)); return; }

    if (!state || typeof state !== 'object') { send('state is not an object: ' + typeof state); return; }

    var topKeys = safeKeys(state);
    send('top-level state keys: ' + topKeys.join(','));

    var interesting = topKeys.filter(function (k) {
      return /sign|auth|login|connect|account|psn|user/i.test(k);
    });
    send('interesting keys: ' + (interesting.join(',') || '(none)'));

    interesting.slice(0, 6).forEach(function (k) {
      var sub = state[k];
      var out;
      try {
        if (sub && typeof sub === 'object') {
          var subKeys = safeKeys(sub);
          var parts = [];
          subKeys.slice(0, 20).forEach(function (sk) {
            var v = sub[sk];
            var t = typeof v;
            var vs;
            if (t === 'object' && v) vs = '{' + safeKeys(v).slice(0, 10).join(',') + '}';
            else if (t === 'function') vs = 'fn';
            else vs = String(v);
            parts.push(sk + '=' + vs);
          });
          out = parts.join(' | ');
        } else {
          out = String(sub);
        }
      } catch (e) { out = 'err:' + (e && e.message ? e.message : e); }
      send('STATE.' + k + ' -> ' + String(out).slice(0, 280));
    });

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    send('v7 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
