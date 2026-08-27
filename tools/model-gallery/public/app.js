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
 * mode u8[n] wrap u8[n]. Triangles are regrouped here by (pass, texture, wrap, mode)
 * so the whole model draws in a handful of calls in the renderer's own order. */
function buildMesh(asset) {
  const n = asset.tris, o = asset.off;
  if (!n) return { groups: [], verts: null };
  const pos = new Float32Array(GEO, o, n * 9);
  const uv = new Float32Array(GEO, o + n * 36, n * 6);
  const col = new Uint8Array(GEO, o + n * 60, n * 12);
  const tex = new Int16Array(GEO, o + n * 72, n);
  const mode = new Uint8Array(GEO, o + n * 74, n);
  const wrap = new Uint8Array(GEO, o + n * 75, n);

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
        w++;
      }
    }
    g.count = g.tris.length * 3;
    delete g.tris;
  }
  return { groups, P, U, C, count: total };
}

function upload(ctx, mesh) {
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

function fit(asset) {
  const [lo, hi] = asset.bbox;
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
  const { c, r } = fit(asset);
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
  upload(ctx, mesh);
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

/* ------------------------------------------------------------------- detail */
const detail = document.getElementById('detail');
const view = document.getElementById('view');
let vctx = null, vmesh = null, vcam = null, vspin = 0, vraf = 0;
const vopt = { tex: true, shade: true, wire: false, gdi: false };

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
  resize();
  loop();
}

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
  if (a.anim) row('Animation', a.anim.frames + ' frames, ' +
    a.anim.clips.map(c => c.t0 + '-' + c.t1 + (c.loop ? ' loop' : ' once')).join(', '));
  if (a.gdi) row('House palette', 'GDI variant present');
  t.push('</dl>');
  t.push('<div class="note">' + esc(a.provenance) + '.</div>');
  // Say what this view is NOT, rather than let a still stand in for a clip.
  if (a.anim)
    t.push('<div class="note">This model carries a baked animation clip. The gallery ' +
      'draws its rest pose; nothing here is playing, and which frame the game shows is ' +
      'decided by the engine rather than by playback. The clip goes out with the FBX.' +
      '</div>');
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
  const tick = () => {
    if (!detail.classList.contains('show')) return;
    if (vspin) vcam.yaw += 0.006;
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
 * byte-checkable. Fixed timestamp (1 Jan 2026) so the same selection zips the same. */
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
