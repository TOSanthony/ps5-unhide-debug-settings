// wh-inspector v4: the log shows the hub's JS runtime restarts every 2-13s while stuck on
// "Se connecter" (a new "[hub] eval" with a fresh rt= id), tearing down our widget before
// the slow queued logger (700ms/line) could finish. v4 sends everything immediately and
// synchronously, most important first, with no throttling queue.
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

  function sendList(tag, arr) {
    if (!arr.length) { send(tag + ': (none)'); return; }
    var line = tag + ': ';
    for (var i = 0; i < arr.length; i++) {
      if (line.length > 280) { send(line); line = tag + '+ '; }
      line += arr[i] + ' ';
    }
    send(line);
  }

  var runs = (root.__wh_insp_runs || 0) + 1;
  root.__wh_insp_runs = runs;
  send('=== wh-inspector v4 run #' + runs + ' (hub runtime restart count) ===');

  try {
    if (!wh) { send('abort: wh missing'); return; }

    var KEY = /sign|login|logout|npstate|account|psn|online|offline|auth|session|connect|network|user/i;
    var base = root.base_dll;
    if (typeof base === 'function' && base.c) {
      var ids = Object.getOwnPropertyNames(base.c), nm = null;
      for (var i = 0; i < ids.length && !nm; i++) {
        var ex; try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
      }
      if (nm) {
        var allNames = Object.keys(nm);
        // Priority 1: matches only, sent first.
        var hitLines = [];
        allNames.forEach(function (name) {
          var m; try { m = nm[name]; } catch (e) { return; }
          if (!m) return;
          var mk = []; try { mk = Object.keys(m); } catch (e) {}
          if (KEY.test(name)) hitLines.push(name + '=ALL{' + mk.slice(0, 15).join(',') + '}');
          else {
            var f = mk.filter(function (x) { return KEY.test(x); });
            if (f.length) hitLines.push(name + '{' + f.join(',') + '}');
          }
        });
        sendList('MATCHES', hitLines);
        // Priority 2: full name list (for completeness).
        sendList('ALL_NM', allNames);
      } else {
        send('NativeModules object not found');
      }
    } else {
      send('base_dll missing');
    }

    // Priority 3: hook createElement immediately (cheap, synchronous, survives even a
    // short-lived runtime since it fires on every render that happens before teardown).
    var seenText = {};
    var TXT = /connect|sign|login|sesi|psn|network|offline|account|compte/i;
    var R = wh.React, orig = R.createElement;
    R.createElement = function (type, props) {
      try {
        var name = (type && (type.displayName || type.name)) || (typeof type === 'string' ? type : '?');
        var ch = props && props.children;
        var s = typeof ch === 'string' ? ch : '';
        if (s && s.length < 150 && !seenText[s]) {
          seenText[s] = 1;
          if (TXT.test(s)) {
            send('TEXT "' + s + '" in <' + name + '> stack: ' +
                 String(new Error().stack || '').replace(/\n/g, ' > ').slice(0, 260));
          } else {
            send('text "' + s + '" in <' + name + '>');
          }
        }
      } catch (e) {}
      return orig.apply(this, arguments);
    };

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    send('v4 init complete, run #' + runs);
  } catch (e) {
    send('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 260) : ''));
  }
})(globalThis.__wh);
