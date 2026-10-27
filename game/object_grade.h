/* Shared object-only display grade. It never changes coverage, depth or fog.
   The exact neutral bypass keeps existing authored materials byte-identical. */
#ifndef OPENCNC3D_OBJECT_GRADE_H
#define OPENCNC3D_OBJECT_GRADE_H
struct FxObjectGrade {
    float bright, contrast, exposure, gamma, sat, warmth, tint, red, green, blue;
};
static inline int fx_object_grade_active(const FxObjectGrade& g)
{
    return g.bright!=1.0f || g.contrast!=1.0f || g.exposure!=0.0f || g.gamma!=1.0f ||
           g.sat!=1.0f || g.warmth!=0.0f || g.tint!=0.0f ||
           g.red!=1.0f || g.green!=1.0f || g.blue!=1.0f;
}
/* Input/output are display RGB; exposure is linear-light stops. Contrast fixes black
   and white, so a transparent additive source cannot acquire a rectangular black lift. */
#define FX_OBJECT_GRADE_GLSL \
    "uniform vec4 uObjectGrade0;\n" \
    "uniform vec4 uObjectGrade1;\n" \
    "uniform vec3 uObjectGradeRGB;\n" \
    "vec3 objectDecode(vec3 c) { c=max(c,vec3(0.0));\n" \
    "    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c)); }\n" \
    "vec3 objectEncode(vec3 c) { c=max(c,vec3(0.0));\n" \
    "    return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-0.055,step(vec3(0.0031308),c)); }\n" \
    "vec3 objectGrade(vec3 c) {\n" \
    "    if (uObjectGrade1.w < 0.5) return c;\n" \
    "    vec3 g = max(c,vec3(0.0));\n" \
    "    if (uObjectGrade0.y != 1.0) {\n" \
    "        vec3 low=min(g,vec3(1.0));\n" \
    "        vec3 a=pow(low,vec3(uObjectGrade0.y)), b=pow(vec3(1.0)-low,vec3(uObjectGrade0.y));\n" \
    "        g=a/max(a+b,vec3(1e-5))+max(g-vec3(1.0),vec3(0.0));\n" \
    "    }\n" \
    "    if (uObjectGrade0.w != 1.0) g=pow(g,vec3(1.0/uObjectGrade0.w));\n" \
    "    g=mix(vec3(dot(g,vec3(0.2126,0.7152,0.0722))),g,uObjectGrade1.x);\n" \
    "    g=max(g,vec3(0.0));\n" \
    "    if (uObjectGrade0.z != 0.0) {\n" \
    "        g=objectEncode(objectDecode(g)*exp2(uObjectGrade0.z));\n" \
    "    }\n" \
    "    vec3 balance=vec3(1.0+0.30*uObjectGrade1.y-0.10*uObjectGrade1.z,\n" \
    "                      1.0+0.20*uObjectGrade1.z,1.0-0.30*uObjectGrade1.y-0.10*uObjectGrade1.z);\n" \
    "    return max(g*uObjectGrade0.x*uObjectGradeRGB*balance,vec3(0.0));\n" \
    "}\n"
#endif
