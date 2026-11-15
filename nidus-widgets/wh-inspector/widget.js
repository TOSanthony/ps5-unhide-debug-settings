// wh-inspector v11: v10 only ever saw 18 createElement calls before the hub restarted --
// too few for a whole screen. Modern RN/Babel bundles usually compile JSX to jsx()/jsxs()
// from the 'react/jsx-runtime' module instead of React.createElement, so our hook was
// likely only catching NiDUS's own internal widget rendering, not the Hub's real screens.
// v11 keeps the createElement hook AND additionally scans base_dll.c for the jsx-runtime
// module (exports.jsx / exports.jsxs as functions) and patches those the same way --
// same (type, props, key) signature, same store-detection logic. Still fully read-only.
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
  send('=== v11 run #' + runs + ' ===');

  function safeKeys(o) {
    try { return Object.keys(o); } catch (e) { return []; }
  }
  function looksLikeStore(o) {
    return !!o && typeof o === 'object'
      && typeof o.getState === 'function'
      && typeof o.dispatch === 'function'
      && typeof o.subscribe === 'function';
  }

  var seen = [];
  var count = 0;
  var MAX_STORES = 10;

  function briefReport(store, where, idx) {
    var state;
    try { state = store.getState(); } catch (e) { send('STORE#' + idx + ' via ' + where + ': getState threw ' + (e && e.message ? e.message : e)); return; }
    if (!state || typeof state !== 'object') { send('STORE#' + idx + ' via ' + where + ': state not object'); return; }
    var topKeys = safeKeys(state);
    send('STORE#' + idx + ' via ' + where + ' keys: ' + topKeys.join(','));
    var interesting = topKeys.filter(function (k) { return /sign|auth|login|connect|account|psn|user/i.test(k); });
    if (interesting.length) {
      interesting.slice(0, 4).forEach(function (k) {
        var sub = state[k], out;
        try {
          if (sub && typeof sub === 'object') {
            out = safeKeys(sub).slice(0, 12).map(function (sk) {
              var v = sub[sk], t = typeof v;
              var vs = t === 'object' && v ? '{' + safeKeys(v).slice(0, 8).join(',') + '}' : (t === 'function' ? 'fn' : String(v));
              return sk + '=' + vs;
            }).join(' | ');
          } else { out = String(sub); }
        } catch (e) { out = 'err:' + (e && e.message ? e.message : e); }
        send('STORE#' + idx + '.' + k + ' -> ' + String(out).slice(0, 260));
      });
    }
  }

  function tryStore(candidate, label) {
    if (!looksLikeStore(candidate)) return false;
    for (var i = 0; i < seen.length; i++) if (seen[i] === candidate) return true;
    if (count >= MAX_STORES) return true;
    seen.push(candidate);
    count++;
    briefReport(candidate, label, count);
    return true;
  }

  function checkProps(props, tag, n) {
    if (!props) return;
    try {
      if (props.store) tryStore(props.store, tag + ' props.store (#' + n + ')');
      else if (props.value) {
        if (!tryStore(props.value, tag + ' props.value (#' + n + ')')) {
          if (typeof props.value === 'object') tryStore(props.value.store, tag + ' props.value.store (#' + n + ')');
        }
      }
    } catch (e) {
      send('hook error (' + tag + '): ' + (e && e.message ? e.message : e));
    }
  }

  function wrap(fn, tag, counterRef) {
    return function (type, props) {
      if (counterRef.n < counterRef.max && count < MAX_STORES) {
        counterRef.n++;
        checkProps(props, tag, counterRef.n);
      }
      return fn.apply(this, arguments);
    };
  }

  try {
    if (!wh || !wh.React) { send('abort: wh/React missing'); return; }

    if (root.__wh_insp11_hooked) {
      send('hooks already installed (persisted), run #' + runs);
    } else {
      root.__wh_insp11_hooked = true;

      var React = wh.React;
      var ceCounter = { n: 0, max: 6000 };
      if (typeof React.createElement === 'function') {
        var origCE = React.createElement;
        React.createElement = wrap(origCE, 'createElement', ceCounter);
        send('createElement hooked');
      } else {
        send('createElement missing on wh.React');
      }

      var base = root.base_dll;
      if (typeof base === 'function' && base.c) {
        var ids = Object.getOwnPropertyNames(base.c);
        var jsxHooked = 0;
        for (var i = 0; i < ids.length; i++) {
          var ex;
          try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
          if (!ex || typeof ex !== 'object') continue;
          ['jsx', 'jsxs', 'jsxDEV'].forEach(function (fname) {
            if (typeof ex[fname] === 'function' && !ex[fname].__wh_wrapped) {
              var origFn = ex[fname];
              var ctr = { n: 0, max: 6000 };
              var wrapped = wrap(origFn, 'module ' + ids[i] + '.' + fname, ctr);
              wrapped.__wh_wrapped = true;
              ex[fname] = wrapped;
              jsxHooked++;
            }
          });
        }
        send('jsx-runtime style hooks installed on ' + jsxHooked + ' export(s) across ' + ids.length + ' modules');
      } else {
        send('base_dll missing, cannot scan for jsx-runtime');
      }
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    send('v11 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
