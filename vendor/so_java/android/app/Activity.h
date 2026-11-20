#ifndef ANDROID_APP_ACTIVITY_H
#define ANDROID_APP_ACTIVITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <so_java/java/lang/types.h>

typedef struct _Activity *Activity;

void setup_android_app_Activity(void);

#ifdef __cplusplus
}
#endif

#endif
