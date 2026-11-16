// orbit-latest v3: real interactive "latest additions" widget for Orbit Store.
//
// Confirmed working on-console (after a full sign-out/sign-in -- the hub
// keeps its JS runtime across a mere Hub close/reopen, per the official SDK
// docs at github.com/geodox/ps5-wh-widgets):
//   - size:3 (FULL, 504x504) with an explicit width/height on the root View
//     renders visible content; size:1 widgets in this project never had.
//   - wh.Focusable + wh.onPressed(wh.Button.X, fn) + wh.React.useState all
//     work and the game list is navigable.
//   - fontSize (not fontSizePS -- that's the enum of allowed VALUES,
//     wh.FontSizePS, not a different style key) must be one of
//     [15,18,21,24,27,30,36,45,54,72] or RNPS throws.
//   - Pressing a game row does NOT launch that game: its catalog titleId
//     isn't a real, locally-launchable PS5 app id, so psgm:play falls back
//     to that title's PlayStation Store page instead. Only Orbit Store's
//     own titleId (PPSA99177) is a real registered app id.
//
// So: fetches Orbit Store's local catalog (127.0.0.1:34177/api/v1/catalog),
// shows the 5 most recently added games as a navigable list, and any press
// (a row, or X) launches Orbit Store itself via the proven
// VshNMApplicationLinkingPSModule.OpenURL('psgm:play?id=PPSA99177') -- the
// user picks the specific game from there. Launching straight into one
// specific game needs Orbit Store's own main.cpp to read a launch
// parameter, which is a separate, parked task.
(function (wh) {
  var root = globalThis;
  var ORBIT_STORE_TITLE_ID = 'PPSA99177';

  // useEffect never fires in this environment (confirmed: the on-console test
  // showed "registered" logged but the effect's own first log line never
  // appeared, across a fresh widget mount). useState does work (proven by v1
  // and this version's own Row/focus state). So the catalog fetch runs once
  // at module scope -- exactly like v1, which succeeded on every single
  // restart in the log -- and pushes its result into whichever component
  // instance's setState is currently live, via this module-level ref.
  var catalogState = { loading: true, error: '', items: [] };
  var rerenderRef = null;
  function applyCatalogState(next) {
    catalogState = next;
    if (rerenderRef) { try { rerenderRef(next); } catch (e) {} }
  }

  // This environment only accepts a fixed set of font sizes (confirmed by
  // the third-party widget-store widget's own Txt() helper, which snaps to
  // the nearest supported value); an arbitrary fontSize like 16 throws a
  // font-related error at render time instead of silently falling back.
  // That widget uses the plain "fontSize" style key (confirmed working,
  // live, on the user's own console at size:3) -- not "fontSizePS".
  var SUPPORTED_FONT_SIZES = [15, 18, 21, 24, 27, 30, 36, 45, 54, 72];
  function snapFontSize(requested) {
    var resolved = 15, distance = 1e9;
    for (var i = 0; i < SUPPORTED_FONT_SIZES.length; i++) {
      var d = Math.abs(SUPPORTED_FONT_SIZES[i] - requested);
      if (d < distance) { distance = d; resolved = SUPPORTED_FONT_SIZES[i]; }
    }
    return resolved;
  }
  function Txt(wh, value, size, color, extra) {
    var style = { color: color || '#ffffff', fontSize: snapFontSize(size || 18) };
    if (extra) for (var k in extra) style[k] = extra[k];
    return wh.React.createElement(wh.Text, { style: style }, value == null ? '' : String(value));
  }

  function sendNow(msg) {
    try {
      var url = 'http://127.0.0.1:8096/__log?w=6f726269742d6c6174657374';
      var text = String(msg).slice(0, 1200);
      if (root.fetch) root.fetch(url, { method: 'POST', body: text }).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('POST', url, true); x.send(text); }
    } catch (e) {}
  }

  // A per-game titleId from Orbit Store's own catalog is NOT a PS5 app id the
  // system can launch directly -- confirmed on-console: pressing a game row
  // opened that title's PlayStation Store page instead of launching anything
  // (psgm:play falls back to the store page when the id isn't a locally
  // launchable app). Only Orbit Store's own titleId (PPSA99177) is a real,
  // registered app id, and only that one is safe to pass to OpenURL. So
  // every press -- on any row, or X -- just launches Orbit Store itself;
  // the user picks the specific game from there. (Launching straight into a
  // specific game is tracked separately: it needs Orbit Store's own main.cpp
  // to read a launch parameter, which it doesn't today.)
  function launchOrbitStore(label) {
    try {
      var base = root.base_dll;
      if (typeof base !== 'function' || !base.c) { sendNow('launch(' + label + '): base_dll missing'); return; }
      var ids = Object.getOwnPropertyNames(base.c), nm = null;
      for (var i = 0; i < ids.length && !nm; i++) {
        var ex; try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
      }
      if (!nm) { sendNow('launch(' + label + '): NativeModules not found'); return; }
      var link = nm['VshNMApplicationLinkingPSModule'];
      if (!link) { sendNow('launch(' + label + '): ApplicationLinkingPSModule missing'); return; }
      var target = 'psgm:play?id=' + ORBIT_STORE_TITLE_ID;
      sendNow('launch(' + label + '): CanOpenURL ' + target);
      Promise.resolve(link.CanOpenURL(target)).then(function (can) {
        if (!can) { sendNow('launch(' + label + '): CanOpenURL false'); return; }
        return Promise.resolve(link.OpenURL(target)).then(function () {
          sendNow('launch(' + label + '): OpenURL resolved');
        });
      }).catch(function (e) {
        sendNow('launch(' + label + '): EXCEPTION ' + (e && e.message ? e.message : e));
      });
    } catch (e) {
      sendNow('launch(' + label + '): top-level EXCEPTION ' + (e && e.message ? e.message : e));
    }
  }

  function Row(props) {
    var focusedPair = wh.React.useState(false), focused = focusedPair[0], setFocused = focusedPair[1];
    var item = props.item;
    return wh.React.createElement(
      wh.Focusable,
      {
        onPress: props.onPress,
        onFocus: function () { setFocused(true); },
        onBlur: function () { setFocused(false); },
        style: {
          flexDirection: 'row', alignItems: 'center', height: 48, marginTop: 6,
          paddingHorizontal: 10, borderRadius: 10,
          backgroundColor: focused ? '#356ac4' : '#1d2b3a',
          borderWidth: focused ? 2 : 1, borderColor: focused ? '#ffffff' : '#2a3b52'
        },
        focusedStyle: { borderColor: '#ffffff', borderWidth: 2 }
      },
      wh.Image && item.cover
        ? wh.React.createElement(wh.Image, { source: { uri: item.cover }, resizeMode: 'cover', style: { width: 34, height: 34, borderRadius: 6, marginRight: 10 } })
        : wh.React.createElement(wh.View, { style: { width: 34, height: 34, borderRadius: 6, marginRight: 10, backgroundColor: '#263a58' } }),
      wh.React.createElement(wh.Text, { style: { color: '#ffffff', fontSize: snapFontSize(18), flex: 1 }, numberOfLines: 1 }, String(item.title || item.titleId || '?'))
    );
  }

  function OrbitLatest(props) {
    var statePair = wh.React.useState(catalogState);
    var state = statePair[0], setState = statePair[1];
    var selPair = wh.React.useState(0), selected = selPair[0], setSelected = selPair[1];
    rerenderRef = setState;

    if (wh.onPressed) {
      wh.onPressed(wh.Button.Cross, function () {
        var item = state.items[selected];
        launchOrbitStore(item ? item.title : null);
        return true;
      });
    }

    var body;
    if (state.loading) {
      body = Txt(wh, 'Chargement...', 15, '#8fa2bf');
    } else if (state.error) {
      body = Txt(wh, 'Erreur: ' + state.error, 15, '#ffd479');
    } else if (!state.items.length) {
      body = Txt(wh, 'Aucun jeu dans le catalogue.', 15, '#8fa2bf');
    } else {
      body = wh.React.createElement(
        wh.View, { style: { marginTop: 4 } },
        state.items.map(function (item, i) {
          return wh.React.createElement(Row, {
            key: item.id || item.titleId || i,
            item: item,
            onPress: function () { setSelected(i); launchOrbitStore(item.title); }
          });
        })
      );
    }

    return wh.React.createElement(
      wh.View, { style: { width: (props && props.width) || 504, height: (props && props.height) || 504, padding: 12, backgroundColor: '#090d16', borderRadius: 10, overflow: 'hidden' } },
      Txt(wh, 'ORBIT STORE - DERNIERS AJOUTS', 18, '#66e4ff', { fontWeight: 'bold' }),
      body
    );
  }

  try {
    if (!wh || !wh.register) { sendNow('abort: wh/wh.register missing'); return; }
    if (!wh.Focusable) { sendNow('abort: wh.Focusable missing - cannot build interactive widget'); return; }

    wh.register(OrbitLatest);
    sendNow('orbit-latest v2: registered');

    sendNow('orbit-latest v2: fetching catalog (module scope)...');
    root.fetch('http://127.0.0.1:34177/api/v1/catalog')
      .then(function (r) { return r.json(); })
      .then(function (list) {
        var items = Array.isArray(list) ? list.slice() : [];
        items.sort(function (a, b) {
          var av = String((a && (a.addedAt || a.releaseDate)) || '');
          var bv = String((b && (b.addedAt || b.releaseDate)) || '');
          return av < bv ? 1 : (av > bv ? -1 : 0);
        });
        items = items.slice(0, 5);
        sendNow('orbit-latest v2: catalog ok, ' + items.length + ' items shown');
        applyCatalogState({ loading: false, error: '', items: items });
      })
      .catch(function (e) {
        sendNow('orbit-latest v2: catalog FAILED: ' + (e && e.message ? e.message : e));
        applyCatalogState({ loading: false, error: String(e && e.message || e), items: [] });
      });
  } catch (e) {
    sendNow('orbit-latest v2 EXCEPTION: ' + (e && e.message ? e.message : e));
  }
})(globalThis.__wh);
