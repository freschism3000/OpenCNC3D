/* The spinning faction logo drawn on top of the score screen.
 *
 * WHY THIS IS AN OVERLAY AND NOT PART OF THE PLATE
 *     Every other thing on the score screen is 1995 software rasterising into a
 *     320x200 palette plate, which camp_draw then uploads as one GL texture. A
 *     3D model cannot go into that plate without pre-rendering it to frames and
 *     giving up the smooth spin. So it draws as a second GL pass over the plate
 *     quad, in its own viewport, with its own depth buffer region. the project owner, 26 Aug
 *     2026: "Dont bake them into the render. Have them appear on top."
 *
 * FIXED FUNCTION ON PURPOSE
 *     Same reason as the rest of the renderer: the Voodoo 2 target has nothing
 *     else. The lighting is done here in C rather than with GL_LIGHTING so the
 *     Win98 backend has one less piece of GL to find an equivalent for, and so
 *     the result is bit-identical run to run.
 */
#include "logo3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_SILENCE_DEPRECATION 1
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

/* The key light, in world space, from over the viewer's left shoulder. Ambient
   keeps the away-facing side readable rather than black -- the logo is small on
   screen and a fully dark half reads as a hole. */
#define LOGO3D_AMBIENT 0.38f
#define LOGO3D_DIFFUSE 0.62f
static const float LOGO3D_LIGHT[3] = { -0.35f, 0.55f, 0.76f };

/* How much of the box's smaller half-axis the model's radius fills. Under 1 so a
   corner of the logo cannot clip the viewport edge as it turns. */
#define LOGO3D_FILL 0.86f

/* THE TILT IS A CALLER'S ARGUMENT NOW, not a constant here -- see logo3d.h for the
   default the sidebar's F5 dials start from. What it does is lean the emblem towards
   the viewer so the plate reads as an object with a face and a rim rather than as a
   flat sticker.

   IT DOES NOT STOP THE EMBLEM GOING EDGE-ON, and the first version of this comment
   claimed it did. It cannot: at 90 and 270 degrees the plate's normal has turned to
   point along X, and a rotation about X leaves an X-pointing normal exactly where it
   was. tools/logosweep.c measures it. A plate spinning about the vertical HAS two
   edge-on moments per revolution; that is what a spinning plate looks like. */

static int l3d_rd(FILE *f, void *p, size_t n)
{
    return fread(p, 1, n, f) == n;
}

static int l3d_fail(Logo3D *L, char *err, int errlen, const char *why, const char *path)
{
    snprintf(err, (size_t)errlen, "%s: %s", path, why);
    logo3d_close(L);
    return 0;
}

int logo3d_open(Logo3D *L, const char *path, char *err, int errlen)
{
    char magic[8];
    unsigned int n;
    FILE *f;
    int i;

    memset(L, 0, sizeof(*L));
    err[0] = 0;

    f = fopen(path, "rb");
    if (!f) {
        snprintf(err, (size_t)errlen, "%s: not found", path);
        return 0;
    }
    if (!l3d_rd(f, magic, 8) || memcmp(magic, "C3DLOGO1", 8) != 0) {
        fclose(f);
        snprintf(err, (size_t)errlen, "%s: not a logo pack", path);
        return 0;
    }
    if (!l3d_rd(f, &n, 4) || n == 0 || n > LOGO3D_MAXLOGO) {
        fclose(f);
        snprintf(err, (size_t)errlen, "%s: bad logo count", path);
        return 0;
    }
    L->n = (int)n;

    for (i = 0; i < L->n; i++) {
        Logo3DModel *m = &L->m[i];
        unsigned int nt, nb;
        int t, b;

        if (!l3d_rd(f, m->name, 8) || !l3d_rd(f, &m->radius, 4) || !l3d_rd(f, &nt, 4)) {
            fclose(f);
            return l3d_fail(L, err, errlen, "truncated header", path);
        }
        m->name[8] = 0;
        if (nt > LOGO3D_MAXTEX || m->radius <= 0.0f) {
            fclose(f);
            return l3d_fail(L, err, errlen, "bad texture count or radius", path);
        }
        m->ntex = (int)nt;

        for (t = 0; t < m->ntex; t++) {
            unsigned short w, h;
            unsigned char *px;
            if (!l3d_rd(f, &w, 2) || !l3d_rd(f, &h, 2) || w == 0 || h == 0) {
                fclose(f);
                return l3d_fail(L, err, errlen, "bad texture size", path);
            }
            px = (unsigned char *)malloc((size_t)w * h * 4);
            if (!px || !l3d_rd(f, px, (size_t)w * h * 4)) {
                free(px);
                fclose(f);
                return l3d_fail(L, err, errlen, "truncated texture", path);
            }
            glGenTextures(1, &m->tex[t]);
            glBindTexture(GL_TEXTURE_2D, m->tex[t]);
            /* GL_LINEAR, not the NEAREST the plate uses: the cartridge filtered
               these bilinearly, and the logo is the one thing on this screen that
               is not a 1995 pixel. */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            free(px);
        }

        if (!l3d_rd(f, &nb, 4) || nb == 0 || nb > LOGO3D_MAXBATCH) {
            fclose(f);
            return l3d_fail(L, err, errlen, "bad batch count", path);
        }
        m->nbatch = (int)nb;
        for (b = 0; b < m->nbatch; b++) {
            Logo3DBatch *bt = &m->batch[b];
            unsigned int ti, nv;
            /* 1,000,000 verts is far above the 2,652 the biggest batch actually
               carries and far below anything that would make the malloc below a
               problem. Every other count in this file is bounded; this one was not,
               so a corrupt pack could ask for an arbitrary allocation. */
            if (!l3d_rd(f, &ti, 4) || !l3d_rd(f, &nv, 4)
             || nv == 0 || nv % 3 || nv > 1000000u) {
                fclose(f);
                return l3d_fail(L, err, errlen, "bad batch", path);
            }
            bt->texidx = (ti == 0xFFFFFFFFu) ? -1 : (int)ti;
            if (bt->texidx >= m->ntex) {
                fclose(f);
                return l3d_fail(L, err, errlen, "batch names a missing texture", path);
            }
            bt->nvert = (int)nv;
            bt->v = (float *)malloc(sizeof(float) * 8 * (size_t)nv);
            if (!bt->v || !l3d_rd(f, bt->v, sizeof(float) * 8 * (size_t)nv)) {
                fclose(f);
                return l3d_fail(L, err, errlen, "truncated vertices", path);
            }
        }
    }
    fclose(f);
    return 1;
}

void logo3d_close(Logo3D *L)
{
    int i, t, b;
    for (i = 0; i < LOGO3D_MAXLOGO; i++) {
        Logo3DModel *m = &L->m[i];
        for (t = 0; t < m->ntex; t++)
            if (m->tex[t])
                glDeleteTextures(1, &m->tex[t]);
        for (b = 0; b < m->nbatch; b++)
            free(m->batch[b].v);
    }
    memset(L, 0, sizeof(*L));
}

void logo3d_draw(Logo3D *L, int which, int fbh, int x, int y, int w, int h,
                 float angle, int fade,
                 float offx, float offy, float sx, float sy, float tilt)
{
    if (which < 0 || which >= L->n || w <= 0 || h <= 0)
        return;
    if (fade <= 0)
        return;                 /* the screen is black; nothing to draw over it */
    if (fade > 256) fade = 256;

    Logo3DModel *m = &L->m[which];
    int b, i;
    const float rad = angle * 3.14159265358979f / 180.0f;
    const float ca = cosf(rad), sa = sinf(rad);

    /* The light, carried into MODEL space rather than turning every normal into
       world space: dot(Ry(a)n, L) == dot(n, Ry(a)^T L), so one 3-vector rotates
       per frame instead of a few thousand. */
    const float trad = tilt * 3.14159265358979f / 180.0f;
    const float ct = cosf(trad), st = sinf(trad);
    /* undo the tilt first (it is applied outside the spin), then the spin */
    const float tx = LOGO3D_LIGHT[0];
    const float ty =  ct * LOGO3D_LIGHT[1] + st * LOGO3D_LIGHT[2];
    const float tz = -st * LOGO3D_LIGHT[1] + ct * LOGO3D_LIGHT[2];
    const float lx = ca * tx - sa * tz;
    const float ly = ty;
    const float lz = sa * tx + ca * tz;

    const float fadef = (float)fade / 256.0f;
    const float s = 1.0f / m->radius;               /* unit radius */
    const float aspect = (float)w / (float)h;
    const float dist = 1.0f / (0.5f * LOGO3D_FILL); /* see LOGO3D_FILL */

    glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_SCISSOR_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glPushMatrix();
    glMatrixMode(GL_MODELVIEW);  glPushMatrix();

    /* GL's viewport origin is bottom-left; every campaign coordinate is top-left. */
    const int gly = fbh - (y + h);
    glViewport(x, gly, w, h);
    glScissor(x, gly, w, h);
    glEnable(GL_SCISSOR_TEST);
    /* DEPTH WRITES ON *BEFORE* THE CLEAR, and the order is the whole of a real bug.
       glClear(GL_DEPTH_BUFFER_BIT) is masked by glDepthMask exactly like a fragment
       write is: with writes off the clear does NOTHING and returns quietly. This used
       to enable them further down with the rest of the draw state, which was fine on
       the score screen -- camp_draw sets glDepthMask(GL_TRUE) before its own clear --
       and silently broken in the sidebar, which draws its 2D bar with depth writes off.
       There the buffer kept the WORLD's depths, so most of the model failed the depth
       test against terrain that is nowhere near it and what survived was one untextured
       batch: the project owner's recording shows the coin as a flat grey ellipse with the emblem
       missing. Nothing about the model, the pack or the textures was wrong. */
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);   /* scissored, so the plate quad is untouched */

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* Half-height 0.5 at the near plane: a ~53 degree vertical field, enough
       perspective for the turn to read as 3D without the near edge ballooning.
       Fitting to the SMALLER axis is what keeps GDI and Nod the same size in a
       box that is not square. */
    if (aspect >= 1.0f)
        glFrustum(-0.5 * aspect, 0.5 * aspect, -0.5, 0.5, 1.0, 40.0);
    else
        glFrustum(-0.5, 0.5, -0.5 / aspect, 0.5 / aspect, 1.0, 40.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* THE NUDGE, in view space at the model's own depth. One unit of the frustum's
       half-height at the near plane covers half the box on screen, so a shift of P
       pixels is (P / (h/2)) * 0.5 * dist. Y is negated because the box's coordinates
       run DOWN and GL's run up. */
    {
        const float half = (h > 0) ? (float)h * 0.5f : 1.0f;
        glTranslatef((offx / half) * 0.5f * dist,
                     -(offy / half) * 0.5f * dist,
                     -dist);
    }
    glRotatef(tilt, 1.0f, 0.0f, 0.0f);          /* view space: tilt, THEN spin */
    glRotatef(angle, 0.0f, 1.0f, 0.0f);
    /* The two scale dials ride on top of the fit, so 1,1 is "as large as the box's
       smaller axis allows" and the numbers stay meaningful when the box changes. Z takes
       the smaller of the two so a squashed emblem does not keep a fat rim. */
    glScalef(s * (sx > 0.0f ? sx : 1.0f),
             s * (sy > 0.0f ? sy : 1.0f),
             s * ((sx < sy ? sx : sy) > 0.0f ? (sx < sy ? sx : sy) : 1.0f));

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);
    /* Alpha TEST rather than blending: the model is two-sided and unsorted, and a
       cutout needs no draw order to come out right. */
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.35f);
    glDisable(GL_CULL_FACE);        /* the logos are thin plates; both faces show */
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    for (b = 0; b < m->nbatch; b++) {
        const Logo3DBatch *bt = &m->batch[b];
        if (bt->texidx >= 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, m->tex[bt->texidx]);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        glBegin(GL_TRIANGLES);
        for (i = 0; i < bt->nvert; i++) {
            const float *v = &bt->v[i * 8];
            float d = v[3] * lx + v[4] * ly + v[5] * lz;
            /* Two-sided: a back face's normal points away, and unlit black is
               worse than lighting it as if it faced the other way. */
            if (d < 0.0f) d = -d;
            /* fade LAST, so the model dims with the plate rather than floating over
               a screen that is on its way to black. */
            const float sh = (LOGO3D_AMBIENT + LOGO3D_DIFFUSE * d) * fadef;
            glColor3f(sh, sh, sh);
            glTexCoord2f(v[6], v[7]);
            glVertex3f(v[0], v[1], v[2]);
        }
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glPopAttrib();
    glColor3f(1.0f, 1.0f, 1.0f);
}
