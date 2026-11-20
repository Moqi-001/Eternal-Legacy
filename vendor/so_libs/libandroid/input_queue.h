#ifndef _ANDROID_INPUT_QUEUE_H
#define _ANDROID_INPUT_QUEUE_H

#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==== 移植层扩展：NativeActivity host 使用的队列创建/投递接口 ====
AInputQueue *AInputQueue_create();
// 销毁前须停止投递并让引擎脱离队列。
void         AInputQueue_destroy(AInputQueue *queue);
// 投递后由队列释放事件，包括投递失败的情况。
void         AInputQueue_push(AInputQueue *queue, AInputEvent *event);

// ===================================================

#ifdef __cplusplus
}
#endif

#endif
