#ifndef ANDROID_CONTEXT_CONTEXT_H
#define ANDROID_CONTEXT_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <so_java/java/lang/types.h>

typedef struct _Context *Context;

void setup_android_content_Context(void);

// 引擎初始化时按名称取服务（"phone" 等），拿不到实例会直接影响设备信息
JObject Context_getSystemService(Context context, JString name);

// 供游戏类以自己为 Context（Activity 继承 Context）
JObject Context_retainInstance(JObject context);

#ifdef __cplusplus
}
#endif

#endif
