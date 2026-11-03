/* compat/linux/OpenGL/gl.h
 *
 * cnc_eyes.cpp asks for Apple's framework header by name. On Linux the same
 * fixed-function API is Mesa's <GL/gl.h>, which this platform's libGL exports,
 * including the 1.3 multitexture entry points. This is a spelling difference.
 *
 * tools/linux/build-linux.sh puts this directory first on the include path.
 * Every other source already chooses <GL/gl.h> itself when it is not on Apple.
 */
#ifndef OPENCNC3D_LINUX_OPENGL_GL_H
#define OPENCNC3D_LINUX_OPENGL_GL_H

#include <GL/gl.h>

#endif
