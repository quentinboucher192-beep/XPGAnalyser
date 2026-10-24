// =============================================================================
//  outils/web/web_host.js - 1.12.31 : l'hote JS de l'IHM en marche sur la page
// -----------------------------------------------------------------------------
//  Fait tourner le moteur de l'IHM (ihm-web.wasm, le meme code C++ que IHM
//  Studio) dans le navigateur : il le charge, lui donne le projet (l'archive
//  embarquee) et la police, le demarre, l'avance a chaque image, et rejoue sur un
//  <canvas> le tampon de commandes que les vrais peintres remplissent dans le
//  wasm. Les gestes de l'operateur (souris, doigt, clavier) repartent au moteur.
//
//  Les equipements (Modbus TCP) ne sont pas relies dans cette version de la
//  page : ses variables IHM, ses scripts, ses alarmes et ses vues tournent ; la
//  barre du bas le dit (voir l'aide, sujet page-web).
//
//  Embarque tel quel dans la page exportee : aucun acces Internet.
// =============================================================================
(function (global) {
  'use strict';

  // ---- un petit WASI preview1 pour le navigateur ----------------------------
  function makeWasi() {
    let mem = null;
    const setMemory = (m) => { mem = m; };
    const view = () => new DataView(mem.buffer);
    const u8 = () => new Uint8Array(mem.buffer);
    const dec = new TextDecoder();
    const wasi = {
      clock_time_get(id, prec, outPtr) {
        const ns = BigInt(Math.round((performance.timeOrigin + performance.now()) * 1000)) * 1000n;
        view().setBigUint64(outPtr, ns, true);
        return 0;
      },
      random_get(ptr, len) { crypto.getRandomValues(u8().subarray(ptr, ptr + len)); return 0; },
      fd_write(fd, iov, cnt, wrote) {
        const dv = view();
        let total = 0, out = '';
        for (let i = 0; i < cnt; i++) {
          const p = dv.getUint32(iov + i * 8, true), n = dv.getUint32(iov + i * 8 + 4, true);
          out += dec.decode(u8().subarray(p, p + n));
          total += n;
        }
        if (out.trim()) console.log('[ihm]', out.replace(/\n$/, ''));
        dv.setUint32(wrote, total, true);
        return 0;
      },
      fd_close() { return 0; }, fd_seek() { return 0; }, fd_fdstat_get() { return 0; },
      fd_fdstat_set_flags() { return 0; }, fd_prestat_get() { return 8; }, fd_prestat_dir_name() { return 8; },
      fd_read() { return 0; }, fd_filestat_get() { return 0; }, fd_sync() { return 0; },
      environ_sizes_get(c, s) { view().setUint32(c, 0, true); view().setUint32(s, 0, true); return 0; },
      environ_get() { return 0; },
      args_sizes_get(c, s) { view().setUint32(c, 0, true); view().setUint32(s, 0, true); return 0; },
      args_get() { return 0; },
      proc_exit(code) { throw new Error('le moteur s\'est arrete (' + code + ')'); },
      poll_oneoff() { return 0; }, sched_yield() { return 0; },
      path_open() { return 44; }, path_filestat_get() { return 44; }, path_create_directory() { return 44; },
      path_unlink_file() { return 44; }, path_rename() { return 44; }, path_remove_directory() { return 44; },
      fd_readdir() { return 8; },
    };
    return { wasi, setMemory };
  }

  // ---- le moteur ------------------------------------------------------------
  //  Les scripts DETACHES (WAIT) sont des coroutines Asyncify : le moteur les
  //  suspend et les reprend par ces imports (src/web/WebFiber.cpp).
  async function instantiate(wasmBytes, extraEnv) {
    const { wasi, setMemory } = makeWasi();
    let EX = null;
    const env = Object.assign({
      xpg_host_sleep(buf) {
        const st = EX.asyncify_get_state();
        if (st === 0) EX.asyncify_start_unwind(buf);
        else if (st === 2) EX.asyncify_stop_rewind();
      },
      xpg_fiber_stop_unwind() { EX.asyncify_stop_unwind(); },
      xpg_fiber_start_rewind(buf) { EX.asyncify_start_rewind(buf); },
      xpg_fiber_state() { return EX.asyncify_get_state(); },
      xpg_get_sp() { return EX.__stack_pointer.value; },
      xpg_set_sp(v) { EX.__stack_pointer.value = v; },
    }, extraEnv || {});
    const mod = await WebAssembly.compile(wasmBytes);
    for (const im of WebAssembly.Module.imports(mod))
      if (im.module === 'env' && im.kind === 'function' && !env[im.name]) env[im.name] = () => 0;
    const inst = await WebAssembly.instantiate(mod, { wasi_snapshot_preview1: wasi, env });
    EX = inst.exports;
    setMemory(EX.memory);
    if (EX._initialize) EX._initialize();
    return EX;
  }

  // ---- le tampon de commandes -> canvas 2D ----------------------------------
  //  Les codes suivent app::web::Op (src/web/WebRenderer.hpp).
  const OP = { End: 0, ClipPush: 1, ClipPop: 2, FillRect: 3, StrokeRect: 4, RoundRect: 5, Line: 6, Tris: 7, Text: 8, Image: 9 };

  function replay(ctx, buf, images, family) {
    const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    const dec = new TextDecoder();
    let o = 0, clips = 0;
    const F = () => { const v = dv.getFloat32(o, true); o += 4; return v; };
    const I = () => { const v = dv.getInt32(o, true); o += 4; return v; };
    const B = () => dv.getUint8(o++);
    const C = () => { const s = 'rgba(' + dv.getUint8(o) + ',' + dv.getUint8(o + 1) + ',' + dv.getUint8(o + 2) + ',' + (dv.getUint8(o + 3) / 255) + ')'; o += 4; return s; };
    const n = buf.length;
    while (o < n) {
      const op = B();
      if (op === OP.ClipPush) {
        const x = F(), y = F(), w = F(), h = F();
        ctx.save(); ctx.beginPath(); ctx.rect(x, y, w, h); ctx.clip(); clips++;
      } else if (op === OP.ClipPop) {
        if (clips > 0) { ctx.restore(); clips--; }
      } else if (op === OP.FillRect) {
        const x = F(), y = F(), w = F(), h = F(); ctx.fillStyle = C(); ctx.fillRect(x, y, w, h);
      } else if (op === OP.StrokeRect) {
        const x = F(), y = F(), w = F(), h = F(), c = C(), t = F();
        ctx.strokeStyle = c; ctx.lineWidth = t; ctx.strokeRect(x + t / 2, y + t / 2, Math.max(0, w - t), Math.max(0, h - t));
      } else if (op === OP.RoundRect) {
        const x = F(), y = F(), w = F(), h = F(), c = C(), rad = F();
        ctx.fillStyle = c; ctx.beginPath();
        if (ctx.roundRect && rad > 0) ctx.roundRect(x, y, w, h, rad); else ctx.rect(x, y, w, h);
        ctx.fill();
      } else if (op === OP.Line) {
        const x1 = F(), y1 = F(), x2 = F(), y2 = F(), c = C(), t = F();
        ctx.strokeStyle = c; ctx.lineWidth = t; ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
      } else if (op === OP.Tris) {
        // Les triangles de meme couleur qui se suivent : un seul chemin (pas de joint visible).
        const count = I();
        let path = null, color = null;
        for (let i = 0; i < count; i++) {
          const x0 = F(), y0 = F(), c0 = C(), x1 = F(), y1 = F(); o += 4;
          const x2 = F(), y2 = F(); o += 4;
          if (c0 !== color) {
            if (path) { ctx.fillStyle = color; ctx.fill(path); }
            path = new Path2D(); color = c0;
          }
          path.moveTo(x0, y0); path.lineTo(x1, y1); path.lineTo(x2, y2); path.closePath();
        }
        if (path) { ctx.fillStyle = color; ctx.fill(path); }
      } else if (op === OP.Text) {
        const x = F(), y = F(), size = F(), c = C(), ang = F(), px = F(), py = F(), width = F(), ascent = F(), len = I();
        const s = dec.decode(buf.subarray(o, o + len)); o += len;
        ctx.save();
        if (Math.abs(ang) > 0.01) { ctx.translate(px, py); ctx.rotate(ang * Math.PI / 180); ctx.translate(-px, -py); }
        ctx.fillStyle = c; ctx.textBaseline = 'alphabetic';
        ctx.font = size.toFixed(1) + 'px ' + family;
        // La largeur que le wasm a mesuree (la meme police) : le texte tombe ou l'appli le pose.
        const natural = ctx.measureText(s).width;
        if (width > 1 && natural > 1 && Math.abs(natural - width) > 0.5) {
          ctx.translate(x, y + ascent); ctx.scale(width / natural, 1); ctx.fillText(s, 0, 0);
        } else {
          ctx.fillText(s, x, y + ascent);
        }
        ctx.restore();
      } else if (op === OP.Image) {
        const id = I(), x = F(), y = F(), w = F(), h = F(), ang = F(), fh = B(), fv = B();
        const alpha = dv.getUint8(o + 3) / 255; o += 4;
        const img = images.get(id);
        if (img) {
          ctx.save();
          ctx.translate(x + w / 2, y + h / 2);
          if (Math.abs(ang) > 0.01) ctx.rotate(ang * Math.PI / 180);
          ctx.scale(fh ? -1 : 1, fv ? -1 : 1);
          ctx.globalAlpha = alpha;
          ctx.drawImage(img, -w / 2, -h / 2, w, h);
          ctx.restore();
        }
      } else {
        break;   // fin, ou commande inconnue
      }
    }
    while (clips > 0) { ctx.restore(); clips--; }
  }

  // Les images neuves (id, w, h, RGBA) : un petit canvas chacune, dessine tel quel.
  function takeImages(bytes, images) {
    const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let o = 0;
    while (o + 12 <= bytes.length) {
      const id = dv.getUint32(o, true), w = dv.getUint32(o + 4, true), h = dv.getUint32(o + 8, true);
      o += 12;
      const n = w * h * 4;
      if (o + n > bytes.length || w === 0 || h === 0) break;
      const c = document.createElement('canvas');
      c.width = w; c.height = h;
      c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(bytes.slice(o, o + n)), w, h), 0, 0);
      images.set(id, c);
      o += n;
    }
  }

  // ---- les octets embarques --------------------------------------------------
  function fromBase64(text) {
    const bin = atob(text.replace(/\s+/g, ''));
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }
  async function gunzip(bytes) {
    if (typeof DecompressionStream === 'undefined') throw new Error('ce navigateur ne sait pas decompresser (DecompressionStream)');
    const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  // ---- les touches du navigateur -> jetons de l'IHM (hmi/HmiKeys.hpp) --------
  function tokenOf(e) {
    const c = e.code || '';
    if (/^Key[A-Z]$/.test(c)) return c.slice(3);
    if (/^Digit[0-9]$/.test(c)) return c;
    if (/^Numpad[0-9]$/.test(c)) return 'Digit' + c.slice(6);
    if (/^F([1-9]|1[0-2])$/.test(c)) return c;
    const map = { Enter: 'Enter', NumpadEnter: 'Enter', Escape: 'Escape', Space: 'Space', Tab: 'Tab', Backspace: 'Backspace',
      Delete: 'Delete', Insert: 'Insert', Home: 'Home', End: 'End', PageUp: 'PageUp', PageDown: 'PageDown',
      ArrowUp: 'Up', ArrowDown: 'Down', ArrowLeft: 'Left', ArrowRight: 'Right' };
    return map[c] || '';
  }
  // hmi::EditKey : Backspace, Delete, Enter, Escape, Tab, BackTab, Left, Right, Home, End.
  function editKeyOf(e) {
    switch (e.key) {
      case 'Backspace': return 0; case 'Delete': return 1; case 'Enter': return 2; case 'Escape': return 3;
      case 'Tab': return e.shiftKey ? 5 : 4; case 'ArrowLeft': return 6; case 'ArrowRight': return 7;
      case 'Home': return 8; case 'End': return 9; default: return -1;
    }
  }

  // ---- faire tourner l'IHM sur la page ---------------------------------------
  //  opts : { canvas, wasm (octets du .wasm), font (octets du TTF), archive (octets
  //  du .xihm), retained (les variables remanentes gardees, texte), onStatus(etat),
  //  onError(texte), env (des imports en plus) }
  async function run(opts) {
    const ex = await instantiate(opts.wasm, opts.env);
    const mem = () => new Uint8Array(ex.memory.buffer);
    const enc = new TextEncoder(), dec = new TextDecoder();
    const cstr = (p) => { const m = mem(); let e = p; while (m[e]) e++; return dec.decode(m.subarray(p, e)); };
    const push = (bytes) => { const p = ex.xpg_malloc(bytes.length || 1); mem().set(bytes, p); return p; };
    const withStr = (s, fn) => { const b = enc.encode(s + '\0'); const p = push(b); try { return fn(p); } finally { ex.xpg_free(p); } };

    // La police : la meme pour les mesures (wasm) et le dessin (canvas).
    const family = "'XpgIhm', 'Segoe UI', Tahoma, 'DejaVu Sans', Arial, sans-serif";
    if (opts.font && opts.font.length) {
      const fp = push(opts.font); ex.xpg_set_font(fp, opts.font.length);   // gardee par le moteur (copie)
      ex.xpg_free(fp);
      try { const face = new FontFace('XpgIhm', opts.font); await face.load(); document.fonts.add(face); } catch (e) { console.warn('police', e); }
    }
    const zp = push(opts.archive);
    const ok = ex.xpg_load(zp, opts.archive.length);
    ex.xpg_free(zp);
    if (!ok) throw new Error('le projet ne se charge pas : ' + cstr(ex.xpg_last_error()));
    // Les variables remanentes gardees par le navigateur (l'ouverture d'avant) : rendues au demarrage.
    if (opts.retained && ex.xpg_set_retained) withStr(opts.retained, (p) => ex.xpg_set_retained(p));
    const t0 = performance.now();
    const now = () => (performance.now() - t0) / 1000;
    ex.xpg_start(now());

    const canvas = opts.canvas, ctx = canvas.getContext('2d', { alpha: false });
    const images = new Map();
    let W = 0, H = 0, dpr = 1;
    function resize() {
      dpr = Math.max(1, window.devicePixelRatio || 1);
      const r = canvas.getBoundingClientRect();
      W = Math.max(1, Math.round(r.width * dpr)); H = Math.max(1, Math.round(r.height * dpr));
      if (canvas.width !== W || canvas.height !== H) { canvas.width = W; canvas.height = H; }
    }
    resize();
    window.addEventListener('resize', resize);

    let frames = 0, lastMs = 0, avgMs = 0;
    const recent = [];                     // les 120 dernieres images : la mediane
    function frame() {
      const a = performance.now();
      const ptr = ex.xpg_frame(now(), W, H);
      const len = ex.xpg_frame_len();
      const il = ex.xpg_images_len();
      if (il > 0) { takeImages(new Uint8Array(ex.memory.buffer, ex.xpg_images(), il).slice(), images); ex.xpg_images_done(); }
      const cmd = new Uint8Array(ex.memory.buffer, ptr, len).slice();
      replay(ctx, cmd, images, family);
      lastMs = performance.now() - a;
      avgMs = frames ? avgMs * 0.95 + lastMs * 0.05 : lastMs;   // la moyenne glissante (une vingtaine d'images)
      recent.push(lastMs);
      if (recent.length > 120) recent.shift();
      frames++;
    }
    let raf = 0;
    function loop() { try { frame(); } catch (e) { if (opts.onError) opts.onError(String(e)); return; } raf = requestAnimationFrame(loop); }
    raf = requestAnimationFrame(loop);
    // Onglet cache : plus d'image, mais l'IHM continue (alarmes, scripts, liaisons).
    setInterval(() => { if (document.hidden) ex.xpg_tick(now()); }, 250);

    // ---- les gestes ----------------------------------------------------------
    let lastDown = 0, lastX = 0, lastY = 0, clicks = 1;
    const pos = (e) => { const r = canvas.getBoundingClientRect(); return [(e.clientX - r.left) * dpr, (e.clientY - r.top) * dpr]; };
    canvas.addEventListener('pointerdown', (e) => {
      canvas.focus();
      try { canvas.setPointerCapture(e.pointerId); } catch (_) {}
      const [x, y] = pos(e), t = performance.now();
      clicks = (t - lastDown < 400 && Math.abs(x - lastX) < 8 * dpr && Math.abs(y - lastY) < 8 * dpr) ? clicks + 1 : 1;
      lastDown = t; lastX = x; lastY = y;
      ex.xpg_mouse(0, x, y, e.button === 2 ? 1 : e.button === 1 ? 2 : 0, clicks, now());
      e.preventDefault();
    });
    canvas.addEventListener('pointermove', (e) => { const [x, y] = pos(e); ex.xpg_mouse(1, x, y, 0, 0, now()); });
    canvas.addEventListener('pointerup', (e) => { const [x, y] = pos(e); ex.xpg_mouse(2, x, y, e.button === 2 ? 1 : e.button === 1 ? 2 : 0, 0, now()); });
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    canvas.addEventListener('wheel', (e) => { ex.xpg_wheel(-e.deltaY / 100); e.preventDefault(); }, { passive: false });
    const mods = (e) => (e.ctrlKey ? 1 : 0) | (e.shiftKey ? 2 : 0) | (e.altKey ? 4 : 0);
    canvas.tabIndex = 0;
    canvas.addEventListener('keydown', (e) => {
      const tok = tokenOf(e);
      const taken = withStr(tok, (p) => ex.xpg_key(p, mods(e), 1, e.repeat ? 1 : 0, now()));
      if (taken) { e.preventDefault(); return; }
      if (ex.xpg_focused()) {
        const k = editKeyOf(e);
        if (k >= 0) { ex.xpg_edit_key(k, now()); e.preventDefault(); return; }
        if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) { withStr(e.key, (p) => ex.xpg_text(p, now())); e.preventDefault(); return; }
      }
      // Ctrl+A sur l'IHM : pas la selection de toute la page (la barre, la documentation).
      if ((e.ctrlKey || e.metaKey) && (e.key === 'a' || e.key === 'A')) e.preventDefault();
    });
    canvas.addEventListener('keyup', (e) => { withStr(tokenOf(e), (p) => ex.xpg_key(p, mods(e), 0, 0, now())); });

    // ---- ce que la barre montre ----------------------------------------------
    const api = {
      exports: ex,
      now,
      lastFrameMs: () => lastMs,
      averageFrameMs: () => avgMs,
      medianFrameMs: () => { const s = recent.slice().sort((a, b) => a - b); return s.length ? s[s.length >> 1] : 0; },
      frames: () => frames,
      read: (name) => withStr(name, (p) => cstr(ex.xpg_read(p))),
      write: (name, value) => withStr(name, (pn) => withStr(value, (pv) => ex.xpg_write(pn, pv))),
      view: () => cstr(ex.xpg_current_view_name()),
      user: () => cstr(ex.xpg_user()),
      equipments: () => { try { return JSON.parse(cstr(ex.xpg_equipments_json())); } catch (_) { return []; } },
      journal: () => cstr(ex.xpg_journal()),
      retained: () => (ex.xpg_retained ? cstr(ex.xpg_retained()) : ''),
      objectCenter: (name) => {
        const s = withStr(name, (p) => cstr(ex.xpg_object_center(p)));
        if (!s) return null;
        const [x, y] = s.split(' ').map(Number);
        return { x: x / dpr, y: y / dpr };   // en pixels CSS du canvas
      },
      objectBox: (name) => {
        const s = withStr(name, (p) => cstr(ex.xpg_object_box ? ex.xpg_object_box(p) : 0));
        if (!s) return null;
        const [x, y, w, h] = s.split(' ').map(Number);
        return { x: x / dpr, y: y / dpr, w: w / dpr, h: h / dpr };
      },
      stop: () => { cancelAnimationFrame(raf); ex.xpg_stop(now()); },
    };
    if (opts.onStatus) setInterval(() => opts.onStatus(api), 500);
    return api;
  }

  global.XpgWebHost = { instantiate, replay, takeImages, fromBase64, gunzip, run, OP };
})(typeof window !== 'undefined' ? window : globalThis);
