#ifndef OPENCNC3D_RA_BARREL_VISUAL_H
#define OPENCNC3D_RA_BARREL_VISUAL_H
#include <string.h>

/* User runtime size override, 25 October 2026: RA3D barrels "40% smaller".
   Runtime presentation only. Both committed barrel models (all three sets) have
   this native base height. Preserve that ground contact while making each dimension
   40 percent smaller, without moving the engine anchor or changing its occupied cell. */
static const float RA_BARREL_VISUAL_SCALE = 0.60f;
static const float RA_BARREL_VISUAL_BASE_Y = -8.192f;

static bool ra_barrel_visual_matrix(bool redAlert, bool enhanced, bool classic,
                                   const char* meshName, bool shadowPlate,
                                   bool alreadyApplied, const float* before, float* after)
{
    if (!redAlert || !enhanced || classic || alreadyApplied || !meshName) return false;
    if (strncmp(meshName,"ebld:BARL:",10) && strncmp(meshName,"ebld:BRL3:",10)) return false;
    /* The death's scorch and fireball remain the engine's original effect size. */
    if (!meshName[10] || !strcmp(meshName+10,"scorch")) return false;
    const float scale=RA_BARREL_VISUAL_SCALE;
    for (int row=0;row<3;++row) for (int col=0;col<4;++col)
        after[row*4+col]=before?scale*before[row*4+col]:(col==row?scale:0.0f);
    /* Native shadow plates lie on y0 instead of the slightly embedded barrel foot. */
    if (!shadowPlate) after[7]+=(1.0f-scale)*RA_BARREL_VISUAL_BASE_Y;
    return true;
}
#endif
