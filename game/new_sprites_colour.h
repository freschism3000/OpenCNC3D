/* Palette-only grading of native, unpremultiplied sprite pixels. No GL dependency. */
#ifndef OPENCNC3D_NEW_SPRITES_COLOUR_H
#define OPENCNC3D_NEW_SPRITES_COLOUR_H
#include <math.h>
struct NsGrade {
    float brightness, contrast, exposure, gamma, saturation;
};
static bool ns_grade_equal(const NsGrade& a,const NsGrade& b)
{
    return a.brightness==b.brightness&&a.contrast==b.contrast&&a.exposure==b.exposure&&
           a.gamma==b.gamma&&a.saturation==b.saturation;
}
static float ns_colour_clamp(float v) { return v<0?0:v>1?1:v; }
static void ns_grade_colour(unsigned char rgba[4],const NsGrade& g)
{
    const NsGrade neutral={1,1,0,1,1};
    /* Preserve exact approved bytes at neutral, including transparent pixels. */
    if(!rgba[3]||ns_grade_equal(g,neutral))return;
    float rgb[3];
    for(int i=0;i<3;i++) {
        float c=rgba[i]/255.f;
        /* Exposure is measured in stops, in linear light. */
        if(g.exposure!=0) {
            c=c<=.04045f?c/12.92f:powf((c+.055f)/1.055f,2.4f);
            c*=exp2f(g.exposure);
            c=c<=.0031308f?12.92f*c:1.055f*powf(c,1/2.4f)-.055f;
        }
        rgb[i]=(c*g.brightness-.5f)*g.contrast+.5f;
    }
    float luminance=.2126f*rgb[0]+.7152f*rgb[1]+.0722f*rgb[2];
    for(int i=0;i<3;i++) {
        float c=ns_colour_clamp(luminance+(rgb[i]-luminance)*g.saturation);
        if(g.gamma!=1)c=powf(c,1/g.gamma);
        rgba[i]=(unsigned char)floorf(ns_colour_clamp(c)*255.f+.5f);
    }
    /* Alpha is deliberately unchanged: no new edges, shadows or picking bounds. */
}
#endif
