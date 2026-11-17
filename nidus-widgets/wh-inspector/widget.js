// wh-inspector v24: v23 confirmed processKeyGuide(2734tBS6.KeyGuidePS) is the
// content-bearing key-guide call, and that its props (style:{flex:1} /
// {marginLeft:'auto'}, autoHideBehavior:'hideWhenTargetLosesFocus',
// targetComponent:<fiber id>) are IDENTICAL in shape for every widget -- only
// targetComponent (a React fiber id, one per widget tile on the Hub) differs.
// So the visible difference (bar inside Orbit Store's card vs below
// Accessibilite's) isn't explained by anything processKeyGuide itself
// receives. This version additionally dumps `wh.__hubFrame` (an own key of
// the per-widget `wh` bridge that NAME-wise looks like shared Hub-frame
// state) the first time each new targetComponent id is seen, to try to
// correlate a fiber id with an actual widget identity/geometry and find
// something that DOES differ between widgets at the Hub level.
// Everything found is logged to /data/wh-inspector.log via the dashboard's
// existing /__log endpoint, same plumbing as v21/v22/v23.
(function (wh) {
  var root = globalThis;
  var q = [];

  function queue(msg) { q.push(String(msg)); }

  function safeStr(v, depth) {
    depth = depth || 0;
    try {
      if (v === undefined) return 'undefined';
      if (v === null) return 'null';
      if (typeof v === 'function') return '<function ' + (v.name || 'anon') + '>';
      if (typeof v !== 'object') return String(v);
      if (depth > 2) return '<deep>';
      // Plain JSON.stringify chokes on cyclic/host objects (React fibers,
      // native refs) -- walk one/two levels by hand instead, each value
      // through safeStr again, so we degrade gracefully instead of throwing.
      if (Array.isArray(v)) {
        return '[' + v.slice(0, 20).map(function (x) { return safeStr(x, depth + 1); }).join(',') + (v.length > 20 ? ',...' : '') + ']';
      }
      var keys;
      try { keys = Object.keys(v); } catch (e) { return '<unkeyable ' + (v.constructor && v.constructor.name) + '>'; }
      var parts = [];
      for (var i = 0; i < keys.length && i < 25; i++) {
        var k = keys[i];
        var val;
        try { val = v[k]; } catch (e) { parts.push(k + '=<getter threw>'); continue; }
        parts.push(k + '=' + safeStr(val, depth + 1));
      }
      var ctorName = '';
      try { ctorName = (v.constructor && v.constructor.name) ? v.constructor.name + ' ' : ''; } catch (e) {}
      return ctorName + '{' + parts.join(',') + (keys.length > 25 ? ',...' : '') + '}';
    } catch (e) { return 'unstringifiable(' + typeof v + '): ' + String(e && e.message); }
  }

  function sendNow(msg) {
    try {
      var url = 'http://127.0.0.1:8096/__log?w=77682d696e73706563746f72';
      var text = String(msg).slice(0, 1500);
      if (root.fetch) root.fetch(url, { method: 'POST', body: text }).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('POST', url, true); x.send(text); }
    } catch (e) {}
  }

  var pumpN = 0;
  var pump = root.setInterval(function () {
    if (!q.length) { if (pumpN > 1200) root.clearInterval(pump); pumpN++; return; }
    sendNow(q.shift());
  }, 350);

  var already = false;
  try { already = root.localStorage && root.localStorage.getItem('wh-inspector-v25-done') === '1'; } catch (e) {}
  if (already) { return; }
  try { if (root.localStorage) root.localStorage.setItem('wh-inspector-v25-done', '1'); } catch (e) {}

  function ownKeys(obj) {
    try { return Object.getOwnPropertyNames(obj).join(','); } catch (e) { return 'n/a(' + e.message + ')'; }
  }

  // Wrap a static/own method on `obj[name]` so every call is logged with its
  // arguments and return value, then falls through to the original. Returns
  // true if the wrap was installed.
  var callCounts = {};
  var seenTargets = {};
  // Every time 2734tBS6.KeyGuidePS.processKeyGuide is called with a NEW
  // targetComponent id (a new widget tile gaining focus), dump wh.__hubFrame
  // once, tagged with that id -- so we can line up a fiber id with whatever
  // per-widget state __hubFrame exposes (if it exposes anything at all).
  function maybeDumpHubFrameFor(args0) {
    try {
      if (!args0 || typeof args0 !== 'object') return;
      var tc = args0.targetComponent;
      if (tc === undefined || tc === null) return;
      if (seenTargets[tc]) return;
      seenTargets[tc] = true;
      // __hubFrame and dims are FUNCTIONS (confirmed by v24) -- call them to
      // get whatever live value they compute, instead of just naming them.
      var hubFrameVal, hubFrameErr, dimsVal, dimsErr;
      try { hubFrameVal = typeof wh.__hubFrame === 'function' ? wh.__hubFrame() : wh.__hubFrame; } catch (e) { hubFrameErr = e; }
      try { dimsVal = typeof wh.dims === 'function' ? wh.dims() : wh.dims; } catch (e) { dimsErr = e; }
      queue('NEW targetComponent=' + tc + ' -- wh.__hubFrame(): ' + (hubFrameErr ? ('THREW ' + (hubFrameErr.message || hubFrameErr)) : safeStr(hubFrameVal, 0)));
      queue('NEW targetComponent=' + tc + ' -- wh.dims(): ' + (dimsErr ? ('THREW ' + (dimsErr.message || dimsErr)) : safeStr(dimsVal, 0)));
    } catch (e) {
      queue('maybeDumpHubFrameFor EXCEPTION: ' + (e && e.message ? e.message : e));
    }
  }

  function wrapMethod(obj, name, tag) {
    try {
      var orig = obj[name];
      if (typeof orig !== 'function') { queue(tag + ': ' + name + ' is not a function (' + typeof orig + ')'); return false; }
      if (orig.__whInspectorWrapped) { queue(tag + ': ' + name + ' already wrapped'); return true; }
      var wrapped = function () {
        var n = (callCounts[tag] = (callCounts[tag] || 0) + 1);
        var argsDesc = '?';
        try {
          argsDesc = Array.prototype.slice.call(arguments).map(function (a) { return safeStr(a); }).join(' | ');
        } catch (e) { argsDesc = 'arg-stringify-failed: ' + (e && e.message); }
        queue(tag + ' CALL#' + n + ' args: ' + argsDesc);
        if (tag === '2734tBS6.KeyGuidePS') { maybeDumpHubFrameFor(arguments[0]); }
        var ret;
        var threw = null;
        try {
          ret = orig.apply(this, arguments);
        } catch (e) {
          threw = e;
        }
        if (threw) {
          queue(tag + ' CALL#' + n + ' THREW: ' + (threw && threw.message ? threw.message : threw));
          throw threw;
        }
        try { queue(tag + ' CALL#' + n + ' returned: ' + safeStr(ret)); } catch (e) {}
        return ret;
      };
      wrapped.__whInspectorWrapped = true;
      obj[name] = wrapped;
      queue(tag + ': ' + name + ' wrapped OK');
      return true;
    } catch (e) {
      queue(tag + ': failed to wrap ' + name + ': ' + (e && e.message ? e.message : e));
      return false;
    }
  }

  try {
    if (!wh) { queue('abort: wh missing'); return; }

    queue('wh own keys: ' + ownKeys(wh));

    var base = root.base_dll;
    if (typeof base !== 'function' || !base.c) {
      queue('base_dll missing');
    } else {
      var ids = Object.getOwnPropertyNames(base.c);
      queue('base_dll.c module count: ' + ids.length);

      // Find the two KeyGuidePS classes + PopupHintPS by module id, exactly
      // as v22 found them (NiSiE5q6, 2734tBS6), and wrap their methods.
      var found = { NiSiE5q6: null, '2734tBS6': null };
      for (var i = 0; i < ids.length; i++) {
        var ex;
        try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (!ex || typeof ex !== 'object') continue;

        if (ids[i] === 'NiSiE5q6' || (ex.KeyGuidePS && ex.FocusLayerPS)) {
          wrapMethod(ex.KeyGuidePS, 'processKeyGuide', 'NiSiE5q6.KeyGuidePS');
          if (ex.PopupHintPS) {
            wrapMethod(ex.PopupHintPS, 'open', 'NiSiE5q6.PopupHintPS');
            wrapMethod(ex.PopupHintPS, 'close', 'NiSiE5q6.PopupHintPS');
          }
        }
        if (ids[i] === '2734tBS6' || (ex.KeyGuidePS && ex.KeyGuidePSFSR)) {
          wrapMethod(ex.KeyGuidePS, 'processKeyGuide', '2734tBS6.KeyGuidePS');
        }
      }
      queue('base_dll.c scan done');
    }

    var hook = root.__REACT_DEVTOOLS_GLOBAL_HOOK__;
    queue(hook ? ('__REACT_DEVTOOLS_GLOBAL_HOOK__ present: ' + ownKeys(hook)) : 'no __REACT_DEVTOOLS_GLOBAL_HOOK__ on globalThis');

    if (wh.register) {
      wh.register(function Dummy() {
        return wh.React.createElement(wh.View || 'View', { style: { width: 8, height: 8 } });
      });
    }
    queue('wh-inspector v25 probe installed -- navigate between widgets on the Hub now to trigger logged calls');
  } catch (e) {
    sendNow('EXCEPTION: ' + (e && e.message ? e.message : e) + (e && e.stack ? ' | ' + String(e.stack).slice(0, 300) : ''));
  }
})(globalThis.__wh);
