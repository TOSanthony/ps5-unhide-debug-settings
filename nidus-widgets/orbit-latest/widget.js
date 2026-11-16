// orbit-latest v1: first visibility test for a real Orbit Store widget in the
// Welcome Hub. The only widget running in production so far (hub-signin-patch)
// only ever rendered an invisible 8x8 box -- this has never been tried: does
// wh.register + a real View/Text tree with actual content show up on screen at
// all? This version does nothing else: it fetches Orbit Store's own local catalog
// API (127.0.0.1:34177/api/v1/catalog, confirmed from the app's own CONTEXTE.md)
// just to have one real number to display, renders "Orbit Store: N jeux" in a
// small box, and logs what happened to dashboard.elf's /__log endpoint (port 8096)
// so we know from the log alone whether the fetch/render code ran, even if nothing
// is visible on screen.
(function (wh) {
  var root = globalThis;

  function sendNow(msg) {
    try {
      var url = 'http://127.0.0.1:8096/__log?w=6f726269742d6c6174657374';
      var text = String(msg).slice(0, 1200);
      if (root.fetch) root.fetch(url, { method: 'POST', body: text }).catch(function () {});
      else if (root.XMLHttpRequest) { var x = new root.XMLHttpRequest(); x.open('POST', url, true); x.send(text); }
    } catch (e) {}
  }

  try {
    if (!wh || !wh.register) { sendNow('abort: wh/wh.register missing'); return; }

    var View = wh.View || 'View';
    var Text = wh.Text || 'Text';
    var count = null;

    wh.register(function OrbitLatest() {
      var label = count === null ? 'Orbit Store: chargement...' : 'Orbit Store: ' + count + ' jeu' + (count === 1 ? '' : 'x');
      return wh.React.createElement(
        View,
        { style: { padding: 12, backgroundColor: '#1d2b3a', borderRadius: 8 } },
        wh.React.createElement(Text, { style: { color: '#ffffff', fontSize: 16 } }, label)
      );
    });
    sendNow('orbit-latest: registered, fetching catalog...');

    root.fetch('http://127.0.0.1:34177/api/v1/catalog')
      .then(function (r) { return r.json(); })
      .then(function (list) {
        count = Array.isArray(list) ? list.length : -1;
        sendNow('orbit-latest: catalog fetched, count=' + count);
      })
      .catch(function (e) {
        count = -1;
        sendNow('orbit-latest: catalog fetch FAILED: ' + (e && e.message ? e.message : e));
      });
  } catch (e) {
    sendNow('orbit-latest EXCEPTION: ' + (e && e.message ? e.message : e));
  }
})(globalThis.__wh);
