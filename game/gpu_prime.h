/* Hybrid laptops keep the display on the integrated GPU and leave the discrete
   NVIDIA card idle unless a process asks for PRIME render offload. SDL then
   creates its GL context on whichever vendor GLVND picks by default, which is
   Mesa on the integrated chip, and the whole mission renders there.

   Set the two variables GLVND reads before it loads a vendor. This has to run
   before SDL_Init: that is what loads libEGL/libGLX. An explicit choice wins:
   OPENCNC3D_GPU=intel leaves the default alone, OPENCNC3D_GPU=nvidia forces
   offload, and so does a variable the user already exported. */
#ifndef OPENCNC3D_GPU_PRIME_H
#define OPENCNC3D_GPU_PRIME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <dirent.h>
#include <unistd.h>

static int opencnc3d_drm_vendor(const char* name)
{
    char path[256];
    unsigned vendor = 0;
    FILE* f;
    snprintf(path, sizeof path, "/sys/class/drm/%s/device/vendor", name);
    f = fopen(path, "r");
    if (!f) return 0;
    if (fscanf(f, "%x", &vendor) != 1) vendor = 0;
    fclose(f);
    return (int)vendor;
}
#endif

static void opencnc3d_prefer_discrete_gpu(void)
{
#if defined(__linux__)
    const char* choice = getenv("OPENCNC3D_GPU");
    int force = 0;
    int nvidia = 0, other = 0;
    DIR* d;
    struct dirent* ent;

    if (choice && choice[0]) {
        if (strcmp(choice, "nvidia") != 0) return;
        force = 1;
    } else if (getenv("__NV_PRIME_RENDER_OFFLOAD")
            || getenv("__GLX_VENDOR_LIBRARY_NAME")
            || getenv("DRI_PRIME")) {
        return;
    }
    if (access("/usr/share/glvnd/egl_vendor.d/10_nvidia.json", R_OK) != 0)
        return;

    d = opendir("/sys/class/drm");
    if (d) {
        while ((ent = readdir(d)) != NULL) {
            int vendor;
            if (strncmp(ent->d_name, "renderD", 7) != 0) continue;
            vendor = opencnc3d_drm_vendor(ent->d_name);
            if (vendor == 0x10de) nvidia = 1;
            else if (vendor) other = 1;
        }
        closedir(d);
    }
    if (!force && (!nvidia || !other)) return;

    setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0);
    setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0);
    fprintf(stderr, "gpu: NVIDIA PRIME render offload "
                    "(the display stays on the other chip)\n");
#endif
}

#endif
