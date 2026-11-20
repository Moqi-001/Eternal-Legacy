/*
 * 输入：Vita 手柄/触摸屏 → Android 的 KeyEvent 与 MotionEvent
 *
 * Java 侧 EternalLegacy.onKeyDown/onKeyUp 与 GameGLSurfaceView.onTouchEvent
 * 只是把参数原样交给引擎，所以这里要发的是 Android 的键码和触摸动作
 * （0 抬起 / 1 按下 / 2 移动），不是 SCE_CTRL_*。标题画面就是等一次触摸。
 */
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <so_common/so_logger.h>
#include <so_java/android/keycodes.h>

#include "controls.h"

#include "java/EternalLegacy.h"
#include "java/GameGLSurfaceView.h"

typedef struct {
    unsigned int sce_button;
    int          key_code;
} ButtonMap;

static const ButtonMap button_map[] = {
    {SCE_CTRL_UP,       AKEYCODE_DPAD_UP      },
    {SCE_CTRL_DOWN,     AKEYCODE_DPAD_DOWN    },
    {SCE_CTRL_LEFT,     AKEYCODE_DPAD_LEFT    },
    {SCE_CTRL_RIGHT,    AKEYCODE_DPAD_RIGHT   },
    {SCE_CTRL_CROSS,    AKEYCODE_DPAD_CENTER  },
    {SCE_CTRL_CIRCLE,   AKEYCODE_BACK         },
    {SCE_CTRL_SQUARE,   AKEYCODE_BUTTON_X     },
    {SCE_CTRL_TRIANGLE, AKEYCODE_BUTTON_Y     },
    {SCE_CTRL_START,    AKEYCODE_MENU         },
    {SCE_CTRL_SELECT,   AKEYCODE_BUTTON_SELECT},
    {SCE_CTRL_LTRIGGER, AKEYCODE_BUTTON_L1    },
    {SCE_CTRL_RTRIGGER, AKEYCODE_BUTTON_R1    },
};

static unsigned int s_prev_buttons = 0;
static int          s_touch_active = 0;
static int          s_touch_x      = 0;
static int          s_touch_y      = 0;

static void controls_poll_buttons(void) {
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) {
        return;
    }

    unsigned int buttons  = pad.buttons;
    unsigned int pressed  = buttons & ~s_prev_buttons;
    unsigned int released = ~buttons & s_prev_buttons;
    s_prev_buttons        = buttons;

    if (!pressed && !released) {
        return;
    }

    for (unsigned int i = 0; i < sizeof(button_map) / sizeof(button_map[0]); i++) {
        const ButtonMap *map = &button_map[i];

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

// Vita 触摸面板报 1920x1088，引擎要的是绘制区坐标（960x544）
static void controls_poll_touch(void) {
    SceTouchData touch;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) < 0) {
        return;
    }

    if (touch.reportNum > 0) {
        int x = touch.report[0].x / 2;
        int y = touch.report[0].y / 2;

        if (!s_touch_active) {
            GameGLSurfaceView_onTouch(1, x, y, 0);
            s_touch_active = 1;
        } else if (x != s_touch_x || y != s_touch_y) {
            GameGLSurfaceView_onTouch(2, x, y, 0);
        }

        s_touch_x = x;
        s_touch_y = y;
    } else if (s_touch_active) {
        GameGLSurfaceView_onTouch(0, s_touch_x, s_touch_y, 0);
        s_touch_active = 0;
    }
}

int controls_create(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    s_prev_buttons = 0;
    s_touch_active = 0;
    return 0;
}

void controls_poll(void) {
    controls_poll_buttons();
    controls_poll_touch();
}
