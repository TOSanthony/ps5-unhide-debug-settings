// orbit-latest v4: "Orbit Store - Derniers ajouts", built to look and behave
// like a stock Welcome Hub widget.
//
// Follows the official SDK's own reference widgets (hello-wh, payload-monitor
// from github.com/geodox/ps5-wh-widgets) for the native look:
//   - wh.Title / wh.Label: the hub's own stock text components (no custom
//     font/colour -- that's what makes a custom widget look hand-rolled).
//   - the stock mat background (#17191Ef0) + lightMat sheen, 16px radius,
//     16px padding -- same shell every stock widget uses.
//   - wh.Focusable with a function child receiving `selected`, exactly like
//     hello-wh's Chip -- no manual useState(focused) per row.
//   - size-adaptive layout from props.width/height, exactly like
//     payload-monitor's COMPACT/maxRows logic -- DEFAULT (152 tall) shows a
//     dense text-only list, LARGE/FULL show cover art and more rows. This is
//     the SDK's built-in L1/R1 resize (DEFAULT -> LARGE -> FULL in edit mode).
//   - wh.logger, the SDK's own logging -- writes to the same /data/wh-widgets
//     /wh.log the dashboard's "Voir wh.log" button now reads, so there's a
//     single log to check instead of this project's earlier custom one.
//
// Behaviour notes learned on-console this session:
//   - fontSize must be one of wh.FontSizePS's values (15/18/21/24/27/30/36/
//     45/54/72) or RNPS throws -- wh.Label/wh.Title already respect this.
//   - A catalog entry's own titleId is NOT a locally-launchable PS5 app id;
//     passing it to psgm:play opens that title's PlayStation Store page
//     instead of launching anything. Only Orbit Store's own titleId
//     (PPSA99177) is a real registered app, so every press here launches
//     Orbit Store itself -- the user picks the specific game from there.
//   - A changed widget.js only takes effect in a NEW hub JS runtime: closing
//     and reopening the Hub is not enough, a full sign-out/sign-in is needed.
(function (wh) {
  var React = wh.React;
  var View = wh.View;
  var Image = wh.Image;
  var Label = wh.Label;
  var ce = React.createElement;
  var F = wh.FontSizePS;
  var B = wh.Button;
  var log = wh.logger ? wh.logger('orbit-latest') : function () {};

  var ORBIT_STORE_TITLE_ID = 'PPSA99177';
  var CATALOG_URL = 'http://127.0.0.1:34177/api/v1/catalog';
  var MAT = '#17191ef0';

  var styles = wh.StyleSheet.create({
    card: { borderRadius: 16, overflow: 'hidden' },
    mat: { position: 'absolute', top: 0, left: 0, right: 0, bottom: 0, backgroundColor: MAT, borderRadius: 16 },
    body: { flex: 1, padding: 16 },
    row: { flexDirection: 'row', alignItems: 'center', borderRadius: 10, paddingHorizontal: 8 },
    rowFocused: { backgroundColor: '#ffffff1f' },
    cover: { borderRadius: 6, marginRight: 10, backgroundColor: '#263a58' },
    coverPlaceholder: { alignItems: 'center', justifyContent: 'center' },
    dim: { opacity: 0.6 },
    small: { fontSize: F.Size4XSmall },   // 15
    body18: { fontSize: F.Size3XSmall }   // 18
  });

  // A per-game catalog row: X (or press) launches Orbit Store. wh.Focusable
  // passes `selected` to a function child, same idiom as hello-wh's Chip.
  function Row(props) {
    var item = props.item;
    return ce(wh.Focusable, {
      onPress: props.onPress,
      style: [styles.row, { height: props.rowHeight }],
      focusedStyle: styles.rowFocused
    }, function (selected) {
      var titleStyle = props.compact ? [styles.small, { flex: 1 }] : { flex: 1, fontSize: F.Size3XSmall };
      return ce(View, { style: { flexDirection: 'row', alignItems: 'center', flex: 1 } },
        !props.compact
          ? ce(View, { style: [styles.cover, styles.coverPlaceholder, { width: props.coverSize, height: props.coverSize }] },
              Image && item.cover
                ? ce(Image, { source: { uri: item.cover }, resizeMode: 'cover', style: { width: props.coverSize, height: props.coverSize, borderRadius: 6 } })
                : ce(Label, { style: [styles.small, styles.dim] }, '?'))
          : null,
        ce(Label, { style: titleStyle, numberOfLines: 1 }, String(item.title || item.titleId || '?'))
      );
    });
  }

  function OrbitLatest(props) {
    var dataState = React.useState({ loading: true, error: '', items: [] });
    var data = dataState[0], setData = dataState[1];
    var mounted = React.useRef(true);

    React.useEffect(function () {
      mounted.current = true;
      function load() {
        fetch(CATALOG_URL, { headers: { Accept: 'application/json' } })
          .then(function (r) { return r.json(); })
          .then(function (list) {
            if (!mounted.current) return;
            var items = Array.isArray(list) ? list.slice() : [];
            items.sort(function (a, b) {
              var av = String((a && (a.addedAt || a.releaseDate)) || '');
              var bv = String((b && (b.addedAt || b.releaseDate)) || '');
              return av < bv ? 1 : (av > bv ? -1 : 0);
            });
            setData({ loading: false, error: '', items: items });
            log('catalog ok, ' + items.length + ' total, showing up to 8');
          })
          .catch(function (e) {
            if (!mounted.current) return;
            setData({ loading: false, error: String((e && e.message) || e), items: [] });
            log('catalog FAILED: ' + ((e && e.message) || e));
          });
      }
      load();
      var id = setInterval(load, 20000);
      return function () { mounted.current = false; clearInterval(id); };
    }, []);

    // Tile size comes from the grid (L1/R1 resize in edit mode): fill the
    // given box and adapt the layout, same idea as payload-monitor.
    var W = (props && props.width) || 504;
    var H = (props && props.height) || 152;
    var compact = H < 300;              // DEFAULT (152)
    var rowH = compact ? 28 : 56;
    var coverSize = compact ? 0 : 40;
    var titleH = 40, pad = 32, gap = 8;
    var maxRows = Math.max(1, Math.floor((H - pad - titleH - gap) / rowH));

    var items = data.items.slice(0, maxRows > 1 && data.items.length > maxRows ? maxRows - 1 : maxRows);
    var hidden = data.items.length - items.length;

    var body;
    if (data.loading) {
      body = ce(Label, { style: styles.dim }, 'Chargement...');
    } else if (data.error) {
      body = ce(Label, { style: styles.dim, numberOfLines: 2 }, 'Erreur: ' + data.error);
    } else if (!data.items.length) {
      body = ce(Label, { style: styles.dim }, 'Aucun jeu dans le catalogue.');
    } else {
      var rows = items.map(function (item, i) {
        return ce(Row, {
          key: item.id || item.titleId || i,
          item: item,
          compact: compact,
          rowHeight: rowH,
          coverSize: coverSize,
          onPress: function () { log('launch via row: ' + (item.title || item.titleId)); launchOrbitStore(); }
        });
      });
      if (hidden > 0) {
        rows.push(ce(View, { key: '__more', style: { height: rowH, justifyContent: 'center', paddingHorizontal: 8 } },
          ce(Label, { style: [styles.small, styles.dim] }, '+' + hidden + ' de plus')));
      }
      body = ce(View, null, rows);
    }

    var cfg = (props && props.config) || {};
    var badge = data.loading ? null : (data.items.length ? data.items.length + ' jeu' + (data.items.length === 1 ? '' : 'x') : null);
    var header = ce(wh.Title, { iconId: cfg.iconId || 'game', badge: badge, style: { marginBottom: 8 } }, cfg.title || 'Orbit Store');

    return ce(View, { style: [styles.card, { width: W, height: H }] },
      ce(View, { lightMat: 'auto', style: styles.mat }),
      ce(View, { style: styles.body }, header, body)
    );
  }

  // A per-game catalog titleId isn't a real, locally-launchable PS5 app id --
  // confirmed on-console: pressing a row opened that title's PlayStation
  // Store page instead of launching anything. Only Orbit Store's own titleId
  // is a real registered app, so every press launches Orbit Store itself;
  // the user picks the specific game from there. (Launching straight into a
  // specific game needs Orbit Store's own main.cpp to read a launch
  // parameter -- tracked separately.)
  function launchOrbitStore() {
    try {
      var base = globalThis.base_dll;
      if (typeof base !== 'function' || !base.c) { log('launch: base_dll missing'); return; }
      var ids = Object.getOwnPropertyNames(base.c), nm = null;
      for (var i = 0; i < ids.length && !nm; i++) {
        var ex; try { ex = base.c[ids[i]].exports; } catch (e) { continue; }
        if (ex && typeof ex === 'object' && ex.NativeModules) nm = ex.NativeModules;
      }
      if (!nm) { log('launch: NativeModules not found'); return; }
      var link = nm['VshNMApplicationLinkingPSModule'];
      if (!link) { log('launch: ApplicationLinkingPSModule missing'); return; }
      var target = 'psgm:play?id=' + ORBIT_STORE_TITLE_ID;
      Promise.resolve(link.CanOpenURL(target)).then(function (can) {
        if (!can) { log('launch: CanOpenURL false'); return; }
        return Promise.resolve(link.OpenURL(target)).then(function () { log('launch: OpenURL resolved'); });
      }).catch(function (e) { log('launch: EXCEPTION ' + ((e && e.message) || e)); });
    } catch (e) {
      log('launch: top-level EXCEPTION ' + ((e && e.message) || e));
    }
  }

  wh.register(OrbitLatest);
})(globalThis.__wh);
