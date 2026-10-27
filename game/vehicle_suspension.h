/* Render-only sprung chassis. Native tick state, interpolated at the draw; no wall clock. */
#ifndef VEHICLE_SUSPENSION_H
#define VEHICLE_SUSPENSION_H
#include <math.h>
#include <string.h>
struct VehicleSuspension {
    float value[3], previous[3], velocity[3]; /* heave (cells), pitch, roll (radians) */
    float speed;
    bool initialized;
};
static float vehicle_suspension_limit(float x, float limit)
{ return x < -limit ? -limit : x > limit ? limit : x; }
static void vehicle_suspension_step(VehicleSuspension& s, float v, float w,
                                     float travel, float groundResidual, bool reset)
{
    if (reset || !s.initialized) { memset(&s, 0, sizeof s); s.initialized = true; return; }
    memcpy(s.previous, s.value, sizeof s.value);
    const float before = s.speed;
    s.speed += (v * 15.0f - s.speed) * 0.35f;
    const float acceleration = (s.speed - before) * 15.0f;
    const float moving = fminf(1.0f, fabsf(s.speed) / 0.40f);
    /* Small, distance-driven road input, rather than the old full-hull 5.7 degree
       washboard. Stops remove its forcing; the chassis then settles on its dampers. */
    const float phase = travel * 9.0f;
    const float target[3] = {
        vehicle_suspension_limit(groundResidual, 0.020f) + moving * 0.009f * sinf(phase),
        vehicle_suspension_limit(-acceleration * 0.025f, 0.050f)
            + moving * 0.025f * sinf(phase + 0.8f),
        vehicle_suspension_limit(s.speed * w * 15.0f * 0.060f, 0.035f)
            + moving * 0.018f * sinf(phase * 0.73f + 1.6f)
    };
    /* 1.6 Hz heavy chassis, damping ratio .62; four stable integration steps/tick. */
    const float omega = 10.05309649f, dt = 1.0f / 60.0f;
    const float limits[3] = { 0.030f, 0.075f, 0.060f };
    for (int n = 0; n < 4; n++) for (int axis = 0; axis < 3; axis++) {
        s.velocity[axis] += (omega * omega * (target[axis] - s.value[axis])
                              - 1.24f * omega * s.velocity[axis]) * dt;
        s.value[axis] += s.velocity[axis] * dt;
        if (fabsf(s.value[axis]) > limits[axis]) {
            s.value[axis] = vehicle_suspension_limit(s.value[axis], limits[axis]);
            s.velocity[axis] = 0.0f;
        }
    }
}
#endif
