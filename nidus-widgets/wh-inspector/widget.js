// wh-inspector v6: locate WHICH rendered component shows the "Se connecter" /
// sign-in text, by hooking React.createElement and watching every element's
// text-bearing props (children/title/label/text) for a match on /connect|connex/i
// (covers "Se connecter" and "Connexion"). Logs the component's type name + the
// matched text for the first few hits, and separately logs any distinct function
// component name that looks sign-in/auth/gate related, to help map the tree.
// This does NOT patch/remove anything yet -- v6 only finds where it's written.
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
  send('=== v6 run #' + runs + ' ===');

  try {
    if (!wh || !wh.React) { send('abort: wh/React missing'); return; }

    if (root.__wh_insp_hooked) {
      send('createElement already hooked (persisted across restart), run #' + runs);
    } else {
      root.__wh_insp_hooked = true;

      var React = wh.React;
      var origCreateElement = React.createElement;
      if (typeof origCreateElement !== 'function') { send('createElement missing, cannot hook'); return; }

      var MAX_HITS = 8;
      var hits = 0;
      var seenTypes = {};
      var MAX_TYPES = 40;

      function typeName(t) {
        if (!t) return String(t);
        if (typeof t === 'string') return t;
        if (t.displayName) return t.displayName;
        if (t.name) return t.name;
        if (t.render && t.render.name) return 'memo(' + t.render.name + ')';
        return typeof t;
      }

      function textOf(props) {
        if (!props) return '';
        var parts = [];
        if (typeof props.children === 'string') parts.push(props.children);
        if (Array.isArray(props.children)) {
          props.children.forEach(function (c) { if (typeof c === 'string') parts.push(c); });
        }
        if (typeof props.title === 'string') parts.push(props.title);
        if (typeof props.label === 'string') parts.push(props.label);
        if (typeof props.text === 'string') parts.push(props.text);
        return parts.join(' | ');
      }

      React.createElement = function (type, props) {
        try {
          if (hits < MAX_HITS) {
            var txt = textOf(props);
            if (txt && /connect|connex/i.test(txt)) {
              hits++;
              send('HIT#' + hits + ' type=' + typeName(type) + ' text="' + txt.slice(0, 80) + '"');
            }
          }
          if (Object.keys(seenTypes).length < MAX_TYPES && typeof type === 'function') {
            var tn = typeName(type);
            if (tn && !seenTypes[tn] && /sign|login|connect|connex|auth|hub|screen|gate|psn/i.test(tn)) {
              seenTypes[tn] = 1;
              send('TYPE seen: ' + tn);
            }
          }
        } catch (e) {
          send('hook error: ' + (e && e.message ? e.message : e));
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
    send('v6 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
