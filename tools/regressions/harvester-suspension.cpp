#include "../../game/vehicle_suspension.h"
#include <assert.h>
#include <stdio.h>
int main() {
    VehicleSuspension a = {}, b = {};
    float distance=0,peakHeave=0,peakPitch=0,peakRoll=0;
    for (int t=0;t<300;t++) {
        float v=t<120?.04f:t<150?.04f*(150-t)/30.f:0;
        float w=t>40&&t<80?.04f:0;
        distance+=v;
        vehicle_suspension_step(a,v,w,distance,0,t==0);
        vehicle_suspension_step(b,v,w,distance,0,t==0);
        assert(!memcmp(&a,&b,sizeof a));
        for(int k=0;k<3;k++)assert(isfinite(a.value[k])&&isfinite(a.velocity[k]));
        peakHeave=fmaxf(peakHeave,fabsf(a.value[0]));peakPitch=fmaxf(peakPitch,fabsf(a.value[1]));peakRoll=fmaxf(peakRoll,fabsf(a.value[2]));
    }
    assert(peakHeave>.005f&&peakPitch>.02f&&peakRoll>.01f);
    for(int k=0;k<3;k++)assert(fabsf(a.value[k])<.00001f);
    VehicleSuspension left = {}, right = {};
    // Same speed, travel and road input; opposite turns isolate centrifugal lean.
    vehicle_suspension_step(left,.04f,0,0,0,true);
    vehicle_suspension_step(right,.04f,0,0,0,true);
    for (int t=0;t<45;t++) {
        vehicle_suspension_step(left,.04f,.035f,1.25f,0,false);
        vehicle_suspension_step(right,.04f,-.035f,1.25f,0,false);
    }
    // +x is the model's left: positive roll lowers the right/outside of a left turn.
    assert(left.value[2] > right.value[2] + .01f);
    vehicle_suspension_step(a,0,0,1000,1,true);
    for(int k=0;k<3;k++)assert(a.value[k]==0&&a.previous[k]==0&&a.velocity[k]==0);
    printf("PASS deterministic motion, acceleration/braking/turn excitation, stationary settling and teleport reset; peaks %.5f %.5f %.5f\n",peakHeave,peakPitch,peakRoll);
}
