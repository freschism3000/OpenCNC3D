# The N64 camera, in closed form

Everything here is derived from `docs/n64-camera.json` and
verified numerically against an independent software render (agreement better than
0.01 px per cell at D = 2400, 3000 and 3800).

The point of this note: with yaw = 0 the projection collapses to something much simpler
than the current 45 degree code, and the fiddly part (picking) has an exact inverse. Nobody
should be inverting a general 4x4 matrix here.

## Setup

World is X right, Y up, Z toward the bottom of the screen. Cell `(cx, cy)` sits at
`(cx * 256, 0, cy * 256)`. Ground is `y = 0`. The look-at target is `y = -1`, a constant
from the ROM.

```
D      in [2400, 3800], default 3000          (leptons, 256 per cell)
pitch  = 0.78 + 0.14 * (D - 2400) / 1400      (radians; the ROM stores it negated)
at     = (camX, -1, camZ)
eye    = at + (0, D*sin(pitch), D*cos(pitch))
up     = (0, 1, 0)
proj   = perspective(fovy = 50 deg vertical, aspect = 4/3, near = 0.1*D, far = 2*D + 2096)
```

Note pitch is **not** a free parameter. Zooming out also tilts the camera further over.

## Forward projection

Because yaw is zero, the lookAt basis is exactly
`xaxis = (1, 0, 0)`, `yaxis = (0, cos p, -sin p)`, `zaxis = (0, sin p, cos p)`,
and the eye offset cancels almost everything. For a ground point at offset
`(ax, az) = (wx - camX, wz - camZ)` from the target:

```
xe    = ax
ye    = cos(p) - az * sin(p)
depth = D - sin(p) - az * cos(p)          (this is -ze)
```

That is the whole transform for the ground plane: two multiplies. For a point at height
`wy` above the ground, add `wy * sin(p)` to `ye` and subtract `wy * cos(p)` from `depth`.

Then with `f = 1 / tan(fovy/2) = 2.1445069`:

```
col = W/2 * (1 + xe * f / (aspect * depth))
row = H/2 * (1 - ye * f / depth)
```

## Picking, exact

Do not invert a matrix. Build the ray and intersect the ground plane:

```
ndcx = 2*col/W - 1
ndcy = 1 - 2*row/H
dvx  = ndcx * aspect / f
dvy  = ndcy / f
                                            /* direction in world space, zaxis term = -1 */
dirx = dvx
diry = dvy*cos(p) - sin(p)
dirz = -dvy*sin(p) - cos(p)

t    = -eye.y / diry                        /* eye.y = -1 + D*sin(p); diry is always < 0 */
wx   = eye.x + t*dirx
wz   = eye.z + t*dirz
```

`diry` cannot reach zero inside the legal pitch range, so no guard is needed beyond an
assert. The horizon is off-screen at every zoom level.

## What this deletes

- `COS45`, `SIN_PITCH`, `COS_PITCH` and the `u`/`v` skew basis. Screen X is world X now.
- `SPRITE_FACING_BIAS`. It exists only to undo the 45 degree yaw, so it becomes 0.
- The per-vertex CPU yaw rotation. Cheaper on Voodoo2, not more expensive.

## What this breaks, and must be fixed with it

1. **Sprite scale is no longer constant.** Under orthographic every sprite had one scale.
   Under perspective, screen pixels per world unit is `(H/2) * f / depth`, so a sprite's
   size depends on its depth. `SPRITE_TEXELS_PER_UNIT` has to become a per-sprite
   computation, not a constant.
2. **Culling.** The visible ground region is a trapezoid, not a rectangle. Widen the test
   or cull against the four unprojected screen corners.
3. **Zoom semantics invert.** Ours counts pixels per cell (bigger = closer); the console
   counts distance (bigger = further). Anything reading `g_zoom` needs its sense checked.

## Win98 and Voodoo2

No risk here. `glFrustum` is OpenGL 1.0 and Glide does perspective-correct texturing in
hardware; it is what the Voodoo2 was built for. The per-vertex cost is one divide, which we
already pay for the ortho path's transform. Do not use `gluPerspective`, since we do not
want a GLU dependency: call `glFrustum` with
`top = near * tan(25 deg)`, `right = top * 4/3`.
