// wh-inspector v22: v21 found the right way to launch a title (base_dll.c's
// VshNMApplicationLinkingPSModule). This version is a different kind of probe:
// it dumps whatever it can reach about the loader/Hub internals, to understand
// the automatic key-guide hint bar (Edit/Prev/Next/Settings) that shows below a
// focused widget -- specifically why it overlaps orbit-latest's own content but
// stays clear of stock widgets like Accessibility. Three things are dumped:
//   1. `wh` itself: every property name the loader actually hands a widget,
//      beyond the documented ones -- in case there's an undocumented API for
//      this (a key-guide/footer-related prop or method).
//   2. base_dll.c's exports (same bridge v21 used for NativeModules), filtered
//      to names that look focus/widget/hub/guide-related, with one extra level
//      of property names for each match.
//   3. React DevTools' global hook, if the runtime exposes one even without
//      devtools attached (common in RN) -- if present, this is the only way to
//      reach *other* widgets' own fiber/props from inside this one, since a
//      widget's own JS has no reference to its siblings otherwise.
// Everything found is logged to /data/wh-inspector.log via the dashboard's
// existing /__log endpoint, same plumbing as v21.
(function (wh) {
  var root = globalThis;
  var q = [];

  function queue(msg) { q.push(String(msg)); }

  function safeStr(v) {
    try {
      if (v === undefined) return 'undefined';
      if (v === null) return 'null';
      if (typeof v === 'function') return '<function ' + (v.name || 'anon') + '>';
      return JSON.stringify(v);
    } catch (e) { return 'unstringifiable(' + typeof v + '): ' + String(v); }
  }

  function sendNow(msg) {
    try {
      var url = 'http://127.0.0.1:8096/__log?w=77682d696e73706563746f72';
      var text = String(msg).slice(0, 1200);
      if (root.fetch) root.fetch(url, { method: 'POST', body: text }).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('POST', url, true); x.send(text); }
    } catch (e) {}
  }

  var pumpN = 0;
  var pump = root.setInterval(function () {
    if (!q.length) { if (pumpN > 400) root.clearInterval(pump); pumpN++; return; }
    sendNow(q.shift());
  }, 500);

  var already = false;
  try { already = root.localStorage && root.localStorage.getItem('wh-inspector-v22-done') === '1'; } catch (e) {}
  if (already) { return; }
  try { if (root.localStorage) root.localStorage.setItem('wh-inspector-v22-done', '1'); } catch (e) {}

  function ownKeys(obj) {
    try { return Object.getOwnPropertyNames(obj).join(','); } catch (e) { return 'n/a(' + e.message + ')'; }
  }

  try {
    if (!wh) { queue('abort: wh missing'); return; }

    // 1. wh itself, beyond the documented surface.
    queue('wh own keys: ' + ownKeys(wh));

    // 2. base_dll.c's exports, filtered.
    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) {
      queue('base_dll missing');
    } else {
      var ids = Object.getOwnPropertyNames(base.c);
      queue('base_dll.c module count: ' + ids.length);
      var interesting = /focus|guide|widget|hub|hint|keyguide|navig|footer/i;
      var matches = 0;
      for (var i = 0; i < ids.length; i++) {
        var ex;
        try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (!ex || typeof ex !== 'object') continue;
        var exKeys;
        try { exKeys = Object.getOwnPropertyNames(ex); } catch (e) { continue; }
        // Match on the module id itself, or on any exported name inside it.
        var hit = interesting.test(ids[i]);
        var hitKeys = [];
        for (var j = 0; j < exKeys.length; j++) {
          if (interesting.test(exKeys[j])) { hit = true; hitKeys.push(exKeys[j]); }
        }
        if (hit) {
          matches++;
          queue('MODULE ' + ids[i] + ' keys(' + exKeys.length + '): ' + exKeys.slice(0, 40).join(','));
          for (var k = 0; k < hitKeys.length; k++) {
            var sub;
            try { sub = ex[hitKeys[k]]; } catch (e) { continue; }
            queue('  ' + ids[i] + '.' + hitKeys[k] + ' = ' + safeStr(typeof sub === 'object' || typeof sub === 'function' ? ownKeys(sub) : sub));
          }
        }
      }
      queue('base_dll.c interesting-name matches: ' + matches);
    }

    // 3. React DevTools global hook, if present even unattached.
    var hook = root.__REACT_DEVTOOLS_GLOBAL_HOOK__;
    if (!hook) {
      queue('no __REACT_DEVTOOLS_GLOBAL_HOOK__ on globalThis');
    } else {
      queue('__REACT_DEVTOOLS_GLOBAL_HOOK__ keys: ' + ownKeys(hook));
      try {
        var renderers = hook.renderers;
        if (renderers && typeof renderers.forEach === 'function') {
          var rCount = 0;
          renderers.forEach(function (r, id) {
            rCount++;
            queue('renderer ' + id + ' keys: ' + ownKeys(r));
          });
          queue('renderer count: ' + rCount);
        } else {
          queue('hook.renderers not iterable: ' + safeStr(renderers));
        }
      } catch (e) {
        queue('renderers EXCEPTION: ' + (e && e.message ? e.message : e));
      }
    }

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v22 probe complete');
  } catch (e) {
    sendNow('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
