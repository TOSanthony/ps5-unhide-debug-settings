// wh-inspector v15: read-only. Lists every NativeModule the Welcome Hub exposes to its
// widgets, and flags any whose name or method names look related to launching another
// app/title (not just sign-in/account, like the earlier v3-v14 runs). Goal: find out
// whether a widget could launch Orbit Store (or any other title) from here. Patches
// nothing, calls nothing it finds -- it only reads names via Object.keys/getOwnPropertyNames.
// Logged to dashboard.elf's own HTTP server (port 8096, same as the dashboard UI/API),
// which appends to /data/wh-inspector.log.
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
      var url = 'http://127.0.0.1:8096/__log?w=77682d696e73706563746f72&h=' + hexOf(String(msg).slice(0, 320));
      if (root.fetch) root.fetch(url).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('GET', url, true); x.send(); }
    } catch (e) {}
  }

  // Drains the queue slowly (one line/700ms) so lines are not dropped or interleaved
  // -- the Hub's JS session has been observed to restart every few seconds.
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

  // Broad on purpose: we don't know the real name of an app-launch module yet.
  var KEY = /launch|app\b|apps\b|title|content|navigat|boot\b|start|shell|home\b|shortcut|activity|game|intent|product|catalog|store/i;

  try {
    if (!wh) { queue('abort: wh missing'); return; }

    chunkList('wh keys', Object.keys(wh));

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
          var nameHit = KEY.test(name);
          var methodHits = mk.filter(function (x) { return KEY.test(x); });
          if (nameHit) hitLines.push(name + '=ALL{' + mk.slice(0, 25).join(',') + '}');
          else if (methodHits.length) hitLines.push(name + '{' + methodHits.join(',') + '}');
        });
        chunkList('launch/app-ish matches', hitLines);

        // Also dump every method name on every module, unfiltered, in case a launch
        // function has a name none of the keywords above catch. One module per line.
        allNames.forEach(function (name) {
          var m; try { m = nm[name]; } catch (e) { return; }
          if (!m) return;
          var mk = []; try { mk = Object.keys(m); } catch (e) {}
          if (mk.length) queue('module ' + name + ': ' + mk.join(','));
        });
      } else {
        queue('NativeModules object not found in any base_dll export');
      }
    } else {
      queue('base_dll missing');
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v15 init complete');
  } catch (e) {
    queue('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
