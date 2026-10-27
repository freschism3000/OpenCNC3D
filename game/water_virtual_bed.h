#ifndef OPENCNC3D_WATER_VIRTUAL_BED_H
#define OPENCNC3D_WATER_VIRTUAL_BED_H
/* The blurred bank continues into a submerged slope before absorption hides it.
   Bound bright Snow donors without crushing the shallow slope to deep-water
   black. Projected cliff contacts use the slope's zero-depth colour: their XZ
   field distance can already be offshore, despite touching the visible bank.
   Dry-cliff black shadow donors receive shallow diffuse water fill, scaled by
   the scene light, before absorption takes them to the unchanged deep colour.
   Both colours share one bank evaluation; no geometry or scene copy is added. */
#define WATER_VIRTUAL_BED_GLSL \
    "vec3 waterVirtualBankColour(vec2 world,vec3 bank,float light) {\n" \
    "    bank*=light;\n" \
    "    float value=max(dot(bank,vec3(.299,.587,.114)),.001);\n" \
    "    bank*=min(0.70,0.42/value);\n" \
    "    float haze=0.96+0.04*sin(world.x*1.7+sin(world.y*1.1));\n" \
    "    return max(bank*haze,vec3(0.16,0.19,0.22)*light);\n" \
    "}\n" \
    "vec3 waterVirtualDepthColour(vec3 bank,float sd,float river) {\n" \
    "    float depth=3.0*smoothstep(0.0,mix(3.0,1.0,river),max(0.0,sd));\n" \
    "    vec3 transmission=exp(-vec3(2.8,1.8,1.3)*depth)*(1.0-smoothstep(1.25,2.7,depth));\n" \
    "    return mix(vec3(0.008,0.018,0.024),bank,transmission);\n" \
    "}\n" \
    "vec3 waterVirtualBedColour(vec2 world,vec3 bank,float sd,float river,float light) {\n" \
    "    return waterVirtualDepthColour(waterVirtualBankColour(world,bank,light),sd,river);\n" \
    "}\n" \
    "vec3 waterVirtualBed(vec2 world,vec2 bend,float sd,float river,float light) {\n" \
    "    return waterVirtualBedColour(world,texture2D(uLand,(world+bend)*uFieldScale).rgb,sd,river,light);\n" \
    "}\n"
#endif
