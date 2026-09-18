/* CNC3D model gallery.
 *
 * Draws the cartridge's models the way the game draws them, because it reads the same
 * baked pack the game reads. The three rules that make a mesh come out right, all of
 * them lifted from game/cnc_eyes.cpp rather than guessed:
 *
 *   1. UNLIT, MODULATED BY THE VERTEX COLOUR. The N64 never lit these meshes;
 *      GL_LIGHTING appears nowhere in the renderer. The baked per-vertex colour IS
 *      the shading, and the texel is multiplied by it (cnc_eyes.cpp:5545).
 *   2. WRAP IS PER TRIANGLE, NOT PER TEXTURE. The RDP keeps the S and T wrap on the
 *      TILE, so the same sheet is clamped on a tree crown and repeated elsewhere.
 *      wrap = cmS | cmT<<2, bit1 CLAMP (wins), bit0 MIRROR, else REPEAT
 *      (cnc_eyes.cpp:1779).
 *   3. FOUR TRIANGLE MODES. opaque, cutout (alpha test > 0.5), shadow and xlu (both
 *      blended, drawn after the solids, no depth write). Drawing a shadow face solid
 *      is what turns a model into a black slab.
 *
 * UVs arrive pre-scaled by uw/w and uh/h (the exporter does it, matching
 * cnc_eyes.cpp:5510) and are NOT flipped: the pack's V is top-down and so is the PNG.
 */
'use strict';

const DATA = 'data/';
let META = null, GEO = null, EXPORTS = null;
let ASSETS = [], SHOWN = [], BY_ID = {};
let selected = null;

/* ------------------------------------------------------------------ WebGL */
const VS = `
attribute vec3 aPos; attribute vec2 aUv; attribute vec4 aCol;
uniform mat4 uMvp;
varying vec2 vUv; varying vec4 vCol;
void main(){ vUv = aUv; vCol = aCol; gl_Position = uMvp * vec4(aPos, 1.0); }`;

const FS = `
precision mediump float;
uniform sampler2D uTex;
uniform float uHasTex, uCutout, uShade, uFlat;
varying vec2 vUv; varying vec4 vCol;
void main(){
  vec4 t = uHasTex > 0.5 ? texture2D(uTex, vUv) : vec4(1.0);
  vec4 c = uShade > 0.5 ? t * vCol : t;
  if (uFlat > 0.5) c = vec4(0.85, 0.88, 0.92, 1.0);
  if (uCutout > 0.5 && c.a < 0.5) discard;
  gl_FragColor = c;
}`;

function makeGL(canvas) {
  const gl = canvas.getContext('webgl', { alpha: true, antialias: true,
                                          preserveDrawingBuffer: true });
  if (!gl) throw new Error('WebGL unavailable');
  const sh = (type, src) => {
    const s = gl.createShader(type);
    gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
      throw new Error(gl.getShaderInfoLog(s));
    return s;
  };
  const p = gl.createProgram();
  gl.attachShader(p, sh(gl.VERTEX_SHADER, VS));
  gl.attachShader(p, sh(gl.FRAGMENT_SHADER, FS));
  gl.bindAttribLocation(p, 0, 'aPos');
  gl.bindAttribLocation(p, 1, 'aUv');
  gl.bindAttribLocation(p, 2, 'aCol');
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS))
    throw new Error(gl.getProgramInfoLog(p));
  gl.useProgram(p);
  const u = {};
  for (const n of ['uMvp', 'uTex', 'uHasTex', 'uCutout', 'uShade', 'uFlat'])
    u[n] = gl.getUniformLocation(p, n);
  gl.uniform1i(u.uTex, 0);
  gl.enable(gl.DEPTH_TEST);
  const ctx = { gl, prog: p, u, tex: new Map(), buf: new Map(), white: null };
  ctx.white = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, ctx.white);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
                new Uint8Array([255, 255, 255, 255]));
  return ctx;
}

const IMGS = new Map();
function image(name) {
  if (IMGS.has(name)) return IMGS.get(name);
  const p = new Promise(res => {
    const im = new Image();
    im.onload = () => res(im);
    im.onerror = () => res(null);
    im.src = DATA + 'tex/' + name;
  });
  IMGS.set(name, p);
  return p;
}

/* wrap = cmS | cmT<<2; bit1 CLAMP wins, bit0 MIRROR, else REPEAT. A texture object
 * carries its wrap in WebGL, so one is made per (texture, wrap, palette) actually used
 * -- which is exactly the rebind the renderer does when a triangle changes either. */
function wrapMode(gl, cm, pot) {
  if (cm & 2) return gl.CLAMP_TO_EDGE;
  if (!pot) return gl.CLAMP_TO_EDGE;      // WebGL1 forbids repeat on non-power-of-two
  if (cm & 1) return gl.MIRRORED_REPEAT;
  return gl.REPEAT;
}

function texture(ctx, ti, wrap, gdi) {
  const key = ti + ':' + wrap + ':' + (gdi ? 1 : 0);
  if (ctx.tex.has(key)) return ctx.tex.get(key);
  const gl = ctx.gl, t = gl.createTexture();
  const m = META.textures[ti];
  const pot = (m.w & (m.w - 1)) === 0 && (m.h & (m.h - 1)) === 0;
  gl.bindTexture(gl.TEXTURE_2D, t);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE,
                new Uint8Array([255, 0, 255, 255]));
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, wrapMode(gl, wrap & 3, pot));
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, wrapMode(gl, (wrap >> 2) & 3, pot));
  const file = 't' + String(ti).padStart(3, '0') + (gdi && m.gdi ? '_gdi' : '') + '.png';
  const rec = { t, ready: false };
  // The upload has to finish before anything draws with it, or the model renders in
  // the magenta placeholder. Callers that render once (the thumbnails) await rec.p;
  // the detail view redraws every frame and simply fills in.
  rec.p = image(file).then(im => {
    if (!im) return;
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, im);
    rec.ready = true;
  });
  ctx.tex.set(key, rec);
  return rec;
}

/* -------------------------------------------------------------- geometry */
/* geo.bin layout per asset: pos f32[9n] uv f32[6n] col u8[12n] tex i16[n]
 * mode u8[n] wrap u8[n] part u8[n]. Triangles are regrouped here by (pass, texture,
 * wrap, mode) so the whole model draws in a handful of calls in the renderer's own
 * order.
 *
 * THE NODE INDEX HAS TO TRAVEL WITH THE VERTEX. In the pack a part is a contiguous run
 * of triangles and the renderer walks it with one monotone cursor; regrouping destroys
 * the run, so the animation would have nothing to attach a node's matrix to. One byte
 * per triangle, expanded to one per vertex here. */
function buildMesh(asset) {
  const n = asset.tris, o = asset.off;
  if (!n) return { groups: [], verts: null };
  const pos = new Float32Array(GEO, o, n * 9);
  const uv = new Float32Array(GEO, o + n * 36, n * 6);
  const col = new Uint8Array(GEO, o + n * 60, n * 12);
  const tex = new Int16Array(GEO, o + n * 72, n);
  const mode = new Uint8Array(GEO, o + n * 74, n);
  const wrap = new Uint8Array(GEO, o + n * 75, n);
  const part = new Uint8Array(GEO, o + n * 76, n);

  const bucket = new Map();
  for (let i = 0; i < n; i++) {
    const pass = (mode[i] === 2 || mode[i] === 3) ? 1 : 0;
    const k = pass + '|' + tex[i] + '|' + wrap[i] + '|' + mode[i];
    let g = bucket.get(k);
    if (!g) bucket.set(k, g = { pass, tex: tex[i], wrap: wrap[i],
                                mode: mode[i], tris: [] });
    g.tris.push(i);
  }
  const groups = [...bucket.values()].sort((a, b) => a.pass - b.pass || a.tex - b.tex);
  const total = n * 3;
  const P = new Float32Array(total * 3), U = new Float32Array(total * 2);
  const C = new Uint8Array(total * 4);
  const NODE = new Uint8Array(total);
  let w = 0;
  for (const g of groups) {
    g.first = w;
    for (const i of g.tris) {
      for (let k = 0; k < 3; k++) {
        P[w * 3] = pos[i * 9 + k * 3];
        P[w * 3 + 1] = pos[i * 9 + k * 3 + 1];
        P[w * 3 + 2] = pos[i * 9 + k * 3 + 2];
        U[w * 2] = uv[i * 6 + k * 2];
        U[w * 2 + 1] = uv[i * 6 + k * 2 + 1];
        C[w * 4] = col[i * 12 + k * 4];
        C[w * 4 + 1] = col[i * 12 + k * 4 + 1];
        C[w * 4 + 2] = col[i * 12 + k * 4 + 2];
        C[w * 4 + 3] = col[i * 12 + k * 4 + 3];
        NODE[w] = part[i];
        w++;
      }
    }
    g.count = g.tris.length * 3;
    delete g.tris;
  }
  return { groups, P, U, C, node: NODE, count: total };
}

function upload(ctx, mesh, posed) {
  const gl = ctx.gl;
  if (!mesh.vbo) {
    mesh.vbo = gl.createBuffer(); mesh.ubo = gl.createBuffer();
    mesh.cbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vbo);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.P, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.ubo);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.U, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.cbo);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.C, gl.STATIC_DRAW);
  }
  // The rest pose is uploaded once; a posed frame overwrites it, and going back to the
  // rest pose puts it back. One buffer either way, so nothing else in draw changes.
  if (posed || mesh.wasPosed) {
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vbo);
    gl.bufferSubData(gl.ARRAY_BUFFER, 0, posed ? mesh.PA : mesh.P);
    mesh.wasPosed = !!posed;
  }
  gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vbo);
  gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
  gl.bindBuffer(gl.ARRAY_BUFFER, mesh.ubo);
  gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 0, 0);
  gl.bindBuffer(gl.ARRAY_BUFFER, mesh.cbo);
  gl.enableVertexAttribArray(2);
  gl.vertexAttribPointer(2, 4, gl.UNSIGNED_BYTE, true, 0, 0);
}

/* ------------------------------------------------------------------- maths */
function perspective(fovy, asp, near, far) {
  const f = 1 / Math.tan(fovy / 2), d = near - far;
  return [f / asp, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) / d, -1,
          0, 0, 2 * far * near / d, 0];
}
function mul(a, b) {
  const o = new Array(16);
  for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) {
    let s = 0;
    for (let k = 0; k < 4; k++) s += a[k * 4 + c] * b[r * 4 + k];
    o[r * 4 + c] = s;
  }
  return o;
}
function lookAt(eye, at, up) {
  const z = norm(sub(eye, at)), x = norm(cross(up, z)), y = cross(z, x);
  return [x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
          -dot(x, eye), -dot(y, eye), -dot(z, eye), 1];
}
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                         a[0] * b[1] - a[1] * b[0]];
function norm(v) { const l = Math.hypot(...v) || 1; return [v[0] / l, v[1] / l, v[2] / l]; }

/* THE BOX A PLAYING MODEL NEEDS is not the box its rest pose needs: the MCV deploy rig
 * ends its clip about 2.2x the size it starts it, and a camera framed on the rest pose
 * loses the construction yard halfway through. anim.bbox is the whole clip's box, baked
 * by the exporter. */
function fit(asset, animated) {
  const [lo, hi] = (animated && asset.anim && asset.anim.bbox) || asset.bbox;
  const c = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
  const r = Math.max(1, Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) / 2);
  return { c, r };
}

/* ------------------------------------------------------------------ render */
function draw(ctx, mesh, asset, cam, opt) {
  const gl = ctx.gl, u = ctx.u;
  const w = gl.canvas.width, h = gl.canvas.height;
  gl.viewport(0, 0, w, h);
  gl.clearColor(0, 0, 0, 0);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  if (!mesh || !mesh.groups.length) return;
  const { c, r } = fit(asset, opt.posed);
  const d = cam.dist * r;
  const eye = [c[0] + d * Math.cos(cam.pitch) * Math.sin(cam.yaw),
               c[1] + d * Math.sin(cam.pitch),
               c[2] + d * Math.cos(cam.pitch) * Math.cos(cam.yaw)];
  const at = [c[0] + cam.pan[0] * r, c[1] + cam.pan[1] * r, c[2]];
  const mvp = mul(perspective(0.7, w / h, r * 0.02, d + r * 8),
                  lookAt(eye, at, [0, 1, 0]));
  gl.uniformMatrix4fv(u.uMvp, false, new Float32Array(mvp));
  gl.uniform1f(u.uShade, opt.shade ? 1 : 0);
  gl.uniform1f(u.uFlat, opt.wire ? 1 : 0);
  upload(ctx, mesh, opt.posed);
  gl.depthMask(true); gl.disable(gl.BLEND);
  for (const pass of [0, 1]) {
    if (pass === 1) {
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      gl.depthMask(false);      // xlu is depth TESTED, never depth WRITTEN
    }
    for (const g of mesh.groups) {
      if (g.pass !== pass) continue;
      const hasTex = g.tex >= 0 && opt.tex;
      gl.activeTexture(gl.TEXTURE0);
      if (hasTex) {
        const rec = texture(ctx, g.tex, g.wrap, opt.gdi);
        gl.bindTexture(gl.TEXTURE_2D, rec.t);
      } else {
        gl.bindTexture(gl.TEXTURE_2D, ctx.white);
      }
      gl.uniform1f(u.uHasTex, hasTex ? 1 : 0);
      gl.uniform1f(u.uCutout, g.mode === 1 ? 1 : 0);
      gl.drawArrays(opt.wire ? gl.LINE_STRIP : gl.TRIANGLES, g.first, g.count);
    }
  }
  gl.depthMask(true); gl.disable(gl.BLEND);
}

/* --------------------------------------------------------------- thumbnails */
const thumbCanvas = document.createElement('canvas');
thumbCanvas.width = thumbCanvas.height = 256;
let thumbCtx = null;
const THUMB_CAM = { yaw: -0.62, pitch: 0.46, dist: 2.5, pan: [0, 0] };

async function thumbnail(asset) {
  if (!thumbCtx) thumbCtx = makeGL(thumbCanvas);
  const mesh = buildMesh(asset);
  // Wrap is per triangle, so the texture object a group wants depends on the group.
  await Promise.all(mesh.groups.filter(g => g.tex >= 0)
    .map(g => texture(thumbCtx, g.tex, g.wrap, false).p));
  draw(thumbCtx, mesh, asset, THUMB_CAM, { tex: true, shade: true, gdi: false });
  const url = thumbCanvas.toDataURL('image/png');
  const gl = thumbCtx.gl;
  for (const b of [mesh.vbo, mesh.ubo, mesh.cbo]) if (b) gl.deleteBuffer(b);
  return url;
}

/* ------------------------------------------------------------------ gallery */
const gallery = document.getElementById('gallery');
const chips = document.getElementById('chips');
const q = document.getElementById('q');
let activeCat = null;

const observer = new IntersectionObserver(entries => {
  for (const e of entries) {
    if (!e.isIntersecting) continue;
    observer.unobserve(e.target);
    const a = BY_ID[e.target.dataset.id];
    thumbnail(a).then(url => { e.target.src = url; });
  }
}, { rootMargin: '250px' });

function renderChips() {
  const counts = {};
  for (const a of ASSETS) counts[a.category] = (counts[a.category] || 0) + 1;
  chips.innerHTML = '';
  const mk = (label, cat, n) => {
    const el = document.createElement('div');
    el.className = 'chip' + (activeCat === cat ? ' on' : '');
    el.innerHTML = label + '<b>' + n + '</b>';
    el.onclick = () => { activeCat = cat; renderChips(); renderGallery(); };
    chips.appendChild(el);
  };
  mk('All', null, ASSETS.length);
  for (const c of META.categories) mk(c, c, counts[c] || 0);
}

function renderGallery() {
  const term = q.value.trim().toLowerCase();
  SHOWN = ASSETS.filter(a =>
    (!activeCat || a.category === activeCat) &&
    (!term || a.name.toLowerCase().includes(term) ||
     a.codes.some(c => c.toLowerCase().includes(term))));
  document.getElementById('count').textContent =
    SHOWN.length + ' of ' + ASSETS.length + ' models';
  gallery.innerHTML = '';
  const groups = new Map();
  for (const a of SHOWN) {
    if (!groups.has(a.category)) groups.set(a.category, []);
    groups.get(a.category).push(a);
  }
  for (const [cat, list] of groups) {
    const sec = document.createElement('section');
    sec.className = 'group';
    const tris = list.reduce((s, a) => s + a.tris, 0);
    sec.innerHTML = '<h2>' + cat + ' <span>' + list.length + ' models, ' +
      tris.toLocaleString() + ' triangles</span></h2>';
    const grid = document.createElement('div');
    grid.className = 'grid';
    for (const a of list) grid.appendChild(card(a));
    sec.appendChild(grid);
    gallery.appendChild(sec);
  }
}

function card(a) {
  const el = document.createElement('div');
  el.className = 'card' + (selected === a ? ' sel' : '');
  el.innerHTML =
    '<img class="thumb" data-id="' + a.id + '" alt="">' +
    '<div class="cap"><div class="nm" title="' + esc(a.name) + '">' + esc(a.name) +
    '</div><div class="mt"><em>' + esc(a.code) + '</em><span>' + a.tris + ' tris</span>' +
    '</div></div>';
  el.onclick = () => openDetail(a);
  observer.observe(el.querySelector('img'));
  return el;
}

const esc = s => String(s).replace(/[&<>"]/g, c =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

/* ------------------------------------------------------------------ animation
 *
 * WHAT PLAYS, AND HOW FAST, IS THE GAME'S DECISION AND NOT THIS PAGE'S. Every number
 * used below arrives in assets.json from tools/art/anim_rates.py, which is a
 * transcription of the renderer's own structure_anim_frame and of the cartridge's
 * per-StructType draw arms; the evidence for each one is in docs/animation-drivers.md.
 * The only arithmetic here is the SHAPE of the walk, and the renderer does it the same
 * way: wrap the counter by the segment, fold it if the arm folds, and lerp between the
 * two baked frames either side of the result.
 *
 * The pack stores, per baked frame and per node, a 3x4 DELTA from the rest pose, row
 * major, applied as v' = M * v + t in mesh units, plus one visibility byte per node per
 * frame. A node the clip has not reached yet is ABSENT rather than sitting at its rest
 * pose (the construction yard's pad is empty while the MCV is still unfolding), which is
 * why a hidden node's triangles collapse to a point here rather than being drawn.
 *
 * A CLIP IS NOT ALWAYS A LOOP, and three of them are not walked by a clock at all: the
 * war factory's is indexed by its door position, the SAM's by its launcher facing, the
 * refinery rig's by the refinery's own unload stage. Those are swept at the rate the
 * game moves them and held at each end; the sweep is the game's, the hold is this
 * page's, and the panel says so rather than letting the difference pass. */
let ANIMBUF = null, animFetch = null;
const CLIPS = new Map();

function loadAnim() {
  if (animFetch) return animFetch;
  animFetch = fetch(DATA + 'anim.bin').then(r => r.arrayBuffer())
    .then(b => { ANIMBUF = b; return b; }).catch(() => null);
  return animFetch;
}

/* The two views over anim.bin for one asset: nf*np 3x4 matrices, then nf*np bytes. */
function clipOf(a) {
  if (!ANIMBUF || !a.anim || a.anim.off === undefined) return null;
  let c = CLIPS.get(a.id);
  if (!c) {
    const cells = a.anim.frames * a.anim.parts;
    c = { mat: new Float32Array(ANIMBUF, a.anim.off, cells * 12),
          vis: new Uint8Array(ANIMBUF, a.anim.off + cells * 48, cells) };
    CLIPS.set(a.id, c);
  }
  return c;
}

const wrapf = (x, n) => { const v = x % n; return v < 0 ? v + n : v; };

/* Seconds held at each end of a swept clip. THIS IS THE ONE NUMBER ON THIS PAGE THAT THE
 * GAME DOES NOT SUPPLY, and it exists only because these three clips are not loops: a
 * door that ran straight from open back to open would read as a door slamming. */
const HOLD = { door: 1.0, sam: 0.6, procrig: 0.6, mcvrig: 1.0 };

/* Forward, hold, back, hold. `hold` is this page's; the run is the game's rate. */
function sweep(t, f0, f1, fps, hold) {
  const n = Math.max(1, f1 - f0 - 1), run = n / fps;
  let x = wrapf(t, 2 * run + 2 * hold);
  if (x < run) return f0 + x * fps;
  x -= run;
  if (x < hold) return f0 + n;
  x -= hold;
  if (x < run) return f0 + n - x * fps;
  return f0;
}
/* Once through, then hold at the end before it starts again. */
function once(t, f0, f1, fps, hold) {
  const n = Math.max(1, f1 - f0 - 1), run = n / fps;
  const x = wrapf(t, run + hold);
  return x < run ? f0 + x * fps : f0 + n;
}

function segmentsOf(a) {
  const d = (a.anim && a.anim.driver) || null;
  if (d && d.segments && d.segments.length) return d.segments;
  return [{ name: 'clip', f0: 0, f1: (a.anim ? a.anim.frames : 0), plays: true, why: '' }];
}

/* The clip frame this model is on `t` seconds into playback. */
function clipFrame(a, t, mode, segi) {
  const an = a.anim;
  if (!an || an.frames <= 1) return 0;
  const d = an.driver || {};
  const nf = an.frames;
  if (mode === 'clip') {                       // the whole baked clip, end to end
    const fps = 15 / (an.ticks_per_frame || 1);
    return wrapf(t * fps, nf);
  }
  const segs = segmentsOf(a);
  const seg = segs[Math.min(segi, segs.length - 1)];
  const f0 = seg.f0, f1 = Math.min(seg.f1, nf), n = f1 - f0;
  const fps = d.fps || 15;
  if (n <= 1 || !seg.plays || d.plays === false) return f0;
  switch (d.driver) {
    case 'arm': {
      const span = d.folded ? (n - 1) * 2 : n;
      const x = wrapf(t * fps, span);
      const f = (d.folded && x >= n) ? (n - 1) * 2 - x : x;
      return f0 + Math.min(Math.max(f, 0), n - 1);
    }
    case 'door':
    case 'sam':
    case 'procrig': return sweep(t, f0, f1, fps, HOLD[d.driver]);
    case 'mcvrig':  return once(t, f0, f1, fps, HOLD.mcvrig);
    default:        return f0 + wrapf(t * fps, n);
  }
}

/* How long one cycle of what is CURRENTLY selected takes. The driver table's own period
 * is for the segment the game plays by default, and a viewer that has picked the other
 * one would otherwise be told the wrong number. */
function cycleSeconds(a, mode, segi) {
  const an = a.anim, d = an.driver || {};
  if (mode === 'clip') return an.frames / (15 / (an.ticks_per_frame || 1));
  const segs = segmentsOf(a), seg = segs[Math.min(segi, segs.length - 1)];
  const n = Math.min(seg.f1, an.frames) - seg.f0, fps = d.fps || 15;
  if (n <= 1 || !seg.plays || d.plays === false) return 0;
  const run = (n - 1) / fps;
  switch (d.driver) {
    case 'arm':     return (d.folded ? (n - 1) * 2 : n) / fps;
    case 'door':
    case 'sam':
    case 'procrig': return 2 * run + 2 * HOLD[d.driver];
    case 'mcvrig':  return run + HOLD.mcvrig;
    default:        return n / fps;
  }
}

/* Write the posed vertices into mesh.PA. Elementwise lerp of the two baked matrices is
 * what the renderer does, and it is only valid because the baker resampled the tracks
 * densely enough that consecutive frames are close: that is the baker's contract. */
function poseMesh(mesh, a, frame) {
  const c = clipOf(a);
  if (!c || !mesh.node) return false;
  const np = a.anim.parts, nf = a.anim.frames;
  let f0 = Math.floor(frame);
  if (!(f0 >= 0)) f0 = 0;
  if (f0 > nf - 1) f0 = nf - 1;
  const mix = Math.min(1, Math.max(0, frame - f0));
  const f1 = Math.min(f0 + 1, nf - 1);
  const M = mesh.M || (mesh.M = new Float32Array(np * 12));
  const V = mesh.V || (mesh.V = new Uint8Array(np));
  for (let p = 0; p < np; p++) {
    const o0 = (f0 * np + p) * 12, o1 = (f1 * np + p) * 12;
    for (let q = 0; q < 12; q++)
      M[p * 12 + q] = c.mat[o0 + q] + (c.mat[o1 + q] - c.mat[o0 + q]) * mix;
    V[p] = c.vis[f0 * np + p];
  }
  const P = mesh.P, PA = mesh.PA || (mesh.PA = new Float32Array(P.length));
  const node = mesh.node;
  for (let v = 0; v < mesh.count; v++) {
    const p = node[v] < np ? node[v] : 0, i = v * 3;
    if (!V[p]) { PA[i] = PA[i + 1] = PA[i + 2] = 0; continue; }
    const m = p * 12, x = P[i], y = P[i + 1], z = P[i + 2];
    PA[i]     = M[m]     * x + M[m + 1] * y + M[m + 2]  * z + M[m + 3];
    PA[i + 1] = M[m + 4] * x + M[m + 5] * y + M[m + 6]  * z + M[m + 7];
    PA[i + 2] = M[m + 8] * x + M[m + 9] * y + M[m + 10] * z + M[m + 11];
  }
  return true;
}

/* ------------------------------------------------------------------- detail */
const detail = document.getElementById('detail');
const view = document.getElementById('view');
let vctx = null, vmesh = null, vcam = null, vspin = 0, vraf = 0;
const vopt = { tex: true, shade: true, wire: false, gdi: false, posed: false };
const vplay = { on: false, mode: 'game', seg: 0, t: 0, frame: 0, last: 0, drag: false };

function openDetail(a) {
  selected = a;
  document.getElementById('bExport').disabled = false;
  detail.classList.add('show');
  if (!vctx) vctx = makeGL(view);
  if (vmesh) for (const b of [vmesh.vbo, vmesh.ubo, vmesh.cbo])
    if (b) vctx.gl.deleteBuffer(b);
  vmesh = buildMesh(a);
  vcam = { yaw: -0.62, pitch: 0.42, dist: 2.4, pan: [0, 0] };
  vopt.gdi = false;
  for (const b of document.querySelectorAll('#tools button[data-t=house]'))
    b.classList.toggle('on', false), b.disabled = !a.gdi;
  document.getElementById('dName').textContent = a.name;
  document.getElementById('dSub').textContent =
    a.code + ' · ' + a.category + ' · ' + a.tris + ' triangles';
  document.getElementById('dBody').innerHTML = infoHTML(a);
  for (const ti of a.tex) texture(vctx, ti, 0, false);
  vplay.mode = 'game'; vplay.seg = 0; vplay.t = 0; vplay.frame = 0; vplay.last = 0;
  vplay.on = !!(a.anim && a.anim.off !== undefined);
  vopt.posed = false;
  renderTransport(a);
  if (a.anim && a.anim.off !== undefined)
    loadAnim().then(() => { if (selected === a) renderTransport(a); });
  resize();
  loop();
}

/* ---------------------------------------------------------------- transport */
const transport = document.getElementById('anim');
const aPlay = document.getElementById('aPlay');
const aScrub = document.getElementById('aScrub');
const aRead = document.getElementById('aRead');
const aMode = document.getElementById('aMode');
const aSeg = document.getElementById('aSeg');

function renderTransport(a) {
  const an = a && a.anim;
  transport.classList.toggle('show', !!an);
  if (!an) return;
  const ready = ANIMBUF && an.off !== undefined;
  aScrub.max = Math.max(0, an.frames - 1);
  aScrub.disabled = aPlay.disabled = aMode.disabled = !ready;
  const segs = segmentsOf(a);
  aSeg.innerHTML = '';
  for (let i = 0; i < segs.length; i++) {
    const o = document.createElement('option');
    o.value = i;
    o.textContent = segs[i].name + (segs[i].plays ? '' : ' (never played)');
    aSeg.appendChild(o);
  }
  aSeg.value = String(Math.min(vplay.seg, segs.length - 1));
  aSeg.style.display = (segs.length > 1 && vplay.mode === 'game') ? '' : 'none';
  aSeg.disabled = !ready;
  aMode.value = vplay.mode;
  aPlay.textContent = vplay.on ? '‖' : '▶';
  readTransport();
}

function readTransport() {
  const a = selected, an = a && a.anim;
  if (!an) return;
  if (!ANIMBUF || an.off === undefined) {
    aRead.textContent = an.frames + ' frames, loading…';
    return;
  }
  const d = an.driver || {};
  const bits = [Math.round(vplay.frame) + ' / ' + (an.frames - 1)];
  if (vplay.mode === 'clip') {
    bits.push((15 / (an.ticks_per_frame || 1)).toFixed(0) + ' fps (baked grid)');
    bits.push(cycleSeconds(a, 'clip', 0).toFixed(2) + ' s a cycle');
  } else if (d.plays === false || !segmentsOf(a)[vplay.seg].plays) {
    bits.push('held: the game does not play this');
  } else {
    bits.push(fmtFps(d.fps) + ' fps');
    const cyc = cycleSeconds(a, vplay.mode, vplay.seg);
    if (cyc) bits.push(cyc.toFixed(2) + ' s a cycle');
  }
  aRead.textContent = bits.join('  ·  ');
  if (!vplay.drag) aScrub.value = String(Math.round(vplay.frame));
}

const fmtFps = v => (Math.abs(v - Math.round(v)) < 0.005
  ? String(Math.round(v)) : v.toFixed(2).replace(/0$/, ''));

function infoHTML(a) {
  const t = [];
  t.push('<dl class="kv">');
  const row = (k, v) => t.push('<dt>' + k + '</dt><dd>' + v + '</dd>');
  row('Name', esc(a.name));
  row('Code', esc(a.code));
  row('Category', esc(a.category));
  row('Triangles', a.tris);
  row('Draw modes', a.modes.join(', '));
  row('Source', a.source === 'pack'
    ? 'baked mission pack (' + esc(META.pack) + ', v' + META.pack_version + ')'
    : 'cartridge ROM, briefing overlay');
  row('Cartridge mesh', esc(a.mesh));
  if (a.sections) row('Build sections', a.sections +
    ' <span class="pill">assembles piece by piece</span>');
  if (a.anim) {
    const d = a.anim.driver || {};
    row('Animation', a.anim.frames + ' frames, ' + a.anim.parts + ' nodes, clip ' +
      a.anim.clips.map(c => c.t0 + '-' + c.t1 + (c.loop ? ' loop' : ' once')).join(', '));
    row('Plays at', d.plays === false ? 'nothing: it holds one frame'
      : fmtFps(d.fps) + ' baked frames a second' +
        (d.period ? ', ' + d.period.toFixed(2) + ' s a cycle' : ''));
    row('Driven by', esc(d.fps_source || 'the pack’s baked grid') +
      (d.cite ? ' <span class="pill">' + esc(d.cite) + '</span>' : ''));
  }
  if (a.gdi) row('House palette', 'GDI variant present');
  t.push('</dl>');
  t.push('<div class="note">' + esc(a.provenance) + '.</div>');
  // Say exactly which rule is being played, and where it stops being the game's.
  if (a.anim) {
    const d = a.anim.driver || {};
    t.push('<div class="note">' + esc(d.note || '') + '</div>');
    if (d.hold)
      t.push('<div class="note">In the game this model does not animate: ' +
        esc(d.hold) + '. The transport still plays the clip so the art can be looked ' +
        'at; switch it to <b>whole clip</b> to see all of it.</div>');
    const idle = ['door', 'sam', 'procrig'].indexOf(d.driver) >= 0;
    if (idle)
      t.push('<div class="note">This clip is not indexed by a clock: the game picks a ' +
        'frame from the door’s position, the launcher’s facing or the ' +
        'refinery’s own unload stage. The page sweeps it at the rate the game ' +
        'moves it and pauses at each end; the sweep is the game’s, the pause is ' +
        'this page’s.</div>');
    const dead = (d.segments || []).filter(sg => !sg.plays);
    for (const sg of dead)
      t.push('<div class="note">Frames ' + sg.f0 + ' to ' + (sg.f1 - 1) + ' are ' +
        esc(sg.why || 'never reached by the game') + ', and the game never plays them. ' +
        'They are in the clip and in the FBX.</div>');
    const tl = (EXPORTS[a.code] || {}).fbx && EXPORTS[a.code].fbx.timeline;
    if (tl)
      t.push('<div class="note">The FBX puts every key on a whole frame: one baked ' +
        'frame is ' + tl.spacing + ' timeline frame' + (tl.spacing === 1 ? '' : 's') +
        ' at ' + tl.fps + ' fps' +
        (tl.error_pct ? ', which is ' + Math.abs(tl.error_pct).toFixed(2) +
         '% off this model’s exact rate because no whole rate expresses it' : '') +
        '.</div>');
  }
  if (a.modes.length && a.modes.every(m => m === 'shadow' || m === 'xlu'))
    t.push('<div class="note">Every triangle here draws in a blended pass: this is a ' +
      'SHADOW mesh, the flat plate the cartridge lays on the ground under something ' +
      'else. It is meant to look like this.</div>');

  if (a.parts && a.parts.length) {
    t.push('<h4 class="sec">Parts and pivots</h4><table class="parts">' +
      '<tr><th>node</th><th>role</th><th>tris</th><th>pivot (x, y, z)</th></tr>');
    a.parts.forEach((p, i) => t.push('<tr><td>p' + i + '</td><td>' + p.role +
      '</td><td>' + p.tris + '</td><td>' +
      p.pivot.map(v => v.toFixed(1)).join(', ') + '</td></tr>'));
    t.push('</table>');
  }
  if (a.aliases && a.aliases.length) {
    t.push('<h4 class="sec">Other codes on this mesh</h4><div>');
    for (const al of a.aliases) {
      const warn = al.confidence === 'low' || al.confidence === 'none';
      t.push('<span class="pill' + (warn ? ' warn' : '') + '">' + esc(al.code) +
        (al.confidence ? ' · ' + al.confidence : '') + '</span>');
    }
    t.push('</div><div style="font-size:10.5px;color:var(--dim2);margin-bottom:14px">' +
      'The pack points these codes at the same mesh. A low-confidence one is a mapping ' +
      'this project has not settled, not a second name for this model.</div>');
  }
  if (a.variants && a.variants.length) {
    t.push('<h4 class="sec">Variant meshes</h4><div>');
    for (const v of a.variants)
      t.push('<span class="pill">' + esc(v.code) + ' · ' + v.tris + ' tris</span>');
    t.push('</div><div style="font-size:10.5px;color:var(--dim2);margin-bottom:14px">' +
      'Turn states and flipbook frames the cartridge stores as separate meshes; they ' +
      'belong to this model rather than beside it.</div>');
  }
  if (a.tex.length) {
    t.push('<h4 class="sec">Textures (' + a.tex.length + ')</h4><div class="texrow">');
    for (const ti of a.tex) {
      const m = META.textures[ti];
      const f = 't' + String(ti).padStart(3, '0') + '.png';
      t.push('<figure><img src="' + DATA + 'tex/' + f + '"><figcaption>' +
        m.uw + '×' + m.uh + (m.gdi ? ' · gdi' : '') + '</figcaption></figure>');
    }
    t.push('</div>');
  }
  return t.join('');
}

function resize() {
  const r = view.getBoundingClientRect();
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  view.width = Math.max(1, Math.round(r.width * dpr));
  view.height = Math.max(1, Math.round(r.height * dpr));
}

function loop() {
  cancelAnimationFrame(vraf);
  vplay.last = 0;
  const tick = now => {
    if (!detail.classList.contains('show')) return;
    // Real elapsed time, clamped so a backgrounded tab does not jump the clip forward
    // by however long the page was hidden.
    const dt = vplay.last ? Math.min(0.25, (now - vplay.last) / 1000) : 0;
    vplay.last = now;
    if (vspin) vcam.yaw += 0.006;
    vopt.posed = false;
    if (selected && selected.anim && ANIMBUF && selected.anim.off !== undefined) {
      if (vplay.on) {
        vplay.t += dt;
        vplay.frame = clipFrame(selected, vplay.t, vplay.mode, vplay.seg);
      }
      vopt.posed = poseMesh(vmesh, selected, vplay.frame);
      readTransport();
    }
    draw(vctx, vmesh, selected, vcam, vopt);
    vraf = requestAnimationFrame(tick);
  };
  vraf = requestAnimationFrame(tick);
}

function closeDetail() {
  detail.classList.remove('show');
  cancelAnimationFrame(vraf);
}

(function wireDetail() {
  let drag = null;
  view.addEventListener('pointerdown', e => {
    drag = { x: e.clientX, y: e.clientY, shift: e.shiftKey };
    view.setPointerCapture(e.pointerId);
  });
  view.addEventListener('pointermove', e => {
    if (!drag) return;
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    drag.x = e.clientX; drag.y = e.clientY;
    if (drag.shift) { vcam.pan[0] -= dx * 0.004; vcam.pan[1] += dy * 0.004; }
    else {
      vcam.yaw -= dx * 0.008;
      vcam.pitch = Math.max(-1.5, Math.min(1.5, vcam.pitch + dy * 0.008));
    }
  });
  const stop = e => { if (drag) { view.releasePointerCapture(e.pointerId); drag = null; } };
  view.addEventListener('pointerup', stop);
  view.addEventListener('pointercancel', stop);
  view.addEventListener('wheel', e => {
    e.preventDefault();
    vcam.dist = Math.max(0.4, Math.min(14, vcam.dist * (1 + Math.sign(e.deltaY) * 0.12)));
  }, { passive: false });
  window.addEventListener('resize', () => { if (vctx) resize(); });

  for (const b of document.querySelectorAll('#tools button')) {
    b.onclick = () => {
      const t = b.dataset.t;
      if (t === 'reset') { vcam.yaw = -0.62; vcam.pitch = 0.42; vcam.dist = 2.4;
                           vcam.pan = [0, 0]; return; }
      if (t === 'spin') { vspin ^= 1; b.classList.toggle('on', !!vspin); return; }
      if (t === 'tex') vopt.tex = !vopt.tex;
      if (t === 'shade') vopt.shade = !vopt.shade;
      if (t === 'wire') vopt.wire = !vopt.wire;
      if (t === 'house') vopt.gdi = !vopt.gdi;
      b.classList.toggle('on', t === 'tex' ? vopt.tex : t === 'shade' ? vopt.shade :
                               t === 'wire' ? vopt.wire : vopt.gdi);
      if (t === 'house') for (const ti of selected.tex) texture(vctx, ti, 0, vopt.gdi);
    };
  }
  const step = d => {
    const i = SHOWN.indexOf(selected);
    if (i < 0) return;
    openDetail(SHOWN[(i + d + SHOWN.length) % SHOWN.length]);
  };
  const setPlay = on => {
    vplay.on = on;
    aPlay.textContent = on ? '‖' : '▶';
  };
  aPlay.onclick = () => setPlay(!vplay.on);
  aMode.onchange = () => {
    vplay.mode = aMode.value; vplay.t = 0; vplay.seg = 0;
    renderTransport(selected); setPlay(true);
  };
  aSeg.onchange = () => {
    vplay.seg = parseInt(aSeg.value, 10) || 0; vplay.t = 0;
    readTransport();
  };
  // Scrubbing pauses: the counter and the slider are the same number, and letting both
  // write it is how a scrub bounces back to wherever the clock had got to.
  aScrub.addEventListener('pointerdown', () => { vplay.drag = true; setPlay(false); });
  const endDrag = () => { vplay.drag = false; };
  aScrub.addEventListener('pointerup', endDrag);
  aScrub.addEventListener('pointercancel', endDrag);
  aScrub.addEventListener('input', () => {
    setPlay(false);
    vplay.frame = parseFloat(aScrub.value) || 0;
    readTransport();
  });
  document.getElementById('dPrev').onclick = () => step(-1);
  document.getElementById('dNext').onclick = () => step(1);
  document.getElementById('dClose').onclick = closeDetail;
  document.getElementById('dExport').onclick = () => openExport(false);
  window.addEventListener('keydown', e => {
    if (document.getElementById('modal').classList.contains('show')) {
      if (e.key === 'Escape') closeModal();
      return;
    }
    if (!detail.classList.contains('show')) return;
    if (e.key === 'Escape') closeDetail();
    if (e.key === 'ArrowLeft') step(-1);
    if (e.key === 'ArrowRight') step(1);
    if (e.key === ' ' && selected && selected.anim) {
      e.preventDefault();
      setPlay(!vplay.on);
    }
    if ((e.key === ',' || e.key === '.') && selected && selected.anim) {
      setPlay(false);
      const nf = selected.anim.frames;
      vplay.frame = Math.min(nf - 1, Math.max(0,
        Math.round(vplay.frame) + (e.key === '.' ? 1 : -1)));
      readTransport();
    }
  });
})();

/* ---------------------------------------------------------------- zip (store) */
const CRC = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();
function crc32(buf) {
  let c = 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i++) c = CRC[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
/* Store method, so no deflate implementation is needed and the archive stays
 * byte-checkable. Fixed timestamp so the same selection zips the same. */
const DOSDATE = ((2026 - 1980) << 9) | (1 << 5) | 1;
function zip(files) {
  const enc = new TextEncoder();
  const locals = [], central = [];
  let off = 0;
  for (const f of files) {
    const name = enc.encode(f.name), data = f.data, c = crc32(data);
    const lh = new Uint8Array(30 + name.length);
    const lv = new DataView(lh.buffer);
    lv.setUint32(0, 0x04034b50, true); lv.setUint16(4, 20, true);
    lv.setUint16(6, 0, true); lv.setUint16(8, 0, true);
    lv.setUint16(10, 0, true); lv.setUint16(12, DOSDATE, true);
    lv.setUint32(14, c, true); lv.setUint32(18, data.length, true);
    lv.setUint32(22, data.length, true); lv.setUint16(26, name.length, true);
    lv.setUint16(28, 0, true); lh.set(name, 30);
    locals.push(lh, data);
    const ch = new Uint8Array(46 + name.length);
    const cv = new DataView(ch.buffer);
    cv.setUint32(0, 0x02014b50, true); cv.setUint16(4, 20, true);
    cv.setUint16(6, 20, true); cv.setUint16(12, DOSDATE, true);
    cv.setUint32(16, c, true); cv.setUint32(20, data.length, true);
    cv.setUint32(24, data.length, true); cv.setUint16(28, name.length, true);
    cv.setUint32(42, off, true); ch.set(name, 46);
    central.push(ch);
    off += lh.length + data.length;
  }
  const cdSize = central.reduce((s, c) => s + c.length, 0);
  const end = new Uint8Array(22);
  const ev = new DataView(end.buffer);
  ev.setUint32(0, 0x06054b50, true);
  ev.setUint16(8, files.length, true); ev.setUint16(10, files.length, true);
  ev.setUint32(12, cdSize, true); ev.setUint32(16, off, true);
  return new Blob([...locals, ...central, end], { type: 'application/zip' });
}

function download(blob, name) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = name;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 4000);
}

/* ----------------------------------------------------------------- export UI */
const FORMATS = [
  ['fbx', 'FBX 7.4 binary', 'Keeps the node hierarchy and the cartridge’s own ' +
   'pivots, so a turret still rotates about the point the game rotates it about. ' +
   'The only format the return path can use. Blender reads binary only.'],
  ['obj', 'OBJ + MTL', 'Universal, and the safest thing to hand a tool that is not a ' +
   'DCC app. Carries no pivots: a turret arrives as loose geometry.'],
  ['gltf', 'glTF 2.0', 'Modern and web-native, with the baked vertex colours as ' +
   'COLOR_0 and an unlit material, which is what the cartridge actually is.'],
];
const modal = document.getElementById('modal');
let mAll = false, mFmt = 'fbx';
const mOptState = { tex: true, gdi: false, folders: true, filtered: false };

function openExport(all) {
  mAll = all;
  const n = all ? (mOptState.filtered ? SHOWN.length : ASSETS.length) : 1;
  document.getElementById('mTitle').textContent =
    all ? 'Export all models' : 'Export ' + selected.name;
  const fmt = document.getElementById('mFmt');
  fmt.innerHTML = '';
  for (const [id, title, desc] of FORMATS) {
    const l = document.createElement('label');
    l.className = 'opt' + (mFmt === id ? ' on' : '');
    l.innerHTML = '<input type="radio" name="fmt" value="' + id + '"' +
      (mFmt === id ? ' checked' : '') + '><span><span class="t">' + title +
      (id === 'fbx' ? ' <span class="pill">default</span>' : '') +
      '</span><br><span class="d">' + desc + '</span></span>';
    l.querySelector('input').onchange = () => { mFmt = id; openExport(all); };
    fmt.appendChild(l);
  }
  const opts = document.getElementById('mOpts');
  opts.innerHTML = '';
  const anyGdi = all ? ASSETS.some(a => a.gdi) : selected.gdi;
  const add = (key, title, desc, disabled) => {
    const l = document.createElement('label');
    l.className = 'opt' + (mOptState[key] && !disabled ? ' on' : '');
    l.innerHTML = '<input type="checkbox"' + (mOptState[key] ? ' checked' : '') +
      (disabled ? ' disabled' : '') + '><span><span class="t">' + title +
      '</span><br><span class="d">' + desc + '</span></span>';
    l.querySelector('input').onchange = e => {
      mOptState[key] = e.target.checked;
      openExport(all);
    };
    opts.appendChild(l);
  };
  add('tex', 'Textures (PNG)',
      'The cartridge’s own texture sheets, unpadded UVs already applied. ' +
      'Without them the model arrives untextured but still carries its UVs.');
  add('gdi', 'GDI house palette',
      anyGdi ? 'Decode through the cartridge’s GDI TLUT (ROM 0x98F30) instead of ' +
      'the Nod/neutral one at 0x99130. Sand and gold instead of blue-grey and red.'
      : 'This model has no texture that differs between the two house tables.',
      !anyGdi);
  if (all) {
    add('folders', 'Sort into folders by category',
        'Vehicles/, Structures/, Walls/ and so on, instead of one flat directory.');
    add('filtered', 'Only the ' + SHOWN.length + ' models currently shown',
        'Respect the category chip and the search box rather than exporting all ' +
        ASSETS.length + '.');
  }
  document.getElementById('mNote').textContent =
    n + (n === 1 ? ' model' : ' models') + (all ? ', as a .zip' : '');
  document.getElementById('prog').classList.remove('show');
  document.getElementById('mGo').disabled = false;
  modal.classList.add('show');
}
function closeModal() { modal.classList.remove('show'); }
document.getElementById('mCancel').onclick = closeModal;
document.getElementById('bExport').onclick = () => selected && openExport(false);
document.getElementById('bExportAll').onclick = () => openExport(true);
modal.onclick = e => { if (e.target === modal) closeModal(); };

function fileList(asset) {
  const e = EXPORTS[asset.code];
  if (!e) return [];
  const f = e[mFmt];
  const texdir = (mOptState.gdi && e.gdi) ? 'tex-gdi' : 'tex';
  const out = [{ url: DATA + 'export/' + mFmt + '/' + f.model, name: f.model }];
  for (const x of (f.extra || []))
    out.push({ url: DATA + 'export/' + mFmt + '/' + x, name: x });
  if (mOptState.tex)
    for (const t of e.tex)
      out.push({ url: DATA + 'export/' + texdir + '/' + t, name: t });
  return out;
}

async function fetchAll(items, onProgress) {
  const out = new Array(items.length);
  let done = 0, i = 0;
  const worker = async () => {
    while (i < items.length) {
      const k = i++;
      const r = await fetch(items[k].url);
      out[k] = { name: items[k].name,
                 data: new Uint8Array(await r.arrayBuffer()) };
      onProgress(++done, items.length);
    }
  };
  await Promise.all(Array.from({ length: 12 }, worker));
  return out;
}

document.getElementById('mGo').onclick = async () => {
  const go = document.getElementById('mGo');
  go.disabled = true;
  const prog = document.getElementById('prog');
  const bar = document.querySelector('#bar i');
  const text = document.getElementById('progText');
  prog.classList.add('show');
  const set = (d, t) => {
    bar.style.width = (100 * d / Math.max(1, t)) + '%';
    text.textContent = 'Fetching ' + d + ' of ' + t + ' files…';
  };
  try {
    if (!mAll) {
      const items = fileList(selected);
      set(0, items.length);
      const files = await fetchAll(items, set);
      if (files.length === 1) {
        download(new Blob([files[0].data]), files[0].name);
      } else {
        text.textContent = 'Zipping…';
        download(zip(files), selected.code + '-' + mFmt + '.zip');
      }
    } else {
      const list = mOptState.filtered ? SHOWN : ASSETS;
      const items = [];
      const seen = new Set();
      for (const a of list) {
        const dir = mOptState.folders ? a.category + '/' : '';
        for (const f of fileList(a)) {
          const name = dir + f.name;
          if (seen.has(name)) continue;
          seen.add(name);
          items.push({ url: f.url, name });
        }
      }
      set(0, items.length);
      const files = await fetchAll(items, set);
      text.textContent = 'Zipping ' + files.length + ' files…';
      await new Promise(r => setTimeout(r, 30));
      download(zip(files), 'cnc3d-models-' + mFmt +
        (mOptState.gdi ? '-gdi' : '') + '.zip');
    }
    text.textContent = 'Done.';
  } catch (err) {
    text.textContent = 'Export failed: ' + err.message;
  }
  go.disabled = false;
};

/* --------------------------------------------------------------------- boot */
(async function boot() {
  const [meta, geo, exp] = await Promise.all([
    fetch(DATA + 'assets.json').then(r => r.json()),
    fetch(DATA + 'geo.bin').then(r => r.arrayBuffer()),
    fetch(DATA + 'export/manifest.json').then(r => r.json()).catch(() => ({})),
  ]);
  META = meta; GEO = geo; EXPORTS = exp;
  ASSETS = meta.assets;
  for (const a of ASSETS) BY_ID[a.id] = a;
  renderChips();
  renderGallery();
  q.oninput = renderGallery;
})();
