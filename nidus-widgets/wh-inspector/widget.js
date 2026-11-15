// wh-inspector: diagnostic widget for NiDUS. It draws nothing useful; it writes to
// /data/wh-widgets/wh.log what the Welcome Hub runtime contains (modules, native modules,
// text shown on screen and where the "sign in" text comes from), to find the sign-in check.
(function (wh) {
  var root = globalThis;
  var out = [];
  var log = wh.logger('wh-inspector');
  var sent = 0, MAX = 110;
  function say(s) { out.push(String(s)); }
  var pump = setInterval(function () {
    if (!out.length || sent >= MAX) { if (sent >= MAX) clearInterval(pump); return; }
    sent++;
    log(out.shift().slice(0, 380));
  }, 600);
  function chunk(tag, arr) {
    var line = tag + ': ', n = 0;
    for (var i = 0; i < arr.length; i++) {
      if (line.length + arr[i].length > 360) { say(line); line = tag + '+ '; if (++n > 12) return; }
      line += arr[i] + ' ';
    }
    if (line.length > tag.length + 3) say(line);
  }
  var KEY = /sign|login|logout|npstate|account|psn|online|offline|auth|session|connected|network|user/i;

  function scan(label) {
    try {
      var base = root.base_dll, ids = [];
      if (typeof base === 'function' && base.c) ids = Object.getOwnPropertyNames(base.c);
      say('[' + label + '] base_dll modules: ' + ids.length);
      var nativeNames = [], hits = [];
      for (var i = 0; i < ids.length; i++) {
        var ex;
        try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (!ex || (typeof ex !== 'object' && typeof ex !== 'function')) continue;
        var keys = [];
        try { keys = Object.keys(ex); } catch (e) { continue; }
        if (ex.NativeModules && typeof ex.NativeModules === 'object') {
          try { nativeNames = Object.keys(ex.NativeModules); } catch (e) {}
        }
        for (var k = 0; k < keys.length && k < 400; k++) {
          if (KEY.test(keys[k])) hits.push(String(ids[i]) + ':' + keys[k]);
        }
      }
      chunk('[' + label + '] NativeModules', nativeNames);
      chunk('[' + label + '] keys~sign/user/online', hits.slice(0, 120));
    } catch (e) { say('[' + label + '] scan failed: ' + e); }
  }

  // Native module methods that look related to sign-in / user state.
  function scanNative() {
    try {
      var base = root.base_dll, ids = Object.getOwnPropertyNames(base.c), nm = null;
      for (var i = 0; i < ids.length && !nm; i++) {
        try { var ex = base.c[ids[i]].exports; if (ex && ex.NativeModules) nm = ex.NativeModules; } catch (e) {}
      }
      if (!nm) { say('NativeModules object not found'); return; }
      var res = [];
      Object.keys(nm).forEach(function (name) {
        var m; try { m = nm[name]; } catch (e) { return; }
        if (!m) return;
        var mk = []; try { mk = Object.keys(m); } catch (e) {}
        var f = mk.filter(function (x) { return KEY.test(x) || KEY.test(name); });
        if (f.length) res.push(name + '{' + f.slice(0, 8).join(',') + '}');
      });
      chunk('native methods', res);
    } catch (e) { say('scanNative failed: ' + e); }
  }

  // Text on screen: who creates a string that looks like a sign-in prompt.
  var seenText = {}, nText = 0, nComp = {}, nCompN = 0;
  var TXT = /connect|sign|login|sesi|PSN|network|offline|account|compte/i;
  try {
    var R = wh.React, orig = R.createElement;
    R.createElement = function (type, props) {
      try {
        var name = (type && (type.displayName || type.name)) || (typeof type === 'string' ? type : '');
        if (name && !nComp[name] && nCompN < 80) { nComp[name] = 1; nCompN++; }
        var ch = props && props.children;
        var s = typeof ch === 'string' ? ch : (arguments.length > 2 && typeof arguments[2] === 'string' ? arguments[2] : '');
        if (s && s.length < 120 && !seenText[s] && nText < 40) {
          seenText[s] = 1; nText++;
          var st = '';
          if (TXT.test(s)) { try { st = ' | ' + String(new Error().stack).replace(/\s+/g, ' ').slice(0, 200); } catch (e) {} }
          say('text "' + s + '" in ' + name + st);
        }
      } catch (e) {}
      return orig.apply(this, arguments);
    };
  } catch (e) { say('createElement hook failed: ' + e); }

  setTimeout(function () { scan('t+1s'); scanNative(); }, 1000);
  setTimeout(function () { scan('t+10s'); chunk('components', Object.keys(nComp)); }, 10000);
  setTimeout(function () {
    try {
      var g = Object.getOwnPropertyNames(root).filter(function (x) { return KEY.test(x) || /^__|hub|Hub/.test(x); });
      chunk('globals', g.slice(0, 150));
    } catch (e) {}
  }, 3000);
  say('wh-inspector started');

  function Dummy() { return wh.React.createElement(wh.View, { style: { width: 10, height: 10 } }); }
  wh.register(Dummy);
})(globalThis.__wh);
