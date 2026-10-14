/* UltraVideo reference renderer: draws the timeline from the neutral model in index.d.ts.
   It is a visual spec for UltraCanvasVideoTimeline, not production code. */
(function () {
  var ICONS = {
    select: 'M4 2.5l8 6-3.6.6 2.1 4.3-1.6.8-2.1-4.3L4 12.5z',
    blade: 'M3 13l7-7M10 6l2.5-2.5a1.4 1.4 0 012 2L12 8M6.5 9.5l-2 4M9.5 3.5L13 7',
    ripple: 'M2 3v10M5 8h8M10.5 5.5L13 8l-2.5 2.5M5 5.5L2.5 8',
    slip: 'M3 3v10M13 3v10M5.5 8h5M7 6.5L5.5 8 7 9.5M9 6.5l1.5 1.5L9 9.5',
    magnet: 'M4 2.5v5.5a4 4 0 008 0V2.5M4 5.5h2.5M9.5 5.5H12M6.5 2.5V8a1.5 1.5 0 003 0V2.5',
    snap: 'M8 2v12M3 5l2 3-2 3M13 5l-2 3 2 3',
    beat: 'M2 8h2l1.5-4 2 8 2-6 1.5 2H14',
    zoomIn: 'M7 3a4 4 0 110 8 4 4 0 010-8zM13.5 13.5L10 10M5 7h4M7 5v4',
    zoomOut: 'M7 3a4 4 0 110 8 4 4 0 010-8zM13.5 13.5L10 10M5 7h4',
    fit: 'M2.5 6V3.5a1 1 0 011-1H6M10 2.5h2.5a1 1 0 011 1V6M13.5 10v2.5a1 1 0 01-1 1H10M6 13.5H3.5a1 1 0 01-1-1V10',
    play: 'M5 3l8 5-8 5z',
    pause: 'M5 3v10M11 3v10',
    stepBack: 'M11 4L6 8l5 4zM4.5 4v8',
    stepFwd: 'M5 4l5 4-5 4zM11.5 4v8',
    toStart: 'M12 4L7 8l5 4zM8 4L3 8l5 4z',
    toEnd: 'M4 4l5 4-5 4zM8 4l5 4-5 4z',
    speaker: 'M2.5 6h2.5L8.5 3v10L5 10H2.5zM11 5.5a3.5 3.5 0 010 5M12.8 3.8a6 6 0 010 8.4',
    speakerOff: 'M2.5 6h2.5L8.5 3v10L5 10H2.5zM11 6l3.5 4M14.5 6L11 10',
    lock: 'M4 7.5h8v6H4zM5.5 7.5V5a2.5 2.5 0 015 0v2.5',
    unlock: 'M4 7.5h8v6H4zM5.5 7.5V5a2.5 2.5 0 014.8-1',
    eye: 'M1.5 8S4 3.5 8 3.5 14.5 8 14.5 8 12 12.5 8 12.5 1.5 8 1.5 8zM8 6a2 2 0 110 4 2 2 0 010-4z',
    eyeOff: 'M2 2l12 12M6.5 4a6.5 6.5 0 018 4 9 9 0 01-1.8 2.4M4 5.3A9 9 0 001.5 8S4 12.5 8 12.5c1 0 1.9-.3 2.7-.7',
    plus: 'M8 3v10M3 8h10',
    more: 'M3.5 8h.01M8 8h.01M12.5 8h.01',
    film: 'M2.5 3h11v10h-11zM5 3v10M11 3v10M2.5 6H5M2.5 10H5M11 6h2.5M11 10h2.5',
    image: 'M2.5 3h11v10h-11zM2.5 11l3.5-3.5 3 3 2-2 2.5 2.5M10.5 5.5h.01',
    motion: 'M2.5 4h7v6h-7zM11 6.5l2.5-1.5v5l-2.5-1.5M5 12.5h5',
    title: 'M3 4V3h10v1M8 3v10M6 13h4',
    logo: 'M8 2l1.8 3.7 4 .6-2.9 2.8.7 4L8 11.2 4.4 13.1l.7-4-2.9-2.8 4-.6z',
    fx: 'M2.5 13l2-10h4M3.5 8h4M9.5 7l4 6M13.5 7l-4 6',
    music: 'M6 12V3.5l7-1.5V10.5M6 12a1.8 1.8 0 11-3.6 0A1.8 1.8 0 016 12zM13 10.5a1.8 1.8 0 11-3.6 0 1.8 1.8 0 013.6 0z',
    transition: 'M2.5 3l5 5-5 5zM13.5 3l-5 5 5 5z',
    speed: 'M2.5 11a5.5 5.5 0 0111 0M8 11l3-3.5M8 11h.01',
    undo: 'M5 3L2.5 5.5 5 8M2.5 5.5H10a3.5 3.5 0 010 7H7',
    redo: 'M11 3l2.5 2.5L11 8M13.5 5.5H6a3.5 3.5 0 000 7h3',
    export: 'M8 2.5v8M5 5.5l3-3 3 3M3 10v3.5h10V10',
    marker: 'M4 2.5h8v8L8 13.5 4 10.5z',
    split: 'M8 2v12M3 4h3v8H3M13 4h-3v8h3',
    trash: 'M3 4.5h10M6.5 4.5V3h3v1.5M4.5 4.5l.7 9h5.6l.7-9',
    duplicate: 'M5.5 5.5h8v8h-8zM10.5 5.5v-3h-8v8h3',
    chevronDown: 'M4 6l4 4 4-4',
    chevronRight: 'M6 4l4 4-4 4',
    search: 'M7 3a4 4 0 110 8 4 4 0 010-8zM13.5 13.5L10 10',
    upload: 'M8 10.5V2.5M5 5.5l3-3 3 3M2.5 9v4.5h11V9',
    clipIn: 'M5 3v10M5 8h7',
    wave: 'M2 8h1M4 5v6M6 3v10M8 6v4M10 4v8M12 6.5v3M14 8h-1'
  };
  function icon(name, cls) {
    return '<svg class="uv-ico' + (cls ? ' ' + cls : '') + '" viewBox="0 0 16 16" aria-hidden="true"><path d="' + (ICONS[name] || '') + '"/></svg>';
  }
  function pad(n) { return (n < 10 ? '0' : '') + n; }
  function tc(t, fps, short) {
    fps = fps || 25;
    var f = Math.round(t * fps), fr = f % fps, s = Math.floor(f / fps), m = Math.floor(s / 60), h = Math.floor(m / 60);
    if (short) return pad(m % 60) + ':' + pad(s % 60);
    return pad(h) + ':' + pad(m % 60) + ':' + pad(s % 60) + ':' + pad(fr);
  }
  function rnd(seed) { var x = seed || 1; return function () { x = (x * 16807) % 2147483647; return (x - 1) / 2147483646; }; }
  function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;'); }

  function peaksPath(w, h, seed, density) {
    var r = rnd(seed), step = density || 2, mid = h / 2, top = [], bot = [], prev = 0.5;
    for (var x = 0; x <= w; x += step) {
      var beat = Math.pow(Math.abs(Math.sin(x / 23)), 6) * 0.35;
      prev = prev * 0.55 + (0.25 + r() * 0.45 + beat) * 0.45;
      var a = Math.min(0.95, prev) * mid;
      top.push(x + ',' + (mid - a).toFixed(1)); bot.unshift(x + ',' + (mid + a).toFixed(1));
    }
    return 'M' + top.join('L') + 'L' + bot.join('L') + 'Z';
  }

  function clipHTML(c, x, w, laneH, fps) {
    var kind = c.kind || 'video', cls = 'uv-clip uv-clip--' + kind;
    if (c.selected) cls += ' is-selected';
    if (c.hover) cls += ' is-hover';
    if (c.muted) cls += ' is-muted';
    if (c.locked) cls += ' is-locked';
    if (c.ghost) cls += ' is-ghost';
    var s = '<div class="' + cls + '" style="left:' + x + 'px;width:' + w + 'px' + (c.ghost && c.lift ? ';top:' + (3 - c.lift) + 'px;bottom:' + (3 + c.lift) + 'px' : '') + '">';
    s += '<div class="uv-clip__band">';
    if (kind === 'image') s += icon('motion', 'uv-ico--sm');
    if (kind === 'audio') s += icon('music', 'uv-ico--sm');
    s += '<span class="uv-clip__name">' + esc(c.name) + '</span>';
    if (w > 70 && c.fx && c.fx.length) { s += '<span class="uv-clip__fx">'; c.fx.forEach(function (f) { s += icon(f, 'uv-ico--sm'); }); s += '</span>'; }
    if (c.muted) s += icon('speakerOff', 'uv-ico--sm');
    if (c.speed && c.speed !== 1) s += '<span class="uv-clip__badge">' + c.speed + '×</span>';
    if (w > 110 && c.showDur !== false) s += '<span class="uv-clip__badge">' + (c.dur).toFixed(1) + ' s</span>';
    s += '</div>';
    if (kind === 'card') s += '<div class="uv-clip__swatch" style="background:' + (c.color || '#101418') + '"></div>';
    else if (kind === 'audio') {
      s += audioSVG(c, w, laneH - 6 - 4 - 16);
    } else if (laneH > 40) {
      var fh = laneH - 6 - 4 - 16, fw = Math.round(fh * 16 / 9), n = Math.ceil(w / fw), shots = c.shots || [0];
      s += '<div class="uv-clip__strip">';
      for (var i = 0; i < n; i++) s += '<div class="uv-clip__frame uv-shot-' + shots[i % shots.length] + '" style="width:' + fw + 'px"></div>';
      s += '</div>';
    }
    s += '<div class="uv-clip__handle uv-clip__handle--l' + (c.trim === 'l' ? ' is-active' : '') + '"></div><div class="uv-clip__handle uv-clip__handle--r' + (c.trim === 'r' ? ' is-active' : '') + '"></div>';
    return s + '</div>';
  }

  function audioSVG(c, w, h) {
    var pps = c._pps || 40, s = '<svg class="uv-wave" viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none" style="height:' + h + 'px">';
    s += '<path class="uv-wave__peaks" d="' + peaksPath(w, h, c.seed || 7, 2) + '"/>';
    (c.crossfades || []).forEach(function (xf) {
      var a = (xf.at - c.start - xf.dur / 2) * pps, b = (xf.at - c.start + xf.dur / 2) * pps;
      s += '<path class="uv-wave__xfade" d="M' + a + ',0L' + b + ',' + h + 'L' + b + ',0L' + a + ',' + h + 'Z"/>';
      s += '<line class="uv-wave__song" x1="' + ((a + b) / 2) + '" x2="' + ((a + b) / 2) + '" y1="0" y2="' + h + '"/>';
    });
    var fi = (c.fadeIn || 0) * pps, fo = (c.fadeOut || 0) * pps, top = 3;
    if (fi) s += '<path class="uv-wave__fade" d="M0,0L' + fi + ',' + top + 'L0,' + h + 'Z"/>';
    if (fo) s += '<path class="uv-wave__fade" d="M' + w + ',0L' + (w - fo) + ',' + top + 'L' + w + ',' + h + 'Z"/>';
    s += '<polyline class="uv-wave__vol" points="0,' + h + ' ' + fi + ',' + top + ' ' + (w - fo) + ',' + top + ' ' + w + ',' + h + '"/>';
    return s + '</svg>';
  }

  function ruler(m, width) {
    var pps = m.pps, fps = m.fps || 25, s = '<div class="uv-ruler" style="width:' + width + 'px">';
    var steps = [1 / fps, 5 / fps, 0.5, 1, 2, 5, 10, 30, 60], minor = steps[0];
    for (var i = 0; i < steps.length; i++) { if (steps[i] * pps >= 8) { minor = steps[i]; break; } }
    var major = minor * (minor < 1 ? (minor === 0.5 ? 2 : 5) : 5);
    while (major * pps < 64) major *= 2;
    if (m.range) s += '<div class="uv-ruler__range" style="left:' + (m.range[0] * pps) + 'px;width:' + ((m.range[1] - m.range[0]) * pps) + 'px"></div>';
    for (var t = 0; t * pps <= width; t += minor) {
      var isMaj = Math.abs(t / major - Math.round(t / major)) < 1e-6;
      s += '<div class="uv-ruler__tick' + (isMaj ? ' uv-ruler__tick--major' : '') + '" style="left:' + (t * pps).toFixed(1) + 'px"></div>';
      if (isMaj) s += '<div class="uv-ruler__label" style="left:' + (t * pps).toFixed(1) + 'px">' + (major < 1 ? tc(t, fps) : tc(t, fps, true)) + '</div>';
    }
    (m.beats || []).forEach(function (b) { s += '<div class="uv-ruler__beat" style="left:' + (b * pps) + 'px"></div>'; });
    (m.markers || []).forEach(function (mk) { s += '<div class="uv-marker uv-marker--' + (mk.color || 'amber') + '" style="left:' + (mk.t * pps) + 'px" title="' + esc(mk.label || '') + '">' + esc(mk.label || '') + '</div>'; });
    return s + '</div>';
  }

  var KIND_ICON = { video: 'film', audio: 'music', overlay: 'title' };
  function trackHeader(tr, h) {
    var s = '<div class="uv-th uv-th--' + tr.kind + (tr.selected ? ' is-selected' : '') + (tr.hover ? ' is-hover' : '') + '" style="height:' + h + 'px">';
    s += '<span class="uv-th__id">' + esc(tr.id) + '</span>' + '<span class="uv-th__name">' + esc(tr.name) + '</span>';
    if (tr.kind === 'video' && tr.magnetic !== undefined) s += '<span class="uv-th__tg' + (tr.magnetic ? ' is-on' : '') + '" title="Magnetic: close gaps automatically">' + icon('magnet') + '</span>';
    if (tr.kind !== 'audio') s += '<span class="uv-th__tg' + (tr.hidden ? ' is-warn' : '') + '" title="Show or hide">' + icon(tr.hidden ? 'eyeOff' : 'eye') + '</span>';
    if (tr.kind !== 'overlay') s += '<span class="uv-th__tg' + (tr.muted ? ' is-warn' : '') + '" title="Mute">' + icon(tr.muted ? 'speakerOff' : 'speaker') + '</span>';
    s += '<span class="uv-th__tg' + (tr.locked ? ' is-on' : '') + '" title="Lock">' + icon(tr.locked ? 'lock' : 'unlock') + '</span>';
    return s + '<span class="uv-th__resize"></span></div>';
  }
  var DEF_H = { video: 64, audio: 48, overlay: 28 };

  function timeline(el, m) {
    var pps = m.pps, width = m.width || Math.round((m.duration || 30) * pps), fps = m.fps || 25;
    var heads = '', lanes = '';
    m.tracks.forEach(function (tr) {
      var h = tr.height || DEF_H[tr.kind];
      heads += trackHeader(tr, h);
      var L = '<div class="uv-lane' + (tr.locked ? ' is-locked' : '') + (tr.drop ? ' is-drop' : '') + '" style="height:' + h + 'px">';
      if (m.range) L += '<div class="uv-lane__range" style="left:' + (m.range[0] * pps) + 'px;width:' + ((m.range[1] - m.range[0]) * pps) + 'px"></div>';
      if (m.showBeats !== false) (m.beats || []).forEach(function (b) { L += '<div class="uv-beat" style="left:' + (b * pps) + 'px"></div>'; });
      (tr.overflow || []).forEach(function (o) {
        L += '<div class="uv-overflow" style="left:' + (o.from * pps) + 'px;width:' + ((o.to - o.from) * pps) + 'px">' + (o.label ? '<span class="uv-overflow__lbl" style="' + (o.side === 'l' ? 'left:0' : 'right:0') + '">' + esc(o.label) + '</span>' : '') + '</div>';
      });
      (tr.items || []).forEach(function (c) {
        var x = Math.round(c.start * pps), w = Math.max(6, Math.round(c.dur * pps) - 1);
        if (tr.kind === 'overlay') L += '<div class="uv-span' + (c.selected ? ' is-selected' : '') + '" style="left:' + x + 'px;width:' + w + 'px">' + icon(c.icon || 'title', 'uv-ico--sm') + esc(c.name) + '</div>';
        else { c._pps = pps; if (tr.kind === 'audio') c.kind = 'audio'; L += clipHTML(c, x, w, h, fps); }
      });
      (tr.transitions || []).forEach(function (t) {
        var w = Math.max(22, t.dur * pps), top = 16 + 3 + Math.round((h - 16 - 6 - 22) / 2);
        L += '<div class="uv-trans' + (t.selected ? ' is-selected' : '') + '" style="left:' + (t.at * pps - w / 2) + 'px;width:' + w + 'px;top:' + top + 'px;margin:0">' + icon('transition', 'uv-ico--sm') + (w > 50 ? '<span>' + t.dur.toFixed(1) + ' s</span>' : '') + (t.selected ? '<span class="uv-trans__edge uv-trans__edge--l"></span><span class="uv-trans__edge uv-trans__edge--r"></span>' : '') + '</div>';
      });
      (tr.fadeKnobs || []).forEach(function (k) { L += '<span class="uv-fadeknob" style="left:' + (k * pps) + 'px;top:' + (16 + 3 + 3) + 'px"></span>'; });
      (tr.inserts || []).forEach(function (i) { L += '<div class="uv-insert' + (i.drop ? ' uv-insert--drop' : '') + '" style="left:' + (i.t * pps) + 'px"></div>'; });
      lanes += L + '</div>';
    });
    var over = '';
    if (m.snap !== undefined) over += '<div class="uv-snap" style="left:' + (m.snap * pps) + 'px"></div>';
    if (m.band) over += '<div class="uv-band" style="left:' + (m.band[0] * pps) + 'px;width:' + ((m.band[1] - m.band[0]) * pps) + 'px;top:' + m.band[2] + 'px;height:' + m.band[3] + 'px"></div>';
    if (m.playhead !== undefined) over += '<div class="uv-playhead" style="left:' + (m.playhead * pps) + 'px"><div class="uv-playhead__cap"></div></div>';
    (m.extra || []).forEach(function (x) { over += x; });
    var corner = '<div class="uv-tl__corner"><span class="uv-tc uv-tc--lg" style="font-size:12px;line-height:16px;padding:0">' + tc(m.playhead || 0, fps) + '</span></div>';
    var html = corner + '<div class="uv-tl__body">' + ruler(m, width) + '<div class="uv-tl__lanes" style="width:' + width + 'px">' + lanes + '</div>' + over + '</div>' + '<div class="uv-tl__heads">' + heads + '</div>';
    if (m.scrollbar !== false) html += '<div class="uv-hscroll"><div class="uv-hscroll__thumb" style="left:8%;width:46%"></div></div>';
    el.classList.add('uv', 'uv-tl');
    el.innerHTML = html;
    return el;
  }

  /* A sample project used by the previews: one magnetic main track (V1), one free overlay track (V2), titles, music. */
  function demoModel(o) {
    var beats = []; for (var b = 0.48; b < 31; b += 0.96) beats.push(+b.toFixed(2));
    var m = {
      fps: 25, pps: 40, duration: 30, playhead: 11.2, beats: beats,
      markers: [{ t: 8, color: 'amber', label: 'Chorus' }, { t: 20.2, color: 'cyan', label: 'Check colour' }],
      tracks: [
        { id: 'T1', kind: 'overlay', name: 'Titles', items: [
          { name: 'Opening title', start: 0.4, dur: 4.2 }, { name: 'Lower third · Anna', start: 18, dur: 5 }, { name: 'Logo', start: 26, dur: 4, icon: 'logo' }] },
        { id: 'V2', kind: 'video', name: 'Overlay video', magnetic: false, height: 48, items: [
          { name: 'Drone_pass.mp4', start: 13.8, dur: 4.6, shots: [4, 0] }] },
        { id: 'V1', kind: 'video', name: 'Main track', magnetic: true, selected: true, items: [
          { kind: 'card', name: 'Black', start: 0, dur: 1.5, color: '#0b0d10' },
          { name: 'Beach_wide.mp4', start: 1.5, dur: 6.5, shots: [0, 0, 5], fx: ['title'] },
          { name: 'Forest_walk.mp4', start: 8, dur: 5.5, shots: [1], selected: true, speed: 1.5 },
          { kind: 'image', name: 'IMG_2041.jpg', start: 13.5, dur: 4, shots: [2] },
          { name: 'Interview_A.mov', start: 17.5, dur: 7, shots: [3], fx: ['fx', 'logo'] },
          { name: 'Sunset.mp4', start: 24.5, dur: 5.5, shots: [5], muted: true }],
          transitions: [{ at: 8, dur: 1 }, { at: 17.5, dur: 1.6 }, { at: 24.5, dur: 0.8 }] },
        { id: 'A1', kind: 'audio', name: 'Music', items: [
          { name: 'Summer Drive · Night Lines  (playlist, 2 songs)', start: 0, dur: 30, fadeIn: 2, fadeOut: 3, crossfades: [{ at: 16, dur: 3 }], seed: 11, showDur: false }] }
      ]
    };
    if (o) for (var k in o) m[k] = o[k];
    return m;
  }

  window.UltraVideo = { demoModel: demoModel, icon: icon, icons: Object.keys(ICONS), timecode: tc, ruler: ruler, trackHeader: trackHeader, clip: clipHTML, timeline: timeline, peaksPath: peaksPath };
})();
