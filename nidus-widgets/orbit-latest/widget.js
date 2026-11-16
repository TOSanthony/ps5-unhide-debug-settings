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
//   - Opening a specific game: Orbit Store's own backend (127.0.0.1:34177,
//     which keeps running even with the TV app closed) now exposes
//     POST/GET /api/v1/open-request. A press posts the catalog id there
//     first, then launches Orbit Store as before; the app itself picks the
//     request up on its next poll and opens straight to that game's page.
//   - Display modes (list/grid/cover): config.json declares a "settings"
//     array (docs/sdk/settings.md), which gets the widget a free Options
//     (Start) popup -- the same one Battery's "View selector" and the
//     Controllers widget use -- with no popup UI code of our own. The
//     chosen value just shows up on props.settings.displayMode.
//   - Cover mode has no wh.Focusable items (it's one big tile, not a list),
//     so it's never "entered"; L1/R1/Cross reach wh.onPressed directly
//     (confirmed in docs/sdk/input-and-focus.md). List/grid keep their rows
//     as wh.Focusable items, where L1/R1 already move the selection and
//     Cross already goes to the selected item -- our handlers below return
//     false for those so the hub's own behaviour is untouched.
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
  var OPEN_REQUEST_URL = 'http://127.0.0.1:34177/api/v1/open-request';
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
    body18: { fontSize: F.Size3XSmall },  // 18
    gridRow: { flexDirection: 'row' },
    gridCell: { borderRadius: 8, backgroundColor: '#263a58', overflow: 'hidden', position: 'relative' },
    gridCellFocused: { backgroundColor: '#3a568a' },
    // Drawn as a sibling on top of the cover image, since the image covers
    // the cell completely and a focusedStyle background behind it would
    // never actually show -- this is how you can tell which game you're on.
    gridCellSelected: { position: 'absolute', top: 0, left: 0, right: 0, bottom: 0, borderWidth: 3, borderColor: '#ffffff', borderRadius: 8 },
    gridLabelBar: { position: 'absolute', left: 0, right: 0, bottom: 0, backgroundColor: '#000000a0', paddingHorizontal: 6, paddingVertical: 3 },
    // Cover mode: the image fills the whole card edge to edge (no gap from
    // the card's own padding), title/dots overlaid on a bottom strip.
    coverFill: { position: 'absolute', top: 0, left: 0, right: 0, bottom: 0 },
    coverPlaceholderFull: { backgroundColor: '#263a58', alignItems: 'center', justifyContent: 'center' },
    coverOverlay: { position: 'absolute', left: 0, right: 0, bottom: 0, backgroundColor: '#000000a0',
                    paddingHorizontal: 16, paddingTop: 8, paddingBottom: 10,
                    borderBottomLeftRadius: 16, borderBottomRightRadius: 16 },
    coverTitle: { color: '#ffffff' },
    coverDots: { flexDirection: 'row', justifyContent: 'center', marginTop: 6 },
    coverDot: { width: 6, height: 6, borderRadius: 3, backgroundColor: '#ffffff40', marginHorizontal: 3 },
    coverDotActive: { backgroundColor: '#ffffff', width: 16 },
    // A button-hint footer baked into the widget's own card, same idea as
    // the stock "Top 10" widget's "Learn More  [L1]/[R1]" row: a divider,
    // then badge+label pairs, instead of relying on anything drawn outside
    // the tile.
    hintDivider: { height: 1, backgroundColor: '#ffffff30', marginTop: 8, marginBottom: 6 },
    hintRow: { flexDirection: 'row', alignItems: 'center', justifyContent: 'center' },
    hintBadge: { borderWidth: 1, borderColor: '#ffffffa0', borderRadius: 4, paddingHorizontal: 5, paddingVertical: 1, marginRight: 6 },
    hintLabel: { color: '#ffffffc0', marginRight: 16 }
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

  // A grid cell: same Focusable-with-cover idiom as Row, laid out as a square
  // tile with the title overlaid on a dark strip instead of beside it, so no
  // extra row height is spent on the label.
  function GridCell(props) {
    var item = props.item, size = props.size;
    return ce(wh.Focusable, {
      onPress: props.onPress,
      style: [styles.gridCell, { width: size, height: size }],
      focusedStyle: styles.gridCellFocused
    }, function (selected) {
      return ce(View, { style: { width: size, height: size } },
        Image && item.cover
          ? ce(Image, { source: { uri: item.cover }, resizeMode: 'cover', style: { width: size, height: size } })
          : null,
        ce(View, { style: styles.gridLabelBar },
          ce(Label, { style: styles.small, numberOfLines: 1 }, String(item.title || item.titleId || '?'))),
        selected ? ce(View, { style: styles.gridCellSelected }) : null
      );
    });
  }

  // One "[L1] Label" pair for the hint footer below.
  function Hint(props) {
    return ce(View, { style: { flexDirection: 'row', alignItems: 'center' } },
      ce(View, { style: styles.hintBadge }, ce(Label, { style: styles.small }, props.button)),
      ce(Label, { style: [styles.small, styles.hintLabel] }, props.label)
    );
  }

  // The PS Store-style pagination strip under a full-tile cover: a dot per
  // game, the current one wider and solid. Falls back to "i / n" past a
  // dozen games so the strip never outgrows the tile.
  function CoverDots(props) {
    if (props.total > 12)
      return ce(Label, { style: [styles.small, styles.dim, { marginTop: 8, textAlign: 'center' }] },
        (props.index + 1) + ' / ' + props.total);
    var dots = [];
    for (var i = 0; i < props.total; i++)
      dots.push(ce(View, { key: i, style: [styles.coverDot, i === props.index ? styles.coverDotActive : null] }));
    return ce(View, { style: styles.coverDots }, dots);
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

    // "Mode d'affichage" (list/grid/cover), set from the stock Options popup
    // (settings.md) -- props.settings is merged over config.json's defaults.
    var settings = (props && props.settings) || {};
    var mode = settings.displayMode || 'list';

    var coverState = React.useState(0);
    var coverIndex = coverState[0], setCoverIndex = coverState[1];
    var total = data.items.length;
    if (coverIndex >= total && total > 0) coverIndex = 0; // the catalog shrank under us

    function openItem(item) {
      log('launch: ' + (item.title || item.titleId));
      requestOpenGame(item).then(launchOrbitStore);
    }

    // Cover mode has no wh.Focusable items, so these reach the tile directly
    // while it's focused (never "entered"). List/grid already own L1/R1/Cross
    // through their rows' wh.Focusable items, so returning false there leaves
    // the hub's normal resize/selection/entering behaviour alone.
    wh.onPressed(B.L1, function () {
      if (mode !== 'cover' || total < 2) return false;
      setCoverIndex(function (i) { return (i - 1 + total) % total; });
    });
    wh.onPressed(B.R1, function () {
      if (mode !== 'cover' || total < 2) return false;
      setCoverIndex(function (i) { return (i + 1) % total; });
    });
    wh.onPressed(B.Cross, function () {
      if (mode !== 'cover' || !total) return false;
      openItem(data.items[coverIndex]);
    });

    // Tile size comes from the grid (L1/R1 resize in edit mode): fill the
    // given box and adapt the layout, same idea as payload-monitor.
    var W = (props && props.width) || 504;
    var H = (props && props.height) || 152;
    var compact = H < 300;              // DEFAULT (152)
    var pad = 32, titleH = 40, gap = 8;
    var innerW = W - pad, innerH = H - pad - titleH - gap;

    // Cover mode, once there's something to show: the cover fills the whole
    // tile edge to edge (no card padding, no separate header row) -- the
    // title/dots sit on an overlay strip on top of the image instead.
    if (mode === 'cover' && !data.loading && !data.error && total)
      return ce(View, { style: [styles.card, { width: W, height: H }] },
        renderCoverFull(data.items, coverIndex, W, H));

    var body;
    if (data.loading) {
      body = ce(Label, { style: styles.dim }, 'Chargement...');
    } else if (data.error) {
      body = ce(Label, { style: styles.dim, numberOfLines: 2 }, 'Erreur: ' + data.error);
    } else if (!total) {
      body = ce(Label, { style: styles.dim }, 'Aucun jeu dans le catalogue.');
    } else if (mode === 'grid') {
      body = renderGrid(data.items, innerW, innerH, openItem);
    } else {
      body = renderList(data.items, compact, innerH, openItem);
    }

    var cfg = (props && props.config) || {};
    var badge = data.loading ? null : (total ? total + ' jeu' + (total === 1 ? '' : 'x') : null);
    var header = ce(wh.Title, { iconId: cfg.iconId || 'game', badge: badge, style: { marginBottom: 8 } }, cfg.title || 'Orbit Store');

    return ce(View, { style: [styles.card, { width: W, height: H }] },
      ce(View, { lightMat: 'auto', style: styles.mat }),
      ce(View, { style: styles.body }, header, body)
    );
  }

  // List mode: a dense text row per game, cover art once there's room (same
  // layout this widget always had).
  function renderList(allItems, compact, innerH, openItem) {
    var rowH = compact ? 28 : 56;
    var coverSize = compact ? 0 : 40;
    var maxRows = Math.max(1, Math.floor(innerH / rowH));
    var items = allItems.slice(0, maxRows > 1 && allItems.length > maxRows ? maxRows - 1 : maxRows);
    var hidden = allItems.length - items.length;
    var rows = items.map(function (item, i) {
      return ce(Row, {
        key: item.id || item.titleId || i,
        item: item,
        compact: compact,
        rowHeight: rowH,
        coverSize: coverSize,
        onPress: function () { openItem(item); }
      });
    });
    if (hidden > 0) {
      rows.push(ce(View, { key: '__more', style: { height: rowH, justifyContent: 'center', paddingHorizontal: 8 } },
        ce(Label, { style: [styles.small, styles.dim] }, '+' + hidden + ' de plus')));
    }
    return ce(View, null, rows);
  }

  // Grid mode: a fixed 3 columns (the tile's width never changes, only its
  // height does), as many rows as fit. Arrows/L1/R1 already move between
  // cells by screen position -- the SDK's own wh.Focusable behaviour.
  var GRID_COLUMNS = 3;
  function renderGrid(allItems, innerW, innerH, openItem) {
    var gap = 8;
    var cellSize = Math.floor((innerW - gap * (GRID_COLUMNS - 1)) / GRID_COLUMNS);
    var rows = Math.max(1, Math.floor((innerH + gap) / (cellSize + gap)));
    var items = allItems.slice(0, rows * GRID_COLUMNS);
    var lines = [];
    for (var r = 0; r < Math.ceil(items.length / GRID_COLUMNS); r++) {
      var cells = [];
      for (var col = 0; col < GRID_COLUMNS; col++) {
        var idx = r * GRID_COLUMNS + col;
        if (idx >= items.length) break;
        var item = items[idx];
        cells.push(ce(GridCell, {
          key: item.id || item.titleId || idx,
          item: item,
          size: cellSize,
          onPress: (function (it) { return function () { openItem(it); }; })(item)
        }));
        if (col < GRID_COLUMNS - 1) cells.push(ce(View, { key: 'g' + idx, style: { width: gap } }));
      }
      lines.push(ce(View, { key: 'row' + r, style: [styles.gridRow, r > 0 ? { marginTop: gap } : null] }, cells));
    }
    return ce(View, null, lines);
  }

  // Cover mode: the cover art fills the entire tile edge to edge, like the
  // stock PS Store widget's featured-game view -- resizeMode 'cover' keeps
  // the image's own proportions and crops to fit (so a tall/square cover in
  // a short DEFAULT-size tile shows its vertical middle, not a squeezed or
  // letterboxed version). Title/dots sit on a translucent strip over the
  // bottom of the image, not in extra space below it. Deliberately has no
  // wh.Focusable item -- X/L1/R1 all reach OrbitLatest's own wh.onPressed
  // handlers directly, instead of first "entering" a one-item tile.
  function renderCoverFull(allItems, index, W, H) {
    var item = allItems[index] || {};
    return ce(View, { style: styles.coverFill },
      (Image && item.cover)
        ? ce(Image, { source: { uri: item.cover }, resizeMode: 'cover', style: { width: W, height: H } })
        : ce(View, { style: [styles.coverFill, styles.coverPlaceholderFull] },
            ce(Label, { style: styles.dim }, '?')),
      ce(View, { style: styles.coverOverlay },
        ce(Label, { style: [styles.body18, styles.coverTitle], numberOfLines: 1 }, String(item.title || item.titleId || '?')),
        allItems.length > 1 ? ce(CoverDots, { index: index, total: allItems.length }) : null,
        ce(View, { style: styles.hintDivider }),
        ce(View, { style: styles.hintRow },
          allItems.length > 1 ? ce(Hint, { button: 'L1/R1', label: 'Jeu suivant' }) : null,
          ce(Hint, { button: 'X', label: 'Lancer' })
        )
      )
    );
  }

  // Drops the catalog's *game* id at Orbit Store's own backend (best-effort:
  // a stale or old build without this route must not block the launch
  // below). item.id is the release's own id -- a game can have several
  // (different sources/qualities) -- while item.gameId is what groups them
  // into one Game, the id App::open_game()/state.game() actually resolve.
  // Sending item.id opens nothing and shows "this game is no longer
  // available" instead (confirmed on-console).
  function requestOpenGame(item) {
    var id = item && (item.gameId || item.id);
    if (!id) return Promise.resolve();
    return fetch(OPEN_REQUEST_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ gameId: id })
    }).catch(function (e) { log('open-request FAILED: ' + ((e && e.message) || e)); });
  }

  // A per-game catalog titleId isn't a real, locally-launchable PS5 app id --
  // confirmed on-console: pressing a row opened that title's PlayStation
  // Store page instead of launching anything. Only Orbit Store's own titleId
  // is a real registered app, so every press launches Orbit Store itself;
  // the open-request above is what gets it to the right game's page.
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
