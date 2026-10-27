/* iOS 26's endian.h brings MacTypes.h into global scope. Its legacy Rect
 * conflicts with Vanilla Conquer's unrelated Rect class. Load that SDK header
 * once with its system-only Rect renamed, before the engine declares its own.
 * Byte order and all engine geometry remain unchanged. */
#ifndef OPENCNC3D_IOS_BRAIN_SDK_COMPAT_H
#define OPENCNC3D_IOS_BRAIN_SDK_COMPAT_H
#define Rect OpenCNC3DAppleSystemRect
#include <endian.h>
#undef Rect
#endif
