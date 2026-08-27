import * as THREE from 'three';
import { OrbitControls } from 'three/addons/OrbitControls.js';

// ---------------------------------------------------------------- constants
// The cartridge's own geometry: a corner's world Y is its height byte * 4 and a
// cell is 256 world units wide, so one cell width = 64 bytes of height. We work
// in CELL units, which makes that ratio exactly 1 and the default view honest.
const CELLS = 64, CORNERS = 65, BYTE_PER_CELL = 64;
const TS = 24, PITCH = 26, GUTTER = 1, COLS = 32;
const LADDER = [0, 64, 128, 191, 255];      // the rungs the cartridge actually uses

const FAM_COLOR = {
  CLEAR:'#b9a887', ROAD:'#8a6f4c', PATCH:'#93894f', BRUSH:'#5c8a4a', BOULDER:'#8d8d8d',
  SLOPE:'#e8622a', RIVER:'#2f7fd8', WATER:'#1b4a8f', SHORE:'#37b8c9',
  FORD:'#12a08a', FALLS:'#a05fd8', BRIDGE:'#e8c832',
};
// One colour per rung of the ladder the cartridge actually uses. A corner sitting
// between two rungs is drawn between the two colours, so a plateau reads as a flat
// band and a cliff reads as the gradient between bands.
const TIER_COLOR = ['#2f62ad','#22a7a0','#a8c342','#f0913a','#f7ece0'];
const HOLE_COLOR = '#1c3444';   // ours, not the cartridge's: see the note in the panel

/* ---- the 3D asset layer ---------------------------------------------------
   Every mission's INI places objects by CELL, and the cartridge ships a mesh for
   most of the types it places. The transform below is not derived here: it is
   copied out of game/cnc_eyes.cpp, the renderer of the game people actually play.
     MODEL_SCALE 1/1024                       cnc_eyes.cpp:433
     facing angle = -face * 2*pi/256          cnc_eyes.cpp:4955
     vwx = cx + (vx*cos + vz*sin) * SCALE     cnc_eyes.cpp:5278
   Cell numbering is Tiberian Dawn's own: x = cell & 63, y = cell >> 6. */
const MODEL_SCALE = 1 / 1024;
const KIND = { terrain:0, structure:1, unit:2, infantry:3, overlay:4, smudge:5 };
/* Tiberian Dawn house colours, as the sidebar and minimap use them. */
const HOUSE_COLOR = ['#e8c341','#c8452e','#9aa0a6','#7f8c99',
                     '#e8c341','#c8452e','#4f8fd0','#4aa564','#c86fc0','#d08a3a'];
const ASSET_GROUPS = [
  ['buildings', 'Buildings',  'structure'],
  ['terrain',   'Trees, rocks','terrain'],
  ['walls',     'Walls',      'wall'],
  ['tiberium',  'Tiberium',   'tiberium'],
  ['units',     'Vehicles',   'unit'],
  ['infantry',  'Infantry',   'infantry'],
  ['decals',    'Craters, decals','decal'],
];

// ---------------------------------------------------------------- scene
// The canvas fills #view; #stage also holds the view bar beneath it, so sizing to
// #stage would push the bottom of the map under that bar.
const stage = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({ antialias:true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
// Without a tone map the key light clips a white surface to paper and the relief we
// came here to look at disappears into it.
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;
stage.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color('#0b0e12');

const camera = new THREE.PerspectiveCamera(42, 1, 0.1, 500);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.maxPolarAngle = Math.PI * 0.495;

// A RAKING key light, not an overhead one. At the cartridge's true 1x vertical scale a
// cliff is a 20-byte rise across two cells, and a sun overhead flattens that to nothing.
// Low and to the north-west, it reads.
scene.add(new THREE.HemisphereLight(0x9fbcdd, 0x30271c, 0.42));
const sun = new THREE.DirectionalLight(0xfff4e2, 2.35);
sun.position.set(-0.62, 0.40, -0.34).multiplyScalar(60);
scene.add(sun);
const rim = new THREE.DirectionalLight(0x7f9fc4, 0.42);
rim.position.set(0.72, 0.30, 0.66).multiplyScalar(60);
scene.add(rim);

const world = new THREE.Group();          // scaled on Y by the exaggeration slider
scene.add(world);
// The asset mesh is NOT in that group: its vertices are model space and its ground
// height rides in an attribute the shader scales, so a building keeps its real
// proportions however far the terrain is exaggerated.
const assetWorld = new THREE.Group();
scene.add(assetWorld);

// ---------------------------------------------------------------- state
let META = null, MAP = null, MESH = null, WIRE = null, BORDER = null, WATER = null;
let MODE = 'tex', showWire = false, showBorder = true, showWater = true;
let showAssets = true, flatPads = true;
const assetOn = new Set(ASSET_GROUPS.map(g => g[0]));
let MODELS = null, ASSETS = null, SEA = null;
let exag = 1.0;
const atlases = {};
const MATS = {
  /* THE GROUND SITS AT ITS TRUE DEPTH. It is tempting to break the sea tie by pulling
     the ground toward the camera, and that works for the sea, but the ground is also
     what every flat decal is layered against: tiberium at +0.010, craters at +0.012.
     Pull the ground forward and it starts winning against those instead, which is the
     tiberium flicker. So the offset goes on the SEA, pushing it AWAY (see WATER_MAT):
     the sea loses the tie wherever the ground is opaque, which is what should happen,
     and where the ground is alpha-cut it wrote no depth and the sea shows through. */
  tex:   new THREE.MeshLambertMaterial({ color:0xffffff, alphaTest:0.5 }),
  white: new THREE.MeshLambertMaterial({ color:0xf2f2f2 }),
  vcol:  new THREE.MeshLambertMaterial({ vertexColors:true }),
};

// ---------------------------------------------------------------- helpers
const b64 = s => { const bin = atob(s), a = new Uint8Array(bin.length);
  for (let i=0;i<bin.length;i++) a[i]=bin.charCodeAt(i); return a; };

/** The atlas keeps its alpha. An alpha-0 texel on a water-capable tile is the
 *  cartridge's own statement that the cell shows SEA, and the sea is now drawn
 *  underneath, so the terrain alpha-cuts those texels instead of painting over
 *  them with a colour of ours. */
function loadAtlas(name){
  if (atlases[name]) return atlases[name];
  atlases[name] = new Promise(res => {
    const img = new Image();
    img.onload = () => {
      const c = document.createElement('canvas');
      c.width = img.width; c.height = img.height;
      const g = c.getContext('2d');
      g.drawImage(img,0,0);
      const t = new THREE.CanvasTexture(c);
      t.flipY = false; t.colorSpace = THREE.SRGBColorSpace;
      t.magFilter = THREE.LinearFilter; t.minFilter = THREE.LinearMipmapLinearFilter;
      t.anisotropy = renderer.capabilities.getMaxAnisotropy();
      t.generateMipmaps = true;
      res(t);
    };
    img.src = 'data/' + name;
  });
  return atlases[name];
}

/** THE REAL ASSETS. assets.bin / atlas.png come out of the game's own baked pack
 *  (playable/<SCEN>.pack), so the geometry, the UVs, the per-vertex colours and
 *  the textures on screen are the ones the shipped game draws. Nothing here is
 *  the viewer's invention. See export_pack.py for the byte formats.
 *  16 bytes per vertex: int16 x,y,z,u,v then u8 r,g,b,a then u16 atlas rect. */
async function loadModels(){
  if (MODELS) return MODELS;
  /* The mesh atlas and the sprite atlas are the same image sampled two ways: the
     structures bilinear so they match the ground, the infantry sprites point-sampled
     so a 16x12 man stays a crisp piece of pixel art. Two Texture objects over one
     Image costs one extra GL upload and nothing else. The shader's texel-centre
     clamp keeps a bilinear kernel strictly inside its own tile, so nothing bleeds
     across the atlas either way. */
  const atlasTex = (filter) => new Promise(res => {
    const im = new Image();
    im.onload = () => {
      const t = new THREE.Texture(im);
      t.flipY = false; t.colorSpace = THREE.SRGBColorSpace;
      t.magFilter = filter; t.minFilter = filter;
      t.generateMipmaps = false; t.needsUpdate = true;
      res(t);
    };
    im.src = 'data/atlas.png';
  });
  const [meta, geo, wrap, tex, sprTex] = await Promise.all([
    fetch('data/assets.json').then(r => r.json()),
    fetch('data/assets.bin').then(r => r.arrayBuffer()),
    fetch('data/assets_wrap.bin').then(r => r.arrayBuffer()),
    atlasTex(THREE.LinearFilter),
    atlasTex(THREE.NearestFilter),
  ]);
  const extra = await fetch('data/models.json').then(r => r.json());   // footprints, anchors
  MODELS = { meta, dv: new DataView(geo), wrap: new Uint8Array(wrap), tex,
             footprints: extra.footprints, centerBase: extra.centerBase,
             stopping: extra.stopping };
  ATLAS_SIZE.value.set(meta.atlasW, meta.atlasH);
  ATLAS_TEX.value = tex;

  const pixel = (src, repeat, filter) => new Promise(res => {
    const im = new Image();
    im.onload = () => {
      const t = new THREE.Texture(im);
      t.flipY = false; t.colorSpace = THREE.SRGBColorSpace;
      t.magFilter = filter; t.minFilter = filter;
      t.generateMipmaps = false;
      if (repeat){ t.wrapS = THREE.RepeatWrapping; t.wrapT = THREE.RepeatWrapping; }
      t.needsUpdate = true; res(t);
    };
    im.src = src;
  });
  // tiberium stays NEAREST: its 24x24 frames are packed edge to edge in the
  // filmstrip with no gutter, so bilinear would blend one growth stage into the next.
  // The sea's three tiles wrap, so bilinear is free of edge cases there.
  const [tib, w1, w2, sb] = await Promise.all([
    meta.tiberium ? pixel('data/tiberium.png', false, THREE.NearestFilter)
                  : Promise.resolve(null),
    pixel('data/water1.png', true, THREE.LinearFilter),
    pixel('data/water2.png', true, THREE.LinearFilter),
    pixel('data/seabed.png', true, THREE.LinearFilter),
  ]);
  /* depthWrite off, exactly as draw_tiberium does: a flat ground decal must never
     occlude anything, and depth TEST stays on so hills still hide distant fields. */
  if (tib) TIB_MAT = patchBase(new THREE.MeshBasicMaterial({
    map: tib, alphaTest: 0.5, side: THREE.DoubleSide, depthWrite: false }));
  SPRITE_MAT = new THREE.ShaderMaterial({
    uniforms: { map: { value: sprTex }, uExag: ASSET_EXAG },
    vertexShader: SPRITE_VERT, fragmentShader: SPRITE_FRAG,
    side: THREE.DoubleSide });
  WATER_MAT = new THREE.ShaderMaterial({
    uniforms: { uExag: ASSET_EXAG, uPhase: WATER_PHASE, uScroll: WATER_SCROLL,
                uW1: { value: w1 }, uW2: { value: w2 }, uBottom: { value: sb } },
    vertexShader: SEA_VERT, fragmentShader: SEA_FRAG,
    transparent: false, depthTest: true, depthWrite: true,
    side: THREE.DoubleSide, depthFunc: THREE.LessEqualDepth,
    // pushed AWAY from the camera, so the ground wins every tie it should win and
    // nothing else in the scene has to move
    polygonOffset: true, polygonOffsetFactor: 1, polygonOffsetUnits: 1 });
  return MODELS;
}

/* The cartridge's own 16-way wall table (RAM 0x802100D0), which cnc_eyes.cpp
   transcribes at line 1938. Index is the connectivity mask the exporter computed
   with the engine's own rule (CellClass::Wall_Update, cell.cpp:1406: bit i set
   when the cell adjacent in direction N,E,S,W carries the SAME overlay). Value is
   [which of the four authored pieces, how far to turn it]. The four pieces are
   named in the pack: SBAG straight, SBAG_L, SBAG_T, SBAG_X, and the same for
   CYCL, BRIK, BARB and WOOD. */
const TEXTURE_BOOK = { FACT: 'FACTT0', SILO: 'SILOT0', PROC: 'PROCU0' };
const WALL_SUFFIX = ['', '_L', '_T', '_X'];
const WALL_VARIANT = [
  [0,0],   [0,192], [0,0],   [1,64],
  [0,192], [0,192], [1,0],   [2,64],
  [0,0],   [1,128], [0,0],   [2,128],
  [1,192], [2,192], [2,0],   [3,0],
];

/** Which toggle group an object belongs to. Walls, tiberium and crates all arrive
 *  in [OVERLAY]; splitting them is what makes the toggles useful. */
function groupOf(kind, type){
  if (kind === KIND.smudge) return 'decals';
  if (kind === KIND.overlay){
    if (META.tiberium.indexOf(type) >= 0) return 'tiberium';
    if (META.crates.indexOf(type)   >= 0) return 'decals';
    return 'walls';       // walls proper, and the V1x overlay pieces
  }
  return ['terrain','buildings','units','infantry'][kind] || 'decals';
}

// ---------------------------------------------------------------- geometry
function buildMesh(m){
  const H = m.h, tiles = m.tiles;
  const AW = m.atlasWidth, AH = m.atlasHeight;
  const n = CELLS*CELLS*4;
  const pos = new Float32Array(n*3), nor = new Float32Array(n*3), uv = new Float32Array(n*2);
  const idx = new Uint32Array(CELLS*CELLS*6);

  // per-corner normal from the shared 65-stride grid, the way the console's vertex
  // builder samples its x+-1 / y+-1 neighbours
  const nx = new Float32Array(CORNERS*CORNERS), ny = new Float32Array(CORNERS*CORNERS),
        nz = new Float32Array(CORNERS*CORNERS);
  const at = (x,y) => H[Math.min(CORNERS-1,Math.max(0,y))*CORNERS + Math.min(CORNERS-1,Math.max(0,x))];
  for (let y=0;y<CORNERS;y++) for (let x=0;x<CORNERS;x++){
    const dx = (at(x+1,y)-at(x-1,y)) / (2*BYTE_PER_CELL);
    const dz = (at(x,y+1)-at(x,y-1)) / (2*BYTE_PER_CELL);
    const l = Math.hypot(-dx,1,-dz), i = y*CORNERS+x;
    nx[i] = -dx/l; ny[i] = 1/l; nz[i] = -dz/l;
  }

  const HALF = CELLS/2;
  let v = 0, f = 0;
  for (let cy=0; cy<CELLS; cy++) for (let cx=0; cx<CELLS; cx++){
    const slot = tiles[cy*CELLS+cx];
    const col = slot % COLS, row = (slot/COLS)|0;
    const x0 = (col*PITCH + GUTTER)/AW, x1 = (col*PITCH + GUTTER + TS)/AW;
    const y0 = (row*PITCH + GUTTER)/AH, y1 = (row*PITCH + GUTTER + TS)/AH;
    const c = [[cx,cy,x0,y0],[cx+1,cy,x1,y0],[cx,cy+1,x0,y1],[cx+1,cy+1,x1,y1]];
    const base = v;
    for (const [gx,gy,u,vv] of c){
      const ci = gy*CORNERS+gx;
      pos[v*3]   = gx - HALF;
      pos[v*3+1] = H[ci] / BYTE_PER_CELL;
      pos[v*3+2] = gy - HALF;
      nor[v*3] = nx[ci]; nor[v*3+1] = ny[ci]; nor[v*3+2] = nz[ci];
      uv[v*2] = u; uv[v*2+1] = vv;
      v++;
    }
    idx[f++]=base; idx[f++]=base+2; idx[f++]=base+1;
    idx[f++]=base+1; idx[f++]=base+2; idx[f++]=base+3;
  }

  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos,3));
  g.setAttribute('normal',   new THREE.BufferAttribute(nor,3));
  g.setAttribute('uv',       new THREE.BufferAttribute(uv,2));
  g.setAttribute('color',    new THREE.BufferAttribute(new Float32Array(n*3),3));
  g.setIndex(new THREE.BufferAttribute(idx,1));
  g.computeBoundingSphere();
  return g;
}

const _c = new THREE.Color(), _t2 = new THREE.Color();
function paint(mode){
  const g = MESH.geometry, col = g.getAttribute('color'), a = col.array;
  const fam = MAP.fam, H = MAP.h;
  for (let cy=0, v=0; cy<CELLS; cy++) for (let cx=0; cx<CELLS; cx++){
    for (let k=0;k<4;k++, v++){
      if (mode === 'fam'){
        _c.set(FAM_COLOR[fam[cy*CELLS+cx]] || '#666');
      } else {
        const gx = cx + (k&1), gy = cy + (k>>1);
        const h = H[gy*CORNERS+gx];
        let i = 0; while (i < LADDER.length-2 && h >= LADDER[i+1]) i++;
        let t = (h - LADDER[i]) / (LADDER[i+1] - LADDER[i]);
        t = Math.max(0, Math.min(1, t));
        t = t*t*(3-2*t);                       // hold near the rungs, sweep between them
        _c.set(TIER_COLOR[i]).lerp(_t2.set(TIER_COLOR[i+1]), t);
      }
      _c.convertSRGBToLinear();
      a[v*3]=_c.r; a[v*3+1]=_c.g; a[v*3+2]=_c.b;
    }
  }
  col.needsUpdate = true;
}

/* --------------------------------------------------------------- assets
   One merged mesh per mission, drawn from the game's own baked geometry and its
   own texture atlas. Two things ride in extra attributes:

   aBase   the terrain height the object stands on. It is added in the VERTEX
           shader and scaled by the exaggeration slider there, so the ground can
           be stretched without stretching the buildings. Within one object every
           vertex shares the value, so it is a pure translation and the normals
           stay correct.
   aRect   the object's texture inside the atlas, in texels, plus aWrap, the
           cartridge's per-TRIANGLE wrap mode (S = wrap & 3, T = (wrap >> 2) & 3;
           2 clamp, 1 mirror, else repeat -- cnc_eyes.cpp:1708). It has to be per
           triangle, not per texture, because the same foliage sheet is clamped on
           a tree crown and repeated elsewhere, and 27.5% of the game's triangles
           have UVs outside [0,1]. The fragment shader does the wrap inside the
           rect, which is what makes an atlas legal here at all. */
const ASSET_EXAG = { value: 1 };
const ATLAS_SIZE = { value: new THREE.Vector2(512, 512) };
const ATLAS_TEX = { value: null };

const WRAP_GLSL = `
float wrapAxis(float t, float m){
  if (m > 1.5) return clamp(t, 0.0, 1.0);                 // clamp
  if (m > 0.5){ float f = fract(t * 0.5) * 2.0;           // mirror
                return f > 1.0 ? 2.0 - f : f; }
  return fract(t);                                        // repeat
}`;

function patchAtlas(mat){
  mat.onBeforeCompile = sh => {
    sh.uniforms.uExag = ASSET_EXAG;
    sh.uniforms.uAtlas = ATLAS_SIZE;
    sh.uniforms.uAtlasTex = ATLAS_TEX;
    sh.vertexShader =
      'attribute float aBase;\nattribute vec4 aRect;\nattribute vec2 aWrap;\n' +
      'attribute vec2 aUv;\nuniform float uExag;\n' +
      'varying vec4 vRect;\nvarying vec2 vWrap;\nvarying vec2 vUvRaw;\n' +
      sh.vertexShader.replace('#include <begin_vertex>',
        'vec3 transformed = vec3( position );\n' +
        '  transformed.y += aBase * uExag;\n' +
        '  vRect = aRect; vWrap = aWrap; vUvRaw = aUv;');
    sh.fragmentShader =
      'uniform vec2 uAtlas;\nuniform sampler2D uAtlasTex;\n' +
      'varying vec4 vRect;\nvarying vec2 vWrap;\n' +
      'varying vec2 vUvRaw;\n' + WRAP_GLSL + '\n' +
      sh.fragmentShader.replace('#include <map_fragment>', `
      if (vRect.z > 0.5){
        vec2 tt = vec2(wrapAxis(vUvRaw.x, vWrap.x), wrapAxis(vUvRaw.y, vWrap.y));
        // NO V FLIP. The pack's V is top-down and so is the atlas: cnc_eyes uploads
        // texdata with glTexImage2D and no swizzle, PIL writes row 0 at the top, and
        // flipY is false, so both conventions are top-first. tools/art/fbxout.py
        // carries a 1.0 - v ONLY because FBX is bottom-left, which is that tool
        // stating the same thing. A flip was tried here to chase orange shards on
        // tree trunks; the shards were a clamp-edge artefact and the flip made them
        // MORE likely, not less (it lands clamp on the crown sheet's 9/32-opaque row
        // 0 instead of its 1/32-opaque row 31).
        // Land on a TEXEL CENTRE inside this tile and never outside it. The old form
        // was rect.xy + tt*rect.zw + 0.5, which at tt = 1 addresses texel N of an
        // N-texel tile: one past the end, i.e. the atlas neighbour. That is where the
        // orange shards on tree trunks came from, and why a V flip appeared to fix
        // them (it only moved which neighbour got sampled).
        vec2 texel = vRect.xy + clamp(tt * vRect.zw, vec2(0.5), vRect.zw - 0.5);
        vec4 sampledDiffuseColor = texture2D(uAtlasTex, texel / uAtlas);
        diffuseColor *= sampledDiffuseColor;
      }`);
  };
  return mat;
}

/* UNLIT, because the cartridge is. GL_LIGHTING appears nowhere in cnc_eyes.cpp:
   every mesh vertex emits glColor4ub(cr, cg, cb, v.a) under GL_MODULATE, so the
   per-vertex colour baked into the pack IS the shading. Running it through a
   Lambert lamp as well multiplies the shade in twice and turns every building
   nearly black, which is exactly what it did. */
const assetMat = patchAtlas(new THREE.MeshBasicMaterial({
  vertexColors: true, alphaTest: 0.5, side: THREE.DoubleSide }));
/* MODE_SHADOW and MODE_XLU: blended, depth tested, no depth write
   (cnc_eyes.cpp:669-676). Shadow faces are baked geometry that happens to be a
   silhouette, so they keep their own dark vertex colour and alpha. */
const assetBlendMat = patchAtlas(new THREE.MeshBasicMaterial({
  vertexColors: true, transparent: true, depthWrite: false,
  side: THREE.DoubleSide }));
const SHADOW_LIFT = 0.012, SHADOW_DX = 5/256, SHADOW_DZ = -5/256;

/** the same ground-riding vertex patch, for materials that sample normally */
function patchBase(mat){
  mat.onBeforeCompile = sh => {
    sh.uniforms.uExag = ASSET_EXAG;
    sh.vertexShader = 'attribute float aBase;\nuniform float uExag;\n' +
      sh.vertexShader.replace('#include <begin_vertex>',
        'vec3 transformed = vec3( position );\n  transformed.y += aBase * uExag;');
  };
  return mat;
}
let TIB_MAT = null, SEABED_MAT = null, WATER_MAT = null, SPRITE_MAT = null;

/* INFANTRY, as the console draws them: one camera-facing billboard per man, out of
   the cartridge's own sprite strips, not a mesh and not a coloured pip.

   A strip is a vertical filmstrip laid out facing-major, and the row order is
   counter-clockwise storing only the WESTERN half (N, NW, W, SW, S), so the eastern
   octants draw mirrored (cnc_eyes.cpp:5890-5917). One cell of the strip is fw by fh
   texels and 24 texels is one cell in the world under the N64 camera
   (sprite_texels_per_unit), so a man is about two thirds of a cell wide. */
const SPRITE_VERT = `
attribute vec2 aCorner;
attribute vec2 aSize;
attribute float aBase;
uniform float uExag;
varying vec2 vUv;
void main(){
  vec3 p = position;
  p.y += aBase * uExag;
  vec4 mv = modelViewMatrix * vec4(p, 1.0);
  mv.xy += vec2(aCorner.x * aSize.x, aCorner.y * aSize.y);
  vUv = uv;
  gl_Position = projectionMatrix * mv;
}`;
const SPRITE_FRAG = `
uniform sampler2D map;
varying vec2 vUv;
void main(){
  vec4 c = texture2D(map, vUv);
  if (c.a < 0.5) discard;
  gl_FragColor = c;
}`;

/* THE 1995 DOS INFANTRY.
 *
 * The cartridge draws infantry as five stored facings mirrored to eight; the PC
 * game stores all eight and mirrors nothing. The two were compared, and the one chosen was the
 * DOS art, which the game itself already ships (game/dosinfantry.pack), so the
 * viewer bakes the same sprites out of the same CONQUER.MIX with the same palette
 * and the same crop rule -- tools/heightmap-viewer/export_dosinf.py, verified
 * texel for texel against the shipped pack.
 *
 * Loaded separately from the model atlas and guarded: without dosinf.png the
 * viewer falls straight back to the cartridge billboards rather than losing its
 * infantry. */
let DOSINF = null, DOSINF_MAT = null;
async function loadDosInfantry(){
  if (DOSINF !== null) return DOSINF;
  try {
    const meta = await (await fetch('data/dosinf.json')).json();
    const tex = await new Promise((res, rej) => {
      const img = new Image();
      img.onload = () => {
        const t = new THREE.Texture(img);
        t.flipY = false; t.colorSpace = THREE.SRGBColorSpace;
        t.magFilter = THREE.NearestFilter;      // 1995 pixels stay 1995 pixels
        t.minFilter = THREE.NearestFilter;
        t.needsUpdate = true;
        res(t);
      };
      img.onerror = rej;
      img.src = 'data/' + (meta.sheet || 'dosinf.png');
    });
    DOSINF = meta;
    DOSINF_MAT = new THREE.ShaderMaterial({
      uniforms: { map: { value: tex }, uExag: ASSET_EXAG },
      vertexShader: SPRITE_VERT, fragmentShader: SPRITE_FRAG,
      side: THREE.DoubleSide });
  } catch (e) {
    DOSINF = false;                              // asked once, absent, stop asking
  }
  return DOSINF;
}

/* The engine's own facing pick, not a re-derivation of it: facenum =
 * HumanShape[Facing32[dir & 255]], infantry.cpp:574, transcribed into
 * game/dosinf_mod.h:86-99 and carried in dosinf.json so the two cannot drift.
 * Eight stored facings counter-clockwise from north, and no mirroring at all. */
function dosPickFacing(face){
  if (!DOSINF) return 0;
  const dir = ((face | 0) + 256) & 255;
  return DOSINF.humanShape[DOSINF.facing32[dir]] & 7;
}

/** facing -> which row of the strip, and whether it draws mirrored */
function spritePickFacing(nfac, face){
  let o = 0, m = false;
  if (nfac > 1 && face >= 0){
    o = ((face + 16) & 255) >> 5;          // nearest of 8, no yaw bias under the N64 camera
    m = (o >= 1 && o <= 3);
  }
  let r = (o <= 4) ? o : 8 - o;
  if (r >= nfac) r = nfac - 1;
  return { row: r, mirror: m };
}

/* THE SEA, as the cartridge draws it (cnc_eyes.cpp:4444-4620).

   There is no water plane. The sea re-emits the TERRAIN's own four corner vertices
   for every cell whose tile art punches an alpha hole, so it is watertight against
   the ground by construction. Two passes: the sea floor (BOTTOM.IMG, opaque, one
   tile per cell) and the surface (WATER1 x WATER2 modulated, alpha 170/255, no
   depth write). The ground is drawn opaque with an alpha cut and keeps its depth,
   which is what gives the shoreline its hard art-texel edge.

   UV. The console does not tile the water linearly. Its vertex builder rewrites s
   and t as 0.72*x + 30*sin(phase + (x+z)) in WORLD units, which through the tile
   descriptors is 1.92 repeats per cell with a 0.3125-repeat warp. Restated in the
   viewer's cell units below, keeping the warp argument in world units because that
   is what makes it a per-vertex jitter rather than a smooth swell.

   Rates are per cartridge tick and the cartridge ticks at 15 Hz, so a wall-clock
   page multiplies seconds by 15 to keep the same visual speed. */
const WATER_TIME = { value: 0 };
const WATER_SCROLL = { value: new THREE.Vector4(0, 0, 0, 0) };
const WATER_PHASE = { value: 0 };
const SEA_VERT = `
attribute float aBase;
attribute vec2 aCell;
attribute float aWarp;
uniform float uExag;
uniform float uPhase;
uniform vec4 uScroll;
varying vec2 vUv1;
varying vec2 vUv2;
varying vec2 vSeaUv;
void main(){
  vec3 p = position;
  p.y += aBase * uExag;
  // The cartridge's warp argument is (x + z) in WORLD units, which reaches 32768 and
  // loses so much float precision in a shader that the sea strobes. The constant part
  // is per vertex, so it is folded to [0, 2pi) on the CPU in double precision.
  float arg = uPhase + aWarp;
  vec2 lin = aCell * 1.92 + vec2(0.3125 * sin(arg), 0.3125 * cos(arg));
  vUv1 = lin + uScroll.xy;
  vUv2 = lin + uScroll.zw;
  vSeaUv = aCell;                      // the floor is exactly one tile per cell
  gl_Position = projectionMatrix * modelViewMatrix * vec4(p, 1.0);
}`;
/* ONE OPAQUE DRAW, not two passes. The floor and the surface share the terrain's own
   corner vertices, so they sit at exactly the same depth and nothing can come between
   them; compositing them in the fragment shader is exactly equivalent and removes two
   real defects at once. A transparent material would be queued after every opaque
   object in three.js regardless of renderOrder, which inverts the console's
   seabed-water-terrain order and washes blue over the opaque half of every partial
   shore cell. And the RDP modulates and blends on 8-bit stored values, in GAMMA space:
   doing it linearly costs up to about 30/255, which is visible. So both texture sets
   are sampled raw, combined in gamma, and converted once at the end. */
const SEA_FRAG = `
uniform sampler2D uW1;
uniform sampler2D uW2;
uniform sampler2D uBottom;
varying vec2 vUv1;
varying vec2 vUv2;
varying vec2 vSeaUv;
vec3 s2l(vec3 c){
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
void main(){
  vec3 w = texture2D(uW1, vUv1).rgb * texture2D(uW2, vUv2).rgb;   // RDP MODULATE
  vec3 b = texture2D(uBottom, vSeaUv).rgb;                        // the sea floor
  vec3 c = mix(b, w, 170.0 / 255.0);                              // RDP alpha blend
  gl_FragColor = vec4(s2l(c), 1.0);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
}`;

/** The cartridge's rates are per tick and it ticks at 15 Hz. Folded on the CPU, in
 *  double precision, so a long-running page never loses sub-texel precision. */
function seaClock(t){
  const F = t * 15.0;
  WATER_PHASE.value = (F * 0.0625) % (Math.PI * 2);
  WATER_SCROLL.value.set(
    -((6.5 * F + 65) % 128) / 128, -((4.6 * F + 88) % 128) / 128,
    -((2.8 * F - 23) % 128) / 128, -((2.2 * F - 43) % 128) / 128);
}


/* THE SEA, as the cartridge draws it (cnc_eyes.cpp:4444-4620).

   There is no water plane. The sea re-emits the TERRAIN's own four corner vertices
   for every cell whose tile art punches an alpha hole, so it is watertight against
   the ground by construction. Two passes: the sea floor (BOTTOM.IMG, opaque, one
   tile per cell) and the surface (WATER1 x WATER2 modulated, alpha 170/255, no
   depth write). The ground is drawn opaque with an alpha cut and keeps its depth,
   which is what gives the shoreline its hard art-texel edge.

   UV. The console does not tile the water linearly. Its vertex builder rewrites s
   and t as 0.72*x + 30*sin(phase + (x+z)) in WORLD units, which through the tile
   descriptors is 1.92 repeats per cell with a 0.3125-repeat warp. Restated in the
   viewer's cell units below, keeping the warp argument in world units because that
   is what makes it a per-vertex jitter rather than a smooth swell.

   Rates are per cartridge tick and the cartridge ticks at 15 Hz, so a wall-clock
   page multiplies seconds by 15 to keep the same visual speed. */


/** THE BUILDING PADS. A building placed on a ramp has one corner buried and another
 *  in the air, because the 1995 engine has no height and will put a structure on any
 *  cell the rules allow. The cartridge's artists answered it by hand: of 211
 *  pre-placed buildings across twelve missions, 169 stand on corners that are all
 *  within one height unit of each other. They levelled the ground under a building.
 *
 *  So the renderer generalises their own choice, exactly as cnc_eyes.cpp:822-843 does:
 *  every building levels the corners of its own footprint to the HIGHEST ground it
 *  stands on. Highest, not mean, because a pad level with the high side can never bury
 *  anything and the low side simply steps down at the pad's edge, which is what a
 *  cut-and-fill platform looks like. Where two pads share a corner the taller wins, so
 *  the result does not depend on object order.
 *
 *  This is the renderer's, not the cartridge's, and it is registered that way
 *  as an open question. The toggle turns it off for the A/B. */
function padHeights(m){
  const H = m.h;
  if (!flatPads || !MODELS) return H;
  const fp = MODELS.footprints || {};
  const out = new Uint8Array(H);
  const padH = new Uint8Array(CORNERS * CORNERS), padOn = new Uint8Array(CORNERS * CORNERS);
  let n = 0;
  for (const ob of (m.objs || [])){
    if (ob.kind !== KIND.structure) continue;
    const { type, cell } = ob;
    const f = fp[type] || [1,1];
    const x0 = cell & 63, z0 = cell >> 6;
    let top = 0;
    for (let dz = 0; dz <= f[1]; dz++) for (let dx = 0; dx <= f[0]; dx++){
      const cx = x0+dx, cz = z0+dz;
      if (cx < 0 || cx > 64 || cz < 0 || cz > 64) continue;
      const h = H[cz*CORNERS+cx];
      if (h > top) top = h;
    }
    for (let dz = 0; dz <= f[1]; dz++) for (let dx = 0; dx <= f[0]; dx++){
      const cx = x0+dx, cz = z0+dz;
      if (cx < 0 || cx > 64 || cz < 0 || cz > 64) continue;
      const k = cz*CORNERS+cx;
      if (!padOn[k] || top > padH[k]){ padH[k] = top; padOn[k] = 1; }
    }
    n++;
  }
  let raised = 0;
  for (let k = 0; k < CORNERS*CORNERS; k++)
    if (padOn[k]){ if (padH[k] !== out[k]) raised++; out[k] = padH[k]; }
  m.padStats = { buildings: n, raised };
  return out;
}

/** Terrain height in cell units, sampled the way the surface is actually built.
 *  Each cell is two triangles sharing the NE-SW diagonal, so a bilinear sample is
 *  a different surface wherever the four corners are not coplanar. cnc_eyes.cpp
 *  measures that gap at 0.121 cell on SCG01EA and 0.262 on SCG02EA, and names it
 *  as the reason objects float or sink on slopes. This is the planar form. */
/** Terrain height in cell units, sampled the way the surface is actually built.
 *  Each cell is two triangles sharing the NE-SW diagonal, so a bilinear sample is
 *  a different surface wherever the four corners are not coplanar. cnc_eyes.cpp
 *  measures that gap at 0.121 cell on SCG01EA and 0.262 on SCG02EA, and names it
 *  as the reason objects float or sink on slopes. This is the planar form. */
function groundAt(H, x, z){
  const cx = Math.max(0, Math.min(63.999, x)), cz = Math.max(0, Math.min(63.999, z));
  const x0 = cx | 0, z0 = cz | 0, fx = cx - x0, fz = cz - z0;
  const h00 = H[z0*CORNERS+x0],       h10 = H[z0*CORNERS+x0+1];
  const h01 = H[(z0+1)*CORNERS+x0],   h11 = H[(z0+1)*CORNERS+x0+1];
  const h = (fx + fz <= 1)
    ? h00 + (h10-h00)*fx + (h01-h00)*fz
    : h11 + (h01-h11)*(1-fx) + (h10-h11)*(1-fz);
  return h / BYTE_PER_CELL;
}

/** The packed 8-byte records, as objects you can edit. Field meanings are in
 *  export_assets.py: kind|house<<4, type index, cell, facing, and a spare byte
 *  carrying the infantry sub-cell or a wall's connectivity mask. */
function decodeObjects(m){
  const raw = b64(m.objects);
  const dv = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
  const out = [];
  for (let o = 0; o + 8 <= raw.length; o += 8){
    const kh = dv.getUint8(o);
    out.push({
      kind: kh & 15, house: kh >> 4,
      type: META.objectTypes[dv.getUint16(o + 1, true)],
      cell: dv.getUint16(o + 3, true),
      face: dv.getUint8(o + 5),
      sub:  dv.getUint8(o + 6),
    });
  }
  return out;
}

/** cell -> the objects standing on it, for the hover readout. */
function assetIndex(m){
  const byCell = new Map();
  const fp = (MODELS && MODELS.footprints) || {};
  for (const ob of (m.objs || [])){
    const { kind, house, type, cell } = ob;
    const cx = cell & 63, cy = cell >> 6;
    const f = (kind === KIND.structure && fp[type]) ? fp[type] : [1,1];
    for (let dy = 0; dy < f[1]; dy++) for (let dx = 0; dx < f[0]; dx++){
      const k = (cy+dy)*64 + (cx+dx);
      if (!byCell.has(k)) byCell.set(k, []);
      byCell.get(k).push({ type, kind, house });
    }
  }
  return byCell;
}

function buildAssets(m, opts){
  const A = MODELS, meta = A.meta, dv = A.dv, wrapOf = A.wrap;
  const H = m.h, HALF = CELLS/2;
  const fp = A.footprints || {}, cb = A.centerBase || {}, st = A.stopping || [];
  const seen = { missing: new Set(), meshes: 0, tib: 0, pips: 0, walls: 0, decals: 0 };

  // two buckets: solid (opaque + cutout) and blended (shadow + xlu)
  const B = [ {pos:[],col:[],uv:[],rect:[],wrp:[],base:[]},
              {pos:[],col:[],uv:[],rect:[],wrp:[],base:[]} ];
  let pos = B[0].pos, col = B[0].col, uv = B[0].uv,
      rect = B[0].rect, wrp = B[0].wrp, base = B[0].base;
  const c = new THREE.Color();

  /** copy one baked triangle range into the buffers, rotated and anchored */
  function emitMesh(mi, ox, oz, y0, face, house){
    const md = meta.meshes[mi];
    if (!md) return false;
    const a = face < 0 ? 0 : -(face * Math.PI * 2 / 256);
    const ca = Math.cos(a), sa = Math.sin(a);
    const gdi = (house === 0 || house === 4) ? meta.gdiOf : null;   // GoodGuy, Multi1
    for (let t = md.t0; t < md.t0 + md.n; t++){
      const w = wrapOf[t] & 15, mode = wrapOf[t] >> 4;
      const ws = w & 3, wt = (w >> 2) & 3;
      const b = B[mode >= 2 ? 1 : 0];
      const lift = (mode === 2) ? SHADOW_LIFT : 0;
      const nx = (mode === 2) ? SHADOW_DX : 0, nz = (mode === 2) ? SHADOW_DZ : 0;
      for (let k = 0; k < 3; k++){
        const o = (t * 3 + k) * 16;
        const vx = dv.getInt16(o, true)     * MODEL_SCALE;
        const vy = dv.getInt16(o + 2, true) * MODEL_SCALE;
        const vz = dv.getInt16(o + 4, true) * MODEL_SCALE;
        b.pos.push(ox - HALF + (vx*ca + vz*sa) + nx, vy + lift,
                   oz - HALF + (-vx*sa + vz*ca) + nz);
        b.uv.push(dv.getInt16(o + 6, true) / meta.uvQuant,
                  dv.getInt16(o + 8, true) / meta.uvQuant);
        b.col.push(dv.getUint8(o + 10)/255, dv.getUint8(o + 11)/255,
                   dv.getUint8(o + 12)/255, dv.getUint8(o + 13)/255);
        let r = dv.getUint16(o + 14, true);
        if (gdi && gdi[r] !== undefined) r = gdi[r];      // the house variant blob
        const R = (r !== 0xFFFF && meta.rects[r]) ? meta.rects[r] : [0,0,0,0];
        b.rect.push(R[0], R[1], R[2], R[3]);
        b.wrp.push(ws, wt);
        b.base.push(y0);
      }
    }
    return true;
  }

  function emitQuad(cx, cy, u0, v0, u1, v1, lift){
    // a ground decal on the cell's own four corner heights, like the game's
    const P = [[cx, cy, u0, v0], [cx+1, cy, u1, v0],
               [cx+1, cy+1, u1, v1], [cx, cy+1, u0, v1]];
    for (const i of [0,2,1, 0,3,2]){
      const q = P[i];
      pos.push(q[0]-HALF, lift, q[1]-HALF);
      uv.push(q[2], q[3]);
      col.push(1,1,1,1);
      rect.push(0,0,0,0);            // tiberium has its own mesh and material
      wrp.push(0,0);
      base.push(groundAt(H, q[0], q[1]));
    }
  }

  // ---- infantry billboards and tiberium, each in its own buffers ------------
  const SPR = { pos: [], corner: [], size: [], uv: [], base: [] };
  const DSPR = { pos: [], corner: [], size: [], uv: [], base: [] };
  const tibPos = [], tibUv = [], tibBase = [];
  const TIB = meta.tiberium;
  const tibCells = new Set();
  for (const ob of (m.objs || []))
    if (ob.kind === KIND.overlay && META.tiberium.indexOf(ob.type) >= 0)
      tibCells.add(ob.cell);

  for (const ob of (m.objs || [])){
    const { kind, house, type, cell, sub } = ob;
    const iniFace = ob.face;
    const cx = cell & 63, cy = cell >> 6;
    const g = groupOf(kind, type);
    if (!(opts && opts.all) && !assetOn.has(g)) continue;

    if (g === 'tiberium'){
      /* The cartridge's own ANY_TI filmstrip, one flat quad per cell hugging the
         terrain's four corners, exactly as draw_tiberium does. The growth frame is
         not in the INI: CellClass::Tiberium_Adjust counts how many of the EIGHT
         adjacent cells also hold tiberium and takes _adj[count]. */
      if (!TIB) continue;
      let n = 0;
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++){
        if (!dx && !dy) continue;
        const nx = cx + dx, ny = cy + dy;
        if (nx >= 0 && nx < 64 && ny >= 0 && ny < 64 && tibCells.has(ny*64+nx)) n++;
      }
      const stage = Math.min(TIB.frames - 1, TIB.growth[Math.min(8, n)]);
      const kindIdx = Math.min(TIB.types - 1, Math.max(0, parseInt(type.slice(2), 10) - 1));
      const sl = TIB.slots[kindIdx * TIB.frames + stage];
      if (!sl) continue;
      const x0 = sl[0] * TIB.texW + sl[1], y0 = sl[2];
      const u0 = (x0 + 0.5) / TIB.atlasW, u1 = (x0 + TIB.frameW - 0.5) / TIB.atlasW;
      const v0 = (y0 + 0.5) / TIB.atlasH, v1 = (y0 + TIB.frameH - 0.5) / TIB.atlasH;
      const P = [[cx, cy, u0, v0], [cx+1, cy, u1, v0],
                 [cx+1, cy+1, u1, v1], [cx, cy+1, u0, v1]];
      for (const i of [0,2,1, 0,3,2]){
        const q = P[i];
        tibPos.push(q[0]-HALF, 0.010, q[1]-HALF);
        tibUv.push(q[2], q[3]);
        tibBase.push(groundAt(H, q[0], q[1]));
      }
      seen.tib++;
      continue;
    }

    if (g === 'decals'){
      // craters and scorch marks: no mesh on the cartridge and none needed
      const P = [[cx+0.08, cy+0.08], [cx+0.92, cy+0.08], [cx+0.92, cy+0.92], [cx+0.08, cy+0.92]];
      c.set('#2b2620').convertSRGBToLinear();
      for (const i of [0,2,1, 0,3,2]){
        const q = P[i];
        pos.push(q[0]-HALF, 0.012, q[1]-HALF);
        uv.push(0,0); col.push(c.r, c.g, c.b, 1);
        rect.push(0,0,0,0); wrp.push(0,0);
        base.push(groundAt(H, q[0], q[1]));
      }
      seen.decals++;
      continue;
    }

    // ---- anchors, one rule per class, none of them interchangeable ----------
    const f = (kind === KIND.structure && fp[type]) ? fp[type] : [1,1];
    let ox, oz;
    if (kind === KIND.structure)     { ox = cx + f[0]/2; oz = cy + f[1]/2; }
    else if (kind === KIND.terrain)  { const b = cb[type] || [0.5,0.5];
                                       ox = cx + b[0]; oz = cy + b[1]; }
    else if (kind === KIND.infantry) { const b = st[sub] || [0.5,0.5];
                                       ox = cx + b[0]; oz = cy + b[1]; }
    else                             { ox = cx + 0.5;  oz = cy + 0.5; }
    const y0 = groundAt(H, ox, oz);

    /* Rotation. cnc_eyes hands buildings and terrain -1 because the cartridge's
       Spawn_Model takes no rotation argument; unit meshes are authored nose-SOUTH
       so they get +128, and BOAT, whose bow is authored west, gets +64. */
    let key = type, face = -1;
    if (kind === KIND.unit) face = (iniFace + (type === 'BOAT' ? 64 : 128)) & 255;

    if (g === 'walls' && WALL_VARIANT_HAS(type)){
      /* Wall snapping, the game's way. The exporter computed the connectivity mask
         with the engine's own rule (CellClass::Wall_Update: bit i set when the cell
         adjacent in N,E,S,W carries the SAME overlay), and the cartridge's 16-way
         table says which of the four authored pieces to use and how far to turn it.
         The pack names those pieces, so there is no guessing: SBAG, SBAG_L, SBAG_T,
         SBAG_X and the same for CYCL, BRIK, BARB and WOOD. */
      const wv = WALL_VARIANT[sub & 15];
      key = type + WALL_SUFFIX[wv[0]];
      /* The wall pass does NOT hand the table's facing straight to the mesh: it goes
         through wall_face(), which is (256 - face) & 255 (cnc_eyes.cpp:5553), and only
         then through the usual -face*2pi/256. Net, a wall turns the OTHER way from
         everything else. Passing the raw value put every L and T piece 180 degrees out,
         which is why four wall cells in a square did not close into a square. */
      face = (256 - wv[1]) & 255;
      seen.walls++;
    }

    if (kind === KIND.infantry){
      if (DOSINF){
        const rows = DOSINF.types[type] || DOSINF.types['E1'];
        const side = (house === 1 || house === 5) ? 'nod' : 'gdi';   // BadGuy, Multi2
        const R = rows && (rows[side] || rows.gdi);
        if (!R){ seen.missing.add(type); continue; }
        const r = R[dosPickFacing(iniFace)];
        const u0 = (r.x + 0.5) / DOSINF.sheetW, u1 = (r.x + r.w - 0.5) / DOSINF.sheetW;
        const v0 = (r.y + 0.5) / DOSINF.sheetH, v1 = (r.y + r.h - 0.5) / DOSINF.sheetH;
        // DOS art is authored at 24 px per cell and so is the cartridge's, which is
        // why the same divisor serves both
        const wid = r.w / meta.spriteTexelsPerUnit, hgt = r.h / meta.spriteTexelsPerUnit;
        const Q = [[-0.5, 0, u0, v1], [0.5, 0, u1, v1],
                   [0.5, 1, u1, v0], [-0.5, 1, u0, v0]];
        for (const i of [0, 2, 1, 0, 3, 2]){
          const q = Q[i];
          DSPR.pos.push(ox - HALF, 0, oz - HALF);
          DSPR.corner.push(q[0], q[1]);
          DSPR.size.push(wid, hgt);
          DSPR.uv.push(q[2], q[3]);
          DSPR.base.push(y0);
        }
        seen.pips++;
        continue;
      }
      const inf = meta.infantry[type] || meta.infantry['E1'];
      const hs = (house === 1 || house === 5) ? 1 : 0;      // BadGuy / Multi2 are Nod
      const si = inf ? inf[hs * 3 + 0] : -1;                // 0 = the STAND strip
      const sp = si >= 0 ? meta.sprites[si] : null;
      if (!sp || sp.rect < 0){ seen.missing.add(type); continue; }
      const R = meta.rects[sp.rect];
      const nfac = sp.facings > 0 ? sp.facings : 1;
      const { row, mirror } = spritePickFacing(nfac, iniFace);
      const stages = Math.max(1, Math.floor(sp.frames / nfac));
      const frame = Math.min(sp.frames - 1, row * stages);
      let u0 = (R[0] + 0.5) / meta.atlasW, u1 = (R[0] + sp.fw - 0.5) / meta.atlasW;
      if (mirror){ const t = u0; u0 = u1; u1 = t; }
      const v0 = (R[1] + frame * sp.fh + 0.5) / meta.atlasH;
      const v1 = (R[1] + (frame + 1) * sp.fh - 0.5) / meta.atlasH;
      const wid = sp.fw / meta.spriteTexelsPerUnit, hgt = sp.fh / meta.spriteTexelsPerUnit;
      // corner = (across, up); the man stands ON the ground and rises by hgt
      const Q = [[-0.5, 0, u0, v1], [0.5, 0, u1, v1],
                 [0.5, 1, u1, v0], [-0.5, 1, u0, v0]];
      for (const i of [0, 2, 1, 0, 3, 2]){
        const q = Q[i];
        SPR.pos.push(ox - HALF, 0, oz - HALF);
        SPR.corner.push(q[0], q[1]);
        SPR.size.push(wid, hgt);
        SPR.uv.push(q[2], q[3]);
        SPR.base.push(y0);
      }
      seen.pips++;
      continue;
    }

    const mi = meta.types[key];
    if (mi === undefined || mi < 0){
      seen.missing.add(type);
      continue;
    }
    if (emitMesh(mi, ox, oz, y0, face, house)) seen.meshes++;
    /* Three structures ship a second mesh that is extra GEOMETRY rather than a
       texture swap, and the console draws it on top of the base every frame.
       (PROC/T, FIX/T and HPAD/T are the swap kind: they overdraw the base entirely
       and carry only a state readout, so a static viewer honestly leaves them out.) */
    const bk = TEXTURE_BOOK[key];
    if (bk !== undefined && meta.types[bk] >= 0)
      emitMesh(meta.types[bk], ox, oz, y0, face, house);
  }

  const group = new THREE.Group();
  [[0, assetMat, 4], [1, assetBlendMat, 6]].forEach(([bi, mat, order]) => {
    const b = B[bi];
    if (!b.pos.length) return;
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(b.pos, 3));
    g.setAttribute('color',    new THREE.Float32BufferAttribute(b.col, 4));
    g.setAttribute('aUv',      new THREE.Float32BufferAttribute(b.uv, 2));
    g.setAttribute('aRect',    new THREE.Float32BufferAttribute(b.rect, 4));
    g.setAttribute('aWrap',    new THREE.Float32BufferAttribute(b.wrp, 2));
    g.setAttribute('aBase',    new THREE.Float32BufferAttribute(b.base, 1));
    const mesh = new THREE.Mesh(g, mat);
    mesh.frustumCulled = false; mesh.renderOrder = order;
    group.add(mesh);
  });
  if (DSPR.pos.length && DOSINF_MAT){
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(DSPR.pos, 3));
    g.setAttribute('aCorner',  new THREE.Float32BufferAttribute(DSPR.corner, 2));
    g.setAttribute('aSize',    new THREE.Float32BufferAttribute(DSPR.size, 2));
    g.setAttribute('uv',       new THREE.Float32BufferAttribute(DSPR.uv, 2));
    g.setAttribute('aBase',    new THREE.Float32BufferAttribute(DSPR.base, 1));
    const mesh = new THREE.Mesh(g, DOSINF_MAT);
    mesh.frustumCulled = false; mesh.renderOrder = 5;
    group.add(mesh);
  }
  if (SPR.pos.length && SPRITE_MAT){
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(SPR.pos, 3));
    g.setAttribute('aCorner',  new THREE.Float32BufferAttribute(SPR.corner, 2));
    g.setAttribute('aSize',    new THREE.Float32BufferAttribute(SPR.size, 2));
    g.setAttribute('uv',       new THREE.Float32BufferAttribute(SPR.uv, 2));
    g.setAttribute('aBase',    new THREE.Float32BufferAttribute(SPR.base, 1));
    const mesh = new THREE.Mesh(g, SPRITE_MAT);
    mesh.frustumCulled = false; mesh.renderOrder = 5;
    group.add(mesh);
  }
  if (tibPos.length && TIB_MAT){
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(tibPos, 3));
    g.setAttribute('uv',       new THREE.Float32BufferAttribute(tibUv, 2));
    g.setAttribute('aBase',    new THREE.Float32BufferAttribute(tibBase, 1));
    const mesh = new THREE.Mesh(g, TIB_MAT);
    mesh.frustumCulled = false; mesh.renderOrder = 3;
    group.add(mesh);
  }
  return { mesh: group.children.length ? group : null, stats: seen };
}

function WALL_VARIANT_HAS(t){ return WALL_SUFFIX_TYPES.indexOf(t) >= 0; }
const WALL_SUFFIX_TYPES = ['SBAG','CYCL','BRIK','BARB','WOOD'];

/** The sea. One quad per holed cell, on the terrain's own corner heights, split
 *  across the terrain's OWN diagonal. That last part is not cosmetic: the ground is
 *  two triangles sharing NE-SW (the console's split, DL emitter ROM 0x196FFC), and
 *  the sea was being split NW-SE. Over the 100 shipped maps 7,048 hole cells have
 *  four non-coplanar corners, where the two diagonals are genuinely different
 *  surfaces and the shore tears; worst gap 0.60 cell on SCB04EB. */
function buildSea(m){
  const raw = m.holeBits || b64(m.holes || '');
  if (!raw.length || !WATER_MAT) return null;
  const H = m.h, HALF = CELLS/2;
  const pos = [], base = [], cell = [], warp = [];
  const TWO_PI = Math.PI * 2;
  let n = 0;
  for (let cy = 0; cy < CELLS; cy++) for (let cx = 0; cx < CELLS; cx++){
    const i = cy * 64 + cx;
    if (!(raw[i >> 3] & (1 << (i & 7)))) continue;
    // NW, NE, SW, SE, wound (NW,SW,NE) and (NE,SW,SE): the terrain's own order
    const P = [[cx, cy], [cx+1, cy], [cx, cy+1], [cx+1, cy+1]];
    for (const k of [0, 2, 1, 1, 2, 3]){
      const q = P[k];
      pos.push(q[0]-HALF, 0, q[1]-HALF);
      base.push(groundAt(H, q[0], q[1]));
      cell.push(q[0], q[1]);
      warp.push(((q[0] + q[1]) * 256) % TWO_PI);
    }
    n++;
  }
  if (!n) return null;
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('aBase',    new THREE.Float32BufferAttribute(base, 1));
  g.setAttribute('aCell',    new THREE.Float32BufferAttribute(cell, 2));
  g.setAttribute('aWarp',    new THREE.Float32BufferAttribute(warp, 1));
  const me = new THREE.Mesh(g, WATER_MAT);
  me.frustumCulled = false;
  me.renderOrder = 0;              // opaque queue, before the ground
  const grp = new THREE.Group();
  grp.add(me);
  grp.userData.cells = n;
  return grp;
}

function buildBorder(m){
  const [X,Y,W,Hh] = m.playable, H = m.h, HALF = CELLS/2, pts = [];
  const push = (x,y) => pts.push(new THREE.Vector3(
    x-HALF, H[Math.min(64,y)*CORNERS+Math.min(64,x)]/BYTE_PER_CELL + 0.03, y-HALF));
  for (let x=X; x<=X+W; x++) push(x, Y);
  for (let y=Y; y<=Y+Hh; y++) push(X+W, y);
  for (let x=X+W; x>=X; x--) push(x, Y+Hh);
  for (let y=Y+Hh; y>=Y; y--) push(X, y);
  return new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints(pts),
    new THREE.LineBasicMaterial({ color:0xf0a02a, transparent:true, opacity:0.85 }));
}

// ---------------------------------------------------------------- load a map
async function show(id){
  const m = await (await fetch(`data/${id}.json`)).json();
  MAP = m;
  /* THE AUTHORED HEIGHTS AND THE DRAWN ONES ARE TWO DIFFERENT THINGS, and keeping
     them apart is the difference between an editor that saves a map and one that
     saves the renderer's cosmetics. m.h0 is what the cartridge stored, or what the
     author will store; m.h is that with the building pads graded into it, which is
     a drawing decision (see padHeights) and must never reach a file. */
  m.h0 = b64(m.heights);
  m.h = m.h0;

  /* One decode of the packed object records into a mutable list. Everything that
     used to walk the bytes now walks this, so an edit is an ordinary array change
     and nothing has to repack to see it. */
  m.objs = decodeObjects(m);
  const raw = b64(m.tiles);
  m.tiles = new Uint16Array(raw.buffer, raw.byteOffset, raw.length/2);
  m.tmpl = b64(m.tmpl);
  /* The sea mask decodes ONCE into a mutable bitset. It used to be re-decoded from
     the base64 on every sea rebuild, which meant a terrain paint could never turn
     water on or off: the string never changed. */
  m.holeBits = b64(m.holes || '');
  m.fam = Array.from(m.tmpl, t => t === 255 ? 'CLEAR' : (META.familyOf[t] || 'CLEAR'));
  m.name = Array.from(m.tmpl, t => t === 255 ? 'CLEAR1' : (META.templates[t] || ('T'+t)));

  world.clear();
  assetWorld.clear();
  SEA = null;
  MESH = new THREE.Mesh(buildMesh(m), MATS[MODE === 'tex' ? 'tex' : MODE === 'white' ? 'white' : 'vcol']);
  world.add(MESH);
  WIRE = new THREE.LineSegments(new THREE.WireframeGeometry(MESH.geometry),
    new THREE.LineBasicMaterial({ color:0x8fe4ff, transparent:true, opacity:0.4 }));
  WIRE.visible = showWire; world.add(WIRE);
  BORDER = buildBorder(m); BORDER.visible = showBorder; world.add(BORDER);

  if (MODE === 'fam' || MODE === 'tier') paint(MODE);
  if (MODE === 'tex') MATS.tex.map = await loadAtlas(m.atlas), MATS.tex.needsUpdate = true;

  await loadModels();
  await loadDosInfantry();
  m.h = padHeights(m);
  world.remove(MESH);
  MESH = new THREE.Mesh(buildMesh(m),
    MATS[MODE === 'tex' ? 'tex' : MODE === 'white' ? 'white' : 'vcol']);
  world.add(MESH);
  if (WIRE){ world.remove(WIRE);
    WIRE = new THREE.LineSegments(new THREE.WireframeGeometry(MESH.geometry),
      new THREE.LineBasicMaterial({ color:0x8fe4ff, transparent:true, opacity:0.4 }));
    WIRE.visible = showWire; world.add(WIRE); }
  if (MODE === 'fam' || MODE === 'tier') paint(MODE);
  MESH.renderOrder = 1;   // opaque queue, after the sea
  SEA = buildSea(m);
  if (SEA){ SEA.visible = showWater; assetWorld.add(SEA); }
  refreshAssets();

  frame(m);
  hud(m);
  showBriefing(m);
  document.querySelectorAll('#list .row').forEach(r =>
    r.classList.toggle('on', r.dataset.id === id));
  location.hash = id;
  API.onMapLoaded.forEach(fn => fn(m));
}

/** Rebuild everything derived from the TERRAIN arrays: the ground mesh, the
 *  wireframe, the border, the sea, the building pads, and the family/name lookups
 *  the readouts use. The editor calls this after a paint stroke. It is deliberately
 *  the same code the load path runs, so a painted map and a loaded map cannot drift
 *  apart. Rebuilding all 4096 cells costs about a millisecond, which is why the
 *  editor does not need incremental terrain updates. */
function rebuildTerrain(){
  const m = MAP;
  if (!m) return;
  m.fam  = Array.from(m.tmpl, t => t === 255 ? 'CLEAR' : (META.familyOf[t] || 'CLEAR'));
  m.name = Array.from(m.tmpl, t => t === 255 ? 'CLEAR1' : (META.templates[t] || ('T'+t)));
  m.h = m.h0;
  m.h = padHeights(m);
  if (MESH) world.remove(MESH);
  MESH = new THREE.Mesh(buildMesh(m),
    MATS[MODE === 'tex' ? 'tex' : MODE === 'white' ? 'white' : 'vcol']);
  MESH.renderOrder = 1;
  world.add(MESH);
  if (WIRE){
    world.remove(WIRE);
    WIRE = new THREE.LineSegments(new THREE.WireframeGeometry(MESH.geometry),
      new THREE.LineBasicMaterial({ color:0x8fe4ff, transparent:true, opacity:0.4 }));
    WIRE.visible = showWire; world.add(WIRE);
  }
  if (MODE === 'fam' || MODE === 'tier') paint(MODE);
  if (SEA){ assetWorld.remove(SEA); SEA = null; }
  SEA = buildSea(m);
  if (SEA){ SEA.visible = showWater; assetWorld.add(SEA); }
  refreshAssets();
}

function refreshAssets(){
  for (let i = assetWorld.children.length - 1; i >= 0; i--){
    if (assetWorld.children[i] !== SEA) assetWorld.remove(assetWorld.children[i]);
  }
  ASSETS = null;
  if (!MAP || !MODELS) return;
  if (!showAssets) { hud(MAP); return; }
  ASSETS = buildAssets(MAP);
  ASSETS.byCell = assetIndex(MAP);
  if (ASSETS.mesh) assetWorld.add(ASSETS.mesh);
  hud(MAP);
}

function frame(m){
  const [X,Y,W,H] = m.playable, HALF = CELLS/2;
  const cxw = X + W/2 - HALF, czw = Y + H/2 - HALF;
  const span = Math.max(W, H, 20);
  // fit the playable square in BOTH axes, whatever shape the window is
  const t = Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2);
  const aspect = (camera.aspect > 0 && isFinite(camera.aspect)) ? camera.aspect : 1.6;
  const dist = Math.max(span / (2*t), span / (2*t*aspect)) * 1.25;
  const pitch = THREE.MathUtils.degToRad(36);
  controls.target.set(cxw, 0.6, czw);
  camera.position.set(cxw + dist*0.10, dist*Math.sin(pitch), czw + dist*Math.cos(pitch));
  controls.update();
}

function hud(m){
  const e = META.maps.find(x => x.id === m.scenario);
  /* The map's identity lives in the top strip now: one chip that names what you are
     editing and opens the browser. It used to be a two-line header above the
     viewport, which spent vertical space on something you read once. */
  document.getElementById('mcid').textContent = m.scenario;
  document.getElementById('mcname').textContent = e.name || e.label || '';
  document.getElementById('mcmeta').textContent =
    `${m.dosTheater || m.theater} · ${CELLS}×${CELLS} · relief ` +
    `${e.ownHeightmap ? (e.relief / BYTE_PER_CELL).toFixed(1) + '×' : 'flat'} · ` +
    `${(m.objs || []).length} objects` +
    (m.unresolvedTiles ? ` · ${m.unresolvedTiles} cells have no cartridge tile` : '');
  document.getElementById('mcdot').style.background =
    e.side === 'Nod' ? '#ff6a5a' : '#e0b070';

  const inner = [];
  for (let y=m.playable[1]; y<=Math.min(64,m.playable[1]+m.playable[3]); y++)
    for (let x=m.playable[0]; x<=Math.min(64,m.playable[0]+m.playable[2]); x++)
      inner.push(m.h[y*CORNERS+x]);
  const lo = Math.min(...inner), hi = Math.max(...inner);
  const hist = new Map();
  for (const v of inner) hist.set(v, (hist.get(v)||0)+1);
  const rungs = [...hist.entries()].filter(([,c]) => c/inner.length >= 0.02)
    .sort((a,b)=>a[0]-b[0]);

  /* THE MAP INFO TAB. The same facts as before, but sectioned instead of run
     together as one paragraph of prose: a reader looking for the object counts
     should not have to read about how the sea is drawn to find them. */
  const kv = rows => `<dl class="kv">` +
    rows.filter(Boolean).map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('') + `</dl>`;
  const counts = m.objectCounts || {};
  const total = Object.values(counts).reduce((a,b)=>a+b,0);
  const bar = rungs.length
    ? `<div class="bars">` + rungs.map(([v,c]) =>
        `<i style="height:${Math.max(3, Math.round(38*c/inner.length/ (rungs[0]?Math.max(...rungs.map(r=>r[1]/inner.length)):1)))}px"
            title="height byte ${v} — ${Math.round(100*c/inner.length)}% of the playable rectangle"></i>`).join('') +
      `</div><div class="barlab">` + rungs.map(([v]) => v).join(' · ') + `</div>`
    : '';

  document.getElementById('facts').innerHTML =
    `<div class="rsec"><div class="rh">Heightmap</div>` +
    kv([
      ['Source', m.source],
      ['Range', `${lo} – ${hi} of 255`],
      ['Relief', `${((hi-lo)/BYTE_PER_CELL).toFixed(2)} cell widths`],
      ['Corners', `${CORNERS} × ${CORNERS} = ${(CORNERS*CORNERS).toLocaleString()}`],
      ['Playable', `${m.playable[2]}×${m.playable[3]} at ${m.playable[0]},${m.playable[1]}`],
    ]) + bar +
    `<p>At true scale <b>${BYTE_PER_CELL} height units = one cell width</b>. The
      terraces above are the height bytes that cover at least 2% of the playable
      rectangle.</p></div>` +

    `<div class="rsec"><div class="rh">Objects on this map</div>` +
    kv([[`All`, `<b>${total}</b>`]].concat(
      Object.entries(counts).filter(([,v]) => v).map(([k,v]) => [k, String(v)]))) +
    `</div>` +

    `<div class="rsec"><div class="rh">How things are drawn</div>` +
    (ASSETS && ASSETS.stats.meshes
      ? `<p><b>${ASSETS.stats.meshes} meshes</b> and their textures are the game's own,
         out of ${MODELS ? MODELS.meta.source : 'the pack'}.` +
        (ASSETS.stats.walls ? ` ${ASSETS.stats.walls} wall cells pick their piece and
          their turn from their neighbours the way the engine does.` : '') +
        (ASSETS.stats.tib ? ` ${ASSETS.stats.tib} tiberium cells use the cartridge's own
          ANY_TI filmstrip at the growth stage the engine would compute.` : '') +
        `</p>` : '') +
    (ASSETS && ASSETS.stats.pips
      ? `<p><b>${ASSETS.stats.pips} infantry</b> drawn as ` +
        (DOSINF
          ? `the 1995 MS-DOS sprites out of CONQUER.MIX, all eight facings stored and
             none mirrored, picked with the engine's own HumanShape[Facing32[dir]].`
          : `the cartridge's own sprite billboards, each picking its strip row from its
             facing and mirroring for the eastern octants.`) + `</p>` : '') +
    (SEA ? `<p><b>${SEA.userData.cells} cells show sea.</b> The tile art punches an
       alpha hole and the sea floor and surface are drawn underneath on the terrain's
       own corners, as the cartridge does.</p>` : '') +
    (ASSETS && ASSETS.stats.missing.size
      ? `<p>No mesh on the cartridge for
         <b>${[...ASSETS.stats.missing].sort().join(', ')}</b>, so nothing is drawn for
         them.</p>` : '') +
    `</div>`;
}

function showBriefing(m){
  const sec = document.getElementById('briefSec'), el = document.getElementById('brief');
  if (!m.briefing){ sec.style.display = 'none';
                    document.getElementById('briefSub').textContent = ''; return; }
  sec.style.display = 'block';
  document.getElementById('briefSub').textContent =
    `${m.scenario} · ${m.briefingTag || 'ENG/MISSION.ENG'}`;
  el.innerHTML = (m.briefingTag && m.briefingTag.includes('Special Ops')
    ? `<div style="font-style:normal;color:var(--gold);margin-bottom:5px">` +
      `the cartridge files this one under "${m.briefingTag}"</div>` : '') +
    m.briefing;
}

// ---------------------------------------------------------------- probe
const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();
let probeText = 'move the pointer over the terrain';
renderer.domElement.addEventListener('pointermove', ev => {
  if (!MESH) return;
  const r = renderer.domElement.getBoundingClientRect();
  ndc.x = ((ev.clientX-r.left)/r.width)*2-1;
  ndc.y = -((ev.clientY-r.top)/r.height)*2+1;
  ray.setFromCamera(ndc, camera);
  const hit = ray.intersectObject(MESH, false)[0];
  if (!hit){ probeText = `${MAP ? MAP.theater : ''} · ${BYTE_PER_CELL} height units per cell`; return; }
  const cell = Math.floor(hit.faceIndex/2), cx = cell%CELLS, cy = (cell/CELLS)|0;
  const H = MAP.h, ci = cy*CORNERS+cx;
  const c = [H[ci], H[ci+1], H[ci+CORNERS], H[ci+CORNERS+1]];
  const rng = Math.max(...c) - Math.min(...c);
  const [X,Y,W,Hh] = MAP.playable;
  const inside = cx>=X && cx<X+W && cy>=Y && cy<Y+Hh;
  probeText =
    `cell <b>${cx},${cy}</b>${inside ? '' : ' <span class="out">outside the border</span>'}` +
    ` · <b>${MAP.name[cell]}</b> <span class="sub">${MAP.fam[cell]}</span>` +
    ` · corners <b>${c[0]} ${c[1]} ${c[2]} ${c[3]}</b>` +
    (rng ? ` · drop <b>${rng}</b>` : '') +
    (() => {
      const on = ASSETS && ASSETS.byCell && ASSETS.byCell.get(cy*64+cx);
      if (!on || !on.length) return '';
      return ` · here: ` + on.map(o => `<b>${o.type}</b>`).join(', ');
    })();
});

// ---------------------------------------------------------------- ui
function setMode(mode){
  MODE = mode;
  document.querySelectorAll('#modesSurface button').forEach(b =>
    b.classList.toggle('on', b.dataset.mode === mode));
  if (!MESH) return;
  if (mode === 'tex'){
    loadAtlas(MAP.atlas).then(t => { MATS.tex.map = t; MATS.tex.needsUpdate = true;
      MESH.material = MATS.tex; });
  } else if (mode === 'white'){
    MESH.material = MATS.white;
  } else {
    paint(mode); MESH.material = MATS.vcol;
  }
  legend(mode);
  hud(MAP);
}

function legend(mode){
  const el = document.getElementById('legend');
  if (!el) return;
  if (mode === 'fam'){
    el.style.display = 'block';
    el.innerHTML = Object.entries(FAM_COLOR).map(([k,v]) =>
      `<div class="li"><span class="sw" style="background:${v}"></span>${k}</div>`).join('');
  } else if (mode === 'tier'){
    el.style.display = 'block';
    el.innerHTML = TIER_COLOR.map((v,i) =>
      `<div class="li"><span class="sw" style="background:${v}"></span>tier ${i} &middot; byte ${LADDER[i]}</div>`)
      .reverse().join('') +
      `<div class="li" style="margin-top:4px;opacity:.7">blends = the climb between rungs</div>`;
  } else el.style.display = 'none';
}

function toggle(id, get, set){
  const el = document.getElementById(id);
  el.classList.toggle('on', get());
  el.onclick = () => { set(!get()); el.classList.toggle('on', get()); };
}

document.querySelectorAll('#modesSurface button').forEach(b =>
  b.onclick = () => setMode(b.dataset.mode));
toggle('tAssets', ()=>showAssets, v => { showAssets = v; refreshAssets(); });
/* The caret opens the class list on a click too. Hover alone is a mouse-only route to
   seven controls, and it closes the moment the pointer strays. */
document.querySelector('#viewbar .haspop .vtog svg:last-child').addEventListener('click', e => {
  e.stopPropagation();
  document.querySelector('#viewbar .haspop').classList.toggle('open');
});
addEventListener('click', e => {
  const h = document.querySelector('#viewbar .haspop');
  if (h && !h.contains(e.target)) h.classList.remove('open');
});
function buildAssetGroups(){
  const el = document.getElementById('assetGroups');
  const ICON = { buildings:'i-house', terrain:'i-trees', walls:'i-wall',
                 tiberium:'i-crystal', units:'i-tank', infantry:'i-person',
                 decals:'i-dots' };
  el.innerHTML = ASSET_GROUPS.map(([key, label]) =>
    `<div class="pc${assetOn.has(key)?' on':''}" data-g="${key}">` +
    `<svg class="ic s12"><use href="#i-check"/></svg>` +
    `<svg class="ic s14 gi"><use href="#${ICON[key] || 'i-dots'}"/></svg>${label}</div>`).join('');
  el.querySelectorAll('.pc').forEach(c => c.onclick = e => {
    e.stopPropagation();
    const k = c.dataset.g;
    assetOn.has(k) ? assetOn.delete(k) : assetOn.add(k);
    c.classList.toggle('on', assetOn.has(k));
    document.getElementById('assetCount').textContent =
      `${assetOn.size}/${ASSET_GROUPS.length}`;
    refreshAssets();
  });
  document.getElementById('assetCount').textContent =
    `${assetOn.size}/${ASSET_GROUPS.length}`;
}
buildAssetGroups();
toggle('tPads',  ()=>flatPads,  v => { flatPads = v; if (MAP) show(MAP.scenario); });
toggle('tWire',  ()=>showWire,   v => { showWire = v;   if (WIRE) WIRE.visible = v; });
toggle('tBorder',()=>showBorder, v => { showBorder = v; if (BORDER) BORDER.visible = v; });
toggle('tWater', ()=>showWater,  v => { showWater = v; if (SEA) SEA.visible = v; });
const exagEl = document.getElementById('exag');
function setExag(v){
  exag = Math.round((0.2 + v * 0.08) * 10) / 10;
  exagEl.value = v;
  document.getElementById('exagV').textContent = exag.toFixed(1) + '×';
  world.scale.y = exag;
  ASSET_EXAG.value = exag;
}
exagEl.oninput = e => setExag(+e.target.value);
document.getElementById('trueScale').onclick = () => setExag(10);

/** Walk to the next or previous map in the filtered list. Shared by the arrow keys
 *  and by the two buttons in the corner of the viewport, which had no handler at
 *  all: a button with a bevel and a tooltip that does nothing is worse than no
 *  button. */
function stepMap(d){
  const rows = [...document.querySelectorAll('#list .row')];
  if (!rows.length) return;
  const i = rows.findIndex(r => r.classList.contains('on'));
  const j = (i + d + rows.length) % rows.length;
  rows[j].click();
  rows[j].scrollIntoView({ block: 'nearest' });
}
document.getElementById('prevmap').onclick = () => stepMap(-1);
document.getElementById('nextmap').onclick = () => stepMap(1);

addEventListener('keydown', e => {
  if (e.target.tagName === 'INPUT') return;
  const k = e.key.toLowerCase();
  if (k in PAN_KEYS) return;                 // W A S D belong to the camera alone
  if (k === 'o') document.getElementById('tAssets').click();
  else if (k === 'l') document.getElementById('tPads').click();
  else if (k === 'r') document.getElementById('tWire').click();
  else if (k === 'b') document.getElementById('tBorder').click();
  else if (k === 'p') document.getElementById('tWater').click();
  else if (k === 't') setMode(MODE === 'tex' ? 'white' : 'tex');
  else if (k === 'f') setMode('fam');
  else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight'){
    stepMap(e.key === 'ArrowRight' ? 1 : -1);
    e.preventDefault();
  }
});

/* --------------------------------------------------------------- filters
   Chips within a group are OR'd, groups are AND'ed, and a group with nothing
   selected does not filter at all. Every predicate below is a fact out of the
   cartridge, not a guess from the file name: `category` comes from the two
   scenario rosters in the ROM's own resident code (see export.py), and
   `ownHeightmap` is simply whether <SCEN>.IMG exists. */
const FILTERS = [
  ['Mode', [
    ['sp',  'Singleplayer', m => m.category === 'singleplayer',
     "the 51 campaign rows of the cartridge's REPLAY MISSION roster " +
     "(26 GDI + 25 Nod), pointer array at ROM 0x21b280"],
    ['mp',  'Multiplayer',  m => m.category === 'multiplayer',
     "SCM.., the MULTI1 house prefix (Set_Scenario_Name, scenarioini.cpp). None " +
     "is on the cartridge; all 16 come from GENERAL.MIX on the MS-DOS CD"],
    ['so',  'Spec Ops',     m => m.category === 'specops',
     "the four the SPECIAL OPS menu offers: pointer array at ROM 0x21b260, two " +
     "rows per side (row bitmask 3). N64-exclusive, and the only non-campaign " +
     "scenarios with a heightmap, a CM tint map and a .JIM 3D scene"],
    ['co',  'Covert Ops',   m => m.category === 'covertops' || m.dinosaur,
     "DOS expansion numbering: expand.cpp scans scenarios 20..59 for the Covert " +
     "Operations dialog. Plus the five SCJ dinosaur missions"],
    ['dino','Dinosaurs',    m => m.dinosaur,
     "SCJ.., the HOUSE_JP prefix. Reached in DOS only through the funpark " +
     "command line; on the CD, never on the cartridge"],
    ['bo',  'Bonus',        m => m.category === 'bonus',
     "the five C&C95 Bonus Missions, scenarios 60..62 per side (expand.cpp " +
     "Bonus_Dialog). Shipped on the cartridge, reachable from no menu on it"],
    ['un',  'Unused',       m => ['bonus','debug','unused'].includes(m.category),
     "on the cartridge and unreachable in a retail save: the Bonus Missions, and " +
     "the tail of the Nod REPLAY MISSION list behind the debug flag at RAM " +
     "0x80138A30"],
  ]],
  ['Side', [
    ['gdi', 'GDI', m => m.side === 'GDI', 'SCG.., Player=GoodGuy'],
    ['nod', 'Nod', m => m.side === 'Nod', 'SCB.., Player=BadGuy'],
  ]],
  ['Media', [
    ['rom', 'Cartridge', m => m.media === 'rom', 'shipped in cnc_eu.z64'],
    ['dos', 'DOS CD',    m => m.media === 'dosdata',
     'from GENERAL.MIX on the MS-DOS CD; the cartridge never carried these'],
  ]],
  ['Elevation', [
    ['hm',  'Heightmap',    m => m.ownHeightmap,
     'ships its own 65x65 <SCEN>.IMG'],
    ['nhm', 'No heightmap', m => !m.ownHeightmap,
     'flat: the cartridge maps fall back to FLAT.IMG (4225 zero bytes) and the ' +
     'DOS maps never had elevation at all'],
  ]],
];
const active = new Set();

function passes(m){
  return FILTERS.every(([, chips]) => {
    const on = chips.filter(c => active.has(c[0]));
    return !on.length || on.some(c => c[2](m));
  });
}

function buildChips(){
  const el = document.getElementById('chips');
  const SWATCH = { gdi: '#e0b070', nod: '#ff6a5a' };
  el.innerHTML = FILTERS.map(([label, chips]) =>
    `<div class="fgrp"><span class="fl">${label}</span><div class="fw">` +
    chips.map(([key, name, pred, why]) => {
      const n = META.maps.filter(pred).length;
      return `<button class="chip${active.has(key)?' on':''}" data-key="${key}"` +
             `${n?'':' disabled'} title="${why.replace(/"/g,'&quot;')}">` +
             (SWATCH[key] ? `<i class="sd" style="--c:${SWATCH[key]}"></i>` : '') +
             `${name}<span class="n">${n}</span></button>`;
    }).join('') + '</div></div>').join('') +
    `<div id="chipclear">clear filters</div>`;
  el.querySelectorAll('.chip').forEach(c => c.onclick = () => {
    const k = c.dataset.key;
    active.has(k) ? active.delete(k) : active.add(k);
    buildChips(); buildList(document.getElementById('filter').value);
  });
  const clear = document.getElementById('chipclear');
  clear.style.display = active.size ? 'block' : 'none';
  clear.onclick = () => { active.clear(); buildChips();
    buildList(document.getElementById('filter').value); };
}

function buildList(filter=''){
  const list = document.getElementById('list');
  const f = filter.trim().toUpperCase();
  const groups = [
    ['Spec Ops, N64 exclusive', m => m.category === 'specops'],
    ['GDI campaign',  m => m.category === 'singleplayer' && m.side === 'GDI'],
    ['Nod campaign',  m => m.category === 'singleplayer' && m.side === 'Nod'],
    ['Covert Ops on the cartridge, reachable from no menu',
                      m => m.category === 'covertops'],
    ['Bonus missions, on the cartridge, no menu reaches them',
                      m => m.category === 'bonus'],
    ['Debug rows, behind the cartridge\'s debug flag',
                      m => m.category === 'debug'],
    ['Named by nothing on the cartridge', m => m.category === 'unused'],
    ['Dinosaur missions, from the DOS CD',   m => m.category === 'dinosaur'],
    ['Multiplayer, from the DOS CD',         m => m.category === 'multiplayer'],
  ];
  const shown = META.maps.filter(m => passes(m) &&
    (!f || m.id.includes(f) || (m.name || '').toUpperCase().includes(f)
        || (m.label || '').toUpperCase().includes(f)));
  list.innerHTML = groups.map(([title, pick]) => {
    const rows = shown.filter(pick);
    if (!rows.length) return '';
    return `<div class="grp">${title} <span style="opacity:.5">${rows.length}</span></div>` +
      rows.map(m => {
        const t = m.theater === 'DESERT' ? '#c9a24a' : '#5c9a5a';
        return `<div class="row${m.ownHeightmap?'':' flat'}" data-id="${m.id}"
          title="${(m.name || m.id).replace(/"/g,'&quot;')}">
          <span class="dot" style="background:${t}"></span>
          <span class="id">${m.id}</span>
          <span class="nm${m.name?'':' lbl'}">${m.name || m.label || ''}</span>
          <span class="rl">${m.ownHeightmap ? (m.relief/BYTE_PER_CELL).toFixed(1)+'×' : 'flat'}</span>
        </div>`;
      }).join('');
  }).join('') || `<div id="empty">Nothing matches those filters.</div>`;
  document.getElementById('hits').textContent = shown.length;
  document.getElementById('missCount').textContent = META.maps.length;
  list.querySelectorAll('.row').forEach(r => r.onclick = () => show(r.dataset.id));
  if (MAP) list.querySelectorAll('.row').forEach(r =>
    r.classList.toggle('on', r.dataset.id === MAP.scenario));
}
document.getElementById('filter').oninput = e => buildList(e.target.value);

// ---------------------------------------------------------------- loop
function resize(){
  // A zero-sized stage (the pane can open before layout) would set aspect to NaN,
  // and frame() would then put the camera at NaN and render nothing at all, with no
  // error anywhere. Skip until there is a real size, and re-frame once there is.
  const w = stage.clientWidth, h = stage.clientHeight;
  if (w < 2 || h < 2) return false;
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  return true;
}
addEventListener('resize', () => { if (resize() && MAP) frame(MAP); });
// and once more when layout settles, for the same reason
new ResizeObserver(() => { if (resize() && MAP && !isFinite(camera.position.x)) frame(MAP); })
  .observe(stage);

let lastProbe = '';
const CLOCK = new THREE.Clock();

/* WASD DRIVES THE CAMERA, and nothing else may claim those four keys.
 *
 * The orbit rig is a camera plus a target, and panning has to move BOTH or the
 * camera swings round a point it has left behind. W is forward as you are looking:
 * the camera's own facing flattened onto the ground plane, so the map turns under
 * you the way you expect after you have orbited. Speed scales with how far out you
 * are zoomed, because a fixed step is a crawl from orbit and a lurch up close, and
 * it is multiplied by real elapsed time so it does not depend on framerate.
 *
 * A key is only released on keyup, so a held key keeps moving; the set is cleared on
 * blur as well, or alt-tabbing away mid-stride leaves the camera drifting forever. */
const HELD = new Set();
const PAN_KEYS = { w: [0, 1], s: [0, -1], a: [-1, 0], d: [1, 0] };
const PAN_CELLS_PER_SEC = 0.55;      // of the current view distance
const _fwd = new THREE.Vector3(), _right = new THREE.Vector3(), _pan = new THREE.Vector3();

function panCamera(dt){
  let mx = 0, mz = 0;
  for (const k of HELD){ const v = PAN_KEYS[k]; if (v){ mx += v[0]; mz += v[1]; } }
  if (!mx && !mz) return;
  const inv = 1 / Math.hypot(mx, mz);
  mx *= inv; mz *= inv;
  // forward = where the camera looks, flattened; right = forward rotated 90 degrees
  _fwd.subVectors(controls.target, camera.position).setY(0);
  if (_fwd.lengthSq() < 1e-9) _fwd.set(0, 0, -1);
  _fwd.normalize();
  _right.set(-_fwd.z, 0, _fwd.x);
  const step = camera.position.distanceTo(controls.target) * PAN_CELLS_PER_SEC * dt;
  _pan.set(0, 0, 0).addScaledVector(_fwd, mz * step).addScaledVector(_right, mx * step);
  camera.position.add(_pan);
  controls.target.add(_pan);
}

const typingInto = el => el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' ||
                                el.isContentEditable);
addEventListener('keydown', e => {
  if (typingInto(e.target) || e.ctrlKey || e.metaKey || e.altKey) return;
  const k = e.key.toLowerCase();
  if (k in PAN_KEYS){ HELD.add(k); e.preventDefault(); }
});
addEventListener('keyup', e => HELD.delete(e.key.toLowerCase()));
addEventListener('blur', () => HELD.clear());

let _lastFrame = 0;
function tick(){
  requestAnimationFrame(tick);
  const now = CLOCK.getElapsedTime();
  const dt = Math.min(0.1, _lastFrame ? now - _lastFrame : 0);   // cap after a stall
  _lastFrame = now;
  panCamera(dt);
  controls.update();
  seaClock(CLOCK.getElapsedTime());
  if (probeText !== lastProbe){
    document.getElementById('vpnote').innerHTML = probeText; lastProbe = probeText;
  }
  renderer.render(scene, camera);
}

/* THE EDITOR'S HANDLE ON THE RENDERER. Everything the editor needs and nothing it
   does not: it never reaches into module state directly, so this file can go on being
   the thing that draws maps and editor.js can be the thing that changes them. */
export const API = {
  THREE, scene, camera, renderer, controls, assetWorld, world,
  get META(){ return META; },
  get MODELS(){ return MODELS; },
  get MAP(){ return MAP; },
  get terrainMesh(){ return MESH; },
  CELLS, CORNERS, BYTE_PER_CELL, KIND, HOUSE_COLOR, MODEL_SCALE,
  groundAt: (x, z) => groundAt(MAP.h, x, z),
  /** the exaggeration the ground is currently drawn at, so an overlay can ride it */
  get exag(){ return exag; },
  /** rebuild the object layer from MAP.objs and refresh the readouts */
  refresh(){ refreshAssets(); hud(MAP); },
  /** put the camera back on the playable rectangle */
  frameMap(){ if (MAP) frame(MAP); },
  /** rebuild the ground itself after a terrain paint, exactly as loading would */
  rebuildTerrain(){ rebuildTerrain(); hud(MAP); },
  /** the tile atlas geometry, so a palette can crop real tile art out of it */
  ATLAS: { TS, PITCH, GUTTER, COLS },
  atlasURL(){ return MAP ? 'data/' + MAP.atlas : null; },
  /** one object, built by exactly the same path the map's own objects take */
  buildOne(ob){
    if (!MODELS || !MAP) return null;
    const r = buildAssets({ h: MAP.h, objs: [ob], holes: '' }, { all: true });
    return r.mesh;
  },
  /** which cell the pointer is over, or null */
  pickCell(ev){
    if (!MESH) return null;
    const r = renderer.domElement.getBoundingClientRect();
    ndc.x = ((ev.clientX - r.left) / r.width) * 2 - 1;
    ndc.y = -((ev.clientY - r.top) / r.height) * 2 + 1;
    ray.setFromCamera(ndc, camera);
    const hit = ray.intersectObject(MESH, false)[0];
    if (!hit) return null;
    const cell = Math.floor(hit.faceIndex / 2);
    return { cx: cell % CELLS, cy: (cell / CELLS) | 0 };
  },
  onMapLoaded: [],
};

(async function main(){
  try {
    META = await (await fetch('data/index.json')).json();
    buildChips();
    buildList();
    setExag(+exagEl.value); resize(); tick();
    const want = location.hash.slice(1);
    await show(META.maps.some(m => m.id === want) ? want : 'SCG01EA');
    setMode('tex');
    const ed = await import('./editor.js');
    ed.init(API);
  } catch (e) {
    const el = document.getElementById('err');
    el.style.display = 'grid';
    el.textContent = 'Could not load the map data: ' + e.message;
    throw e;
  }
})();
