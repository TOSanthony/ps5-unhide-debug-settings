// wh-inspector v3: v2 confirmed the widget runs fine (__wh, React, View, base_dll all
// present, wh.register() works). Now: list every NativeModule name and method matching a
// sign-in/user/account keyword, find where the "Se connecter" text comes from, and send
// everything in multiple chunks (each previous version's single 700-char log cut the list
// short).
(function (wh) {
  var root = globalThis;
  var q = [];

  function queue(msg) { q.push(String(msg)); }

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

  // Drains the queue slowly (one line/700ms) so lines are not dropped or interleaved.
  var pumpN = 0;
  var pump = root.setInterval(function () {
    if (!q.length) { if (pumpN > 200) root.clearInterval(pump); pumpN++; return; }
    send(q.shift());
  }, 700);

  function chunkList(tag, arr) {
    if (!arr.length) { queue(tag + ': (none)'); return; }
    var line = tag + ': ';
    for (var i = 0; i < arr.length; i++) {
      if (line.length > 280) { queue(line); line = tag + '+ '; }
      line += arr[i] + ' ';
    }
    queue(line);
  }

  var KEY = /sign|login|logout|npstate|account|psn|online|offline|auth|session|connect|network|user/i;

  try {
    if (!wh) { queue('abort: wh missing'); return; }

    // 1) full wh keys
    chunkList('wh keys', Object.keys(wh));

    // 2) every NativeModule name, and for each one every method matching KEY
    var base = root.base_dll;
    if (typeof base === 'function' && base.c) {
      var ids = Object.getOwnPropertyNames(base.c), nm = null;
      for (var i = 0; i < ids.length && !nm; i++) {
        var ex; try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
      }
      if (nm) {
        var allNames = Object.keys(nm);
        chunkList('ALL NativeModules', allNames);
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
        chunkList('sign/user/online matches', hitLines);
      } else {
        queue('NativeModules object not found in any base_dll export');
      }
    } else {
      queue('base_dll missing');
    }

    // 3) hook React.createElement to catch the "Se connecter" / "not signed in" text and its
    // caller stack, plus every distinct component name seen.
    var seenText = {}, nText = 0, seenComp = {}, nComp = 0;
    var TXT = /connect|sign|login|sesi|psn|network|offline|account|compte/i;
    var R = wh.React, orig = R.createElement;
    R.createElement = function (type, props) {
      try {
        var name = (type && (type.displayName || type.name)) || (typeof type === 'string' ? type : '?');
        if (!seenComp[name] && nComp < 150) { seenComp[name] = 1; nComp++; }
        var kids = [];
        for (var a = 2; a < arguments.length; a++) kids.push(arguments[a]);
        var ch = (props && props.children) || kids[0];
        var s = typeof ch === 'string' ? ch : '';
        if (s && s.length < 150 && !seenText[s]) {
          seenText[s] = 1; nText++;
          queue('text "' + s + '" in <' + name + '>');
          if (TXT.test(s)) {
            try { queue('  stack: ' + String(new Error().stack).replace(/\n/g, ' > ').slice(0, 500)); } catch (e) {}
          }
        }
      } catch (e) {}
      return orig.apply(this, arguments);
    };
    queue('React.createElement hooked OK');

    root.setTimeout(function () {
      chunkList('components seen after 6s', Object.keys(seenComp));
      queue('distinct texts seen: ' + nText);
    }, 6000);

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v3 init complete');
  } catch (e) {
    queue('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
