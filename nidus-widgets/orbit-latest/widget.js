// orbit-latest v2: real interactive "latest additions" widget for Orbit Store.
//
// v1 confirmed the first open question (can a widget render visible content at
// all) by fetching a count and showing "Orbit Store: N jeux" -- not yet tested
// on-console as of this version. This version goes straight to the full
// interactive widget, using patterns confirmed to work by a third-party widget
// (manchildtechnologies-ui/ps5-widget-store's "Widget Store" widget), which is
// a known-good, currently-installable widget proving:
//   - wh.React.useState / useEffect DO work inside wh.React.createElement trees
//   - wh.Focusable (a focusable View wrapper with onPress + focusedStyle) is
//     the real way to make a row clickable/navigable with the controller
//   - wh.onPressed(wh.Button.X, fn) captures face/shoulder button presses
//     globally within the widget while it has focus
//   - wh.View / wh.Text / wh.Image / wh.ScrollView all render real content
//
// This widget:
//   1. Fetches Orbit Store's local catalog (127.0.0.1:34177/api/v1/catalog),
//      which returns entries with at least {id, title, titleId, cover,
//      releaseDate, addedAt, genre, tagline, ...} (confirmed from
//      orbit-store's backend/catalog.c).
//   2. Sorts by addedAt (fallback releaseDate) descending, keeps the top 5.
//   3. Renders each as a focusable row (cover icon placeholder + title).
//   4. On press (or X), launches that game directly via the one proven
//      launch mechanism: VshNMApplicationLinkingPSModule.OpenURL('psgm:play?id=<titleId>')
//      after CanOpenURL confirms it. Falls back to Orbit Store's own
//      titleId (PPSA99177) if anything about a specific row's launch fails
//      or if the list is empty / still loading, so pressing something in
//      the widget always does *something* useful.
//   5. Logs every step to /data/orbit-latest.log via dashboard.elf's shared
//      /__log endpoint (port 8096), same convention as every other
//      diagnostic widget this session.
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
    var style = { color: color || '#ffffff', fontSizePS: snapFontSize(size || 18) };
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

  function launchTitleId(titleId, label) {
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
      var target = 'psgm:play?id=' + titleId;
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
      wh.React.createElement(wh.Text, { style: { color: '#ffffff', fontSizePS: snapFontSize(18), flex: 1 }, numberOfLines: 1 }, String(item.title || item.titleId || '?'))
    );
  }

  function OrbitLatest() {
    var statePair = wh.React.useState(catalogState);
    var state = statePair[0], setState = statePair[1];
    var selPair = wh.React.useState(0), selected = selPair[0], setSelected = selPair[1];
    rerenderRef = setState;

    if (wh.onPressed) {
      wh.onPressed(wh.Button.Cross, function () {
        var item = state.items[selected];
        launchTitleId((item && item.titleId) || ORBIT_STORE_TITLE_ID, item ? item.title : 'Orbit Store (fallback)');
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
            onPress: function () { setSelected(i); launchTitleId(item.titleId || ORBIT_STORE_TITLE_ID, item.title); }
          });
        })
      );
    }

    return wh.React.createElement(
      wh.View, { style: { padding: 12, backgroundColor: '#090d16', borderRadius: 10 } },
      Txt(wh, 'ORBIT STORE - DERNIERS AJOUTS', 18, '#66e4ff', { fontWeight: 'bold' }),
      body
    );
  }

  try {
    if (!wh || !wh.register) { sendNow('abort: wh/wh.register missing'); return; }
    if (!wh.Focusable) { sendNow('abort: wh.Focusable missing - cannot build interactive widget'); return; }

    // Diagnostic: the fetch/state pipeline is confirmed working (log shows
    // "catalog ok, 5 items shown"), yet nothing renders, not even the static
    // "Chargement..." text that's present on every render from the very
    // first frame. That points at one of the render primitives themselves
    // (wh.Text, wh.View, wh.React.createElement...) not being what we
    // assume -- log their typeof up front to find out which.
    sendNow('diag: typeof wh.View=' + typeof wh.View + ' wh.Text=' + typeof wh.Text +
      ' wh.Image=' + typeof wh.Image + ' wh.Focusable=' + typeof wh.Focusable +
      ' wh.React=' + typeof wh.React + ' wh.React.createElement=' + (wh.React && typeof wh.React.createElement) +
      ' wh.React.useState=' + (wh.React && typeof wh.React.useState) +
      ' wh.register=' + typeof wh.register + ' wh.onPressed=' + typeof wh.onPressed +
      ' wh.Button=' + typeof wh.Button);
    try {
      var probe = wh.React.createElement(wh.View || 'View', { style: { padding: 4 } }, wh.React.createElement(wh.Text || 'Text', { style: { fontSizePS: 18, color: '#fff' } }, 'probe'));
      sendNow('diag: probe element created ok, type=' + typeof probe + ' keys=' + (probe ? Object.keys(probe).join(',') : 'n/a'));
    } catch (e) {
      sendNow('diag: probe element creation THREW: ' + (e && e.message ? e.message : e));
    }

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
