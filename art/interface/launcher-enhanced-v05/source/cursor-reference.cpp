/* Independent hidden OpenGL witness of the actual game's Enhanced CUR00.
 * Includes the game's unmodified draw routine; does not copy/reinvent its mesh.
 * Usage: cursor-reference INSTALL BASE_RGBA OUTPUT_RGBA WIDTH HEIGHT X Y
 */
#include <SDL.h>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unistd.h>
#include "enhanced_menu_cursor.h"

int main(int argc, char **argv)
{
    if (argc != 8) {
        std::fprintf(stderr, "usage: cursor-reference INSTALL BASE_RGBA OUTPUT_RGBA WIDTH HEIGHT X Y\n");
        return 2;
    }
    const int width = std::atoi(argv[4]), height = std::atoi(argv[5]);
    const int pointer_x = std::atoi(argv[6]), pointer_y = std::atoi(argv[7]);
    if (width < 1 || height < 1 || width > 4096 || height > 2048) return 2;
    if (chdir(argv[1])) return 2;
    SDL_setenv("OPENCNC3D_BACKGROUND", "1", 1);
    SDL_setenv("SDL_MAC_BACKGROUND_APP", "1", 1);
#ifdef SDL_HINT_MAC_BACKGROUND_APP
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL init: %s\n", SDL_GetError());
        return 2;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_Window *window = SDL_CreateWindow("Hidden cursor reference", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_BORDERLESS);
    if (!window) return 2;
    const Uint32 flags = SDL_GetWindowFlags(window);
    if (!(flags & SDL_WINDOW_HIDDEN) || !(flags & SDL_WINDOW_BORDERLESS) ||
        (flags & (SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS))) {
        std::fprintf(stderr, "Unexpected hidden window flags: %u\n", unsigned(flags));
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        std::fprintf(stderr, "GL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit(); return 2;
    }
    SDL_GL_SetSwapInterval(0);
    int drawable_w = 0, drawable_h = 0;
    SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
    if (drawable_w < width || drawable_h < height) return 2;
    const size_t count = size_t(width) * height * 4;
    std::vector<unsigned char> baseline(count), bottom_up(count), output(count);
    FILE *file = std::fopen(argv[2], "rb");
    if (!file || std::fread(baseline.data(), 1, count, file) != count) return 2;
    std::fclose(file);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE); glDisable(GL_FOG);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    GLuint texture;
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, baseline.data());
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2i(0, 0);
    glTexCoord2f(1, 0); glVertex2i(width, 0);
    glTexCoord2f(1, 1); glVertex2i(width, height);
    glTexCoord2f(0, 1); glVertex2i(0, height);
    glEnd();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    emcursor::draw(pointer_x, pointer_y, 1);
    if (emcursor::mesh.empty()) return 2;
    glFinish(); glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottom_up.data());
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::fprintf(stderr, "GL read error: %u\n", unsigned(error)); return 2;
    }
    for (int row = 0; row < height; row++)
        std::memcpy(output.data() + size_t(row) * width * 4,
                    bottom_up.data() + size_t(height - 1 - row) * width * 4, size_t(width) * 4);
    file = std::fopen(argv[3], "wb");
    if (!file || std::fwrite(output.data(), 1, count, file) != count) return 2;
    std::fclose(file);
    std::printf("CURSORORACLE|hidden=1|borderless=1|flags=%u|triangles=%zu|width=%d|height=%d|x=%d|y=%d\n",
                unsigned(flags), emcursor::mesh.size(), width, height, pointer_x, pointer_y);
    glDeleteTextures(1, &texture);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
