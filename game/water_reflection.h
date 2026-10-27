#ifndef OPENCNC3D_WATER_REFLECTION_H
#define OPENCNC3D_WATER_REFLECTION_H

#include <math.h>

/* Water's highlight bearing is independent of the shared sun. A positive offset
   turns clockwise on the map; elevation and every global light remain intact. */
static inline void water_reflection_direction(const float* lightTravel, float yaw,
                                             float* towardLight)
{
    const float x = -lightTravel[0], y = -lightTravel[1], z = -lightTravel[2];
    if (yaw == 0.0f) {
        towardLight[0] = x; towardLight[1] = y; towardLight[2] = z;
        return;
    }
    const float radians = yaw * 0.017453292519943295f;
    const float c = cosf(radians), s = sinf(radians);
    towardLight[0] = c * x - s * z;
    towardLight[1] = y;
    towardLight[2] = s * x + c * z;
}

#endif
