#ifndef OPENCNC3D_WATER_BED_H
#define OPENCNC3D_WATER_BED_H

/* Enhanced water supplies its own fogged virtual slope. There are no generated
   banks, node maps, material lookup textures, or extra normal/depth meshes.
   Only authored native falls retain their original two-triangle floor. */
static bool water_bed_draw(bool normalPass=false)
{
    if(!water_fx_wanted() || !water_plane_on() || !water_prog_get())return false;
    if(normalPass)return true;
    const auto& cells=water_cells();
    const GLuint dirt=g_pack.seabedTex>=0 && g_pack.seabedTex<(int)g_pack.tex.size()?g_pack.tex[g_pack.seabedTex].gl:0;
    GLint prior=0;glGetIntegerv(GL_CURRENT_PROGRAM,&prior);
    glPushAttrib(GL_ENABLE_BIT|GL_TEXTURE_BIT|GL_DEPTH_BUFFER_BIT|GL_COLOR_BUFFER_BIT|GL_CURRENT_BIT);
    glDisable(GL_ALPHA_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
    static const int dx[6]={0,0,1,1,0,1},dz[6]={0,1,0,0,1,1};
    if(const char* path=getenv("OPENCNC3D_WATER_PLANE_DUMP"))if(*path)if(FILE* f=fopen(path,"wb")) {
        const unsigned dims[2]={(unsigned)g_gridW,(unsigned)g_gridH};fwrite("C3DPLN1",1,8,f);fwrite(dims,4,2,f);
        fwrite(g_waterPlane.surf.data(),4,g_waterPlane.surf.size(),f);
        for(int z=0;z<g_gridH;z++)for(int x=0;x<g_gridW;x++){const int body=water_plane_render_body(x,z);fwrite(&body,4,1,f);}fclose(f);
    }
    if(const char* path=getenv("OPENCNC3D_WATER_BED_DUMP"))if(*path)if(FILE* f=fopen(path,"wb")) {
        const unsigned n=0;fwrite("C3DBED1",1,8,f);fwrite(&n,4,1,f);fclose(f);
    }
    if(!normalPass) {
        // Authored falls and unsupported border bodies retain native geometry.
        // Replacing the planar floor must not remove their original opaque bed.
        const bool ra=water_src_ra(),painted=!ra && (!strcmp(g_pack.theater,"WINTER") || !strcmp(g_pack.theater,"SNOW")) && water_painted_cut_td() && water_td_painting();
        const GLuint floor=ra?water_atlas(water_atlas_index()).gl:painted?g_waterTdPainting.base:dirt;
        if(floor) {
            fx_glUseProgram(0);
            for(int unit=4;unit>0;unit--){glActiveTexture(GL_TEXTURE0+unit);glDisable(GL_TEXTURE_2D);}
            glActiveTexture(GL_TEXTURE0);glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,floor);
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
            const float repeats=painted?water_base_repeats():1.0f;
            const bool oneSun=fx_unified_lighting() || (g_fxActive && g_fx.sun_lambert && g_fx.terrain_normals);
            glBegin(GL_TRIANGLES);
            for(const auto& c:cells)if(!water_plane_covers(c) && water_cell_wet(c) && cell_shown(c.x,c.y) && cell_in_view(c.x,c.y)) {
                for(int k=0;k<6;k++) {
                    const int x=c.x+dx[k],z=c.y+dz[k];
                    const float gain=(ra||painted)?(oneSun?160.0f/255.0f:terrain_shade(x,z)):1.0f;
                    const float light=gain*shroud_corner_vis(x,z);glColor3f(light,light,light);
                    glTexCoord2f(ra?c.u0+(c.u1-c.u0)*dx[k]:x*repeats,ra?c.v0+(c.v1-c.v0)*dz[k]:z*repeats);
                    glVertex3f((float)x,water_plane_floor_y(c,x,z),(float)z);
                }
            }
            glEnd();
        }
    }
    fx_glUseProgram((GLuint)prior);glPopAttrib();glActiveTexture(GL_TEXTURE0);return true;
}
#endif
