/* ================================================================================== *
 *  verdict_mod.h -- the cartridge's own MISSION ACCOMPLISHED / MISSION FAILED banner.
 *
 *  THE CONSOLE DOES NOT PRINT TEXT AT THE END OF A MISSION. The BriefingCode routine at
 *  ROM 0x1CDBB8 (RAM 0x801D99E8) builds three widgets:
 *
 *    "I0" / "I1"   BLACK.IMG, stretched to 328x240 at x=0 and 320x240 at x=320. Both
 *                  name pointers (RAM 0x801EC2FC and 0x801EC304) are the SAME string
 *                  "BLACK.IMG". Together, with the -160 view origin, they cover the
 *                  WHOLE screen -- the battlefield AND the sidebar.
 *    "I2" / "I3"   a pre-rendered gold bevelled 200x53 banner, MACCOMP.IMG when the win
 *                  flag at RAM 0x80093864 is set and MFAILED.IMG when it is clear
 *                  (ROM 0x1CDD6C), placed at ((640 - 200*n) >> 1, 99.0f) -- the 99.0
 *                  being the `lui $a3,0x42c6` at ROM 0x1CDE14. That is x=60, y=99 on a
 *                  320x240 screen.
 *
 *  1995 DOS did something else entirely: Do_Win / Do_Lose (scenario.cpp:428) print two
 *  VCR.FNT strings over the live battlefield. We follow the CONSOLE, because that is
 *  what this project is; the DOS-shaped fallback below exists only so a MISSING PACK
 *  fails loudly instead of showing a black screen with nothing on it.
 *
 *  RENDER RULES (Voodoo2-safe): GL 1.1 immediate mode, one untextured black quad over
 *  the whole framebuffer, then one textured quad. GL_NEAREST and an INTEGER scale, so
 *  the banner is pixel-exact rather than resampled. GL_CLAMP, not GL_CLAMP_TO_EDGE --
 *  the charter is GL 1.1 (cnc_sidebar.h already breaks that rule; do not spread it).
 *  ALPHA TEST rather than blending: the art's alpha is a hard 0/255 cutout straight out
 *  of RGBA5551, so the two give identical pixels today, and the alpha test keeps giving
 *  the right answer if anyone ever re-encodes the art with soft edges.
 * ================================================================================== */

#ifndef CNC3D_VERDICT_MOD_H
#define CNC3D_VERDICT_MOD_H

static GLuint g_vrdWin = 0, g_vrdLose = 0;
static int    g_vrdW = 0, g_vrdH = 0;       /* padded texture size, e.g. 256x64 */
static int    g_vrdUW = 0, g_vrdUH = 0;     /* used sub-rect, 200x53            */
static bool   g_vrdHave = false;

/* Call with a live GL context: it uploads textures. */
static bool verdict_load(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "verdict: no banner pack at %s (run game/bake_verdict.py); "
                        "falling back to sidebar-font text\n", path);
        return false;
    }
    char magic[8];
    unsigned ver = 0, count = 0;
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "CNC3DVRD", 8) != 0 ||
        fread(&ver, 4, 1, f) != 1 || ver != 1 ||
        fread(&count, 4, 1, f) != 1 || count < 1 || count > 8) {
        fprintf(stderr, "verdict: %s is not a CNC3DVRD v1 pack; falling back to text\n",
                path);
        fclose(f);
        return false;
    }
    for (unsigned i = 0; i < count; i++) {
        char name[9]; memset(name, 0, sizeof name);
        unsigned w = 0, h = 0, uw = 0, uh = 0;
        if (fread(name, 1, 8, f) != 8 ||
            fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 ||
            fread(&uw, 4, 1, f) != 1 || fread(&uh, 4, 1, f) != 1 ||
            w < 1 || w > 1024 || h < 1 || h > 1024 || uw > w || uh > h) {
            fprintf(stderr, "verdict: %s record %u header unreadable\n", path, i);
            fclose(f);
            return false;
        }
        std::vector<unsigned char> px((size_t)w * h * 4);
        if (fread(&px[0], 1, px.size(), f) != px.size()) {
            fprintf(stderr, "verdict: %s record %s truncated\n", path, name);
            fclose(f);
            return false;
        }
        GLuint* slot = !strcmp(name, "MACCOMP") ? &g_vrdWin
                     : !strcmp(name, "MFAILED") ? &g_vrdLose
                     : NULL;
        if (!slot)
            continue;                       /* a localised pair, or a future record */
        glGenTextures(1, slot);
        glBindTexture(GL_TEXTURE_2D, *slot);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        /* NOT registered: UI art is never bilinear. See fx_filter.h. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (int)w, (int)h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
        g_vrdW = (int)w;  g_vrdH = (int)h;
        g_vrdUW = (int)uw; g_vrdUH = (int)uh;
    }
    fclose(f);
    g_vrdHave = (g_vrdWin != 0 && g_vrdLose != 0);
    if (g_vrdHave)
        fprintf(stderr, "verdict: %s: MACCOMP and MFAILED, %dx%d used of %dx%d "
                        "(the cartridge's own banners)\n",
                path, g_vrdUW, g_vrdUH, g_vrdW, g_vrdH);
    else
        fprintf(stderr, "verdict: %s carries no MACCOMP/MFAILED pair; falling back to "
                        "sidebar-font text\n", path);
    return g_vrdHave;
}

/* Draws the console's whole announcement: the black plate over EVERYTHING (battlefield
   and sidebar alike, which is what the cartridge's two BLACK.IMG widgets do) and the
   banner centred on it. Returns false when there is no pack, so the caller can fall back
   loudly. The caller wraps this in begin_overlay/end_overlay. */
static bool verdict_draw(int fbw, int fbh, int win)
{
    if (!g_vrdHave)
        return false;

    /* The two BLACK.IMG plates, as one quad. */
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex2f(0.0f,        0.0f);
    glVertex2f((float)fbw,  0.0f);
    glVertex2f((float)fbw, (float)fbh);
    glVertex2f(0.0f,       (float)fbh);
    glEnd();

    /* The banner. The console's screen is 320x240 and the widget's top-left is (60,99);
       we letterbox that space into the framebuffer at an INTEGER scale so the art stays
       pixel-exact. The banner's own width is 200*n where n = *RAM 0x800971F4, a word no
       code in the ROM ever writes, so n is taken as 1 -- and the placement is CENTRED
       either way, since the ROM computes x = (640 - 200*n) >> 1. */
    int s = (int)floorf(fminf((float)fbw / 320.0f, (float)fbh / 240.0f));
    if (s < 1) s = 1;
    const float ox = floorf(((float)fbw - 320.0f * (float)s) * 0.5f);
    const float oy = floorf(((float)fbh - 240.0f * (float)s) * 0.5f);
    const float x0 = ox + 60.0f * (float)s;
    const float y0 = oy + 99.0f * (float)s;
    const float x1 = x0 + (float)g_vrdUW * (float)s;
    const float y1 = y0 + (float)g_vrdUH * (float)s;
    const float u1 = (float)g_vrdUW / (float)g_vrdW;
    const float v1 = (float)g_vrdUH / (float)g_vrdH;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, win ? g_vrdWin : g_vrdLose);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
    glTexCoord2f(u1,   0.0f); glVertex2f(x1, y0);
    glTexCoord2f(u1,   v1);   glVertex2f(x1, y1);
    glTexCoord2f(0.0f, v1);   glVertex2f(x0, y1);
    glEnd();
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    return true;
}

static void verdict_free(void)
{
    if (g_vrdWin)  glDeleteTextures(1, &g_vrdWin);
    if (g_vrdLose) glDeleteTextures(1, &g_vrdLose);
    g_vrdWin = g_vrdLose = 0;
    g_vrdHave = false;
}

#endif /* CNC3D_VERDICT_MOD_H */
