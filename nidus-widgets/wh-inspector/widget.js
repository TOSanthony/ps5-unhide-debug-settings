// wh-inspector v10: v9 found A store (productRating/userRating/hubScene/experiments/
// friendsWhoPlay) but it stopped at the FIRST one found -- that's a small, widget-local
// store (ratings/social card), not the app-wide sign-in/auth store. createElement calls
// fire leaf-first (JS evaluates arguments before the call), so the first Provider we hit
// is an INNER one, not the outermost app Provider. v10 keeps scanning and reports EVERY
// distinct store it sees (deduped by reference), with just its top-level state keys
// (brief, to avoid flooding before the next hub restart) -- still fully read-only.
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
  send('=== v10 run #' + runs + ' ===');

  function safeKeys(o) {
    try { return Object.keys(o); } catch (e) { return []; }
  }
  function looksLikeStore(o) {
    return !!o && typeof o === 'object'
      && typeof o.getState === 'function'
      && typeof o.dispatch === 'function'
      && typeof o.subscribe === 'function';
  }

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

  try {
    if (!wh || !wh.React) { send('abort: wh/React missing'); return; }

    if (root.__wh_insp10_hooked) {
      send('createElement already hooked (persisted), run #' + runs);
    } else {
      root.__wh_insp10_hooked = true;
      var React = wh.React;
      var origCreateElement = React.createElement;
      if (typeof origCreateElement !== 'function') { send('createElement missing'); return; }

      var checked = 0;
      var MAX_CHECK = 6000;
      var MAX_STORES = 10;
      var seen = [];
      var count = 0;

      function tryStore(candidate, label) {
        if (!looksLikeStore(candidate)) return false;
        for (var i = 0; i < seen.length; i++) if (seen[i] === candidate) return true; // already reported
        if (count >= MAX_STORES) return true;
        seen.push(candidate);
        count++;
        briefReport(candidate, label, count);
        return true;
      }

      React.createElement = function (type, props) {
        if (checked < MAX_CHECK && count < MAX_STORES) {
          checked++;
          try {
            if (props && props.store) tryStore(props.store, 'props.store (#' + checked + ')');
            else if (props && props.value) {
              if (!tryStore(props.value, 'props.value (#' + checked + ')')) {
                if (props.value && typeof props.value === 'object') {
                  tryStore(props.value.store, 'props.value.store (#' + checked + ')');
                }
              }
            }
          } catch (e) {
            send('hook error: ' + (e && e.message ? e.message : e));
          }
        }
        return origCreateElement.apply(this, arguments);
      };
      send('createElement hooked for the first time, run #' + runs);
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    send('v10 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
