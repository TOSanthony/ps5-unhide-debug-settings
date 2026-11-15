// wh-inspector v2: no [widget:*] line ever appeared from any widget (not even
// hello-wh/payload-monitor), so first confirm whether widget.js code runs at all and
// whether wh.logger's queue actually reaches wh.log. Logs over a direct fetch to the
// asset server's /__log endpoint, synchronously at the top, bypassing wh.logger's queue.
(function (wh) {
  var root = globalThis;

  function raw(msg) {
    try {
      var url = 'http://127.0.0.1:8098/__log?w=776828696e73706563746f72&h='; // "wh-inspector" hex, fixed tag
      var hex = '';
      var s = String(msg);
      for (var i = 0; i < s.length && hex.length < 700; i++) {
        var c = s.charCodeAt(i);
        hex += (c < 16 ? '0' : '') + c.toString(16);
      }
      url += hex;
      if (root.fetch) {
        root.fetch(url).catch(function () {});
      } else if (root.XMLHttpRequest) {
        var x = new root.XMLHttpRequest();
        x.open('GET', url, true);
        x.send();
      }
    } catch (e) {}
  }

  raw('v2 top-level reached; __wh=' + (wh ? 'yes' : 'NO') +
      ' React=' + (wh && wh.React ? 'yes' : 'no') +
      ' View=' + (wh && wh.View ? 'yes' : 'no') +
      ' fetch=' + (typeof root.fetch) +
      ' base_dll=' + (typeof root.base_dll));

  try {
    if (!wh) { raw('abort: wh is null/undefined'); return; }

    raw('wh keys: ' + Object.keys(wh).join(','));

    var base = root.base_dll;
    if (typeof base === 'function' && base.c) {
      var ids = Object.getOwnPropertyNames(base.c);
      raw('base_dll.c module count=' + ids.length);
      var nmFound = [];
      for (var i = 0; i < ids.length; i++) {
        var ex;
        try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (ex && typeof ex === 'object' && ex.NativeModules) {
          try { nmFound = nmFound.concat(Object.keys(ex.NativeModules)); } catch (e) {}
        }
      }
      raw('NativeModules seen: ' + nmFound.slice(0, 60).join(','));
    } else {
      raw('base_dll missing or not a function (' + typeof base + ')');
    }

    if (wh.logger) {
      var log = wh.logger('wh-inspector');
      log('v2 reached wh.logger() call too');
    } else {
      raw('wh.logger missing');
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React ? wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } }) : null;
      });
      raw('wh.register() called without throwing');
    } else {
      raw('wh.register missing');
    }
  } catch (e) {
    raw('EXCEPTION: ' + (e && e.message ? e.message : e) +
        (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
