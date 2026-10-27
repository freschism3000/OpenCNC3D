/* Static iOS binding for the existing Tiberian Dawn C ABI. */
#ifndef OPENCNC3D_IOS_STATIC_BRAIN_H
#define OPENCNC3D_IOS_STATIC_BRAIN_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Returns NULL for unavailable/optional exports, just as dlsym does. The returned
 * address must be cast to the existing, correctly typed CNC_* function pointer. */
void *opencnc3d_ios_brain_symbol(const char *name);
size_t opencnc3d_ios_brain_symbol_count(void);
/* Platform-only compatibility hooks for the existing renderer loader. */
void *cnc_ios_dlopen(const char *name, int flags);
void *cnc_ios_dlsym(void *handle, const char *name);
int cnc_ios_dlclose(void *handle);
const char *cnc_ios_dlerror(void);
#ifdef __cplusplus
}
#endif
#endif
