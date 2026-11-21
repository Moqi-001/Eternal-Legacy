/* Measure imported libc and GL boundaries without per-call console output. */
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <so_common/so_logger.h>
#include <so_libs/libc/bionic_stdio.h>
#include <vitaGL.h>
#include "diagnostics.h"
#include "utils/perf.h"

#if PERF_DIAGNOSTICS
extern ssize_t read_soloader(int fd, void *buf, size_t count);
extern off_t lseek_soloader(int fd, off_t offset, int whence);

static size_t trace_fread(void *ptr, size_t size, size_t count, FILE *file) {
    uint64_t t = perf_now_us();
    size_t n = 0;
    if (file) n = fread_soloader(ptr, size, count, file);
    else errno = EBADF;
    int saved_errno = errno, ok = file && !ferror(file);
    perf_record(PERF_FREAD, NULL, t, n * size, ok, 0);
    errno = saved_errno;
    return n;
}
static ssize_t trace_read(int fd, void *ptr, size_t count) {
    uint64_t t = perf_now_us();
    ssize_t n = read_soloader(fd, ptr, count);
    perf_record(PERF_READ, NULL, t, n > 0 ? n : 0, n >= 0, 0);
    return n;
}
static int trace_fseek(FILE *file, long offset, int whence) {
    uint64_t t = perf_now_us();
    int result;
    if (file) result = fseek_soloader(file, offset, whence);
    else { errno = EBADF; result = -1; }
    int saved_errno = errno;
    perf_record(PERF_SEEK, NULL, t, 0, result == 0, 0);
    errno = saved_errno;
    return result;
}
static int trace_fseeko(FILE *file, long offset, int whence) {
    uint64_t t = perf_now_us();
    int result;
    if (file) result = fseeko_soloader(file, offset, whence);
    else { errno = EBADF; result = -1; }
    int saved_errno = errno;
    perf_record(PERF_SEEK, NULL, t, 0, result == 0, 0);
    errno = saved_errno;
    return result;
}
static off_t trace_lseek(int fd, off_t offset, int whence) {
    uint64_t t = perf_now_us();
    off_t result = lseek_soloader(fd, offset, whence);
    perf_record(PERF_SEEK, NULL, t, 0, result >= 0, 0);
    return result;
}
static void trace_compile(GLuint shader) {
    uint64_t t = perf_now_us();
    glCompileShader(shader);
    GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    perf_record(PERF_COMPILE, NULL, t, 0, ok != 0, 0);
}
static void trace_link(GLuint program) {
    uint64_t t = perf_now_us();
    glLinkProgram(program);
    GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
    perf_record(PERF_LINK, NULL, t, 0, ok != 0, 0);
}
static size_t texture_bytes(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels) {
    if (!pixels || width <= 0 || height <= 0) return 0;
    unsigned pixel_size;
    if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1)
        pixel_size = 2;
    else if (type == GL_UNSIGNED_BYTE) {
        if (format == GL_RGBA || format == GL_BGRA) pixel_size = 4;
        else if (format == GL_RGB || format == GL_BGR) pixel_size = 3;
        else if (format == GL_LUMINANCE_ALPHA) pixel_size = 2;
        else if (format == GL_LUMINANCE || format == GL_ALPHA) pixel_size = 1;
        else return 0;
    }
    else return 0; // Unknown format: report call/time without inventing byte counts.
    return (size_t)width * height * pixel_size;
}
static void trace_tex_image(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                             GLint border, GLenum format, GLenum type, const void *pixels) {
    uint64_t t = perf_now_us();
    glTexImage2D(target, level, internal, w, h, border, format, type, pixels);
    perf_record(PERF_UPLOAD, NULL, t, texture_bytes(w, h, format, type, pixels), 1, 0);
}
static void trace_tex_sub(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                           GLenum format, GLenum type, const void *pixels) {
    uint64_t t = perf_now_us();
    glTexSubImage2D(target, level, x, y, w, h, format, type, pixels);
    perf_record(PERF_UPLOAD, NULL, t, texture_bytes(w, h, format, type, pixels), 1, 0);
}
static void trace_compressed_image(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h,
                                    GLint border, GLsizei bytes, const void *pixels) {
    uint64_t t = perf_now_us();
    glCompressedTexImage2D(target, level, internal, w, h, border, bytes, pixels);
    perf_record(PERF_UPLOAD, NULL, t, bytes > 0 ? bytes : 0, 1, 0);
}
static void trace_compressed_sub(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                  GLenum format, GLsizei bytes, const void *pixels) {
    uint64_t t = perf_now_us();
    glCompressedTexSubImage2D(target, level, x, y, w, h, format, bytes, pixels);
    perf_record(PERF_UPLOAD, NULL, t, bytes > 0 ? bytes : 0, 1, 0);
}
#endif

int soloader_patch_diagnostics(so_module *mod) {
#if PERF_DIAGNOSTICS
    const struct { const char *name; void *fn; } hooks[] = {
        {"fread", trace_fread}, {"read", trace_read}, {"fseek", trace_fseek},
        {"fseeko", trace_fseeko}, {"lseek", trace_lseek},
        {"glCompileShader", trace_compile}, {"glLinkProgram", trace_link},
        {"glTexImage2D", trace_tex_image}, {"glTexSubImage2D", trace_tex_sub},
        {"glCompressedTexImage2D", trace_compressed_image}, {"glCompressedTexSubImage2D", trace_compressed_sub},
    };
    unsigned installed = 0;
    for (unsigned i = 0; i < sizeof(hooks) / sizeof(hooks[0]); ++i) {
        if (!so_module_find_import(mod, hooks[i].name)) continue;
        if (so_module_hook_import(mod, hooks[i].name, hooks[i].fn, NULL) < 0) return -1;
        ++installed;
    }
    so_loga("[PERF] import_hooks=%u (counts cover imported GL entry points; upload bytes describe submissions)", installed);
#endif
    return 0;
}
