#ifndef ANDROID_TELEPHONY_TELEPHONYMANAGER_H
#define ANDROID_TELEPHONY_TELEPHONYMANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <so_java/java/lang/types.h>

typedef struct _TelephonyManager *TelephonyManager;

void setup_android_telephony_TelephonyManager(void);

// 单例：Context.getSystemService("phone") 返回它
JObject TelephonyManager_instance(void);

#ifdef __cplusplus
}
#endif

#endif
