/* ==================================================================================
 *  editor.js -- the write path.
 *
 *  app.js draws maps. This changes them. It reaches the renderer only through the
 *  API object app.js exports, so the two stay separable.
 *
 *  THE CORE LOOP, and it is deliberately not "drag and drop" in the web sense:
 *  you ARM a thing from the palette and STAMP it with a click, the ghost following
 *  the cursor from the moment you arm and staying armed so you can place ten in a
 *  row. Dragging is reserved for MOVING something already placed. Every C&C and RTS
 *  editor checked converges on that split, and the native HTML5 drag-and-drop API
 *  cannot do the job anyway: it cannot repaint the drag image as the ghost snaps,
 *  which is the one thing this needs.
 *
 *  VALIDITY IS PER CELL, never one verdict for the whole object, and the invalid
 *  markers draw OVER the ghost rather than under it, so the red always punches
 *  through the art. That is OpenRA's trick and it is the single best readability
 *  win in the corpus.
 *
 *  THE NO-GO RULE IS NOT OURS. It is Ground[Land_Type()].Build out of EA's GPL
 *  source, keyed on (TemplateType, icon) with the AltLand exception list that makes
 *  11 of the 38 cliff templates walkable. export_editor.py ships that table; this
 *  file only reads it. It is richer than "cliffs and water": beach and tiberium are
 *  passable but unbuildable, and none of it consults height.
 * ================================================================================== */

let A = null;                 // the renderer API
let T = null;                 // editor_tables.json
let on = false;               // edit mode
let armed = null;             // { kind, type } or null
let owner = 0;                // index into HOUSES
let ghost = null, decals = null, nogo = null;
let hoverCell = null;
let dragging = null;          // { ob, grabCx, grabCy } while moving something
let undoStack = [], redoStack = [];

const HOUSES = [
  { key: 'GoodGuy', label: 'Yours',   idx: 0, color: '#e0b070' },
  { key: 'BadGuy',  label: 'Enemy',   idx: 1, color: '#ff6a5a' },
  { key: 'Neutral', label: 'Neutral', idx: 2, color: '#9aa7b4' },
  { key: 'Special', label: 'Special', idx: 3, color: '#b98fe4' },
];

/* The palette. Types are named, not guessed: everything here is a code the mission
   INIs actually place and the pack actually has a mesh for. */
const TABS = [
  ['Buildings', 'structure', [
    'FACT','NUKE','NUK2','PROC','SILO','PYLE','HAND','WEAP','AFLD','HPAD',
    'HQ','EYE','TMPL','FIX','BIO','HOSP','MISS','GUN','SAM','ATWR','GTWR','OBLI']],
  ['Units', 'unit', [
    'MCV','HARV','JEEP','BGGY','APC','LTNK','MTNK','HTNK','FTNK','STNK',
    'ARTY','MSAM','MHQ','BIKE','BOAT','LST']],
  ['Infantry', 'infantry', ['E1','E2','E3','E4','E5','E6','RMBO','C1','C2','C5','CHAN','DELPHI','MOEBIUS']],
  ['Scenery', 'terrain', ['T01','T02','T05','T06','T07','T08','T11','T13','T16','T17',
    'ROCK1','ROCK2','ROCK3','ROCK4','ROCK5','ROCK6','ROCK7','SPLIT2','SPLIT3']],
  ['Overlay', 'overlay', ['TI1','TI2','TI3','TI4','TI5','SBAG','CYCL','BRIK','BARB','WOOD']],
];

// ------------------------------------------------------------------ the palette art

/* Every tile in the palette shows the thing it places, not its four-letter code.
   Anything you can build in the real game has a sidebar cameo on the disc, so that
   is what the tile wears; trees, rocks, tiberium and the civilian units never appear
   in a sidebar and have no cameo, so for those the editor renders the actual mesh
   from the cartridge into a small picture. Nothing here is drawn by hand. */

const thumbCache = new Map();
let TRT = null, TSCENE = null, TCAM = null, TCANVAS = null;
const THUMB = 96;

function meshThumb(kind, type, house){
  const key = `${kind}:${type}:${house}`;
  if (thumbCache.has(key)) return thumbCache.get(key);
  const THREE = A.THREE, R = A.renderer;
  let url = null;
  try {
    const mesh = A.buildOne({ kind, house, type, cell: 0, face: 0, sub: 0 });
    if (mesh){
      if (!TRT){
        TRT = new THREE.WebGLRenderTarget(THUMB, THUMB);
        TRT.texture.colorSpace = R.outputColorSpace;
        TSCENE = new THREE.Scene();
        TCAM = new THREE.OrthographicCamera(-1, 1, 1, -1, -1e5, 1e5);
        TCANVAS = document.createElement('canvas');
        TCANVAS.width = TCANVAS.height = THUMB;
      }
      TSCENE.add(mesh);
      /* Box3.setFromObject would read the raw position attribute, and every object
         layer here carries its terrain height in aBase and adds it in the vertex
         shader, so the real geometry sits somewhere the plain box never looks. The
         infantry sprites go further: they are a point per corner that the shader
         blows up to aSize view-space units. Measure what the shader will actually
         draw, or the thumbnail frames empty air. */
      const box = new THREE.Box3(), v = new THREE.Vector3();
      mesh.traverse(o => {
        const g = o.geometry;
        if (!g) return;
        const pa = g.attributes.position, ba = g.attributes.aBase,
              sa = g.attributes.aSize;
        let pad = 0;
        if (sa) for (let i = 0; i < sa.count; i++)
          pad = Math.max(pad, Math.abs(sa.getX(i)), Math.abs(sa.getY(i)));
        for (let i = 0; i < pa.count; i++){
          v.set(pa.getX(i), pa.getY(i) + (ba ? ba.getX(i) * A.exag : 0), pa.getZ(i));
          if (pad){ box.expandByPoint(v.clone().addScalar(pad));
                    box.expandByPoint(v.addScalar(-pad)); }
          else box.expandByPoint(v);
        }
      });
      if (box.isEmpty()) throw new Error('no geometry');
      const c = box.getCenter(new THREE.Vector3());
      const sz = box.getSize(new THREE.Vector3());
      const r = Math.max(sz.x, sz.y, sz.z, 0.5) * 0.62;
      TCAM.left = -r; TCAM.right = r; TCAM.top = r; TCAM.bottom = -r;
      TCAM.updateProjectionMatrix();
      TCAM.position.copy(c).add(new THREE.Vector3(0.7, 0.8, 1).normalize()
                                     .multiplyScalar(r * 40));
      TCAM.lookAt(c);

      const prevRT = R.getRenderTarget();
      const prevClear = R.getClearColor(new THREE.Color());
      const prevAlpha = R.getClearAlpha();
      R.setRenderTarget(TRT);
      R.setClearColor(0x000000, 0);
      R.clear(true, true, false);
      R.render(TSCENE, TCAM);
      const buf = new Uint8Array(THUMB * THUMB * 4);
      R.readRenderTargetPixels(TRT, 0, 0, THUMB, THUMB, buf);
      R.setRenderTarget(prevRT);
      R.setClearColor(prevClear, prevAlpha);
      TSCENE.remove(mesh);

      const ctx = TCANVAS.getContext('2d');
      const img = ctx.createImageData(THUMB, THUMB);
      for (let y = 0; y < THUMB; y++)      // GL reads bottom-up; the canvas is top-down
        img.data.set(buf.subarray((THUMB - 1 - y) * THUMB * 4, (THUMB - y) * THUMB * 4),
                     y * THUMB * 4);
      ctx.putImageData(img, 0, 0);
      if (buf.some((v, i) => (i & 3) === 3 && v > 8)) url = TCANVAS.toDataURL();
    }
  } catch (e) { url = null; }
  thumbCache.set(key, url);
  return url;
}

/** The face of one palette tile: a sidebar cameo where the game has one, the mesh
 *  rendered where it does not, and the bare code only if neither exists. */
/** The face of one palette tile, as inline style for the .cam box: the game's own
 *  sidebar cameo where one exists, the cartridge mesh rendered where it does not.
 *  Percentage background sizing so the sprite scales with the tile instead of being
 *  pinned to a pixel size the grid does not have. */
function tileArt(kind, type, house){
  const cam = T.cameos && T.cameos.cells[type];
  if (cam){
    const side = (house === 1) ? 'nod' : 'gdi';
    const r = cam[side] || cam.gdi || cam.nod;
    const sw = T.cameos.w, sh = T.cameos.h;
    const px = (sw - r[2]) ? (100 * r[0] / (sw - r[2])) : 0;
    const py = (sh - r[3]) ? (100 * r[1] / (sh - r[3])) : 0;
    return { cls: '', style:
      `background-image:url(data/${T.cameos.sheet});` +
      `background-size:${(100 * sw / r[2]).toFixed(3)}% ${(100 * sh / r[3]).toFixed(3)}%;` +
      `background-position:${px.toFixed(3)}% ${py.toFixed(3)}%` };
  }
  const url = meshThumb(kind, type, house);
  if (url) return { cls: ' mesh', style:
    `background-image:url(${url});background-size:contain;` +
    `background-position:center;background-repeat:no-repeat;image-rendering:auto` };
  return { cls: '', style: '' };
}

const displayName = t => (T.names && T.names[t]) || t;

// ---------------------------------------------------------------- the truth layer

/** The engine's own answer for a cell: which LandType its (template, icon) is.
 *  cdata.cpp gives each template a Land, and an AltLand for the icons named in its
 *  AltIcons list, which is exactly how a cliff gets a walkable notch. */
function landAt(m, cx, cy){
  const i = cy * A.CELLS + cx;
  const t = m.tmpl[i];
  if (t === 255) return 'CLEAR';                    // the clear filler
  const row = T.templates[t];
  if (!row) return 'CLEAR';
  const slot = m.tiles[i];
  const pair = (T.slots[m.theater] || [])[slot];
  const icon = pair ? pair[1] : 0;
  return (row.altIcons.indexOf(icon) >= 0) ? row.altLand : row.land;
}

const canBuild = (m, cx, cy) => !!(T.ground[landAt(m, cx, cy)] || {}).build;
const canWalk  = (m, cx, cy) => !!(T.ground[landAt(m, cx, cy)] || {}).passable;

/** Footprint of a type in cells. A bibbed building lays a dirt apron and occupies
 *  Width x (Height+1); without that a 3x2 Construction Yard looks legal here and is
 *  refused in the game. */
function footprint(kind, type){
  if (kind !== A.KIND.structure) return [1, 1];
  const f = (A.MODELS.footprints || {})[type] || [1, 1];
  return T.bibbed[type] ? [f[0], f[1] + 1] : [f[0], f[1]];
}

/* WHERE A TREE ACTUALLY IS, which is not where the file says it is.
 *
 * A TerrainTypeClass carries an occupy list, and for 27 of the 32 scenery types it
 * does NOT contain the object's own cell. T01's is `_List0010 = {MAP_CELL_W}`: one
 * cell SOUTH. Its CenterBase, XYP_COORD(11,41), is 1.708 cells south, which lands
 * inside that occupied cell. So the engine stores a tree a row north of where the
 * tree stands, the renderer reproduces that faithfully, and clicking a cell in the
 * editor therefore dropped the tree one cell below the cursor.
 *
 * The editor works in the cells a thing STANDS on -- that is what a person points at
 * -- and converts to the stored cell only when writing the object. Everything else
 * here (occupancy, the land check, the footprint decal, click-to-select) uses the
 * standing cells, so all of it agrees with what is on screen. */
const occupyOf = type => (T.occupy && T.occupy[type]) || [[0, 0]];

/** The offset from an object's stored cell to the cell a person would point at:
 *  the top-left of its occupy box. */
function anchorOffset(kind, type){
  if (kind !== A.KIND.terrain) return [0, 0];
  const oc = occupyOf(type);
  return [Math.min(...oc.map(c => c[0])), Math.min(...oc.map(c => c[1]))];
}

/** The cells an object standing at (cx, cy) covers. For scenery that is its real
 *  occupy list; for everything else it is the footprint rectangle. */
function coveredCells(kind, type, cx, cy){
  if (kind === A.KIND.terrain){
    const a = anchorOffset(kind, type);
    return occupyOf(type).map(c => [cx + c[0] - a[0], cy + c[1] - a[1]]);
  }
  const f = footprint(kind, type);
  const out = [];
  for (let dy = 0; dy < f[1]; dy++) for (let dx = 0; dx < f[0]; dx++) out.push([cx + dx, cy + dy]);
  return out;
}

/** Where an already-placed object stands, from the cell the file recorded. */
function standingCell(ob){
  const a = anchorOffset(ob.kind, ob.type);
  return [(ob.cell & 63) + a[0], (ob.cell >> 6) + a[1]];
}

/** What already stands on each cell, and what that blocks.
 *
 *  The engine will not let two buildings share a cell and neither will this. A
 *  building is blocked by anything solid: another building, a tree or rock, a wall,
 *  or a tiberium field. Vehicles and infantry only care about the solid things, and
 *  a wall or a patch of tiberium only refuses a second one of itself, so a drag can
 *  paint across ground it has already covered without stuttering. */
function occupancy(m){
  const map = new Map();
  for (const ob of m.objs){
    const [sx, sy] = standingCell(ob);
    for (const [x, y] of coveredCells(ob.kind, ob.type, sx, sy)){
      if (x < 0 || x > 63 || y < 0 || y > 63) continue;
      const k = y * A.CELLS + x;
      if (!map.has(k)) map.set(k, []);
      map.get(k).push(ob);
    }
  }
  return map;
}

/* WHAT MAY SHARE A CELL.
 *
 *  Almost nothing. A cell holds one solid thing: a building, a tree or rock, a wall,
 *  a vehicle. Not two of them, and not one of each. The single exception is the one
 *  the engine itself makes: INFANTRY stand in sub-cell positions, five to a cell
 *  (StoppingCoordAbs, five entries), so five soldiers can share a cell without ever
 *  standing on each other -- and the editor hands each new one the next free slot so
 *  they spread out the way they do in the game.
 *
 *  Tiberium is the other exception and it goes the other way: it is ground cover, not
 *  an object. It refuses a second patch of itself on the same cell and refuses
 *  nothing else, because units drive over it, infantry walk on it, and the engine
 *  clears it out from under a building on its own. */
const isTiberium = ob => ob.kind === A.KIND.overlay &&
                         (A.META.tiberium || []).indexOf(ob.type) >= 0;
const isSolid = ob => !isTiberium(ob) && ob.kind !== A.KIND.smudge;

const INFANTRY_PER_CELL = 5;    // StoppingCoordAbs, brain/vanilla/tiberiandawn

/** What already on this cell refuses the thing being placed, or null if nothing does.
 *  Also reports the sub-cell slot an infantryman would take. */
function blockedBy(here, kind, type){
  if (!here || !here.length) return null;
  if (kind === A.KIND.overlay && (A.META.tiberium || []).indexOf(type) >= 0){
    const b = here.find(isTiberium);
    return b ? b.type : null;
  }
  if (kind === A.KIND.infantry){
    const other = here.find(o => isSolid(o) && o.kind !== A.KIND.infantry);
    if (other) return other.type;
    const men = here.filter(o => o.kind === A.KIND.infantry);
    return men.length >= INFANTRY_PER_CELL ? men[0].type : null;
  }
  const b = here.find(isSolid);
  return b ? b.type : null;
}

/** The sub-cell an infantryman placed here would stand in: the lowest slot nobody
 *  else has taken, so two soldiers in one cell are never in the same spot. */
function freeSubcell(occ, cell){
  const here = (occ.get(cell) || []).filter(o => o.kind === A.KIND.infantry);
  const taken = new Set(here.map(o => o.sub));
  for (let i = 0; i < INFANTRY_PER_CELL; i++) if (!taken.has(i)) return i;
  return 0;
}

/** Every cell an object would occupy, and whether each one is legal. */
function placement(m, kind, type, cx, cy, occ){
  const f = footprint(kind, type);
  const a = anchorOffset(kind, type);
  occ = occ || occupancy(m);
  let blocker = null;
  /* Clamp so nothing can hang off the map, the way the engine's own placement
     cursor does -- and clamp the STORED cell too, since scenery is stored north of
     where it stands and could otherwise be written to a negative row. */
  const span = coveredCells(kind, type, 0, 0);
  const wSpan = Math.max(...span.map(c => c[0])) + 1;
  const hSpan = Math.max(...span.map(c => c[1])) + 1;
  cx = Math.max(a[0], Math.min(A.CELLS - wSpan + a[0], cx));
  cy = Math.max(a[1], Math.min(A.CELLS - hSpan + a[1], cy));
  const cells = coveredCells(kind, type, cx, cy), bad = [];
  const store = (cy - a[1]) * A.CELLS + (cx - a[0]);
  for (const [x, y] of cells){
    const land = kind === A.KIND.structure ? canBuild(m, x, y) : canWalk(m, x, y);
    const by = blockedBy(occ.get(y * A.CELLS + x), kind, type);
    if (by && !blocker) blocker = by;
    if (!land || by) bad.push([x, y]);
  }
  return { cx, cy, f, cells, bad, blocker, occ, store, ok: bad.length === 0 };
}

// ---------------------------------------------------------------- overlays

function flatGeom(cells, lift){
  const { THREE, CELLS } = A, H = A.MAP.h, HALF = CELLS / 2;
  const pos = [], base = [];
  for (const [x, y] of cells){
    const P = [[x, y], [x + 1, y], [x, y + 1], [x + 1, y + 1]];
    for (const k of [0, 2, 1, 1, 2, 3]){
      const q = P[k];
      pos.push(q[0] - HALF, lift, q[1] - HALF);
      base.push(A.groundAt(q[0], q[1]));
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('aBase', new THREE.Float32BufferAttribute(base, 1));
  return g;
}

/** A flat cell wash that rides the ground at the current vertical scale. */
function washMat(hex, opacity, order){
  const { THREE } = A;
  const m = new THREE.ShaderMaterial({
    uniforms: { uColor: { value: new THREE.Color(hex) },
                uOpacity: { value: opacity }, uExag: { value: 1 } },
    vertexShader: `
      attribute float aBase; uniform float uExag;
      void main(){ vec3 p = position; p.y += aBase * uExag;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(p, 1.0); }`,
    fragmentShader: `
      uniform vec3 uColor; uniform float uOpacity;
      void main(){ gl_FragColor = vec4(uColor, uOpacity); }`,
    transparent: true, depthWrite: false, side: THREE.DoubleSide,
  });
  m.userData.order = order;
  return m;
}

let MAT_OK = null, MAT_BAD = null, MAT_NOGO = null;

function rebuildNoGo(){
  if (nogo){ A.assetWorld.remove(nogo); nogo = null; }
  if (!on || !A.MAP || !T) return;
  const m = A.MAP, cells = [];
  nogoCount = 0;
  for (let cy = 0; cy < A.CELLS; cy++) for (let cx = 0; cx < A.CELLS; cx++)
    if (!canBuild(m, cx, cy)) cells.push([cx, cy]);
  if (!cells.length) return;
  nogo = new A.THREE.Mesh(flatGeom(cells, 0.006), MAT_NOGO);
  nogo.frustumCulled = false; nogo.renderOrder = 2;
  nogo.visible = !!armed || nogoPinned;
  A.assetWorld.add(nogo);
  nogoCount = cells.length;
  const c = document.getElementById('edNogoCount');
  if (c) c.textContent = nogoCount;
}

function clearGhost(){
  for (const g of [ghost, decals]) if (g) A.assetWorld.remove(g);
  ghost = decals = null;
}


/* THE GHOST'S MATERIAL, and the trap that made the preview invisible for a week.
 *
 * Every object layer in this viewer keeps its terrain height in an `aBase` vertex
 * attribute and adds it in the vertex shader, and that shader is installed by
 * assigning `onBeforeCompile` on the material. `Material.clone()` does NOT carry
 * `onBeforeCompile` across -- three copies a fixed list of properties and that is not
 * on it. So a cloned material compiles the STOCK shader: no aBase lift, and no atlas
 * sampling either. The ghost was being built correctly, added to the scene correctly,
 * and then drawn at y = 0 instead of y = ground, which buries it under the very
 * terrain it is supposed to stand on. It was visible only where the ground happened
 * to be near zero, which is why it looked intermittent rather than broken.
 *
 * Verified from the compiled program: the real asset material's shader source
 * contains `aBase`, the cloned one did not.
 *
 * One ghost material per source material, cached, so arming a palette item does not
 * leak a program per pointer move. */
const ghostMats = new WeakMap();
const GHOST_ALPHA = 0.5;

function ghostMaterial(src){
  const hit = ghostMats.get(src);
  if (hit) return hit;
  const m = src.clone();
  m.transparent = true;
  m.depthWrite = false;
  if (m.isShaderMaterial){
    /* ShaderMaterial.copy deep-copies the uniforms, which severs the shared uExag
       object the vertical-scale slider drives. Re-point every uniform at the
       original, then fade the one line that writes the colour: the sprite shader
       discards on alpha and assigns gl_FragColor itself, so .opacity never reaches
       it and a soldier's ghost would be fully solid. */
    for (const k of Object.keys(src.uniforms)) m.uniforms[k] = src.uniforms[k];
    m.fragmentShader = m.fragmentShader.replace(
      'gl_FragColor = c;', `gl_FragColor = vec4(c.rgb, c.a * ${GHOST_ALPHA});`);
  } else {
    m.opacity = GHOST_ALPHA;
    m.onBeforeCompile = src.onBeforeCompile;
    m.customProgramCacheKey = src.customProgramCacheKey;
    /* Fade the alpha cut with the colour. The fragment alpha is opacity * texA, so at
       half opacity a cutout that scraped past a 0.5 alphaTest at full strength now
       fails it, and the ghost loses exactly the fringe of every leaf and fence -- or
       vanishes wholesale. Scale the threshold by the same factor the colour was
       scaled by, and a little under, so the silhouette matches the object. */
    m.alphaTest = src.alphaTest * GHOST_ALPHA * 0.8;
  }
  m.needsUpdate = true;
  ghostMats.set(src, m);
  return m;
}

function showGhost(kind, type, cx, cy, house){
  clearGhost();
  const m = A.MAP;
  const p = placement(m, kind, type, cx, cy);
  /* Build the preview from the STORED cell, not the standing cell. The renderer
     applies the same CenterBase offset it applies to the map's own objects, so the
     ghost lands exactly where the object will -- under the cursor. */
  const mesh = A.buildOne({ kind, type, house, cell: p.store, face: 0, sub: 0 });
  if (mesh){
    mesh.traverse(o => { if (o.material) o.material = ghostMaterial(o.material); });
    mesh.renderOrder = 8;
    ghost = mesh;
    A.assetWorld.add(ghost);
  }
  const good = p.cells.filter(c => !p.bad.some(b => b[0] === c[0] && b[1] === c[1]));
  const grp = new A.THREE.Group();
  if (good.length){
    const g = new A.THREE.Mesh(flatGeom(good, 0.008), MAT_OK);
    g.frustumCulled = false; g.renderOrder = 7;   // UNDER the ghost
    grp.add(g);
  }
  if (p.bad.length){
    const b = new A.THREE.Mesh(flatGeom(p.bad, 0.010), MAT_BAD);
    b.frustumCulled = false; b.renderOrder = 9;   // OVER it, so red always shows
    grp.add(b);
  }
  decals = grp;
  A.assetWorld.add(decals);
  return p;
}

// ---------------------------------------------------------------- edits + undo

/* WALLS SNAP TO EACH OTHER, and they have to be re-snapped every time one is added
   or removed, not only when the map is loaded. The mask is the engine's own rule
   (CellClass::Wall_Update: bit i set when the cell adjacent in N, E, S, W carries the
   SAME overlay), and the viewer's renderer turns that mask into a piece and a facing
   through the cartridge's 16-way table. So the editor computes exactly the mask the
   exporter would have, and a wall drawn next to another wall becomes a corner on the
   spot. */
const WALL_TYPES = ['SBAG', 'CYCL', 'BRIK', 'BARB', 'WOOD'];
function reconnectWalls(m){
  const at = new Map();
  for (const ob of m.objs)
    if (ob.kind === A.KIND.overlay && WALL_TYPES.indexOf(ob.type) >= 0)
      at.set(ob.cell, ob.type);
  for (const ob of m.objs){
    if (ob.kind !== A.KIND.overlay || WALL_TYPES.indexOf(ob.type) < 0) continue;
    const x = ob.cell & 63, y = ob.cell >> 6;
    let mask = 0;
    [[0, -1], [1, 0], [0, 1], [-1, 0]].forEach(([dx, dy], bit) => {
      const nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < 64 && ny >= 0 && ny < 64 &&
          at.get(ny * 64 + nx) === ob.type) mask |= 1 << bit;
    });
    ob.sub = mask;
  }
}

/** Redraw whatever an edit touched. A terrain edit has to rebuild the ground itself
 *  (and with it the sea, the building pads and the no-go wash); an object edit only
 *  has to rebuild the object layer. */
function after(edit){
  reconnectWalls(A.MAP);
  if (edit && edit.terrain) A.rebuildTerrain(); else A.refresh();
  rebuildNoGo();
  /* Rebuilding the object layer empties assetWorld, which takes the ghost and its
     footprint decals with it. Without this the preview disappeared the moment you
     placed the first thing and did not come back until the pointer crossed into
     another cell -- which reads exactly like "there is no preview". */
  reshowGhost();
  status();
  drawRadar();
  /* The three readouts that describe the state of the edit, not the map. They used
     to be painted only by ui() and by save(), so the undo button stayed greyed out
     after your first edit and the mission check reported the map as it was when it
     loaded. */
  finishUI();
  paintLint(lint(A.MAP));
  saveState();
  autosave();
}

/** The top strip's one-line answer to "have I saved this?". */
function saveState(){
  const el = $('savestate');
  if (!el || el.dataset.flash === '1') return;
  el.classList.remove('bad');
  el.querySelector('use').setAttribute('href', '#i-check');
  el.querySelector('span').textContent = undoStack.length
    ? `${undoStack.length} edit${undoStack.length > 1 ? 's' : ''} unsaved`
    : 'No edits yet';
}

/** Put the preview back under the pointer, whichever mode is armed. */
function reshowGhost(){
  if (!on || !hoverCell) return;
  if (mode === 'elevation') showElevBrush(hoverCell.cx, hoverCell.cy);
  else if (mode === 'terrain'){ if (brush !== null) showBrush(hoverCell.cx, hoverCell.cy); }
  else if (armed) showGhost(armed.kind, armed.type, hoverCell.cx, hoverCell.cy, owner);
}

/** Record an edit for undo. `alreadyApplied` is for a paint stroke, which has been
 *  drawing itself on screen all along and must not be replayed on top of itself. */
function pushEdit(edit, alreadyApplied){
  if (!alreadyApplied) edit.redo();
  undoStack.push(edit);
  redoStack.length = 0;
  if (undoStack.length > 200) undoStack.shift();
  after(edit);
}

function apply(edit){ pushEdit(edit, false); }

function undo(){
  const e = undoStack.pop();
  if (!e) return;
  e.undo(); redoStack.push(e); after(e);
}
function redo(){
  const e = redoStack.pop();
  if (!e) return;
  e.redo(); undoStack.push(e); after(e);
}

const addObject = ob => apply({
  label: `place ${ob.type}`,
  redo: () => A.MAP.objs.push(ob),
  undo: () => { const i = A.MAP.objs.indexOf(ob); if (i >= 0) A.MAP.objs.splice(i, 1); },
});
const removeObject = ob => apply({
  label: `delete ${ob.type}`,
  redo: () => { const i = A.MAP.objs.indexOf(ob); if (i >= 0) A.MAP.objs.splice(i, 1); },
  undo: () => A.MAP.objs.push(ob),
});
const moveObject = (ob, cell) => {
  const from = ob.cell;
  apply({ label: `move ${ob.type}`, redo: () => { ob.cell = cell; },
          undo: () => { ob.cell = from; } });
};

/** The topmost object standing on a cell, for click-to-select. */
function objectAt(cx, cy){
  const m = A.MAP;
  for (let i = m.objs.length - 1; i >= 0; i--){
    const ob = m.objs[i];
    const [sx, sy] = standingCell(ob);
    if (coveredCells(ob.kind, ob.type, sx, sy).some(c => c[0] === cx && c[1] === cy))
      return ob;
  }
  return null;
}

// ------------------------------------------------------------------- the terrain

/* TERRAIN PAINTING, in the only vocabulary that has ever worked for these tools:
   blocks of ground with a picture of the ground on them, grouped the way a mapmaker
   thinks, never a list of 216 numbered TemplateTypes. The groups below are not a
   taxonomy I invented: they are the cartridge's own template name families (S=cliff,
   SH=shore, RV=river, D=dirt road, B/BR=boulders and brush, P=patch, W=water), so a
   tile lands in the drawer its own name puts it in.

   The picture on each tile is the real thing: every template's icons cropped straight
   out of that theater's tile atlas and composed at their true w x h layout, which is
   also exactly how the block will land on the map. Nothing is drawn by hand and
   nothing is a stand-in. */

/* No Cliffs drawer. A cliff is not a decoration you stamp, it is the edge of a
   step in the ground, so it lives in the Elevation tool where the step is
   authored. Painting one by hand produced cliff art standing on flat ground,
   which the cartridge does on 3.6% of its SLOPE cells and clearly by accident. */
const TERRAIN_GROUPS = [
  ['Ground',  ['CLEAR', 'P']],
  ['Water',   ['W']],
  ['Shore',   ['SH']],
  ['Rivers',  ['RV', 'FALLS', 'FORD']],
  ['Roads',   ['D']],
  ['Rock',    ['B', 'BR']],
  ['Bridges', ['BRIDGE']],
];

/* Six templates have icons the cartridge never shipped, identically in both
   theaters. They are in the enum and in the GPL source and they cannot be drawn, so
   the palette does not offer them; each has a same-direction sibling that can.
   docs/design-map-editor.md section 4a. */
/* The destroyed-bridge variants. BRIDGE1D and BRIDGE2D have no art in either
   bank at all, BRIDGE3D and BRIDGE4D are missing icons, and across all 100
   shipped maps the only *D cells that appear are the three-cell fragments of a
   bridge something blew up mid-mission. They are a damage state, not a thing you
   place. */
const NO_ART = new Set(['BRIDGE1D', 'BRIDGE2D', 'BRIDGE3D', 'BRIDGE4D']);

let SLOT_OF = null, HOLE_SET = null, ATLAS = null, THEATER = null, WATER_TEX = null;
const blockArt = new Map();

const familyOf = ini => (ini.match(/^[A-Z]+/) || [''])[0];

/** Rebuild the theater-dependent lookups. Called once per map load. */
function terrainInit(m){
  THEATER = m.theater;
  SLOT_OF = new Map();
  (T.slots[THEATER] || []).forEach((p, slot) => {
    if (p && !SLOT_OF.has(p[0] * 256 + p[1])) SLOT_OF.set(p[0] * 256 + p[1], slot);
  });
  HOLE_SET = new Set(T.holeSlots[THEATER] || []);
  blockArt.clear();
  const img = new Image();
  img.onload = () => { ATLAS = img; if (on && mode === 'terrain') ui(); };
  img.src = A.atlasURL();
  if (!WATER_TEX){
    /* Water tiles are ALPHA HOLES: the N64 terrain palettes carry no water colour at
       all, and the console draws the sea plane underneath. So a water thumbnail
       drawn from the atlas alone is a blank square, and every river and shore tile
       loses exactly the part that identifies it. Lay the game's own water texture
       down first, the same way the console does. */
    const w = new Image();
    w.onload = () => { WATER_TEX = w; blockArt.clear(); if (on && mode === 'terrain') ui(); };
    w.src = 'data/water1.png';
  }
}

const slotFor = (tid, icon) => SLOT_OF.get(tid * 256 + icon);

/** Every template this theater can actually draw, in palette order. */
function terrainList(groupIdx){
  const fams = TERRAIN_GROUPS[groupIdx][1];
  const out = [];
  T.templates.forEach((row, tid) => {
    if (!row || NO_ART.has(row.ini.toUpperCase())) return;
    if (fams.indexOf(familyOf(row.ini.toUpperCase())) < 0) return;
    let any = false;
    for (let i = 0; i < row.w * row.h && !any; i++) if (slotFor(tid, i) !== undefined) any = true;
    if (any) out.push(tid);
  });
  return out;
}

/** The block's real art, composed at its real size, as a data URL. */
function blockThumb(tid){
  if (blockArt.has(tid)) return blockArt.get(tid);
  if (!ATLAS) return null;
  const row = T.templates[tid], { TS, PITCH, GUTTER, COLS } = A.ATLAS;
  const cv = document.createElement('canvas');
  cv.width = row.w * TS; cv.height = row.h * TS;
  const g = cv.getContext('2d');
  g.imageSmoothingEnabled = false;
  let drew = 0, holed = false;
  for (let i = 0; i < row.w * row.h && !holed; i++){
    const sl = slotFor(tid, i);
    if (sl !== undefined && HOLE_SET.has(sl)) holed = true;
  }
  if (holed && WATER_TEX) g.drawImage(WATER_TEX, 0, 0, cv.width, cv.height);
  for (let dy = 0; dy < row.h; dy++) for (let dx = 0; dx < row.w; dx++){
    const slot = slotFor(tid, dy * row.w + dx);
    if (slot === undefined) continue;      // an icon the cartridge does not carry
    const col = slot % COLS, srow = (slot / COLS) | 0;
    g.drawImage(ATLAS, col * PITCH + GUTTER, srow * PITCH + GUTTER, TS, TS,
                dx * TS, dy * TS, TS, TS);
    drew++;
  }
  const url = drew ? cv.toDataURL() : null;
  blockArt.set(tid, url);
  return url;
}

/* CLEAR is not template 0 on disk. Every shipped map writes 255 (TEMPLATE_NONE) for
   plain ground and scatters the sixteen CLEAR1 icons across it at random: on
   SCG01EA, 3,429 of 4,096 cells are 255 and slots 0-15 each appear about 220 times.
   Painting a solid block of icon 0 would tile visibly and would not look like any
   map the game ships, so the brush scatters too, deterministically by cell so the
   same map always comes out the same way. */
const CLEAR_TID = 0;
const clearIcon = i => { let h = (i * 2654435761) >>> 0; h ^= h >>> 15; return h & 15; };

/* A BRIDGE ONLY FITS WHERE THE RIVER RUNS UNDER IT.
 *
 * Measured, not invented, and the first attempt at this rule was thrown out for
 * being invented. Over the 221 complete bridge and ford placements in the 100
 * shipped maps, this is the set of (icon, direction) pairs where the cell just
 * outside the footprint is water in EVERY single instance -- 24/24, 27/27, 15/15
 * and 26/26 respectively, across dozens of different maps. Every other side of
 * every other water cell is mixed or never water, which is what you would expect:
 * a bridge meets the river at its mouths and dry land everywhere else.
 *
 * BRIDGE4's eastern mouth is 25 of 26 and is deliberately left out; a rule that
 * is unanimous is evidence of intent, one that is 96% is evidence of a habit.
 *
 * FORD1 and FORD2 get NO rule. They are shallow crossings that sit in the river
 * rather than spanning it, and not one (icon, direction) pair is unanimous across
 * their 147 placements. Refusing to guess is the whole point. */
const BRIDGE_MOUTHS = {
  165: [[4, 'W'], [8, 'W'], [15, 'E']],      // BRIDGE1, 4x4, temperate/winter
  167: [[9, 'E'], [10, 'W'], [15, 'W']],     // BRIDGE2, 5x5, temperate/winter
  169: [[12, 'W'], [23, 'E']],               // BRIDGE3, 6x5, desert
  171: [[12, 'W'], [18, 'W']],               // BRIDGE4, 6x4, desert
};
const STEP = { N: [0, -1], E: [1, 0], S: [0, 1], W: [-1, 0] };

/** Does a bridge dropped here meet the river at its mouths? */
function crossingFits(m, tid, cx, cy){
  const mouths = BRIDGE_MOUTHS[tid];
  if (!mouths) return { ok: true };            // fords, and everything else
  const row = T.templates[tid];
  const missing = [];
  for (const [icon, dir] of mouths){
    const [dx, dy] = STEP[dir];
    const x = cx + (icon % row.w) + dx, y = cy + ((icon / row.w) | 0) + dy;
    if (x < 0 || x > 63 || y < 0 || y > 63){ missing.push([x, y, dir]); continue; }
    if (landAt(m, x, y) !== 'WATER') missing.push([x, y, dir]);
  }
  return missing.length
    ? { ok: false, missing,
        why: `the river has to carry on past ${missing.length > 1 ? 'both ends' :
              'the ' + missing[0][2] + ' end'}. Lay the water first, then bridge it.` }
    : { ok: true };
}

const isCrossing = tid => BRIDGE_MOUTHS[tid] !== undefined;

/** Which cells a block would cover, and what each would become. */
function blockCells(tid, cx, cy){
  const row = T.templates[tid], out = [], skipped = [];
  for (let dy = 0; dy < row.h; dy++) for (let dx = 0; dx < row.w; dx++){
    const x = cx + dx, y = cy + dy;
    if (x < 0 || x > 63 || y < 0 || y > 63){ skipped.push([x, y]); continue; }
    const i = y * 64 + x;
    if (tid === CLEAR_TID){
      const icon = clearIcon(i), slot = slotFor(CLEAR_TID, icon);
      out.push({ i, x, y, tmpl: 255, slot: slot === undefined ? 0 : slot });
      continue;
    }
    const slot = slotFor(tid, dy * row.w + dx);
    if (slot === undefined){ skipped.push([x, y]); continue; }
    out.push({ i, x, y, tmpl: tid, slot });
  }
  return { out, skipped };
}

const snapshot = (m, i) =>
  ({ i, tmpl: m.tmpl[i], slot: m.tiles[i],
     hole: (m.holeBits[i >> 3] >> (i & 7)) & 1 });

function writeCells(m, list){
  for (const c of list){
    m.tmpl[c.i] = c.tmpl;
    m.tiles[c.i] = c.slot;
    const hole = ('hole' in c) ? c.hole : (HOLE_SET.has(c.slot) ? 1 : 0);
    if (hole) m.holeBits[c.i >> 3] |= 1 << (c.i & 7);
    else      m.holeBits[c.i >> 3] &= ~(1 << (c.i & 7));
  }
}

/* A stroke is ONE undo step, not one per cell, and it paints as you drag so you can
   see what you are doing. The before-state of a cell is captured the first time the
   stroke touches it, so dragging back and forth over the same ground still undoes to
   where it started. */
let stroke = null;

function strokeBegin(){
  stroke = { before: new Map(), after: new Map() };
  if (mode === 'elevation' && TIER){
    stroke.tierBefore = TIER.slice();
    stroke.passBefore = new Set(PASS);
    stroke.rampBefore = new Set(RAMPS);
  }
}

function strokePaint(tid, cx, cy){
  const m = A.MAP;
  if (!crossingFits(m, tid, cx, cy).ok) return false;
  const { out } = blockCells(tid, cx, cy);
  if (!out.length) return false;
  for (const c of out){
    if (!stroke.before.has(c.i)) stroke.before.set(c.i, snapshot(m, c.i));
    stroke.after.set(c.i, c);
  }
  writeCells(m, out);
  A.rebuildTerrain();
  rebuildNoGo();
  return true;
}

function strokeEnd(){
  if (!stroke) return;
  const s = stroke; stroke = null;
  if (!s.after.size) return;
  const before = [...s.before.values()], after = [...s.after.values()];
  const m = A.MAP;
  m.terrainEdited = true;
  /* An elevation stroke changes the corner heightmap as well as the tiles, and
     undo has to put BOTH back or the ground keeps the new shape while the art
     goes back to the old one. */
  const h0Before = s.h0Before, hAfter = s.hAfter;
  const tierAfter = s.elev ? TIER.slice() : null;
  const tierBefore = s.tierBefore || null;
  const passAfter = s.elev ? new Set(PASS) : null;
  const rampAfter = s.elev ? new Set(RAMPS) : null;
  pushEdit({
    label: s.elev ? 'raise ground' : 'paint terrain',
    redo: () => { writeCells(m, after); m.terrainEdited = true;
                  if (hAfter) m.h0 = hAfter;
                  if (tierAfter){ TIER = tierAfter.slice();
                                  PASS = new Set(passAfter); RAMPS = new Set(rampAfter);
                                  _elevVer++; } },
    undo: () => { writeCells(m, before);
                  if (h0Before) m.h0 = h0Before;
                  if (tierBefore){ TIER = tierBefore.slice();
                                   PASS = new Set(s.passBefore); RAMPS = new Set(s.rampBefore);
                                   _elevVer++; } },
    terrain: true,
  }, true);
}

// ----------------------------------------------------------------- elevation

/* CLIFFS ARE NOT ART YOU PAINT. They are what you can see of a step in the
 * ground, and in the shipped data that is exactly how they behave: a SLOPE
 * template is never an independent decoration, it is the visible consequence of a
 * tier change. So this tool authors the TIER and derives both the heightmap and
 * the cliff art from it, which is why the Cliffs drawer is gone from the terrain
 * palette -- you raise ground and the cliff appears around it.
 *
 * Every number below was measured over the 55 cartridge maps that ship their own
 * heightmap, restricted to each map's playable rectangle, and then independently
 * re-derived by a second pass before it was written down. The measurement lives in
 * docs/design-map-editor.md section 0c. */

/* THE LADDER. 151,325 playable corners: 58.56% sit exactly on one of these five
 * values and 99.05% within 31 of one. Ground is rung 1 (64), not rung 0 -- rung 0
 * holds 1.32% of corners -- so the brush calls rung 1 "ground". */
const RUNG = [0, 64, 128, 191, 255];
const tierOf = b => {
  let best = 0, bd = Infinity;
  for (let i = 0; i < RUNG.length; i++){
    const d = Math.abs(b - RUNG[i]);
    if (d < bd){ bd = d; best = i; }       // ties keep the LOWER rung
  }
  return best;
};

/* THE COMPASS. Four bits, one per corner of a 2x2 block: NW, NE, SW, SE, set when
 * that corner is above the block's own lowest corner. The template each mask maps
 * to is the cartridge's own modal answer for that mask, and where three siblings
 * are interchangeable they are listed together so a wall of cliff is not visibly
 * tiled. Shares are P(mask | template) on the block lattice; the inverse,
 * P(template | mask), which is what a lookup table is really judged by, runs
 * 67.0% to 90.6% and the residue is always the end-cap templates this table
 * deliberately never emits.
 *
 * Masks 6 and 9 are diagonal saddles. There are five of each in the whole
 * cartridge, so the tool refuses them rather than guess.
 * S01/S07/S08/S14/S15/S28 are never auto-emitted: their block-lattice modal share
 * is 40.4%-73.5% because they are ramp end-caps, not plain runs. S14 is still
 * offered, but only where the player asks for a ramp. */
const S = n => 12 + n;                       // TEMPLATE_SLOPE1 is 13 (defines.h)
const CLIFF_FOR_MASK = {
  1:  [S(32)],                 2:  [S(29)],
  3:  [S(3), S(4), S(5)],      4:  [S(31)],
  5:  [S(25), S(24), S(26)],   7:  [S(34)],
  8:  [S(30)],                 10: [S(11), S(10), S(12)],
  11: [S(35)],                 12: [S(17), S(18), S(19)],
  13: [S(33)],                 14: [S(36)],
};
const SADDLE = new Set([6, 9]);

/* The only self-contained ramp block in the game, and it faces EAST. S14's icons
 * 1 and 2 are walkable (Land ROCK, AltLand CLEAR), it is the one SLOPE template
 * that is 8-connected crossable in both axes, and S14.2 -> S14.1 is the single
 * most common slope-to-slope tier-gaining step in all 55 maps (26 of them). Its
 * icon 3 has no art in either bank; the cartridge fills that cell with plain
 * CLEAR1, which is passable anyway, and so does this. */
const RAMP_EAST = S(14);

const BLOCKS = 32;                           // 32 x 32 blocks of 2 x 2 cells
const bIndex = (bx, by) => by * BLOCKS + bx;

/** Read a tier field back out of a map's authored corner heights. */
function seedTiers(m){
  const tier = new Uint8Array(BLOCKS * BLOCKS);
  let off = 0, total = 0;
  const [PX, PY, PW, PH] = m.playable;
  for (let by = 0; by < BLOCKS; by++) for (let bx = 0; bx < BLOCKS; bx++){
    // a block's tier is the LOWEST of its four outer corners: that is the ground
    // it stands on, with any cliff rising out of it
    let lo = 4;
    for (const [dx, dy] of [[0,0],[2,0],[0,2],[2,2]]){
      const gx = 2 * bx + dx, gy = 2 * by + dy;
      const b = m.h0[gy * 65 + gx];
      lo = Math.min(lo, tierOf(b));
      if (gx >= PX && gx < PX + PW && gy >= PY && gy < PY + PH){
        total++;
        if (RUNG.indexOf(b) < 0) off++;
      }
    }
    tier[bIndex(bx, by)] = lo;
  }
  return { tier, off, total, exact: off === 0 };
}

/** The coarse corner tiers a tier field implies.
 *
 *  max, not min: the cliff belongs OUTSIDE the plateau you painted, so raising a
 *  block raises the ground it stands on and the step appears around it. The cost
 *  is real and is checked for below -- a low area narrower than three blocks has
 *  no corner left that is still low, and would silently fill in. */
function coarseCorners(tier){
  const K = new Uint8Array(33 * 33);
  for (let gy = 0; gy <= BLOCKS; gy++) for (let gx = 0; gx <= BLOCKS; gx++){
    let hi = 0;
    for (const [dx, dy] of [[-1,-1],[0,-1],[-1,0],[0,0]]){
      const bx = gx + dx, by = gy + dy;
      if (bx < 0 || bx >= BLOCKS || by < 0 || by >= BLOCKS) continue;
      hi = Math.max(hi, tier[bIndex(bx, by)]);
    }
    K[gy * 33 + gx] = hi;
  }
  return K;
}

/** Everything a tier field implies, computed before anything is written: the
 *  corner heights, the per-block cliff mask, and every reason to refuse. */
function deriveElevation(m, tier, passSet){
  const K = coarseCorners(tier);

  /* Corner heights by bilinear interpolation across each block. Verified against
     the cartridge: the median error of its own mid-edge corners against this
     formula is +0.000 over 12,964 samples, and of the block-centre corner -0.008.
     A north cliff from tier 2 to tier 1 comes out 128 / 96 / 64, and the
     cartridge's own medians for the three north siblings are 122/96/67,
     122/95/66 and 126/97/65. */
  const h = new Uint8Array(65 * 65);
  for (let gy = 0; gy <= BLOCKS; gy++) for (let gx = 0; gx <= BLOCKS; gx++){
    const a = RUNG[K[gy * 33 + gx]];
    const b = RUNG[K[gy * 33 + Math.min(BLOCKS, gx + 1)]];
    const c = RUNG[K[Math.min(BLOCKS, gy + 1) * 33 + gx]];
    const d = RUNG[K[Math.min(BLOCKS, gy + 1) * 33 + Math.min(BLOCKS, gx + 1)]];
    const put = (fx, fy, v) => { if (fx <= 64 && fy <= 64) h[fy * 65 + fx] = v; };
    put(2 * gx,     2 * gy,     a);
    put(2 * gx + 1, 2 * gy,     Math.round((a + b) / 2));
    put(2 * gx,     2 * gy + 1, Math.round((a + c) / 2));
    put(2 * gx + 1, 2 * gy + 1, Math.round((a + b + c + d) / 4));
  }

  const blocks = [], refusals = [];
  for (let by = 0; by < BLOCKS; by++) for (let bx = 0; bx < BLOCKS; bx++){
    const k = (dx, dy) => K[(by + dy) * 33 + (bx + dx)];
    const nw = k(0,0), ne = k(1,0), sw = k(0,1), se = k(1,1);
    const lo = Math.min(nw, ne, sw, se), hi = Math.max(nw, ne, sw, se);
    const mask = (nw > lo ? 1 : 0) | (ne > lo ? 2 : 0) |
                 (sw > lo ? 4 : 0) | (se > lo ? 8 : 0);
    const b = bIndex(bx, by);

    if (hi - lo > 1)
      refusals.push({ bx, by, why: `a step of ${hi - lo} tiers at once. ` +
        `The cartridge has no art for it -- terrace it one rung at a time.` });
    else if (SADDLE.has(mask))
      refusals.push({ bx, by, why: `a diagonal pinch. There are five of these in ` +
        `the whole cartridge, so this editor will not guess at one -- widen it.` });
    else if (lo > tier[b])
      refusals.push({ bx, by, why: `too narrow to stay low. Raised ground claims ` +
        `the cliff outside itself, so a low area needs to be three blocks across ` +
        `or it fills in.` });

    blocks.push({ bx, by, b, mask, lo, hi, pass: passSet.has(b) });
  }
  return { K, h, blocks, refusals };
}

/* Write a derived elevation into the map -- but ONLY where the brush has been.
 *
 * The first version of this rebuilt all 1,024 blocks from the tier field every
 * time, which is correct for a map made of nothing but tiers and wrong for every
 * map that exists: it quantised heights nobody had touched and, far worse, it
 * overwrote the sea, the shore, the roads and the tiberium with plain ground,
 * because a tier field knows about steps and knows nothing about water.
 *
 * So the tool owns exactly two things and only inside the region you paint: the
 * corner heights, and the CLIFF art. Cells outside that region are not read and
 * not written. Raising a block changes the mask of its neighbours too -- a mask is
 * read off the four coarse corners it shares with them -- so the region is the
 * painted blocks grown by one in every direction. */
function writeElevation(m, tier, passSet, ramps, dirty){
  const { h, blocks } = deriveElevation(m, tier, passSet);
  const cells = [];
  const clearAt = i => ({ i, tmpl: 255, slot: slotFor(0, clearIcon(i)) });

  for (const blk of blocks){
    const { bx, by, mask, b } = blk;
    if (!dirty.has(b)) continue;
    const idx = (dx, dy) => (2 * by + dy) * 64 + (2 * bx + dx);
    const four = [[0,0],[1,0],[0,1],[1,1]];

    /* Flat, a saddle we refuse to guess at, or a block the player asked to leave
       undressed: plain ground, and the slope in the corner heights carries the
       climb on its own. That is what the cartridge itself does on 81.4% of the
       passable edges where it gains a tier. */
    if (mask === 0 || mask === 15 || blk.pass || SADDLE.has(mask)){
      for (const [dx, dy] of four) cells.push(clearAt(idx(dx, dy)));
      continue;
    }

    const t = (ramps.has(b) && rampable(mask)) ? RAMP_EAST : pickCliff(mask, bx, by);
    if (t === null){
      for (const [dx, dy] of four) cells.push(clearAt(idx(dx, dy)));
      continue;
    }
    for (const [dx, dy] of four){
      const i = idx(dx, dy), icon = dy * 2 + dx;
      const slot = slotFor(t, icon);
      // S14 icon 3 and S32 icon 3 have no art in either bank; the cartridge fills
      // that cell with plain CLEAR1, which is passable anyway, and so does this
      cells.push(slot === undefined ? clearAt(i) : { i, tmpl: t, slot });
    }
  }

  /* Heights, for the corners the dirty blocks own. Copying the whole regenerated
     array would quantise the rest of the map back to the ladder behind the
     player's back. */
  const h0Before = m.h0.slice();
  const next = m.h0.slice();
  for (const b of dirty){
    const bx = b % BLOCKS, by = (b / BLOCKS) | 0;
    for (let gy = 2 * by; gy <= 2 * by + 2; gy++)
      for (let gx = 2 * bx; gx <= 2 * bx + 2; gx++)
        if (gx <= 64 && gy <= 64) next[gy * 65 + gx] = h[gy * 65 + gx];
  }
  const before = cells.map(c => snapshot(m, c.i));
  writeCells(m, cells);
  return { before, h0Before, after: cells, h: next };
}

/** One of the interchangeable siblings for this mask, chosen by position so the
 *  same map always redraws the same way and a long cliff is not visibly tiled. */
function pickCliff(mask, bx, by){
  const opts = CLIFF_FOR_MASK[mask];
  if (!opts) return null;
  const usable = opts.filter(t => slotFor(t, 0) !== undefined);
  if (!usable.length) return null;
  let x = (bx * 73856093) ^ (by * 19349663);
  x = (x ^ (x >>> 13)) >>> 0;
  return usable[x % usable.length];
}

/** Can a block carry a dressed ramp? Exactly one piece of ramp art in the game is
 *  a self-contained 2x2 block, and it faces east. Every other face gets the
 *  graded pass instead, which is the cartridge's own commonest answer anyway. */
const rampable = mask => mask === 10;

// ------------------------------------------------ the elevation tool's state

/* The tier field is per map and is seeded from the map's own authored corner
 * heights. Seeding a SHIPPED map is lossy: 41.44% of playable corners across the
 * 55 heightmapped cartridge maps are not on a rung, and quantising them rewrites
 * terrain nobody asked to change. So the tool will not touch a map until the
 * player has said, once, that they accept that. */
let TIER = null, PASS = new Set(), RAMPS = new Set();
let tierSeed = null, elevArmed = 1, elevBrush = 1, elevConverted = false;

function elevInit(m){
  tierSeed = seedTiers(m);
  TIER = tierSeed.tier.slice();
  PASS = new Set(); RAMPS = new Set(); TOUCHED = new Set(); _elevVer++;
  elevConverted = tierSeed.exact;     // an already-quantised map needs no warning
}

const blockAt = (cx, cy) => [Math.min(BLOCKS - 1, cx >> 1), Math.min(BLOCKS - 1, cy >> 1)];

/** The blocks a brush of the current size covers, centred on one block. */
function brushBlocks(bx, by){
  const r = elevBrush - 1, out = [];
  for (let dy = -r; dy <= r; dy++) for (let dx = -r; dx <= r; dx++){
    const x = bx + dx, y = by + dy;
    if (x >= 0 && x < BLOCKS && y >= 0 && y < BLOCKS) out.push([x, y]);
  }
  return out;
}

/** Paint a tier, or toggle a pass or a ramp, then re-derive everything. */
function elevPaint(cx, cy, tool){
  if (!elevConverted) return false;
  const [bx, by] = blockAt(cx, cy);
  let touched = false;
  for (const [x, y] of brushBlocks(bx, by)){
    const b = bIndex(x, y);
    if (tool === 'pass'){
      if (PASS.has(b)) PASS.delete(b); else { PASS.add(b); RAMPS.delete(b); }
      touched = true;
    } else if (tool === 'ramp'){
      if (RAMPS.has(b)) RAMPS.delete(b); else { RAMPS.add(b); PASS.delete(b); }
      touched = true;
    } else if (TIER[b] !== elevArmed){
      TIER[b] = elevArmed; touched = true;
    }
    if (touched) TOUCHED.add(b);
  }
  if (!touched) return false;
  if (!stroke) return false;
  _elevVer++;
  stroke.elev = true;
  stroke.dirty = stroke.dirty || new Set();
  for (const [x, y] of brushBlocks(bx, by))
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++){
      const nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < BLOCKS && ny >= 0 && ny < BLOCKS)
        stroke.dirty.add(bIndex(nx, ny));
    }
  applyElevation();
  return true;
}

/** Push the current tier field through to the map and redraw. */
function applyElevation(){
  const m = A.MAP;
  const w = writeElevation(m, TIER, PASS, RAMPS, stroke.dirty);
  if (!stroke.h0Before) stroke.h0Before = w.h0Before;
  for (const c of w.before) if (!stroke.before.has(c.i)) stroke.before.set(c.i, c);
  for (const c of w.after) stroke.after.set(c.i, c);
  m.h0 = w.h;
  stroke.hAfter = w.h;
  m.terrainEdited = true;
  m.heightEdited = true;
  A.rebuildTerrain();
  rebuildNoGo();
}

/** The refusals the current field would produce, for the readout. */
let _probCache = null, _probKey = -1, _elevVer = 0;
function elevProblems(){
  if (_probKey === _elevVer && _probCache) return _probCache;
  const all = deriveElevation(A.MAP, TIER, PASS).refusals;
  // a map seeded from cartridge heights is full of steps this tool cannot draw;
  // only complain about ground the player has actually moved
  _probCache = all.filter(r => TOUCHED.has(bIndex(r.bx, r.by)));
  _probKey = _elevVer;
  return _probCache;
}
let TOUCHED = new Set();

/** The elevation panel. Tiers are shown as what they are -- steps of ground at
 *  five fixed heights -- not as a number in a box. */
function elevUI(){
  const el = $('elevBody');

  if (!elevConverted){
    const pct = (100 * tierSeed.off / Math.max(1, tierSeed.total)).toFixed(1);
    el.innerHTML = `
      <div class="gate">
        <svg class="ic" style="color:var(--gold);flex:0 0 22px;width:22px;height:22px;stroke-width:1.64"><use href="#i-warn"/></svg>
        <div style="flex:1;min-width:0">
          <div class="tt">Convert this map to tiers</div>
          <p>${A.MAP.scenario} carries a smooth heightmap.
            ${tierSeed.off.toLocaleString()} of ${tierSeed.total.toLocaleString()}
            corners inside the playable rectangle (${pct}%) sit between rungs. Where
            you paint, the ground snaps to the nearest of five rungs and the cliff art
            is redrawn from it; everything you do not paint is left exactly as it
            shipped, sea and shore and roads included.</p>
        </div>
        <button id="edConvert">Convert<br>cannot be undone</button>
      </div>`;
    $('edConvert').onclick = () => { elevConverted = true; ui(); status(); };
    return;
  }

  const n = elevProblems().length;
  const tierName = t => t === 1 ? 'Ground' : t === 0 ? 'Low' : '+' + (t - 1);
  el.innerHTML =
    `<div class="secline">Tier brush</div>
     <div class="tiers">${[0,1,2,3,4].map(t =>
       `<div class="tr${elevArmed === t ? ' on' : ''}" data-t="${t}"
          title="${t === 1 ? 'Ground level, where most of the map sits'
                 : t === 0 ? 'One step below ground - valley floors and riverbeds'
                 : t - 1 + ' step' + (t > 2 ? 's' : '') + ' above ground'} (height byte ${RUNG[t]})">
          <i class="bar" style="height:${8 + t * 9}px"></i>${tierName(t)}</div>`).join('')}</div>
     <div class="secline">Tool</div>
     <div class="row3">
       <div class="tbtn${elevTool === 'tier' ? ' on' : ''}" data-k="tier"
         title="Paint the armed height onto the ground"><svg class="ic"><use href="#i-raise"/></svg>Raise / lower</div>
       <div class="tbtn${elevTool === 'pass' ? ' on' : ''}" data-k="pass"
         title="Leave the cliff art off this block so the ground just slopes up it. This is how the cartridge itself gets up a tier four times out of five."><svg class="ic"><use href="#i-grade"/></svg>Graded pass</div>
       <div class="tbtn${elevTool === 'ramp' ? ' on' : ''}" data-k="ramp"
         title="Dress the climb with the game's own ramp block. There is exactly one, and it only faces east."><svg class="ic"><use href="#i-ramp"/></svg>Ramp (east)</div>
     </div>
     <div class="secline">Brush size</div>
     <div class="row3">${[1,2,3].map(b =>
       `<div class="bsz${elevBrush === b ? ' on' : ''}" data-b="${b}" title="${b*2}x${b*2} cells">
          <i class="sq" style="width:${4 + b * 8}px;height:${4 + b * 8}px"></i>${b*2}&times;${b*2}</div>`).join('')}</div>
     <div class="derived"><svg class="ic s14"><use href="#i-info"/></svg>
       Cliff art and the heightmap are both <b>derived</b> — there is nothing to pick.
       Paint a tier; the editor finds the cartridge cliff pieces that fit.</div>` +
     (n ? `<div class="derived bad"><svg class="ic s14"><use href="#i-warn"/></svg>
       <b>${n} block${n > 1 ? 's' : ''}</b> the game has no art for. Hover one to read
       why; they are left as plain ground.</div>` : '');

  el.querySelectorAll('.tr').forEach(b => b.onclick = () => {
    elevArmed = +b.dataset.t; elevTool = 'tier'; ui(); status(); });
  el.querySelectorAll('.tbtn').forEach(b => b.onclick = () => {
    elevTool = b.dataset.k; ui(); status(); });
  el.querySelectorAll('.bsz').forEach(b => b.onclick = () => {
    elevBrush = +b.dataset.b; ui();
    if (hoverCell) showElevBrush(hoverCell.cx, hoverCell.cy);
    status(); });
}

/** The blocks the brush would touch, and any the tool would refuse. */
function showElevBrush(cx, cy){
  clearGhost();
  if (!elevConverted) return;
  const [bx, by] = blockAt(cx, cy);
  const good = [], bad = [];
  const refused = new Map(elevProblems().map(r => [bIndex(r.bx, r.by), r.why]));
  for (const [x, y] of brushBlocks(bx, by)){
    const t = refused.has(bIndex(x, y)) ? bad : good;
    for (const [dx, dy] of [[0,0],[1,0],[0,1],[1,1]]) t.push([2*x+dx, 2*y+dy]);
  }
  const grp = new A.THREE.Group();
  if (good.length){
    const g = new A.THREE.Mesh(flatGeom(good, 0.008), MAT_OK);
    g.frustumCulled = false; g.renderOrder = 7; grp.add(g);
  }
  if (bad.length){
    const b = new A.THREE.Mesh(flatGeom(bad, 0.010), MAT_BAD);
    b.frustumCulled = false; b.renderOrder = 9; grp.add(b);
  }
  decals = grp;
  A.assetWorld.add(decals);
}

// ---------------------------------------------------------------- the file

/** The mission INI, in the engine's own shape.
 *
 *  Two things here are load-bearing and easy to get wrong. Theater lives in [MAP],
 *  not [Basic] (display.cpp reads it there), and n64_terrain.py will match a
 *  THEATER= line anywhere in the file, so a misplaced one bakes one theater and
 *  plays another with nothing cross-checking. And all four of X/Y/Width/Height must
 *  be written: the defaults are sized for a 128-cell map and are wrong here.
 */
function writeINI(m, name){
  const L = [], H = A.META.houses;
  const e = A.META.maps.find(x => x.id === m.scenario) || {};
  const P = m.playable;
  L.push('; Written by the CNC3D map editor. Cells are y*64 + x.');
  L.push('[Basic]');
  L.push(`Name=${name || m.scenario}`);
  L.push('Player=GoodGuy');
  L.push('CarryOverMoney=0', 'BuildLevel=7', 'Theme=No theme',
         'Intro=x', 'Brief=x', 'Action=x', 'Win=x', 'Lose=x', 'Percent=0');
  L.push('', '[MAP]');
  L.push(`Theater=${m.theater === 'TEMPERAT' ? 'TEMPERATE' : m.theater}`);
  L.push(`X=${P[0]}`, `Y=${P[1]}`, `Width=${P[2]}`, `Height=${P[3]}`);

  const by = k => m.objs.filter(o => o.kind === k);
  const sec = (title, rows) => {
    L.push('', `[${title}]`);
    rows.forEach(r => L.push(r));
  };
  sec('TERRAIN', by(A.KIND.terrain).map(o => `${o.cell}=${o.type},None`));
  sec('OVERLAY', by(A.KIND.overlay).map(o => `${o.cell}=${o.type}`));
  sec('SMUDGE', by(A.KIND.smudge).map(o => `${o.cell}=${o.type},${o.cell},0`));
  let n = 0;
  sec('STRUCTURES', by(A.KIND.structure).map(o =>
    `${String(n++).padStart(3, '0')}=${H[o.house]},${o.type},256,${o.cell},${o.face},None`));
  n = 0;
  sec('UNITS', by(A.KIND.unit).map(o =>
    `${String(n++).padStart(3, '0')}=${H[o.house]},${o.type},256,${o.cell},${o.face},Guard,None`));
  n = 0;
  sec('INFANTRY', by(A.KIND.infantry).map(o =>
    `${String(n++).padStart(3, '0')}=${H[o.house]},${o.type},256,${o.cell},${o.sub || 0},Guard,${o.face},None`));

  // Every house that owns something needs a section, or the engine has nobody to
  // give it to.
  const used = new Set(m.objs.filter(o => o.kind === A.KIND.structure ||
      o.kind === A.KIND.unit || o.kind === A.KIND.infantry).map(o => H[o.house]));
  used.add('GoodGuy'); used.add('BadGuy');
  for (const h of used){
    L.push('', `[${h}]`);
    L.push('Credits=5000', 'MaxBuilding=150', 'MaxUnit=150',
           `Allies=${h}`, 'Edge=North');
  }
  L.push('', '[Waypoints]');
  (m.waypoints || []).forEach((c, i) => L.push(`${i}=${c}`));
  L.push('');
  return L.join('\r\n');
}

/** The linter. Every one of these failures is otherwise SILENT: a map with no pack
 *  never appears in the menu rather than erroring, an illegal tile is quietly turned
 *  to clear, and nothing anywhere cross-checks that a borrowed pack's theater matches
 *  the INI's. So this list is the most valuable thing on the page. */
function lint(m){
  const out = [];
  const ok = (t, d) => out.push({ ok: true, t, d });
  const bad = (t, d) => out.push({ ok: false, t, d });
  const H = A.META.houses;

  const P = m.playable;
  if (P[2] > 0 && P[3] > 0 && P[0] + P[2] <= 64 && P[1] + P[3] <= 64)
    ok('Playable rectangle', `${P[2]}x${P[3]} at ${P[0]},${P[1]}`);
  else bad('Playable rectangle', `${P.join(',')} does not fit in 64x64`);

  // an MCV needs a clear 3x3 to unfold into, which is what the shipped skirmish
  // converter checks too
  for (const side of [0, 1]){
    const mcvs = m.objs.filter(o => o.kind === A.KIND.unit && o.type === 'MCV' &&
                                    o.house === side);
    const label = `${H[side]} start`;
    if (!mcvs.length){ out.push({ ok: null, t: label, d: 'no MCV placed' }); continue; }
    let worst = null;
    for (const u of mcvs){
      const x = u.cell & 63, y = u.cell >> 6;
      let blocked = 0;
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++){
        const nx = x + dx, ny = y + dy;
        if (nx < 0 || nx > 63 || ny < 0 || ny > 63 || !canBuild(m, nx, ny)) blocked++;
      }
      if (blocked && (worst === null || blocked > worst[0])) worst = [blocked, x, y];
    }
    if (worst) bad(label, `MCV at ${worst[1]},${worst[2]} has ${worst[0]} of 9 pad cells unbuildable`);
    else ok(label, `${mcvs.length} MCV, 3x3 pad clear`);
  }

  // tiberium only grows on bare buildable ground
  const tib = m.objs.filter(o => o.kind === A.KIND.overlay &&
                                 A.META.tiberium.indexOf(o.type) >= 0);
  const tibBad = tib.filter(o => !canBuild(m, o.cell & 63, o.cell >> 6));
  if (!tib.length) out.push({ ok: null, t: 'Tiberium', d: 'none placed' });
  else if (tibBad.length) bad('Tiberium', `${tibBad.length} of ${tib.length} on unbuildable ground`);
  else ok('Tiberium', `${tib.length} cells, all on buildable ground`);

  // every structure still legal where it stands
  const sBad = m.objs.filter(o => o.kind === A.KIND.structure)
    .filter(o => { const [sx, sy] = standingCell(o);
                   return !placement(m, o.kind, o.type, sx, sy).ok; });
  if (sBad.length) bad('Structures', `${sBad.length} standing on unbuildable ground`);
  else ok('Structures', `${m.objs.filter(o => o.kind === A.KIND.structure).length} placed, all legal`);

  // the name has to match a scanner pattern or the map is invisible
  const id = m.scenario;
  const skirmish = /^SCM\d\d E?[ABC]$/.test(id.replace(/(\d\d)/, '$1 '));
  if (/^SCM\d\d[EW][ABC]$/.test(id)) ok('Name', `${id} will be scanned as a skirmish map`);
  else if (/^SC[GB]\d\d[EW][ABC]$/.test(id)) ok('Name', `${id} will be scanned as Spec Ops`);
  else bad('Name', `${id} matches no scanner pattern; the map will never appear in a menu`);

  /* This row used to be a constant, which was safe only while nothing could change a
     tile. It can now, and a borrowed pack under changed terrain is the one failure
     that shows up as units walking through visible cliffs rather than as an error. */
  if (m.heightEdited){
    const probs = elevProblems();
    if (probs.length)
      bad('Elevation', `${probs.length} block${probs.length>1?'s':''} the game has no ` +
          `art for; they were left as plain ground. First: ${probs[0].bx},${probs[0].by} -- ` +
          probs[0].why);
    else
      ok('Elevation', 'every step is one rung and has cartridge art');
    out.push({ ok: null, t: 'Heights',
               d: `saved as ${id}.HGT, 4,225 corner bytes. Bake with ` +
                  `bake5.build("${id}", out, heights=list(open("${id}.HGT","rb").read()))` });
  }
  if (m.terrainEdited)
    bad('Pack', `terrain was edited, so ${id}.pack no longer matches this map. ` +
                `The .BIN is saved alongside the .INI; bake the pack from it before playing.`);
  else
    out.push({ ok: null, t: 'Pack',
               d: 'terrain unchanged, so the pack can be borrowed from ' + id });
  return out;
}

function download(data, filename){
  const bin = data instanceof Uint8Array;
  const b = new Blob([data], { type: bin ? 'application/octet-stream' : 'text/plain' });
  const u = URL.createObjectURL(b);
  const a = document.createElement('a');
  a.href = u; a.download = filename;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(u), 1000);
}

// ---------------------------------------------------------------- autosave

const saveKey = () => 'cnc3d-edit-' + (A.MAP ? A.MAP.scenario : '');
function autosave(){
  if (!on || !A.MAP) return;
  try { localStorage.setItem(saveKey(), JSON.stringify(A.MAP.objs)); }
  catch (e) { /* private mode, quota: an autosave that cannot save is not an error */ }
}
function restoreIfAny(m){
  try {
    const s = localStorage.getItem('cnc3d-edit-' + m.scenario);
    if (!s) return false;
    const objs = JSON.parse(s);
    if (!Array.isArray(objs)) return false;
    m.objs = objs;
    return true;
  } catch (e) { return false; }
}

// ---------------------------------------------------------------- the panel

let nogoPinned = false, tab = 0, nogoCount = 0;
let mode = 'objects', tgroup = 0, brush = null, elevTool = 'tier';

/* THE PANEL IS MARKUP NOW, NOT A STRING.
 *
 * ui() used to rebuild the whole sidebar's innerHTML on every click, which meant the
 * layout lived in a template literal inside the editor and could not be designed. The
 * shell -- mode tabs, panes, owners, foot -- is in index.html where it belongs, and
 * this fills the containers. */

const $ = id => document.getElementById(id);

function ui(){
  // which pane is showing, and what colour the whole tool takes from it
  document.querySelectorAll('#modes .mtab').forEach(t => {
    const on = t.dataset.m === mode;
    t.classList.toggle('on', on);
    if (on) document.documentElement.style.setProperty('--mode', t.dataset.c);
  });
  document.querySelectorAll('#side .pane').forEach(p =>
    p.classList.toggle('on', p.dataset.p === mode));

  const badge = $('vpbadge');
  badge.querySelector('.m').textContent = mode.toUpperCase();
  badge.querySelector('svg use').setAttribute('href',
    mode === 'terrain' ? '#i-water' : mode === 'elevation' ? '#i-cliff' : '#i-house');
  $('vpowner').innerHTML = mode === 'objects'
    ? `placing for <b>${HOUSES[owner].label}</b>` : '';

  if (mode === 'terrain'){ terrainUI(); return finishUI(); }
  if (mode === 'elevation'){ elevUI(); return finishUI(); }

  $('edOwners').innerHTML = HOUSES.map((h, i) =>
    `<div class="ow${i === owner ? ' on' : ''}" data-o="${i}" style="--c:${h.color}">` +
    `<span class="h">${h.key.toUpperCase()}</span>${h.label}</div>`).join('');
  $('edOwners').querySelectorAll('.ow').forEach(b => b.onclick = () => {
    owner = +b.dataset.o; ui();
    if (armed && hoverCell) showGhost(armed.kind, armed.type, hoverCell.cx, hoverCell.cy, owner);
    status();
  });

  const CATICON = ['i-house', 'i-tank', 'i-person', 'i-trees', 'i-crystal'];
  $('cats').innerHTML = TABS.map(([n, , list], i) =>
    `<div class="cat${i === tab ? ' on' : ''}" data-t="${i}">` +
    `<svg class="ic s16"><use href="#${CATICON[i]}"/></svg>${n}` +
    `<span class="n">${list.length}</span></div>`).join('');
  $('cats').querySelectorAll('.cat').forEach(b => b.onclick = () => { tab = +b.dataset.t; ui(); });

  const [, kindName, list] = TABS[tab];
  const kind = A.KIND[kindName];
  /* Ten of the scenery entries are all called "Tree" and five overlays are all
     called "Tiberium", because that is genuinely what the game calls them. Where a
     name is not unique in the tab, show the code too, so the tiles stay tellable
     apart without hovering every one. */
  const seenName = {};
  for (const t of list){ const n = displayName(t); seenName[n] = (seenName[n] || 0) + 1; }
  $('palgrid').innerHTML = list.map(t => {
    const have = kind === A.KIND.structure || kind === A.KIND.unit || kind === A.KIND.terrain
      ? (A.MODELS.meta.types[t] >= 0) : true;
    const isArmed = armed && armed.type === t && armed.kind === kind;
    const f = footprint(kind, t);
    const size = kind === A.KIND.structure ? ` - ${f[0]}x${f[1]} cells` : '';
    const tip = have ? `${displayName(t)} (${t})${size}`
                     : `${displayName(t)} (${t}): no mesh on the cartridge`;
    const art = tileArt(kind, t, HOUSES[owner].idx);
    return `<div class="tile${isArmed ? ' on' : ''}${have ? '' : ' miss'}"
      data-t="${t}" data-k="${kind}" title="${tip}">
      <div class="cam${art.cls}" style="${art.style}"></div>
      <span class="code${owner === 1 ? ' nod' : ''}${art.cls ? ' mesh' : ''}">${t}</span>` +
      (kind === A.KIND.structure ? `<span class="fp">${f[0]}&times;${f[1]}</span>` : '') +
      `<div class="nm">${seenName[displayName(t)] > 1 ? '' : displayName(t)}</div></div>`;
  }).join('');
  $('palgrid').querySelectorAll('.tile').forEach(b => b.onclick = () => {
    const k = +b.dataset.k, t = b.dataset.t;
    armed = (armed && armed.type === t && armed.kind === k) ? null : { kind: k, type: t };
    ui(); syncOverlays();
    /* The ghost is rebuilt once per cell crossed, not once per pixel, so picking a
       different palette item while the pointer sits still would otherwise leave the
       previous item's ghost standing there. Rebuild it here too. */
    if (armed && hoverCell) showGhost(armed.kind, armed.type, hoverCell.cx, hoverCell.cy, owner);
    status();
  });

  finishUI();
}

/** The bottom half of the panel, which both modes share. */
function finishUI(){
  const nb = $('edNogo');
  nb.classList.toggle('on', nogoPinned);
  nb.onclick = () => { nogoPinned = !nogoPinned; nb.classList.toggle('on', nogoPinned);
                       syncOverlays(); };
  $('edNogoCount').textContent = nogoCount.toLocaleString();
  $('edUndo').classList.toggle('dis', !undoStack.length);
  $('edRedo').classList.toggle('dis', !redoStack.length);
}

/** The terrain palette: one drawer per template family, real block art on every
 *  tile, at the size the block will actually land. */
function terrainUI(){
  const DRAWICON = { Ground:'i-flat', Water:'i-water', Shore:'i-water',
                     Rivers:'i-water', Roads:'i-tiles', Rock:'i-trees',
                     Bridges:'i-tiles' };
  $('drawers').innerHTML = TERRAIN_GROUPS.map(([n], i) =>
    `<div class="dw${i === tgroup ? ' on' : ''}" data-t="${i}">` +
    `<svg class="ic s14"><use href="#${DRAWICON[n] || 'i-tiles'}"/></svg>${n}</div>`).join('');
  $('drawers').querySelectorAll('.dw').forEach(b =>
    b.onclick = () => { tgroup = +b.dataset.t; ui(); });

  const pal = $('blockgrid');
  if (!ATLAS){ pal.innerHTML = `<div class="derived">Loading the tile art…</div>`; return; }
  const list = terrainList(tgroup);
  if (!list.length){
    pal.innerHTML = `<div class="derived">Nothing in this drawer exists in the
      ${THEATER} theater. Every tile this editor offers is one the cartridge can
      actually draw.</div>`;
    return;
  }
  pal.innerHTML = list.map(tid => {
    const row = T.templates[tid], url = blockThumb(tid);
    const on = brush === tid;
    const walk = row.altIcons.length
      ? ` - ${row.altIcons.length} walkable notch${row.altIcons.length > 1 ? 'es' : ''}` : '';
    const tip = `${row.ini} - ${row.w}x${row.h} cells, ${row.land.toLowerCase()}${walk}` +
                (isCrossing(tid) ? ' - only fits where the river carries on past both ends' : '');
    /* A block is shown at its true shape: 18px per cell, so a 1x1 patch and a 5x4
       river mouth are told apart before you read either name. */
    const CELL = 18;
    return `<div class="blk${on ? ' on' : ''}" data-tid="${tid}" title="${tip}"
      style="--bw:${row.w * CELL + 2}px">
      <div class="art" style="width:${row.w * CELL}px;height:${row.h * CELL}px;
        background-image:url(${url});background-size:100% 100%"></div>
      <div class="lb">${row.ini}<i>${row.w}&times;${row.h}</i></div></div>`;
  }).join('');
  pal.querySelectorAll('.blk').forEach(b => b.onclick = () => {
    const tid = +b.dataset.tid;
    brush = brush === tid ? null : tid;
    ui(); syncOverlays();
    if (brush !== null && hoverCell) showBrush(hoverCell.cx, hoverCell.cy);
    status();
  });
}

/** The terrain, in the format the engine reads it in.
 *
 *  A .BIN is 4,096 cells of (template byte, icon byte) in 64-wide row order, 8,192
 *  bytes exactly (game/missions/make_skirmish_map.py:299). The icon is not stored in
 *  the viewer's arrays: the viewer stores an atlas SLOT, and the theater's slot table
 *  is what turns one back into the other. */
function writeBIN(m){
  const out = new Uint8Array(8192);
  const slots = T.slots[m.theater] || [];
  for (let i = 0; i < 4096; i++){
    const pair = slots[m.tiles[i]];
    out[i * 2]     = m.tmpl[i];
    out[i * 2 + 1] = pair ? pair[1] : 0;
  }
  return out;
}

/** The authored 65x65 corner heightmap, one byte per corner, row major -- the
 *  same 4,225 bytes the cartridge keeps in <SCEN>.IMG after its 16-byte header,
 *  and exactly what bake5.build(scen, out, heights=...) now takes. Without this
 *  file every hill drawn here would be browser-only. */
const writeHGT = m => Uint8Array.from(m.h0);

/** Paint the lint report into the foot. Two failing rows stay visible; the rest fold
 *  away, because a wall of green ticks is how a real failure gets missed. */
function paintLint(rows){
  const bad = rows.filter(r => r.ok === false);
  const rest = rows.filter(r => r.ok !== false);
  $('tallyOk').innerHTML = `<svg class="ic s14"><use href="#i-check"/></svg>${rest.length}`;
  $('tallyBad').innerHTML = `<svg class="ic s14"><use href="#i-x"/></svg>${bad.length}`;
  $('tallyBad').style.display = bad.length ? '' : 'none';
  $('edLint').innerHTML =
    bad.map(r => `<div class="lrow bad"><span class="k">${r.t}</span>` +
                 `<span class="d">${r.d}</span></div>`).join('') +
    (rest.length
      ? `<div class="lmore" id="lintmore"><svg class="ic s14"><use href="#i-caret"/></svg>` +
        `${rest.length} more — ${rest.map(r => r.t.toLowerCase()).join(', ')}</div>` +
        `<div class="lrest" id="lintrest">` +
        rest.map(r => `<div class="lrow${r.ok ? ' ok' : ''}"><span class="k">${r.t}</span>` +
                      `<span class="d">${r.d}</span></div>`).join('') + `</div>`
      : '');
  const more = $('lintmore');
  if (more) more.onclick = () => {
    const box = $('lintrest');
    const open = box.classList.toggle('open');
    more.classList.toggle('open', open);
  };
}

function save(){
  const m = A.MAP;
  const text = writeINI(m, m.scenario);
  const rows = lint(m);
  paintLint(rows);
  lastINI = text;
  const bad = rows.filter(r => r.ok === false).length;
  flash(bad ? `Saved with ${bad} problem${bad > 1 ? 's' : ''}` : 'Saved', bad > 0);
  download(text, m.scenario + '.INI');
  /* Only when a tile actually changed. Handing over an unchanged .BIN would invite
     someone to bake a pack that is byte for byte the one they already have. */
  if (m.terrainEdited) download(writeBIN(m), m.scenario + '.BIN');
  if (m.heightEdited) download(writeHGT(m), m.scenario + '.HGT');
}

let lastINI = '';

/** The .INI preview, on demand. It used to be a <details> nobody opened. */
function showINI(){
  const m = A.MAP;
  lastINI = writeINI(m, m.scenario);
  const box = $('inibox');
  box.classList.toggle('open');
  if (box.classList.contains('open'))
    box.querySelector('pre').textContent = lastINI;
}

/** A short word in the top strip when something happened. */
let flashTimer = null;
function flash(msg, bad){
  const el = $('savestate');
  el.classList.toggle('bad', !!bad);
  el.querySelector('span').textContent = msg;
  el.querySelector('use').setAttribute('href', bad ? '#i-warn' : '#i-check');
  el.dataset.flash = '1';
  clearTimeout(flashTimer);
  flashTimer = setTimeout(() => { el.dataset.flash = '0'; saveState(); }, 2600);
}



/* THE STATUS CARD.
 *
 * It used to be a strip of text under the viewport with no visual link to the thing
 * it described. It is now a card in the viewport itself, in three fixed lines: WHAT
 * is armed, WHETHER it can go where the pointer is, and the keys. The middle line is
 * the one that carries a refusal, and it is the only line that ever turns red, so a
 * problem is visible without reading. */
function card(icon, title, meta, verdict, bad, keys, right){
  return `<div class="l1">` +
    `<svg class="ic s16" style="color:var(--mode)"><use href="#${icon}"/></svg>` +
    `<span class="nm">${title}</span>` +
    (meta ? `<span class="sz">${meta}</span>` : '') + `</div>` +
    (verdict ? `<div class="l2${bad ? ' bad' : ''}">` +
      `<svg class="ic s14" style="flex:0 0 14px;margin-top:1px"><use href="#${bad ? 'i-x' : 'i-check'}"/></svg>` +
      `<span>${verdict}</span></div>` : '') +
    `<div class="l3">${(keys || []).map(k => `<span>${k}</span>`).join('')}` +
    (right ? `<span style="margin-left:auto">${right}</span>` : '') + `</div>`;
}

function status(){
  if (!on) return;
  const el = document.getElementById('status');
  if (mode === 'elevation'){
    if (!elevConverted){
      el.innerHTML = card('i-warn', 'Elevation is locked on this map', '',
        'Its ground is not on the tier ladder. Convert it in the panel, or stay in ' +
        'Objects and Terrain, which do not touch heights.', true, []);
      return;
    }
    const name = elevTool === 'pass' ? 'Graded pass'
               : elevTool === 'ramp' ? 'Ramp, facing east'
               : elevArmed === 1 ? 'Ground' : elevArmed === 0 ? 'Low' : '+' + (elevArmed - 1);
    const meta = elevTool === 'tier'
      ? `height ${RUNG[elevArmed]} · ${elevBrush*2}×${elevBrush*2} cells`
      : `${elevBrush*2}×${elevBrush*2} cells`;
    let verdict = '', bad = false, right = '';
    if (hoverCell){
      const [bx, by] = blockAt(hoverCell.cx, hoverCell.cy);
      const here = elevProblems().find(r => r.bx === bx && r.by === by);
      const t = TIER[bIndex(bx, by)];
      right = `block ${bx},${by}`;
      if (here){ verdict = here.why; bad = true; }
      else verdict = elevTool === 'pass'
        ? 'Click to strip the cliff art here and let the ground slope up instead.'
        : elevTool === 'ramp'
        ? 'Only a climb facing east can be dressed; the rest get a graded pass.'
        : `This block is at ${t === 1 ? 'ground' : t === 0 ? 'low' : '+' + (t-1)}. Drag to paint.`;
    }
    el.innerHTML = card('i-cliff', name, meta, verdict, bad,
      ['<kbd>Esc</kbd> cancels', '<kbd>Ctrl</kbd>+<kbd>Z</kbd> undoes the stroke'], right);
    return;
  }
  if (mode === 'terrain'){
    if (brush === null){
      el.innerHTML = card('i-water', 'Nothing armed', '',
        'Pick a block of ground on the right, then drag to paint it.', false,
        ['<kbd>Right-drag</kbd> orbits', '<kbd>WASD</kbd> pans']);
      return;
    }
    const row = T.templates[brush];
    const meta = `${row.w}×${row.h} cells of ${row.land.toLowerCase()}` +
      (row.altIcons.length ? ' · walkable notch' : '');
    let verdict = '', bad = false, right = '';
    if (hoverCell){
      const { out, skipped } = blockCells(brush, hoverCell.cx, hoverCell.cy);
      const fit = crossingFits(A.MAP, brush, hoverCell.cx, hoverCell.cy);
      right = `cell ${hoverCell.cx},${hoverCell.cy}`;
      if (!fit.ok){ verdict = `Will not fit here: ${fit.why}`; bad = true; }
      else if (!out.length){ verdict = 'Nothing to paint here.'; bad = true; }
      else {
        verdict = isCrossing(brush) ? 'Click to place it whole.' : 'Drag to paint.';
        if (skipped.length) verdict += ` ${skipped.length} cell` +
          `${skipped.length > 1 ? 's' : ''} skipped: off the map, or an icon the ` +
          `cartridge does not carry.`;
      }
    }
    el.innerHTML = card('i-water', row.ini, meta, verdict, bad,
      ['<kbd>Esc</kbd> cancels', '<kbd>Ctrl</kbd>+<kbd>Z</kbd> undoes the stroke'], right);
    return;
  }
  if (!armed){
    el.innerHTML = dragging
      ? card('i-move', `Moving ${dragging.ob.type}`, '',
             'Release to drop it. Esc puts it back.', false, ['<kbd>Esc</kbd> cancels'])
      : card('i-select', 'Nothing armed', '',
             'Pick something on the right to place, or click a placed object to select it.',
             false, ['<kbd>Right-drag</kbd> orbits', '<kbd>WASD</kbd> pans',
                     '<kbd>1</kbd><kbd>2</kbd><kbd>3</kbd> change mode']);
    return;
  }
  const m = A.MAP;
  const f = footprint(armed.kind, armed.type);
  const meta = `${f[0]}×${f[1]}` + (T.bibbed[armed.type] ? ' including its bib' : '');
  let verdict = '', bad = false, right = '';
  if (hoverCell){
    const p = placement(m, armed.kind, armed.type, hoverCell.cx, hoverCell.cy);
    right = `cell ${p.cx},${p.cy}`;
    if (p.ok) verdict = 'Click to place it here.';
    else {
      bad = true;
      verdict = `Cannot place: ${p.bad.length} cell${p.bad.length > 1 ? 's' : ''} blocked by ` +
        (p.blocker ? `a ${p.blocker} already there`
                   : landAt(m, p.bad[0][0], p.bad[0][1]).toLowerCase()) + '.';
    }
  }
  el.innerHTML = card('i-house',
    `${HOUSES[owner].label} ${armed.type} — ${displayName(armed.type)}`, meta,
    verdict, bad,
    ['<kbd>Tab</kbd> changes side', '<kbd>Esc</kbd> cancels',
     '<kbd>Del</kbd> removes', '<kbd>Ctrl</kbd>+<kbd>Z</kbd> undoes'], right);
}

/** The terrain brush's preview: the exact cells the block will overwrite. There is
 *  no translucent mesh here because a tile is not an object, it IS the ground, and
 *  the honest preview of "these cells change" is those cells lit up. */
function showBrush(cx, cy){
  clearGhost();
  if (brush === null) return null;
  const { out, skipped } = blockCells(brush, cx, cy);
  const fit = crossingFits(A.MAP, brush, cx, cy);
  const grp = new A.THREE.Group();
  if (out.length){
    const g = new A.THREE.Mesh(flatGeom(out.map(c => [c.x, c.y]),
                                        fit.ok ? 0.008 : 0.010),
                               fit.ok ? MAT_OK : MAT_BAD);
    g.frustumCulled = false; g.renderOrder = 7;
    grp.add(g);
  }
  const off = skipped.filter(c => c[0] >= 0 && c[0] < 64 && c[1] >= 0 && c[1] < 64);
  if (off.length){
    const b = new A.THREE.Mesh(flatGeom(off, 0.010), MAT_BAD);
    b.frustumCulled = false; b.renderOrder = 9;
    grp.add(b);
  }
  decals = grp;
  A.assetWorld.add(decals);
  return { out, skipped, fit };
}

const active = () => mode === 'elevation' ? elevConverted
                   : mode === 'terrain'   ? (brush !== null)
                   : !!armed;

function syncOverlays(){
  if (nogo) nogo.visible = active() || nogoPinned;
  if (!active()) clearGhost();
}

// ---------------------------------------------------------------- pointer

/* THE BROWSER TRAPS, each named because each has bitten this project or one like it.
   OrbitControls calls setPointerCapture on this very canvas, so the left button has
   to be taken away from it while editing or it eats the drag. pointermove and
   pointerup go on WINDOW and nothing here captures the pointer: a capture is exactly
   what stole a click in the project owner's own GamedevTycoon map pins. The canvas needs
   touch-action none or a touch drag scrolls the page instead. pointercancel has to
   clean up as well as pointerup. And OrbitControls binds contextmenu, so the right
   button needs its own preventDefault or the browser menu lands mid-orbit. */
let DRAG_SLOP = 4, downAt = null;

function onMove(ev){
  if (!on) return;
  const c = A.pickCell(ev);
  const moved = !c !== !hoverCell || (c && hoverCell &&
                (c.cx !== hoverCell.cx || c.cy !== hoverCell.cy));
  hoverCell = c;
  if (!c){ clearGhost(); return; }
  if (!moved && !dragging) return;      // one rebuild per CELL, not per pixel

  if (mode === 'elevation'){
    if (stroke) elevPaint(c.cx, c.cy, elevTool);
    showElevBrush(c.cx, c.cy);
    status();
    return;
  }
  if (mode === 'terrain'){
    if (brush === null) return;
    /* Anti-smear: a stroke only stamps when the pointer crosses into a new cell, so
       holding still does not repaint the same block forty times a second and a slow
       drag lays one block per cell instead of a blur. */
    /* Anti-smear applies twice over to a bridge. Its own water cells would
       satisfy the mouth test one cell along, so a held drag would chain bridges
       across dry ground. A crossing is placed by a click, full stop. */
    if (stroke && !isCrossing(brush)) strokePaint(brush, c.cx, c.cy);
    showBrush(c.cx, c.cy);
    status();
    return;
  }

  if (dragging){
    // gripped where you took hold of it, not snapped by its corner
    const nx = c.cx - dragging.grabCx, ny = c.cy - dragging.grabCy;
    showGhost(dragging.ob.kind, dragging.ob.type, nx, ny, dragging.ob.house);
    return;
  }
  if (downAt && armed && paintable(armed.kind)){
    // a held drag paints a stroke, so tiberium and walls do not need a click per cell
    stamp(c);
    return;
  }
  if (armed){ showGhost(armed.kind, armed.type, c.cx, c.cy, owner); status(); }
}

const paintable = k => k === A.KIND.overlay || k === A.KIND.terrain;

function stamp(c){
  const m = A.MAP;
  const p = placement(m, armed.kind, armed.type, c.cx, c.cy);
  if (!p.ok) return false;
  addObject({ kind: armed.kind, type: armed.type,
              house: armed.kind === A.KIND.terrain ? 2 : owner,
              cell: p.store, face: 0,
              sub: armed.kind === A.KIND.infantry
                 ? freeSubcell(p.occ, p.cy * A.CELLS + p.cx) : 0 });
  return true;
}

function onDown(ev){
  if (!on || ev.button !== 0) return;
  const c = A.pickCell(ev);
  if (!c) return;
  downAt = { x: ev.clientX, y: ev.clientY, c };

  /* The rail's tools, which only apply to objects: terrain and elevation are brushes
     and have their own gesture. */
  if (mode === 'objects' && tool !== 'place'){
    const ob = objectAt(c.cx, c.cy);
    if (tool === 'erase'){ if (ob) removeObject(ob); return; }
    if (tool === 'pick'){
      if (ob){
        armed = { kind: ob.kind, type: ob.type };
        const h = HOUSES.findIndex(x => x.idx === ob.house);
        if (h >= 0) owner = h;
        tab = TABS.findIndex(([, kn]) => A.KIND[kn] === ob.kind);
        if (tab < 0) tab = 0;
        setTool('place');
        showGhost(armed.kind, armed.type, c.cx, c.cy, owner);
      }
      return;
    }
    if (tool === 'move'){ if (ob) downAt.ob = ob; return; }   // the drag does the rest
  }

  if (mode === 'elevation'){
    if (!elevConverted) return;
    strokeBegin();
    elevPaint(c.cx, c.cy, elevTool);
    showElevBrush(c.cx, c.cy);
    status();
    return;
  }
  if (mode === 'terrain'){
    if (brush === null) return;
    strokeBegin();
    strokePaint(brush, c.cx, c.cy);
    showBrush(c.cx, c.cy);
    status();
    return;
  }
  if (armed){ stamp(c); return; }
  const ob = objectAt(c.cx, c.cy);
  if (ob) downAt.ob = ob;
}

function onUp(ev){
  if (!on) return;
  if (stroke){ strokeEnd(); downAt = null; ui(); status(); return; }
  if (dragging){
    const c = A.pickCell(ev) || hoverCell;
    if (c){
      const p = placement(A.MAP, dragging.ob.kind, dragging.ob.type,
                          c.cx - dragging.grabCx, c.cy - dragging.grabCy);
      if (p.ok) moveObject(dragging.ob, p.store);
    }
    dragging = null; clearGhost(); status();
  }
  downAt = null;
}

function onDragStart(ev){
  if (!on || !downAt || !downAt.ob || dragging || armed) return;
  if (Math.hypot(ev.clientX - downAt.x, ev.clientY - downAt.y) < DRAG_SLOP) return;
  const ob = downAt.ob;
  const [sx, sy] = standingCell(ob);
  dragging = { ob, grabCx: downAt.c.cx - sx, grabCy: downAt.c.cy - sy };
  status();
}

function onKey(ev){
  if (!on || ev.target.tagName === 'INPUT') return;
  const k = ev.key.toLowerCase();
  // W A S D drive the camera and nothing else. Ctrl+S still saves.
  if ('wasd'.includes(k) && !ev.ctrlKey && !ev.metaKey) return;
  if (ev.key === 'Escape'){ armed = null; brush = null; dragging = null;
    clearGhost(); ui(); syncOverlays(); status(); }
  else if (ev.key === 'Tab'){ owner = (owner + 1) % HOUSES.length; ui();
    if (armed && hoverCell) showGhost(armed.kind, armed.type, hoverCell.cx, hoverCell.cy, owner);
    status(); ev.preventDefault(); }
  else if ((ev.ctrlKey || ev.metaKey) && k === 'z'){ ev.shiftKey ? redo() : undo(); ev.preventDefault(); }
  else if ((ev.ctrlKey || ev.metaKey) && k === 'y'){ redo(); ev.preventDefault(); }
  else if ((ev.ctrlKey || ev.metaKey) && k === 's'){ save(); ev.preventDefault(); }
  else if ((ev.ctrlKey || ev.metaKey) && k === 'p'){ play(); ev.preventDefault(); }
  else if (k === 'n'){ $('edNogo').click(); }
  else if (k === '1' || k === '2' || k === '3'){
    document.querySelectorAll('#modes .mtab')[+k - 1].click();
  }
  else if (k === 'v'){ setTool('place'); }
  else if (k === 'm'){ setTool('move'); }
  else if (k === 'i'){ setTool('pick'); }
  else if (k === 'x'){ setTool('erase'); }
  else if (k === '0'){ A.frameMap(); }
  else if (ev.key === 'Delete' || ev.key === 'Backspace'){
    if (!hoverCell) return;
    const ob = objectAt(hoverCell.cx, hoverCell.cy);
    if (ob){ removeObject(ob); ev.preventDefault(); }
  }
}

// ---------------------------------------------------------------- mode

/* There is no view mode any more. This is a mission editor: it opens editing, the
   left mouse button belongs to it from the first frame, and the camera keeps
   right-drag and the wheel. */
function setMode(v){
  on = v;
  document.body.classList.toggle('editing', on);
  A.controls.mouseButtons.LEFT = on ? null : A.THREE.MOUSE.ROTATE;
  if (on){ ui(); rebuildNoGo(); }
  else { armed = null; clearGhost();
         if (nogo){ A.assetWorld.remove(nogo); nogo = null; } }
  status();
}


// -------------------------------------------------------------------- the radar

/* A MAP THIS SIZE NEEDS AN OVERVIEW, and the editor has never had one. 64x64 cells
 * on a 64x64 canvas, one texel per cell, scaled up with nearest-neighbour: the land
 * type gives the ground colour and every placed object puts a dot of its owner's
 * colour on top. The white rectangle is where the camera is actually looking,
 * computed by casting the four corners of the viewport at the ground plane, and
 * dragging it flies the camera there. */
const RADAR_LAND = {
  CLEAR: '#4a6b3a', ROAD: '#6b6350', WATER: '#1d4a63', ROCK: '#5c554a',
  BEACH: '#8a7a55', TIBERIUM: '#3f7a4a', WALL: '#7a7168',
};

function drawRadar(){
  const cv = $('radarcv');
  if (!cv || !A.MAP || !T) return;
  const m = A.MAP, g = cv.getContext('2d');
  const img = g.createImageData(64, 64);
  const [PX, PY, PW, PH] = m.playable;
  for (let cy = 0; cy < 64; cy++) for (let cx = 0; cx < 64; cx++){
    const hex = RADAR_LAND[landAt(m, cx, cy)] || '#31414f';
    let r = parseInt(hex.slice(1,3),16), gg = parseInt(hex.slice(3,5),16),
        b = parseInt(hex.slice(5,7),16);
    // shade by height so relief reads, and dim everything outside the playable rect
    const h = m.h0[cy * 65 + cx] / 255;
    const k = 0.72 + 0.55 * h;
    const inside = cx >= PX && cx < PX + PW && cy >= PY && cy < PY + PH;
    const d = inside ? 1 : 0.42;
    const i = (cy * 64 + cx) * 4;
    img.data[i]   = Math.min(255, r  * k * d);
    img.data[i+1] = Math.min(255, gg * k * d);
    img.data[i+2] = Math.min(255, b  * k * d);
    img.data[i+3] = 255;
  }
  for (const ob of m.objs){
    const [sx, sy] = standingCell(ob);
    if (sx < 0 || sx > 63 || sy < 0 || sy > 63) continue;
    if (ob.kind === A.KIND.terrain) continue;          // trees are scenery, not sides
    const c = isTiberium(ob) ? [126, 224, 160]
            : ob.house === 0 || ob.house === 4 ? [224, 176, 112]
            : ob.house === 1 || ob.house === 5 ? [255, 106, 90]
            : [154, 167, 180];
    const i = (sy * 64 + sx) * 4;
    img.data[i] = c[0]; img.data[i+1] = c[1]; img.data[i+2] = c[2];
  }
  g.putImageData(img, 0, 0);
  $('radarPlay').textContent = `PLAYABLE ${PX},${PY} · ${PW}×${PH}`;

  const counts = m.objs.reduce((a, o) => {
    const k = o.kind === A.KIND.structure ? 'structures'
            : o.kind === A.KIND.unit ? 'units'
            : o.kind === A.KIND.infantry ? 'infantry'
            : o.kind === A.KIND.terrain ? 'scenery' : 'overlay';
    a[k] = (a[k] || 0) + 1; return a; }, {});
  const lo = Math.min(...m.h0), hi = Math.max(...m.h0);
  $('radarMini').innerHTML =
    `Heights <b>${lo}–${hi}</b><br>` +
    `${counts.structures || 0} structures · ${counts.units || 0} units<br>` +
    `${counts.infantry || 0} infantry · ${counts.scenery || 0} scenery`;
}

/** The white box: where the camera is looking, in cell space. */
function drawRadarFrame(){
  const el = $('rframe');
  if (!el || !A.MAP) return;
  const THREE = A.THREE, cam = A.camera;
  const plane = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0);
  const ray = new THREE.Raycaster(), hit = new THREE.Vector3();
  let minx = 99, maxx = -99, miny = 99, maxy = -99, got = 0;
  for (const [nx, ny] of [[-1,-1],[1,-1],[1,1],[-1,1]]){
    ray.setFromCamera(new THREE.Vector2(nx, ny), cam);
    if (!ray.ray.intersectPlane(plane, hit)) continue;
    got++;
    const cx = hit.x + 32, cy = hit.z + 32;
    minx = Math.min(minx, cx); maxx = Math.max(maxx, cx);
    miny = Math.min(miny, cy); maxy = Math.max(maxy, cy);
  }
  if (got < 4){ el.style.display = 'none'; return; }   // horizon in shot
  el.style.display = 'block';
  const cl = v => Math.max(0, Math.min(64, v));
  el.style.left   = (cl(minx) / 64 * 100) + '%';
  el.style.top    = (cl(miny) / 64 * 100) + '%';
  el.style.width  = ((cl(maxx) - cl(minx)) / 64 * 100) + '%';
  el.style.height = ((cl(maxy) - cl(miny)) / 64 * 100) + '%';
}

/** Fly the camera so it looks at a cell, keeping its current angle and distance. */
function lookAtCell(cx, cy){
  const THREE = A.THREE;
  const t = new THREE.Vector3(cx - 32, A.groundAt(cx, cy) * A.exag, cy - 32);
  const off = A.camera.position.clone().sub(A.controls.target);
  A.controls.target.copy(t);
  A.camera.position.copy(t).add(off);
  A.controls.update();
}

/** The centre of mass of one side's things, which is what "their base" means. */
function sideCentre(pred){
  const m = A.MAP;
  let n = 0, sx = 0, sy = 0;
  for (const ob of m.objs){
    if (!pred(ob)) continue;
    const [x, y] = standingCell(ob);
    sx += x; sy += y; n++;
  }
  return n ? [Math.round(sx / n), Math.round(sy / n)] : null;
}

function wireRadar(){
  const jump = (id, pred, what) => $(id).onclick = () => {
    const c = sideCentre(pred);
    if (!c){ flash(`No ${what} on this map`, true); return; }
    lookAtCell(c[0], c[1]);
  };
  jump('jumpGdi', o => (o.house === 0 || o.house === 4) && o.kind !== A.KIND.terrain, 'GDI structures');
  jump('jumpNod', o => (o.house === 1 || o.house === 5) && o.kind !== A.KIND.terrain, 'Nod structures');
  jump('jumpTib', o => isTiberium(o), 'tiberium');

  // drag anywhere on the radar to fly there
  const rd = $('radar');
  const go = ev => {
    const r = rd.getBoundingClientRect();
    lookAtCell(Math.max(0, Math.min(63, ((ev.clientX - r.left) / r.width) * 64)),
               Math.max(0, Math.min(63, ((ev.clientY - r.top) / r.height) * 64)));
  };
  rd.addEventListener('pointerdown', ev => {
    ev.preventDefault(); go(ev);
    const mv = e => go(e);
    const up = () => { window.removeEventListener('pointermove', mv);
                       window.removeEventListener('pointerup', up); };
    window.addEventListener('pointermove', mv);
    window.addEventListener('pointerup', up);
  });
}

// ------------------------------------------------------------------ play in editor

/* PRESS PLAY AND THE GAME STARTS ON YOUR MAP.
 *
 * The page cannot bake or launch anything by itself, so serve.py does both: the three
 * files go straight into game/authored/ instead of through the Downloads folder, then
 * stage-skirmish-maps.sh bakes them and the engine is started with --scen and --pack.
 * The bake takes the better part of a minute, so the job streams its log back here
 * rather than leaving a button greyed out in silence.
 *
 * When serve.py is not the thing serving this page -- a plain http.server, or the
 * published copy of the site -- /api/hello 404s and the button says so instead of
 * failing mysteriously. */
let HAVE_SERVER = null;

async function probeServer(){
  try {
    const r = await fetch('api/hello');
    if (!r.ok) throw 0;
    HAVE_SERVER = await r.json();
  } catch (e) { HAVE_SERVER = false; }
  const b = $('playbtn');
  if (!b) return;
  if (!HAVE_SERVER){
    b.classList.add('busy');
    b.title = 'Play needs the editor to be served by tools/heightmap-viewer/serve.py. ' +
              'Check and save still writes the files.';
  } else if (!HAVE_SERVER.binary){
    b.classList.add('busy');
    b.title = 'No cnc3d binary is built yet, so there is nothing to launch.';
  } else {
    b.title = `Bake this map and start ${HAVE_SERVER.binary} on it`;
  }
}

const b64 = u8 => {
  let s = '';
  for (let i = 0; i < u8.length; i++) s += String.fromCharCode(u8[i]);
  return btoa(s);
};

function jobLog(title, lines, bad){
  const box = $('joblog');
  box.classList.add('open');
  box.classList.toggle('bad', !!bad);
  $('jobtitle').textContent = title;
  const pre = $('joblines');
  pre.textContent = lines.join('\n');
  pre.scrollTop = pre.scrollHeight;
}

async function play(){
  const m = A.MAP;
  if (!HAVE_SERVER){
    jobLog('Play is unavailable', [
      'This page is not being served by the editor\'s own server, so it cannot bake',
      'or launch anything.',
      '',
      '    python3 tools/heightmap-viewer/serve.py',
      '',
      'Check and save still works and writes the files to your downloads.'], true);
    return;
  }
  const btn = $('playbtn');
  if (btn.dataset.busy === '1') return;
  btn.dataset.busy = '1'; btn.classList.add('busy');
  try {
    jobLog(`${m.scenario} — saving`, ['writing the map into game/authored…']);
    const save = await (await fetch('api/save', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        scen: m.scenario,
        ini: writeINI(m, m.scenario),
        /* The .BIN and the .HGT go every time, not only when they changed. A map
           whose terrain came from a shipped scenario still needs terrain to bake
           against, and the editor's copy is the same bytes anyway. */
        bin: b64(writeBIN(m)),
        hgt: b64(writeHGT(m)),
      })})).json();
    if (save.error) throw new Error(save.error);

    const start = await (await fetch('api/play', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ scen: m.scenario })})).json();
    if (start.error) throw new Error(start.error);

    // poll: the bake is long enough that a spinner with no detail reads as a hang
    for (;;){
      await new Promise(r => setTimeout(r, 700));
      const j = await (await fetch('api/job?id=' + start.job)).json();
      jobLog(`${m.scenario} — ${j.state}`, j.log, j.state === 'failed');
      if (j.state !== 'running'){
        flash(j.state === 'done' ? 'Launched' : 'Bake failed', j.state !== 'done');
        break;
      }
    }
  } catch (e){
    jobLog(`${m.scenario} — failed`, [String(e.message || e)], true);
    flash('Play failed', true);
  } finally {
    btn.dataset.busy = '0'; btn.classList.remove('busy');
  }
}

/** Everything in index.html that belongs to the editor rather than to the renderer.
 *  Bound once; the panes themselves are refilled by ui(). */
function wireChrome(){
  document.querySelectorAll('#modes .mtab').forEach(t => t.onclick = () => {
    if (mode === t.dataset.m) return;
    mode = t.dataset.m;
    armed = null; brush = null; clearGhost();
    if (mode === 'elevation' && !TIER) elevInit(A.MAP);
    ui(); syncOverlays(); status();
  });

  $('savebtn').onclick = save;
  $('playbtn').onclick = play;
  $('jobclose').onclick = () => $('joblog').classList.remove('open');
  $('edUndo').onclick = undo;
  $('edRedo').onclick = redo;
  $('edReset').onclick = () => {
    if (!confirm('Throw away every edit to this map and reload it as it shipped?')) return;
    localStorage.removeItem(saveKey());
    location.reload();
  };
  $('edIniBtn').onclick = showINI;
  document.querySelector('#lintbox .hd').onclick = () => {
    const box = $('lintrest'), more = $('lintmore');
    if (!box) return;
    const open = box.classList.toggle('open');
    if (more) more.classList.toggle('open', open);
  };

  /* The three faces of the top box: where am I, which map, what is this map made of.
     They are tabs rather than three stacked panels because you look at exactly one
     of them at a time. */
  document.querySelectorAll('#topbox .boxtab').forEach(t => t.onclick = () => {
    document.querySelectorAll('#topbox .boxtab').forEach(x =>
      x.classList.toggle('on', x === t));
    document.querySelectorAll('#topbox .face').forEach(f =>
      f.classList.toggle('on', f.dataset.f === t.dataset.f));
    /* Picking a mission is a different job from editing one, so the browser takes
       the whole sidebar while it is open and the editing panes step aside. You do
       this once per session; it should not be a 120px slot for the rest of it. */
    const browsing = t.dataset.f === 'miss';
    document.body.classList.toggle('browsing', browsing);
    $('topbox').classList.toggle('grow', browsing);
  });
  // the chip in the top strip is the way into the browser
  $('mapchip').onclick = () =>
    document.querySelector('#topbox .boxtab[data-f="miss"]').click();

  /* The tool rail. Only tools that do something are here: a rail of greyed-out
     promises is worse than a short rail. */
  document.querySelectorAll('#rail .tool').forEach(t => t.onclick = () => {
    if (t.dataset.t === 'camera'){ A.frameMap(); return; }
    setTool(t.dataset.t);
  });
}

/** Which of the pointer tools has the left button. */
let tool = 'place';
function setTool(t){
  tool = t;
  document.querySelectorAll('#rail .tool').forEach(x =>
    x.classList.toggle('on', x.dataset.t === tool));
  if (tool !== 'place'){ armed = null; brush = null; clearGhost(); }
  ui(); syncOverlays(); status();
}

export function init(api){
  A = api;
  MAT_OK = washMat('#5ad46a', 0.30, 7);
  MAT_BAD = washMat('#ff4a3d', 0.55, 9);
  MAT_NOGO = washMat('#ff6a4a', 0.16, 2);

  wireChrome();
  wireRadar();
  probeServer();

  fetch('data/editor_tables.json').then(r => r.json()).then(t => {
    T = t;
    if (A.MAP){ terrainInit(A.MAP); elevInit(A.MAP);
                if (restoreIfAny(A.MAP)) A.refresh(); }
    setMode(true);
    drawRadar(); paintLint(lint(A.MAP));                 // the editor IS the application; it opens editing
  });

  // keep the wash riding the vertical-scale slider
  const tick = () => {
    for (const m of [MAT_OK, MAT_BAD, MAT_NOGO])
      if (m) m.uniforms.uExag.value = A.exag;
    if (on) drawRadarFrame();
    requestAnimationFrame(tick);
  };
  tick();

  const cv = A.renderer.domElement;
  cv.style.touchAction = 'none';
  cv.addEventListener('pointerdown', onDown);
  cv.addEventListener('contextmenu', e => e.preventDefault());
  window.addEventListener('pointermove', ev => { onDragStart(ev); onMove(ev); });
  window.addEventListener('pointerup', onUp);
  window.addEventListener('pointercancel', () => { dragging = null; downAt = null;
                                                   clearGhost(); status(); });
  window.addEventListener('keydown', onKey);

  A.onMapLoaded.push(m => {
    undoStack = []; redoStack = []; armed = null; brush = null; dragging = null;
    clearGhost();
    if (T){ terrainInit(m); elevInit(m); }   // both are per MAP, not global
    if (on){ restoreIfAny(m); ui(); rebuildNoGo(); status();
             drawRadar(); paintLint(lint(m)); }
  });
}
