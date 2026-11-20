/*
 * 输入：实体按键/摇杆 → 合成触摸，触摸屏 → 直接透传
 *
 * 这个游戏的安卓端不做实体按键处理：nativeKeyDown 一路走到 appKeyPressed，
 * 里面只有一条日志（KeyPressed in App.cpp）。真正的操作都在触摸界面上——
 * 左下虚拟摇杆、右下动作键、右上暂停键。所以这里照 bio4 的做法，把按键换算成
 * 对应屏幕坐标的触摸事件（onTouch 的 type：0 抬起 / 1 按下 / 2 移动）。
 *
 * 坐标以 960x544 的绘制区为基准，改键位只需要动下面几张表。
 */
#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include <so_common/so_logger.h>
#include <so_java/android/keycodes.h>

#include "controls.h"
#include "java/EternalLegacy.h"
#include "java/GameGLSurfaceView.h"

// 触摸点编号：0 留给真实触摸屏，虚拟摇杆和虚拟按键各占一个
#define TOUCH_ID_SCREEN 0
#define TOUCH_ID_STICK  1

// 左下虚拟摇杆（中心与最大偏移，单位是绘制区像素）
#define VSTICK_CENTER_X 142
#define VSTICK_CENTER_Y 425
#define VSTICK_RADIUS   55

// 摇杆死区（SCE_CTRL_MODE_ANALOG_WIDE 下 0..255，中心 128）
#define VSTICK_DEADZONE 32

typedef struct {
    unsigned int button;
    int          x;
    int          y;
    int          touch_id;
    int          down;
} ButtonTouch;

// 屏幕按钮坐标：右上暂停、右下交互/动作
static ButtonTouch s_buttons[] = {
    {SCE_CTRL_START, 925, 40,  2, 0},
    {SCE_CTRL_CROSS, 855, 455, 3, 0},
};

// Java 侧只对返回/菜单键查状态，其余键码引擎不处理（只打日志）
typedef struct {
    unsigned int sce_button;
    int          key_code;
} ButtonMap;

static const ButtonMap s_key_map[] = {
    {SCE_CTRL_CIRCLE, AKEYCODE_BACK         },
    {SCE_CTRL_START,  AKEYCODE_MENU         },
    {SCE_CTRL_SELECT, AKEYCODE_BUTTON_SELECT},
};

static unsigned int s_prev_buttons = 0;

static int s_touch_active = 0;
static int s_touch_x      = 0;
static int s_touch_y      = 0;

static int s_stick_active = 0;
static int s_stick_x      = 0;
static int s_stick_y      = 0;

static void send_touch(int type, int x, int y, int touch_id) {
    GameGLSurfaceView_onTouch(type, x, y, touch_id);
}

// 真实触摸屏：前面板坐标是 1920x1088，缩到绘制区
static void controls_poll_touchscreen(void) {
    SceTouchData touch;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) < 0) {
        return;
    }

    if (touch.reportNum > 0) {
        int x = touch.report[0].x / 2;
        int y = touch.report[0].y / 2;

        if (!s_touch_active) {
            send_touch(1, x, y, TOUCH_ID_SCREEN);
            s_touch_active = 1;
        } else if (x != s_touch_x || y != s_touch_y) {
            send_touch(2, x, y, TOUCH_ID_SCREEN);
        }

        s_touch_x = x;
        s_touch_y = y;
    } else if (s_touch_active) {
        send_touch(0, s_touch_x, s_touch_y, TOUCH_ID_SCREEN);
        s_touch_active = 0;
    }
}

/*
 * 虚拟摇杆：把摇杆方向和大小换算成以屏幕摇杆中心为原点的触摸点。
 * 十字键当成满偏移方向用，没有摇杆输入时才生效。
 */
static void controls_poll_stick(const SceCtrlData *pad) {
    // Vita 摇杆 0 = 上/左，255 = 下/右；屏幕坐标 y 向下增大，正好同向
    int dx = pad->lx - 128;
    int dy = pad->ly - 128;

    if (dx > -VSTICK_DEADZONE && dx < VSTICK_DEADZONE) {
        dx = 0;
    }
    if (dy > -VSTICK_DEADZONE && dy < VSTICK_DEADZONE) {
        dy = 0;
    }

    if (!dx && !dy) {
        if (pad->buttons & SCE_CTRL_UP) {
            dy = -128;
        } else if (pad->buttons & SCE_CTRL_DOWN) {
            dy = 128;
        }
        if (pad->buttons & SCE_CTRL_LEFT) {
            dx = -128;
        } else if (pad->buttons & SCE_CTRL_RIGHT) {
            dx = 128;
        }
    }

    if (!dx && !dy) {
        if (s_stick_active) {
            send_touch(0, s_stick_x, s_stick_y, TOUCH_ID_STICK);
            s_stick_active = 0;
        }
        return;
    }

    // 限制在半径内：斜向和正向最大偏移一致
    int magnitude = dx * dx + dy * dy;
    if (magnitude > 128 * 128) {
        dx = dx * 128 * 128 / magnitude;
        dy = dy * 128 * 128 / magnitude;
    }

    int x = VSTICK_CENTER_X + dx * VSTICK_RADIUS / 128;
    int y = VSTICK_CENTER_Y + dy * VSTICK_RADIUS / 128;

    // 按住期间每帧都发移动：引擎按"手指当前压到这个位置"理解摇杆
    if (!s_stick_active) {
        so_logd("[CTRL] stick lx=%d ly=%d -> (%d,%d)", pad->lx, pad->ly, x, y);
    }

    send_touch(s_stick_active ? 2 : 1, x, y, TOUCH_ID_STICK);
    s_stick_active = 1;
    s_stick_x      = x;
    s_stick_y      = y;
}

// 屏幕按钮：按下/抬起各发一次触摸
static void controls_poll_screen_buttons(const SceCtrlData *pad) {
    unsigned int pressed  = pad->buttons & ~s_prev_buttons;
    unsigned int released = ~pad->buttons & s_prev_buttons;

    for (unsigned int i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        ButtonTouch *bt = &s_buttons[i];

        if (pressed & bt->button) {
            send_touch(1, bt->x, bt->y, bt->touch_id);
            bt->down = 1;
        }
        if ((released & bt->button) && bt->down) {
            send_touch(0, bt->x, bt->y, bt->touch_id);
            bt->down = 0;
        }
    }
}

static void controls_send_key_edges(unsigned int pressed, unsigned int released) {
    for (unsigned int i = 0; i < sizeof(s_key_map) / sizeof(s_key_map[0]); i++) {
        const ButtonMap *map = &s_key_map[i];

        if (pressed & map->sce_button) {
            so_logd("[CTRL] key down: %d", map->key_code);
            EternalLegacy_onKeyDown(map->key_code);
        }
        if (released & map->sce_button) {
            so_logd("[CTRL] key up: %d", map->key_code);
            EternalLegacy_onKeyUp(map->key_code);
        }
    }
}

int soloader_setup_controls(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    s_prev_buttons = 0;
    s_touch_active = 0;
    s_stick_active = 0;

    for (unsigned int i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); i++) {
        s_buttons[i].down = 0;
    }

    return 0;
}

void soloader_poll_controls(void) {
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) {
        return;
    }

    unsigned int pressed  = pad.buttons & ~s_prev_buttons;
    unsigned int released = ~pad.buttons & s_prev_buttons;

    controls_poll_touchscreen();
    controls_poll_stick(&pad);
    controls_poll_screen_buttons(&pad);
    controls_send_key_edges(pressed, released);

    s_prev_buttons = pad.buttons;
}
