/* 1.47.0 - planches 08, 09, 10 : les petits graphiques des statistiques, dessines dans la page (SVG).
   La methode (dataviz) : un seul axe par graphique, traits de 2 px, barres de 24 px au plus au bout arrondi (4 px),
   2 px de surface entre les morceaux, grille fine et discrete (jamais de pointilles), la comparaison en gris,
   une legende des 2 series et plus, peu d'etiquettes directes. Le survol : une regle verticale (courbes) ou la bande
   de la colonne (barres), et une bulle qui donne toutes les series a cet endroit. Un clic filtre la liste.
   Les couleurs : extra.css (en tete). Les textes sont toujours en encre, jamais de la couleur de la serie. */
(function () {
  'use strict';
  var NS = 'http://www.w3.org/2000/svg';
  var C = { grid: '#eef1f5', base: '#d5dce6', axt: '#94a3b8', ink: '#0f172a', ink2: '#334155', mut: '#64748b',
    cmp: '#a3adbf', blue: '#2a78d6', orange: '#eb6834', hover: '#f1f4f8', rule: '#94a3b8' };
  var nf0 = new Intl.NumberFormat('fr-FR', { maximumFractionDigits: 0 });
  var nf1 = new Intl.NumberFormat('fr-FR', { maximumFractionDigits: 1 });
  var X = window.X = { C: C };

  X.n = function (v) { return nf0.format(Math.round(v)); };
  X.n1 = function (v) { return nf1.format(v); };
  X.e = function (v) { return nf0.format(Math.round(v)) + ' €'; };
  X.p = function (v) { return nf0.format(Math.round(v)) + ' %'; };
  X.h = function (v) { var h = Math.floor(v), m = Math.round((v - h) * 60); if (m === 60) { h++; m = 0; } return h + ' h' + (m ? ' ' + (m < 10 ? '0' : '') + m : ''); };
  X.sum = function (a) { var s = 0; a.forEach(function (v) { s += (+v || 0); }); return s; };
  X.ico = function (n, cls, st) { return '<svg class="ico ' + (cls || '') + '"' + (st ? ' style="' + st + '"' : '') + '><use href="#i-' + n + '"/></svg>'; };
  X.esc = function (s) { var d = document.createElement('div'); d.textContent = s; return d.innerHTML; };
  // l'ecart (+12 %) : sa couleur = le sens x « monter est-il bien ? »
  X.dl = function (cur, prev, upGood, txt) {
    var p = prev ? Math.round((cur - prev) * 100 / prev) : 0;
    var cls = p === 0 ? 'neu' : ((p > 0) === (upGood !== false) ? 'good' : 'bad');
    return '<span class="dl ' + cls + '">' + X.ico(p > 0 ? 'trend' : p < 0 ? 'trenddown' : 'minus') +
      (txt || ((p > 0 ? '+' : p < 0 ? '−' : '') + Math.abs(p) + ' %')) + '</span>';
  };

  function S(tag, at, par) {
    var e = document.createElementNS(NS, tag);
    for (var k in at) if (at[k] != null) e.setAttribute(k, at[k]);
    if (par) par.appendChild(e);
    return e;
  }
  function T(par, x, y, s, o) {
    o = o || {};
    var t = S('text', { x: x, y: y, 'font-size': o.size || 11, fill: o.fill || C.axt, 'text-anchor': o.anchor || 'start',
      'font-weight': o.weight || 500 }, par);
    t.textContent = s;
    return t;
  }
  X.S = S; X.T = T;

  // une colonne : le bout de la donnee arrondi (4 px), la base carree ; y1 = le bout, y0 = la base (vers le haut ou le bas)
  function colpath(x, w, y1, y0, r) {
    var h = Math.abs(y0 - y1);
    if (h < 0.4) return null;
    r = Math.min(r, h, w / 2);
    var f = function (v) { return v.toFixed(1); };
    if (y1 < y0) return 'M' + f(x) + ',' + f(y0) + 'V' + f(y1 + r) + 'Q' + f(x) + ',' + f(y1) + ' ' + f(x + r) + ',' + f(y1) +
      'H' + f(x + w - r) + 'Q' + f(x + w) + ',' + f(y1) + ' ' + f(x + w) + ',' + f(y1 + r) + 'V' + f(y0) + 'Z';
    return 'M' + f(x) + ',' + f(y0) + 'V' + f(y1 - r) + 'Q' + f(x) + ',' + f(y1) + ' ' + f(x + r) + ',' + f(y1) +
      'H' + f(x + w - r) + 'Q' + f(x + w) + ',' + f(y1) + ' ' + f(x + w) + ',' + f(y1 - r) + 'V' + f(y0) + 'Z';
  }
  X.colpath = colpath;

  // des graduations rondes : 1, 2, 2,5, 5 x 10^k
  function step(raw) {
    if (raw <= 0) return 1;
    var p = Math.pow(10, Math.floor(Math.log10(raw))), m = raw / p;
    return (m <= 1 ? 1 : m <= 2 ? 2 : m <= 2.5 ? 2.5 : m <= 5 ? 5 : 10) * p;
  }
  X.ticks = function (lo, hi, n) {
    var st = step((hi - lo) / (n || 3)), a = Math.floor(lo / st + 1e-9) * st, b = Math.ceil(hi / st - 1e-9) * st, t = [];
    for (var v = a; v <= b + st / 1e6; v += st) t.push(Math.round(v * 1e6) / 1e6);
    return t;
  };

  // la carte : son titre, sa valeur, son ecart, sa legende ; rend l'element ou dessiner
  X.card = function (id, c) {
    var el = typeof id === 'string' ? document.getElementById(id) : id;
    el.classList.add('gc', 'x');
    var h = '<div class="ch"><div class="ct">' + c.t + (c.ts ? '<small>' + c.ts + '</small>' : '') + '</div>' +
      '<span class="tv" title="Voir en tableau">' + X.ico(c.ico || 'list') + '</span></div>';
    if (c.v != null) h += '<div class="cv"><b>' + c.v + '</b>' + (c.dl || '') + (c.cc ? '<span class="cc">' + c.cc + '</span>' : '') + '</div>';
    if (c.lg) h += '<div class="lg">' + c.lg.map(function (l) {
      var k = l[0] === 'ln' ? '<i class="ln" style="background:' + l[1] + '"></i>' : '<i style="background:' + l[1] + '"></i>';
      return '<span>' + k + l[2] + '</span>';
    }).join('') + '</div>';
    if (c.side) h += '<div class="row2"><div class="plot"></div><div class="vl">' + c.side + '</div></div>';
    else h += '<div class="plot"></div>';
    el.innerHTML = h;
    return el.querySelector('.plot');
  };
  // la legende avec ses chiffres, a cote du graphique
  X.side = function (rows) {
    return rows.map(function (r) {
      var k = r[0] === 'ln' ? '<i class="ln" style="background:' + r[1] + '"></i>' : r[0] === '' ? '<span></span>' : '<i style="background:' + r[1] + '"></i>';
      return '<div class="r' + (r[4] ? ' ' + r[4] : '') + '">' + k + '<span>' + r[2] + '</span><span>' + r[3] + '</span></div>';
    }).join('');
  };

  /* le graphique
     o.kind : 'line' | 'col' (une serie de colonnes) | 'stack' | 'div' (au-dessus / au-dessous de 0) | 'group'
     o.x : les libelles courts (un par point) ; o.xl : les libelles longs (la bulle) ; o.xt : [[i, libelle]] sur l'axe
     o.s : [{n: nom, c: couleur | fonction(i), v: [..], cmp: comparaison (gris, 1,5 px), area, dot (le point de fin), lab (etiquette de fin),
            neg: sous la ligne de 0 (div), f: format, line: courbe par-dessus des barres, lw}]
     o.yf : format des graduations ; o.vf : format des valeurs (bulle) ; o.ymax, o.ymin, o.nt (nb de graduations)
     o.tip : l'index montre au chargement (l'exemple) ; o.tipx : texte en plus ; o.click : la liste qu'un clic filtre
     o.ref : [{v, l}] une valeur de reference (trait plein, fin) ; o.vm : [{i, l}] des reperes verticaux ; o.partial : index en cours (pale) */
  X.chart = function (host, o) {
    if (typeof host === 'string') host = document.getElementById(host);
    var W = o.w || host.clientWidth, H = o.h || host.clientHeight;
    var svg = S('svg', { width: W, height: H, viewBox: '0 0 ' + W + ' ' + H, role: 'img' }, host);
    svg.style.fontFamily = 'Inter, system-ui, sans-serif';
    var n = o.x.length, kind = o.kind || 'line', ser = o.s;
    var band = kind !== 'line' || o.band;
    var lo = o.ymin != null ? o.ymin : 0, hi = 0;
    var bars = ser.filter(function (s) { return !s.cmp && !s.line && kind !== 'line'; });
    var lines = ser.filter(function (s) { return kind === 'line' || s.cmp || s.line; });
    var i, j, v;
    if (kind === 'stack' || kind === 'div') {
      var neg = 0;
      for (i = 0; i < n; i++) {
        var up = 0, dn = 0;
        bars.forEach(function (s) { var q = s.v[i] || 0; if (s.neg) dn += q; else up += q; });
        hi = Math.max(hi, up); neg = Math.max(neg, dn);
      }
      if (kind === 'div') lo = -neg;
    } else {
      bars.forEach(function (s) { s.v.forEach(function (q) { if (q != null) hi = Math.max(hi, q); }); });
    }
    lines.forEach(function (s) { s.v.forEach(function (q) { if (q != null) hi = Math.max(hi, q); }); });
    if (o.ref) o.ref.forEach(function (r) { hi = Math.max(hi, r.v); });
    if (o.ymax != null) hi = o.ymax;
    var tk;
    if (o.ticks) tk = o.ticks;
    else if (kind === 'div') {
      var st = step(Math.max(hi, -lo) / 2);
      tk = []; for (v = -Math.ceil(-lo / st - 1e-9) * st; v <= Math.ceil(hi / st - 1e-9) * st + 1e-9; v += st) tk.push(Math.round(v * 1e6) / 1e6);
    } else tk = X.ticks(lo, hi, o.nt || 3);
    lo = Math.min(lo, tk[0]); hi = Math.max(hi, tk[tk.length - 1]);
    var yf = o.yf || function (q) { return X.n(q); };
    var maxl = 0; tk.forEach(function (t) { maxl = Math.max(maxl, String(yf(t)).length); });
    var pl = o.pl != null ? o.pl : 8 + maxl * 6.3, pr = o.pr != null ? o.pr : 4, pt = o.pt != null ? o.pt : 6, pb = o.pb != null ? o.pb : 17;
    var pw = W - pl - pr, ph = H - pt - pb;
    var Y = function (q) { return pt + ph * (1 - (q - lo) / (hi - lo)); };
    var Xc = function (k) { return band ? pl + pw * (k + 0.5) / n : pl + pw * (n === 1 ? 0.5 : k / (n - 1)); };
    var slot = pw / n;
    // la grille
    var gG = S('g', {}, svg);
    tk.forEach(function (t) {
      var y = Math.round(Y(t)) + 0.5;
      S('line', { x1: pl, x2: W - pr, y1: y, y2: y, stroke: t === 0 ? C.base : C.grid, 'stroke-width': 1 }, gG);
      if (!o.noy) T(gG, pl - 6, y + 3.5, yf(t), { anchor: 'end' });
    });
    if (lo === 0 && tk.indexOf(0) < 0) { var yb = Math.round(Y(0)) + 0.5; S('line', { x1: pl, x2: W - pr, y1: yb, y2: yb, stroke: C.base }, gG); }
    // l'axe du temps
    var xt = o.xt;
    if (!xt) { xt = []; var k = Math.max(1, Math.round(n / 4)); for (i = 0; i < n; i += k) xt.push([i, o.x[i]]); }
    xt.forEach(function (p) {
      var a = 'middle', x = Xc(p[0]);
      if (!band && p[0] === 0) a = 'start';
      if (!band && p[0] === n - 1) a = 'end';
      if (p[2]) a = p[2];
      T(gG, x, H - pb + 13.5, p[1], { anchor: a });
    });
    var gBg = S('g', {}, svg), gM = S('g', {}, svg), gL = S('g', {}, svg), gH = S('g', {}, svg);
    // les reperes verticaux (une publication, aujourd'hui...)
    (o.vm || []).forEach(function (m) {
      var x = Math.round(m.x != null ? m.x : Xc(m.i)) + 0.5;
      S('line', { x1: x, x2: x, y1: pt - 14, y2: pt + ph, stroke: m.c || '#cbd5e1', 'stroke-width': 1 }, gBg);
      if (m.l) T(gBg, x + (m.a === 'end' ? -4 : m.a === 'middle' ? 0 : 4), pt - 5, m.l, { size: 10.5, fill: m.tc || C.mut, anchor: m.a || 'start', weight: 600 });
    });
    // les barres
    var bw;
    if (kind === 'group') {
      var kk = bars.length;
      bw = Math.min(o.bw || 12, (slot * 0.74 - (kk - 1) * 2) / kk);
      bars.forEach(function (s, si) {
        for (i = 0; i < n; i++) {
          v = s.v[i]; if (!v) continue;
          var x0 = Xc(i) - (kk * bw + (kk - 1) * 2) / 2 + si * (bw + 2);
          var d = colpath(x0, bw, Y(v), Y(0), 4);
          if (d) S('path', { d: d, fill: typeof s.c === 'function' ? s.c(i) : s.c, opacity: o.partial != null && i >= o.partial ? 0.45 : null }, gM);
        }
      });
    } else if (kind !== 'line') {
      bw = Math.min(o.bw || 24, slot * (o.frac || 0.62));
      for (i = 0; i < n; i++) {
        var x = Xc(i) - bw / 2, op = o.partial != null && i >= o.partial ? 0.45 : null;
        ['up', 'dn'].forEach(function (dir) {
          var parts = bars.filter(function (s) { return (dir === 'dn') === !!s.neg && (s.v[i] || 0) > 0; });
          var base = 0;
          parts.forEach(function (s, pj) {
            var q = s.v[i], last = pj === parts.length - 1, gap = pj ? 1 : 0;
            var yb, yt;
            if (dir === 'up') { yb = Y(base) - gap; yt = Y(base + q) + (last ? 0 : 1); }
            else { yb = Y(-base) + gap; yt = Y(-(base + q)) - (last ? 0 : 1); }
            var d = colpath(x, bw, yt, yb, last ? 4 : 0);
            if (d) S('path', { d: d, fill: typeof s.c === 'function' ? s.c(i) : s.c, opacity: op }, gM);
            base += q;
          });
        });
      }
    }
    // les courbes (la comparaison d'abord, dessous)
    var ordered = lines.filter(function (s) { return s.cmp; }).concat(lines.filter(function (s) { return !s.cmp; }));
    ordered.forEach(function (s) {
      var segs = [], cur = [];
      s.v.forEach(function (q, k2) { if (q == null) { if (cur.length) segs.push(cur); cur = []; } else cur.push([Xc(k2), Y(q)]); });
      if (cur.length) segs.push(cur);
      segs.forEach(function (pts) {
        var d = 'M' + pts.map(function (p) { return p[0].toFixed(1) + ',' + p[1].toFixed(1); }).join('L');
        if (s.step) {
          d = 'M' + pts[0][0].toFixed(1) + ',' + pts[0][1].toFixed(1);
          for (var q2 = 1; q2 < pts.length; q2++) d += 'H' + pts[q2][0].toFixed(1) + 'V' + pts[q2][1].toFixed(1);
        }
        if (s.area && pts.length > 1) S('path', { d: d + 'L' + pts[pts.length - 1][0].toFixed(1) + ',' + Y(Math.max(lo, 0)).toFixed(1) + 'L' + pts[0][0].toFixed(1) + ',' + Y(Math.max(lo, 0)).toFixed(1) + 'Z', fill: s.c, opacity: 0.1 }, gL);
        S('path', { d: d, fill: 'none', stroke: s.cmp ? C.cmp : s.c, 'stroke-width': s.lw || (s.cmp ? 1.5 : 2), 'stroke-linejoin': 'round', 'stroke-linecap': 'round' }, gL);
      });
      if ((s.dot || s.lab) && segs.length) {
        var lp = segs[segs.length - 1][segs[segs.length - 1].length - 1];
        if (s.dot !== false) S('circle', { cx: lp[0], cy: lp[1], r: 4, fill: s.cmp ? C.cmp : s.c, stroke: '#fff', 'stroke-width': 2 }, gL);
        if (s.lab) T(gL, lp[0] + (s.lax || 8), lp[1] + 4 + (s.lay || 0), s.lab, { fill: C.ink2, weight: 600, size: 11, anchor: s.laa || 'start' });
      }
    });
    // les references (un plafond...)
    (o.ref || []).forEach(function (r) {
      var y = Math.round(Y(r.v)) + 0.5;
      S('line', { x1: pl, x2: W - pr, y1: y, y2: y, stroke: r.c || C.mut, 'stroke-width': 1 }, gL);
      if (r.l) T(gL, r.a === 'start' ? pl + 4 : W - pr - 2, y - 4, r.l, { size: 10.5, fill: C.mut, anchor: r.a || 'end', weight: 600 });
    });
    (o.marks || []).forEach(function (m) { T(gL, Xc(m.i) + (m.dx || 0), Y(m.v) + (m.dy || -8), m.l, { fill: C.ink2, weight: 600, anchor: m.a || 'middle', size: m.size || 11 }); });

    // le survol
    var card = host.closest('.gc') || host.parentNode;
    var tip = document.createElement('div'); tip.className = 'xtip'; card.appendChild(tip);
    var hit = S('rect', { x: pl - (band ? 0 : 6), y: 0, width: pw + (band ? 0 : 12), height: H, fill: 'transparent' }, svg);
    function show(k) {
      while (gH.firstChild) gH.removeChild(gH.firstChild);
      while (gBg.lastChild && gBg.lastChild.getAttribute('data-h')) gBg.removeChild(gBg.lastChild);
      var x = Xc(k);
      if (band) S('rect', { x: x - slot / 2 + 1, y: pt - 2, width: slot - 2, height: ph + 2, rx: 4, fill: C.hover, 'data-h': 1 }, gBg);
      else S('line', { x1: Math.round(x) + 0.5, x2: Math.round(x) + 0.5, y1: pt - 2, y2: pt + ph, stroke: C.rule, 'stroke-width': 1 }, gH);
      lines.forEach(function (s) {
        var q = s.v[k]; if (q == null) return;
        S('circle', { cx: x, cy: Y(q), r: 4, fill: s.cmp ? C.cmp : s.c, stroke: '#fff', 'stroke-width': 2 }, gH);
      });
      var rows = (o.tipFwd ? ser.slice() : ser.slice().reverse()).filter(function (s) { return !s.notip; });
      var h = '<div class="tt">' + X.esc(o.xl ? o.xl[k] : o.x[k]) + (o.partial != null && k >= o.partial ? ' · en cours' : '') + '</div>';
      rows.forEach(function (s) {
        var q = s.v[k];
        var isLine = kind === 'line' || s.cmp || s.line;
        if ((q == null || (!isLine && q === 0)) && !s.zero) return;
        var f = s.f || o.vf || function (z) { return X.n(z); };
        var col = s.cmp ? C.cmp : (typeof s.c === 'function' ? s.c(k) : s.c);
        h += '<div class="tr"><b>' + X.esc(f(q || 0)) + '</b><i class="' + (isLine ? '' : 'bx') + '" style="background:' + col + '"></i><span>' + X.esc(s.n) + '</span></div>';
      });
      if (o.tipx) { var ex = o.tipx(k); if (ex) h += '<div class="ex">' + ex + '</div>'; }
      if (o.click) h += '<div class="ck">' + X.ico('pointer') + X.esc(typeof o.click === 'function' ? o.click(k) : o.click) + '</div>';
      tip.innerHTML = h;
      tip.style.display = 'block';
      var cr = card.getBoundingClientRect(), hr = svg.getBoundingClientRect();
      var ax = hr.left - cr.left + x, tw = tip.offsetWidth;
      var left = ax + (band ? slot / 2 : 0) + 10 + (o.tipDx || 0);
      if (o.tipSide === 'left' || hr.left + x + tw + 24 > (o.limit || cr.right + 60)) left = ax - (band ? slot / 2 : 0) - 10 - tw;
      tip.style.left = left + 'px';
      tip.style.top = (hr.top - cr.top + (o.tipBottom ? pt + ph - tip.offsetHeight : o.tipTop != null ? o.tipTop : pt)) + 'px';
    }
    function hide() {
      while (gH.firstChild) gH.removeChild(gH.firstChild);
      while (gBg.lastChild && gBg.lastChild.getAttribute('data-h')) gBg.removeChild(gBg.lastChild);
      tip.style.display = 'none';
    }
    hit.addEventListener('pointermove', function (e) {
      var r = svg.getBoundingClientRect(), mx = e.clientX - r.left, k;
      k = band ? Math.floor((mx - pl) / slot) : Math.round((mx - pl) / (pw / Math.max(1, n - 1)));
      k = Math.max(0, Math.min(n - 1, k));
      show(k);
    });
    hit.addEventListener('pointerleave', function () { if (o.tip != null) show(o.tip); else hide(); });
    if (o.tip != null) show(o.tip);
    return { svg: svg, X: Xc, Y: Y, pl: pl, pt: pt, pw: pw, ph: ph, W: W, H: H, slot: slot, bw: bw };
  };

  // les PC de toute la page : dates
  X.ddmm = function (d) { return ('0' + d.getDate()).slice(-2) + '/' + ('0' + (d.getMonth() + 1)).slice(-2); };
  X.addDays = function (d, k) { var r = new Date(d.getTime()); r.setDate(r.getDate() + k); return r; };
  // un petit generateur pseudo-aleatoire (les memes chiffres a chaque rendu)
  X.rng = function (seed) { var s = seed >>> 0; return function () { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; }; };
})();
