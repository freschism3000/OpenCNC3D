#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using GLuint = unsigned;
using GLenum = unsigned;
using GLint = int;
enum {
    GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_REPEAT,
    GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR,
    GL_LINEAR, GL_NEAREST, GL_RGBA, GL_UNSIGNED_BYTE
};
static bool uploadReuse = true;
static bool linearFilter = true;
static int fx_bilfix_on = 1;
static std::vector<unsigned> calls;
static std::vector<unsigned char> uploaded;
static unsigned copiedInputs = 0, borrowedInputs = 0;
static const unsigned char* expectedInput = nullptr;

static bool mesh_perf_switch(const char*) { return uploadReuse; }
static GLint fx_filter_mode(GLint fallback) { return linearFilter ? GL_LINEAR : fallback; }
static void fx_filter_note(GLuint texture, GLint fallback) { calls.insert(calls.end(), {5, texture, (unsigned)fallback}); }
static void glGenTextures(int count, GLuint* texture) { *texture = 17; calls.insert(calls.end(), {1, (unsigned)count, *texture}); }
static void glBindTexture(GLenum target, GLuint texture) { calls.insert(calls.end(), {2, target, texture}); }
static void glTexParameteri(GLenum target, GLenum parameter, GLint value) { calls.insert(calls.end(), {3, target, parameter, (unsigned)value}); }
static void capture(const unsigned char* pixels, int width, int height) {
    uploaded.assign(pixels, pixels + (size_t)width * height * 4);
    if (pixels == expectedInput) borrowedInputs++;
    else copiedInputs++;
}
static void glTexImage2D(GLenum target, int level, GLint internal, int width, int height,
                         int border, GLenum format, GLenum type, const void* pixels) {
    calls.insert(calls.end(), {4, target, (unsigned)level, (unsigned)internal, (unsigned)width,
                              (unsigned)height, (unsigned)border, format, type});
    capture((const unsigned char*)pixels, width, height);
}
static void gl_upload_mips(const unsigned char* pixels, int width, int height) {
    calls.insert(calls.end(), {6, (unsigned)width, (unsigned)height});
    capture(pixels, width, height);
}

EXTRACTED_FUNCTIONS

static uint32_t read_integer(std::ifstream& stream) {
    unsigned char bytes[4] = {};
    stream.read((char*)bytes, 4);
    return bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream stream(argv[1], std::ios::binary);
    char magic[8] = {};
    stream.read(magic, 8);
    if (memcmp(magic, "UPTEX001", 8)) return 3;
    const uint32_t cases = read_integer(stream);
    unsigned comparisons = 0, bypassed = 0;
    for (uint32_t index = 0; index < cases; index++) {
        const uint32_t nameBytes = read_integer(stream);
        const int width = (int)read_integer(stream), height = (int)read_integer(stream);
        const bool bled = read_integer(stream) != 0;
        if (!nameBytes || nameBytes > 1024 || width < 1 || width > 2048 || height < 1 || height > 2048) return 4;
        std::string name(nameBytes, ' ');
        stream.read(&name[0], nameBytes);
        std::vector<unsigned char> pixels((size_t)width * height * 4);
        stream.read((char*)pixels.data(), pixels.size());
        if (!stream) return 5;
        const std::vector<unsigned char> original = pixels;
        expectedInput = pixels.data();
        for (unsigned mode = 0; mode < 2; mode++) {
            linearFilter = mode != 0;
            for (unsigned mipmapped = 0; mipmapped < 2; mipmapped++) {
                calls.clear(); uploaded.clear();
                if (mipmapped) reference_mips(pixels, width, height, bled);
                else reference_plain(pixels, width, height);
                const auto expectedCalls = calls;
                const auto expectedPixels = uploaded;
                for (unsigned control = 0; control < 2; control++) {
                    uploadReuse = control != 0;
                    copiedInputs = borrowedInputs = 0;
                    calls.clear(); uploaded.clear();
                    if (mipmapped) ebld_upload_mips(pixels, width, height, bled);
                    else n64fix_upload(pixels, width, height, bled && uploadReuse);
                    comparisons++;
                    if (calls != expectedCalls || uploaded != expectedPixels || pixels != original) {
                        fprintf(stderr, "Mismatch %s mip=%u control=%u filter=%u bled=%d\n",
                                name.c_str(), mipmapped, control, mode, bled ? 1 : 0);
                        return 6;
                    }
                    if (bled && uploadReuse) {
                        if (borrowedInputs != 1 || copiedInputs != 0) return 7;
                        bypassed++;
                    } else if (copiedInputs != 1 || borrowedInputs != 0) return 8;
                }
            }
        }
    }
    printf("{\"passed\":true,\"pointer_bits\":%u,\"cases\":%u,\"comparisons\":%u,\"exact_borrowed_uploads\":%u}\n",
           (unsigned)(sizeof(void*)*8), cases, comparisons, bypassed);
    return 0;
}
