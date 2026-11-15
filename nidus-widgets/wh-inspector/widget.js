// wh-inspector v9: v8's props.value check expected the context value ITSELF to duck-type
// as a store, but react-redux's Context.Provider actually passes { store, subscription },
// not the store directly. v9 adds a check for props.value.store too (still read-only).
// createElement hook, but instead of logging component type names, it watches every
// element's props for a "store" key that duck-types as redux (getState/dispatch/
// subscribe), and the first time it finds one, reads getState() ONCE (read-only, no
// dispatch) and reports the sign-in/auth/account-looking slices of state.
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
  send('=== v9 run #' + runs + ' ===');

  function safeKeys(o) {
    try { return Object.keys(o); } catch (e) { return []; }
  }
  function looksLikeStore(o) {
    return !!o && typeof o === 'object'
      && typeof o.getState === 'function'
      && typeof o.dispatch === 'function'
      && typeof o.subscribe === 'function';
  }

  function reportStore(store, where) {
    send('STORE via ' + where);
    var state;
    try { state = store.getState(); } catch (e) { send('getState threw: ' + (e && e.message ? e.message : e)); return; }
    if (!state || typeof state !== 'object') { send('state not an object: ' + typeof state); return; }
    var topKeys = safeKeys(state);
    send('top-level state keys: ' + topKeys.join(','));
    var interesting = topKeys.filter(function (k) { return /sign|auth|login|connect|account|psn|user/i.test(k); });
    send('interesting keys: ' + (interesting.join(',') || '(none)'));
    interesting.slice(0, 6).forEach(function (k) {
      var sub = state[k], out;
      try {
        if (sub && typeof sub === 'object') {
          var parts = [];
          safeKeys(sub).slice(0, 20).forEach(function (sk) {
            var v = sub[sk], t = typeof v, vs;
            if (t === 'object' && v) vs = '{' + safeKeys(v).slice(0, 10).join(',') + '}';
            else if (t === 'function') vs = 'fn';
            else vs = String(v);
            parts.push(sk + '=' + vs);
          });
          out = parts.join(' | ');
        } else { out = String(sub); }
      } catch (e) { out = 'err:' + (e && e.message ? e.message : e); }
      send('STATE.' + k + ' -> ' + String(out).slice(0, 280));
    });
  }

  try {
    if (!wh || !wh.React) { send('abort: wh/React missing'); return; }

    if (root.__wh_insp8_hooked) {
      send('createElement already hooked (persisted), run #' + runs);
    } else {
      root.__wh_insp8_hooked = true;
      var React = wh.React;
      var origCreateElement = React.createElement;
      if (typeof origCreateElement !== 'function') { send('createElement missing'); return; }

      var storeFound = false;
      var checked = 0;
      var MAX_CHECK = 4000;

      React.createElement = function (type, props) {
        if (!storeFound && checked < MAX_CHECK) {
          checked++;
          try {
            if (props && props.store && looksLikeStore(props.store)) {
              storeFound = true;
              reportStore(props.store, 'Provider-like props.store (after ' + checked + ' elements)');
            } else if (props && props.value && looksLikeStore(props.value)) {
              storeFound = true;
              reportStore(props.value, 'Context props.value (after ' + checked + ' elements)');
            } else if (props && props.value && typeof props.value === 'object'
                       && looksLikeStore(props.value.store)) {
              storeFound = true;
              reportStore(props.value.store, 'Context props.value.store (after ' + checked + ' elements)');
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
    send('v9 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
