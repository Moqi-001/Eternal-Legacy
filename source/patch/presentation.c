/* A virtual 960x640 Android surface on the physical 960x544 Vita framebuffer.
 * Only game imports are adapted. Host video/vitaGL calls stay in physical space.
 * GL viewport/scissor state is global, including across framebuffer bindings:
 * keep the ORIGINAL rectangles and re-apply them on binding changes. Never read
 * an already scaled rectangle back into the logical state. */
#include "presentation.h"
#include "config.h"
#include <limits.h>
#include <so_common/so_logger.h>
#include <stdint.h>
#include <string.h>
#include <vitaGL.h>

#define FIT_X 72
#define FIT_Y 0
#define FIT_W 816
#define FIT_H 544
_Static_assert(GAME_LOGICAL_W * 17 == FIT_W * 20 && GAME_LOGICAL_H * 17 == FIT_H * 20,
               "Presentation fit must match the logical canvas");
_Static_assert(FIT_W + FIT_X * 2 == SCREEN_W && FIT_H + FIT_Y * 2 == SCREEN_H,
               "Presentation fit must be centered on the physical framebuffer");

typedef struct {
    GLint x, y, w, h;
} Rect;
static Rect s_view = {0, 0, GAME_LOGICAL_W, GAME_LOGICAL_H};
static Rect s_scissor = {0, 0, GAME_LOGICAL_W, GAME_LOGICAL_H};
static GLuint s_framebuffer, s_default_framebuffer;
static GLint s_max_viewport[2];
static int s_ready;

static GLint narrow(int64_t x) {
    return x > INT_MAX ? INT_MAX : x < INT_MIN ? INT_MIN : (GLint)x;
}
/* Ties always go toward +infinity. Unlike rounding away from zero, endpoint
 * differences then stay bounded by ceil(length * scale) across the origin. */
static int64_t round_scale(int64_t x) {
    return x >= 0 ? (x * 17 + 10) / 20 : -((-x * 17 + 9) / 20);
}
static int64_t floor_scale(int64_t x) {
    return x >= 0 ? x * 17 / 20 : -((-x * 17 + 19) / 20);
}
static int64_t ceil_scale(int64_t x) {
    return -floor_scale(-x);
}
static int is_default(void) {
    return s_framebuffer == s_default_framebuffer;
}
static Rect mapped(Rect r, int scissor) {
    if (!is_default())
        return r;
    int64_t x1 = (scissor ? floor_scale(r.x) : round_scale(r.x)) + FIT_X;
    int64_t y1 = (scissor ? floor_scale(r.y) : round_scale(r.y)) + FIT_Y;
    int64_t x2 =
        (scissor ? ceil_scale((int64_t)r.x + r.w) : round_scale((int64_t)r.x + r.w)) + FIT_X;
    int64_t y2 =
        (scissor ? ceil_scale((int64_t)r.y + r.h) : round_scale((int64_t)r.y + r.h)) + FIT_Y;
    if (scissor) {
        // An empty logical scissor must remain empty after outward rounding.
        if (!r.w)
            x2 = x1;
        if (!r.h)
            y2 = y1;
        if (x1 < FIT_X)
            x1 = FIT_X;
        if (y1 < FIT_Y)
            y1 = FIT_Y;
        if (x1 > FIT_X + FIT_W)
            x1 = FIT_X + FIT_W;
        if (y1 > FIT_Y + FIT_H)
            y1 = FIT_Y + FIT_H;
        if (x2 < FIT_X)
            x2 = FIT_X;
        if (y2 < FIT_Y)
            y2 = FIT_Y;
        if (x2 > FIT_X + FIT_W)
            x2 = FIT_X + FIT_W;
        if (y2 > FIT_Y + FIT_H)
            y2 = FIT_Y + FIT_H;
    }
    return (Rect){narrow(x1), narrow(y1), narrow(x2 - x1), narrow(y2 - y1)};
}
static void apply_rects(void) {
    Rect v = mapped(s_view, 0), s = mapped(s_scissor, 1);
    if (s_ready) {
        if (v.w > s_max_viewport[0])
            v.w = s_max_viewport[0];
        if (v.h > s_max_viewport[1])
            v.h = s_max_viewport[1];
    }
    glViewport(v.x, v.y, v.w, v.h);
    glScissor(s.x, s.y, s.w, s.h);
}
static void game_viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    if (w < 0 || h < 0) {
        glViewport(x, y, w, h);
        return;
    }
    /* Enforce the queried limits before saving the accepted logical state.
     * A default-target cap is virtual; RTT caps remain backend pixels. */
    if (s_ready) {
        GLint mw = s_max_viewport[0], mh = s_max_viewport[1];
        if (is_default()) {
            mw = narrow((int64_t)mw * 20 / 17);
            mh = narrow((int64_t)mh * 20 / 17);
        }
        if (w > mw)
            w = mw;
        if (h > mh)
            h = mh;
    }
    s_view = (Rect){x, y, w, h};
    Rect v = mapped(s_view, 0);
    glViewport(v.x, v.y, v.w, v.h);
}
static void game_scissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    if (w < 0 || h < 0) {
        glScissor(x, y, w, h);
        return;
    }
    s_scissor = (Rect){x, y, w, h};
    Rect s = mapped(s_scissor, 1);
    glScissor(s.x, s.y, s.w, s.h);
}
static void game_bind_framebuffer(GLenum target, GLuint fb) {
    glBindFramebuffer(target, fb);
    if (target != GL_FRAMEBUFFER)
        return;
    GLint actual;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &actual);
    if ((GLuint)actual != fb)
        return; /* Invalid binds must not change our shadow. */
    s_framebuffer = fb;
    apply_rects(); /* Also undo mapping when changing from default to an RTT. */
}
static void game_get_integer(GLenum pname, GLint *v) {
    if (!v) {
        glGetIntegerv(pname, v);
        return;
    }
    if (is_default() && pname == GL_VIEWPORT) {
        memcpy(v, &s_view, sizeof(s_view));
        return;
    }
    if (is_default() && pname == GL_SCISSOR_BOX) {
        memcpy(v, &s_scissor, sizeof(s_scissor));
        return;
    }
    glGetIntegerv(pname, v);
    if (pname == GL_MAX_VIEWPORT_DIMS && is_default()) {
        v[0] = narrow((int64_t)v[0] * 20 / 17);
        v[1] = narrow((int64_t)v[1] * 20 / 17);
    }
}
static void game_get_float(GLenum pname, GLfloat *v) {
    if (v && (pname == GL_VIEWPORT || pname == GL_SCISSOR_BOX || pname == GL_MAX_VIEWPORT_DIMS)) {
        GLint a[4];
        game_get_integer(pname, a);
        int n = pname == GL_MAX_VIEWPORT_DIMS ? 2 : 4;
        for (int i = 0; i < n; i++)
            v[i] = (GLfloat)a[i];
        return;
    }
    glGetFloatv(pname, v);
}
static void game_delete_framebuffers(GLsizei n, const GLuint *ids) {
    glDeleteFramebuffers(n, ids);
    if (n <= 0 || !ids)
        return;
    for (GLsizei i = 0; i < n; i++) {
        if (ids[i] && ids[i] == s_framebuffer) {
            s_framebuffer = s_default_framebuffer;
            apply_rects();
        }
    }
}
int presentation_gl_init(void) {
    if (s_ready)
        return 0;
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, s_max_viewport);
    if (s_max_viewport[0] < FIT_W || s_max_viewport[1] < FIT_H) {
        so_loga("[PRESENT] backend viewport limit %dx%d cannot hold %dx%d", s_max_viewport[0],
                s_max_viewport[1], FIT_W, FIT_H);
        return -1;
    }
    GLint fb;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    s_default_framebuffer = s_framebuffer = (GLuint)fb;
    s_ready = 1;
    apply_rects();
    so_loga("[PRESENT] canvas=%dx%d physical=%dx%d content=%d,%d,%d,%d scale=17/20 default_fbo=%u "
            "max_viewport=%dx%d",
            GAME_LOGICAL_W, GAME_LOGICAL_H, SCREEN_W, SCREEN_H, FIT_X, FIT_Y, FIT_W, FIT_H,
            s_default_framebuffer, s_max_viewport[0], s_max_viewport[1]);
    return 0;
}
void presentation_frame_begin(void) {
    if (!s_ready && presentation_gl_init() < 0)
        return;
    GLint fb;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    s_framebuffer = (GLuint)fb;
    apply_rects();
}
void presentation_frame_end(void) {
    if (!s_ready)
        return;
    /* glClear ignores viewport. Clear only side bars, preserving every touched
     * GL state; direct calls bypass game hooks and never alter logical shadows. */
    GLint fb, vp[4], sc[4];
    GLfloat color[4];
    GLboolean mask[4], enabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_SCISSOR_BOX, sc);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, color);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    glBindFramebuffer(GL_FRAMEBUFFER, s_default_framebuffer);
    glEnable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glScissor(0, 0, FIT_X, SCREEN_H);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(FIT_X + FIT_W, 0, SCREEN_W - FIT_X - FIT_W, SCREEN_H);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glScissor(sc[0], sc[1], sc[2], sc[3]);
    glClearColor(color[0], color[1], color[2], color[3]);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (!enabled)
        glDisable(GL_SCISSOR_TEST);
}
int presentation_map_touch(int x, int y, int *lx, int *ly) {
    if (x < FIT_X || x >= FIT_X + FIT_W || y < FIT_Y || y >= FIT_Y + FIT_H)
        return 0;
    *lx = (x - FIT_X) * 20 / 17;
    *ly = (y - FIT_Y) * 20 / 17;
    return 1;
}
int soloader_patch_presentation(so_module *mod) {
    const struct {
        const char *name;
        void *fn;
    } hooks[] = {
        {"glViewport", game_viewport},
        {"glScissor", game_scissor},
        {"glBindFramebuffer", game_bind_framebuffer},
        {"glGetIntegerv", game_get_integer},
        {"glGetFloatv", game_get_float},
        {"glDeleteFramebuffers", game_delete_framebuffers},
    };
    for (unsigned i = 0; i < sizeof(hooks) / sizeof(hooks[0]); i++)
        if (so_module_find_import(mod, hooks[i].name) &&
            so_module_hook_import(mod, hooks[i].name, hooks[i].fn, NULL) < 0)
            return -1;
    return 0;
}
