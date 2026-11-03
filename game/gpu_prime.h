/* game/gpu_prime.h -- DISCRETE GPU ON HYBRID LINUX, BEFORE SDL_Init.
 *
 * Hybrid laptops keep the display on the integrated GPU unless the process asks
 * for PRIME render offload. SDL loads libGL on the default chip if nothing is
 * set first, so this runs once in main before SDL_Init.
 *
 * We only act when sysfs shows exactly one boot GPU (boot_vga=1) and exactly one
 * other PCI display adapter. One GPU in the machine, or a messy topology, does
 * nothing. Existing DRI_PRIME or NVIDIA PRIME variables are never overwritten.
 *
 * Intel boot GPU plus a second adapter: NVIDIA uses NVIDIA PRIME; AMD uses Mesa
 * DRI_PRIME with an explicit pci- address so enumeration order does not matter.
 * AMD boot plus AMD secondary only on a laptop (a Battery entry under
 * power_supply), because on a desktop the graphics card is often boot_vga=1 and
 * the iGPU is secondary -- Mesa PRIME there would move rendering to the iGPU.
 *
 * Linux only. Other platforms compile an empty stub.
 */
#ifndef OPENCNC3D_GPU_PRIME_H
#define OPENCNC3D_GPU_PRIME_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)

#include <dirent.h>
#include <unistd.h>

#define OPENCNC3D_GPU_INTEL  0x8086
#define OPENCNC3D_GPU_NVIDIA 0x10de
#define OPENCNC3D_GPU_AMD    0x1002

typedef struct {
    unsigned vendor;
    int boot_vga;
    char pci[16];
} opencnc3d_gpu_info;

static int opencnc3d_read_hex(const char* path, unsigned* value)
{
    FILE* f = fopen(path, "r");
    int ok;

    if (!f)
        return 0;
    ok = fscanf(f, "%x", value) == 1;
    fclose(f);
    return ok;
}

static int opencnc3d_read_int(const char* path, int* value)
{
    FILE* f = fopen(path, "r");
    int ok;

    if (!f)
        return 0;
    ok = fscanf(f, "%d", value) == 1;
    fclose(f);
    return ok;
}

static int opencnc3d_drm_card_is_numeric(const char* name)
{
    const char* p = name + 4;

    if (strncmp(name, "card", 4) != 0 || !name[4])
        return 0;
    while (*p) {
        if (*p < '0' || *p > '9')
            return 0;
        ++p;
    }
    return 1;
}

static int opencnc3d_gpu_pci_address(const char* card, char* pci, size_t size)
{
    char path[288];
    char target[256];
    char* name;
    ssize_t len;

    snprintf(path, sizeof path, "/sys/class/drm/%s/device", card);
    len = readlink(path, target, sizeof target - 1);
    if (len <= 0)
        return 0;
    target[len] = '\0';
    name = strrchr(target, '/');
    name = name ? name + 1 : target;
    if (strlen(name) >= size)
        return 0;
    /* dddd:bb:ss.f */
    if (strlen(name) != 12 || name[4] != ':' || name[7] != ':' || name[10] != '.')
        return 0;
    snprintf(pci, size, "%s", name);
    return 1;
}

static int opencnc3d_gpu_topology(opencnc3d_gpu_info* primary,
                                  opencnc3d_gpu_info* secondary)
{
    DIR* d = opendir("/sys/class/drm");
    struct dirent* ent;
    int primary_count = 0;
    int secondary_count = 0;
    int unknown_count = 0;

    if (!d)
        return 0;

    while ((ent = readdir(d)) != NULL) {
        char path[288];
        opencnc3d_gpu_info gpu;
        unsigned vendor;
        unsigned class_code;
        int boot_vga;

        if (!opencnc3d_drm_card_is_numeric(ent->d_name))
            continue;

        snprintf(path, sizeof path, "/sys/class/drm/%s/device/vendor", ent->d_name);
        if (!opencnc3d_read_hex(path, &vendor))
            continue;

        snprintf(path, sizeof path, "/sys/class/drm/%s/device/class", ent->d_name);
        if (!opencnc3d_read_hex(path, &class_code))
            continue;
        if ((class_code >> 16) != 0x03)
            continue;

        snprintf(path, sizeof path, "/sys/class/drm/%s/device/boot_vga", ent->d_name);
        if (!opencnc3d_read_int(path, &boot_vga) || (boot_vga != 0 && boot_vga != 1)) {
            unknown_count++;
            continue;
        }

        gpu.vendor = vendor;
        gpu.boot_vga = boot_vga;
        gpu.pci[0] = '\0';
        if (!opencnc3d_gpu_pci_address(ent->d_name, gpu.pci, sizeof gpu.pci)) {
            unknown_count++;
            continue;
        }

        if (boot_vga == 1) {
            *primary = gpu;
            primary_count++;
        } else {
            *secondary = gpu;
            secondary_count++;
        }
    }

    closedir(d);
    if (unknown_count != 0 || primary_count != 1 || secondary_count != 1)
        return 0;
    return 1;
}

static int opencnc3d_on_battery_machine(void)
{
    DIR* d = opendir("/sys/class/power_supply");
    struct dirent* ent;

    if (!d)
        return 0;

    while ((ent = readdir(d)) != NULL) {
        char path[320];
        char type[32];
        FILE* f;

        if (ent->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", ent->d_name);
        f = fopen(path, "r");
        if (!f)
            continue;
        if (fscanf(f, "%31s", type) == 1 && !strcmp(type, "Battery")) {
            fclose(f);
            closedir(d);
            return 1;
        }
        fclose(f);
    }

    closedir(d);
    return 0;
}

static int opencnc3d_enable_nvidia_prime(void)
{
    if (access("/usr/share/glvnd/egl_vendor.d/10_nvidia.json", R_OK) != 0)
        return 0;
    if (setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0) != 0)
        return 0;
    if (setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0) != 0) {
        unsetenv("__NV_PRIME_RENDER_OFFLOAD");
        return 0;
    }
    fprintf(stderr, "gpu: NVIDIA PRIME render offload\n");
    return 1;
}

static int opencnc3d_enable_mesa_prime(const char* pci)
{
    char value[64];
    size_t i;

    snprintf(value, sizeof value, "pci-%s", pci);
    for (i = 0; value[i]; ++i) {
        if (value[i] == ':' || value[i] == '.')
            value[i] = '_';
    }
    if (setenv("DRI_PRIME", value, 0) != 0)
        return 0;
    fprintf(stderr, "gpu: Mesa PRIME %s\n", value);
    return 1;
}

#endif /* __linux__ */

/* Call before SDL_Init(). No-op on platforms other than Linux. */
static void opencnc3d_prefer_discrete_gpu(void)
{
#if defined(__linux__)
    opencnc3d_gpu_info primary;
    opencnc3d_gpu_info secondary;

    if (getenv("DRI_PRIME")
            || getenv("__NV_PRIME_RENDER_OFFLOAD")
            || getenv("__GLX_VENDOR_LIBRARY_NAME"))
        return;
    if (!opencnc3d_gpu_topology(&primary, &secondary))
        return;

    if (primary.vendor == OPENCNC3D_GPU_INTEL) {
        if (secondary.vendor == OPENCNC3D_GPU_NVIDIA)
            opencnc3d_enable_nvidia_prime();
        else if (secondary.vendor == OPENCNC3D_GPU_AMD)
            opencnc3d_enable_mesa_prime(secondary.pci);
        return;
    }

    if (primary.vendor == OPENCNC3D_GPU_AMD
            && secondary.vendor == OPENCNC3D_GPU_AMD
            && opencnc3d_on_battery_machine())
        opencnc3d_enable_mesa_prime(secondary.pci);
#endif
}

#endif /* OPENCNC3D_GPU_PRIME_H */
