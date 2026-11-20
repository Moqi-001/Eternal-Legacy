#include <so_common/so_logger.h>

#include "input.h"
#include "input_queue.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

// 轴数组容量：覆盖到 AMOTION_EVENT_AXIS_GAS/BRAKE(22/23)，更大的轴按需扩
#define AXIS_CAPACITY 32

typedef struct InputEvent {
    int32_t type;
    int32_t deviceId;
    int32_t source;
} InputEvent;

typedef struct KeyEvent {
    InputEvent event;
    int32_t    action;
    int32_t    flags;
    int32_t    keyCode;
    int32_t    scanCode;
    int32_t    metaState;
    int32_t    repeatCount;
    int64_t    downTime;
    int64_t    eventTime;
} KeyEvent;

#define MAX_POINTERS 8

typedef struct MotionEvent {
    InputEvent event;
    int32_t    action;
    int32_t    flags;
    int32_t    metaState;
    int32_t    edgeFlags;
    int64_t    downTime;
    int64_t    eventTime;
    float      xOffset;
    float      yOffset;
    float      xPrecision;
    float      yPrecision;
    size_t     pointerCount;
    int32_t   *pointerIds;
    float      pointerCoords[MAX_POINTERS][2];     // [i] = {x, y}
    float      axes[MAX_POINTERS][AXIS_CAPACITY];  // [指针][AMOTION_EVENT_AXIS_*]
} MotionEvent;

static int is_key_event(const AInputEvent *event) {
    return ((const InputEvent *)event)->type == AINPUT_EVENT_TYPE_KEY;
}

static int is_motion_event(const AInputEvent *event) {
    return ((const InputEvent *)event)->type == AINPUT_EVENT_TYPE_MOTION;
}

int32_t AInputEvent_getType(const AInputEvent *event) {
    return ((InputEvent *)event)->type;
}

int32_t AInputEvent_getDeviceId(const AInputEvent *event) {
    return ((InputEvent *)event)->deviceId;
}

int32_t AInputEvent_getSource(const AInputEvent *event) {
    return ((InputEvent *)event)->source;
}

int32_t AKeyEvent_getAction(const AInputEvent *key_event) {
    return ((KeyEvent *)key_event)->action;
}

int32_t AKeyEvent_getFlags(const AInputEvent *key_event) {
    return ((KeyEvent *)key_event)->flags;
}

int32_t AKeyEvent_getKeyCode(const AInputEvent *key_event) {
    return ((KeyEvent *)key_event)->keyCode;
}

int32_t AKeyEvent_getScanCode(const AInputEvent *key_event) {
    return ((KeyEvent *)key_event)->scanCode;
}

int32_t AKeyEvent_getMetaState(const AInputEvent *key_event) {
    return ((KeyEvent *)key_event)->metaState;
}

int32_t AKeyEvent_getRepeatCount(const AInputEvent *key_event) {
    return ((KeyEvent *)key_event)->repeatCount;
}

int64_t AKeyEvent_getDownTime(const AInputEvent *key_event) {
    return ((KeyEvent *)key_event)->downTime;
}

int64_t AKeyEvent_getEventTime(const AInputEvent *key_event) {
    return ((KeyEvent *)key_event)->eventTime;
}

int32_t AMotionEvent_getAction(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->action;
}

int32_t AMotionEvent_getFlags(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->flags;
}

int32_t AMotionEvent_getMetaState(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->metaState;
}

int32_t AMotionEvent_getEdgeFlags(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->edgeFlags;
}

int64_t AMotionEvent_getDownTime(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->downTime;
}

int64_t AMotionEvent_getEventTime(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->eventTime;
}

float AMotionEvent_getXOffset(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->xOffset;
}

float AMotionEvent_getYOffset(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->yOffset;
}

float AMotionEvent_getXPrecision(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->xPrecision;
}

float AMotionEvent_getYPrecision(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->yPrecision;
}

size_t AMotionEvent_getPointerCount(const AInputEvent *motion_event) {
    return ((MotionEvent *)motion_event)->pointerCount;
}

int32_t AMotionEvent_getPointerId(const AInputEvent *motion_event, size_t pointer_index) {
    const MotionEvent *event = (const MotionEvent *)motion_event;
    if (pointer_index >= event->pointerCount) {
        return 0;
    }
    return event->pointerIds[pointer_index];
}

float AMotionEvent_getRawX(const AInputEvent *motion_event, size_t pointer_index) {
    const MotionEvent *event = (const MotionEvent *)motion_event;
    if (pointer_index >= event->pointerCount) {
        return 0;
    }
    return event->pointerCoords[pointer_index][0];
}

float AMotionEvent_getRawY(const AInputEvent *motion_event, size_t pointer_index) {
    const MotionEvent *event = (const MotionEvent *)motion_event;
    if (pointer_index >= event->pointerCount) {
        return 0;
    }
    return event->pointerCoords[pointer_index][1];
}

float AMotionEvent_getX(const AInputEvent *motion_event, size_t pointer_index) {
    return AMotionEvent_getRawX(motion_event, pointer_index);
}

float AMotionEvent_getY(const AInputEvent *motion_event, size_t pointer_index) {
    return AMotionEvent_getRawY(motion_event, pointer_index);
}

float AMotionEvent_getAxisValue(const AInputEvent *motion_event, int32_t axis, size_t pointer_index) {
    const MotionEvent *event = (const MotionEvent *)motion_event;

    // X/Y 是主指针位置的别名，其余轴按指针各自存
    if (axis == AMOTION_EVENT_AXIS_X) {
        return AMotionEvent_getX(motion_event, pointer_index);
    }
    if (axis == AMOTION_EVENT_AXIS_Y) {
        return AMotionEvent_getY(motion_event, pointer_index);
    }
    if (pointer_index >= MAX_POINTERS || axis < 0 || axis >= AXIS_CAPACITY) {
        return 0;
    }
    return event->axes[pointer_index][axis];
}

void AInputEvent_setAxisValue(AInputEvent *event, int32_t axis, size_t pointer_index, float value) {
    if (!event || !is_motion_event(event) ||
        pointer_index >= MAX_POINTERS || axis < 0 || axis >= AXIS_CAPACITY) {
        return;
    }

    MotionEvent *motion               = (MotionEvent *)event;
    motion->axes[pointer_index][axis] = value;
    if (pointer_index + 1 > motion->pointerCount) {
        motion->pointerCount = pointer_index + 1;
    }
}

float AMotionEvent_getPressure(const AInputEvent *motion_event, size_t pointer_index) {
    return 0;
}

float AMotionEvent_getSize(const AInputEvent *motion_event, size_t pointer_index) {
    return 0;
}

float AMotionEvent_getTouchMajor(const AInputEvent *motion_event, size_t pointer_index) {
    return 0;
}

float AMotionEvent_getTouchMinor(const AInputEvent *motion_event, size_t pointer_index) {
    return 0;
}

float AMotionEvent_getToolMajor(const AInputEvent *motion_event, size_t pointer_index) {
    return 0;
}

float AMotionEvent_getToolMinor(const AInputEvent *motion_event, size_t pointer_index) {
    return 0;
}

float AMotionEvent_getOrientation(const AInputEvent *motion_event, size_t pointer_index) {
    return 0;
}

size_t AMotionEvent_getHistorySize(const AInputEvent *motion_event) {
    return 0;
}

int64_t AMotionEvent_getHistoricalEventTime(const AInputEvent *motion_event,
                                            size_t             history_index) {
    return 0;
}

float AMotionEvent_getHistoricalRawX(const AInputEvent *motion_event, size_t pointer_index,
                                     size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalRawY(const AInputEvent *motion_event, size_t pointer_index,
                                     size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalX(const AInputEvent *motion_event, size_t pointer_index,
                                  size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalY(const AInputEvent *motion_event, size_t pointer_index,
                                  size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalPressure(const AInputEvent *motion_event, size_t pointer_index,
                                         size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalSize(const AInputEvent *motion_event, size_t pointer_index,
                                     size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalTouchMajor(const AInputEvent *motion_event, size_t pointer_index,
                                           size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalTouchMinor(const AInputEvent *motion_event, size_t pointer_index,
                                           size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalToolMajor(const AInputEvent *motion_event, size_t pointer_index,
                                          size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalToolMinor(const AInputEvent *motion_event, size_t pointer_index,
                                          size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalOrientation(const AInputEvent *motion_event, size_t pointer_index,
                                            size_t history_index) {
    return 0;
}

float AMotionEvent_getHistoricalAxisValue(const AInputEvent *motion_event, int32_t axis,
                                          size_t pointer_index, size_t history_index) {
    return 0;
}

int32_t AMotionEvent_getButtonState(const AInputEvent *motion_event) {
    return 0;
}

int32_t AMotionEvent_getToolType(const AInputEvent *motion_event, size_t pointer_index) {
    return AMOTION_EVENT_TOOL_TYPE_UNKNOWN;
}

// ==== 事件对象创建/销毁（移植层扩展） ====

AInputEvent *AInputEvent_create(int32_t type, int32_t deviceId, int32_t source) {
    if (type != AINPUT_EVENT_TYPE_KEY && type != AINPUT_EVENT_TYPE_MOTION) {
        so_loge("AInputEvent_create: unknown event type %d", type);
        return NULL;
    }

    InputEvent *event = (InputEvent *)calloc(1, type == AINPUT_EVENT_TYPE_KEY
                                                    ? sizeof(KeyEvent)
                                                    : sizeof(MotionEvent));
    if (!event) {
        return NULL;
    }
    event->type     = type;
    event->deviceId = deviceId;
    event->source   = source;

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        // 指针槽位按最大手指数一次性分配，setPointer 直接写槽位
        MotionEvent *motion = (MotionEvent *)event;
        motion->pointerIds  = (int32_t *)calloc(MAX_POINTERS, sizeof(int32_t));
        if (!motion->pointerIds) {
            free(event);
            return NULL;
        }
    }
    return (AInputEvent *)event;
}

void AInputEvent_destroy(AInputEvent *event) {
    if (!event) {
        return;
    }
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        free(((MotionEvent *)event)->pointerIds);
    }
    free(event);
}

void AInputEvent_setAction(AInputEvent *event, int32_t action) {
    if (!event) {
        return;
    }

    if (is_key_event(event)) {
        ((KeyEvent *)event)->action = action;
    } else {
        ((MotionEvent *)event)->action = action;
    }
}

void AInputEvent_setMetaState(AInputEvent *event, int32_t metaState) {
    if (!event) {
        return;
    }

    if (is_key_event(event)) {
        ((KeyEvent *)event)->metaState = metaState;
    } else {
        ((MotionEvent *)event)->metaState = metaState;
    }
}

void AInputEvent_setFlags(AInputEvent *event, int32_t flags) {
    if (!event) {
        return;
    }

    if (is_key_event(event)) {
        ((KeyEvent *)event)->flags = flags;
    } else {
        ((MotionEvent *)event)->flags = flags;
    }
}

void AInputEvent_setKeyCode(AInputEvent *event, int32_t keyCode) {
    if (!event || !is_key_event(event)) {
        return;
    }
    ((KeyEvent *)event)->keyCode = keyCode;
}

void AInputEvent_setScanCode(AInputEvent *event, int32_t scanCode) {
    if (!event || !is_key_event(event)) {
        return;
    }
    ((KeyEvent *)event)->scanCode = scanCode;
}

void AInputEvent_setRepeatCount(AInputEvent *event, int32_t repeatCount) {
    if (!event || !is_key_event(event)) {
        return;
    }
    ((KeyEvent *)event)->repeatCount = repeatCount;
}

void AInputEvent_setTime(AInputEvent *event, int64_t downTime, int64_t eventTime) {
    if (!event) {
        return;
    }

    if (is_key_event(event)) {
        ((KeyEvent *)event)->downTime  = downTime;
        ((KeyEvent *)event)->eventTime = eventTime;
    } else {
        ((MotionEvent *)event)->downTime  = downTime;
        ((MotionEvent *)event)->eventTime = eventTime;
    }
}

void AInputEvent_setPointer(AInputEvent *event, size_t index, int32_t id, float x, float y) {
    if (!event || !is_motion_event(event) || index >= MAX_POINTERS) {
        return;
    }

    MotionEvent *motion             = (MotionEvent *)event;
    motion->pointerIds[index]       = id;
    motion->pointerCoords[index][0] = x;
    motion->pointerCoords[index][1] = y;
    if (index + 1 > motion->pointerCount) {
        motion->pointerCount = index + 1;
    }
}
