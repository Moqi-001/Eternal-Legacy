/*
 * GL_OES_texture_3D 的补充实现。
 *
 * vitaGL 只支持 2D 纹理，没有 3D 纹理对象；这几个入口先以空实现放行符号解析，
 * 被调用时打一条警告（WARN 级别，release 构建不输出）。若游戏画面依赖 3D 纹理，
 * 再回来看这里是哪几个调用、能否用 2D 分层/图集体素近似。
 *
 * GL_OES_mapbuffer 不需要实现：直接复用 vitaGL 的 glMapBuffer/glUnmapBuffer。
 */
#include <so_common/so_logger.h>
#include <vitaGL/GLES2/gl2.h>

void glTexImage3DOES_soloader(GLenum target, GLint level, GLenum internalformat,
                              GLsizei width, GLsizei height, GLsizei depth, GLint border,
                              GLenum format, GLenum type, const void *pixels) {
    (void)internalformat;
    (void)border;
    (void)format;
    (void)type;
    (void)pixels;
    so_logw("[GL] glTexImage3DOES(target=0x%x, %dx%dx%d) ignored: no 3D texture support",
            target, width, height, depth);
    (void)level;
}

void glTexSubImage3DOES_soloader(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                 GLint zoffset, GLsizei width, GLsizei height, GLsizei depth,
                                 GLenum format, GLenum type, const void *pixels) {
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)zoffset;
    (void)format;
    (void)type;
    (void)pixels;
    so_logw("[GL] glTexSubImage3DOES(target=0x%x, %dx%dx%d) ignored: no 3D texture support",
            target, width, height, depth);
}

void glCompressedTexImage3DOES_soloader(GLenum target, GLint level, GLenum internalformat,
                                        GLsizei width, GLsizei height, GLsizei depth,
                                        GLint border, GLsizei imageSize, const void *data) {
    (void)level;
    (void)internalformat;
    (void)border;
    (void)imageSize;
    (void)data;
    so_logw("[GL] glCompressedTexImage3DOES(target=0x%x, %dx%dx%d) ignored: no 3D texture support",
            target, width, height, depth);
}

void glCompressedTexSubImage3DOES_soloader(GLenum target, GLint level, GLint xoffset,
                                           GLint yoffset, GLint zoffset, GLsizei width,
                                           GLsizei height, GLsizei depth, GLenum format,
                                           GLsizei imageSize, const void *data) {
    (void)level;
    (void)xoffset;
    (void)yoffset;
    (void)zoffset;
    (void)format;
    (void)imageSize;
    (void)data;
    so_logw("[GL] glCompressedTexSubImage3DOES(target=0x%x, %dx%dx%d) ignored: no 3D texture support",
            target, width, height, depth);
}
