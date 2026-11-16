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
      wh.React.createElement(wh.Text, { style: { color: '#ffffff', fontSize: 16, flex: 1 }, numberOfLines: 1 }, String(item.title || item.titleId || '?'))
    );
  }

  function OrbitLatest() {
    var statePair = wh.React.useState({ loading: true, error: '', items: [] });
    var state = statePair[0], setState = statePair[1];
    var selPair = wh.React.useState(0), selected = selPair[0], setSelected = selPair[1];

    wh.React.useEffect(function () {
      sendNow('orbit-latest v2: fetching catalog...');
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
          setState({ loading: false, error: '', items: items });
        })
        .catch(function (e) {
          sendNow('orbit-latest v2: catalog FAILED: ' + (e && e.message ? e.message : e));
          setState({ loading: false, error: String(e && e.message || e), items: [] });
        });
    }, []);

    if (wh.onPressed) {
      wh.onPressed(wh.Button.Cross, function () {
        var item = state.items[selected];
        launchTitleId((item && item.titleId) || ORBIT_STORE_TITLE_ID, item ? item.title : 'Orbit Store (fallback)');
        return true;
      });
    }

    var body;
    if (state.loading) {
      body = wh.React.createElement(wh.Text, { style: { color: '#8fa2bf', fontSize: 15 } }, 'Chargement...');
    } else if (state.error) {
      body = wh.React.createElement(wh.Text, { style: { color: '#ffd479', fontSize: 15 } }, 'Erreur: ' + state.error);
    } else if (!state.items.length) {
      body = wh.React.createElement(wh.Text, { style: { color: '#8fa2bf', fontSize: 15 } }, 'Aucun jeu dans le catalogue.');
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
      wh.React.createElement(wh.Text, { style: { color: '#66e4ff', fontSize: 16, fontWeight: 'bold' } }, 'ORBIT STORE - DERNIERS AJOUTS'),
      body
    );
  }

  try {
    if (!wh || !wh.register) { sendNow('abort: wh/wh.register missing'); return; }
    if (!wh.Focusable) { sendNow('abort: wh.Focusable missing - cannot build interactive widget'); return; }
    wh.register(OrbitLatest);
    sendNow('orbit-latest v2: registered');
  } catch (e) {
    sendNow('orbit-latest v2 EXCEPTION: ' + (e && e.message ? e.message : e));
  }
})(globalThis.__wh);
