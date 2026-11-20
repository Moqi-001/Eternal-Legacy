/*
 * android/app/Activity：引擎从 EternalLegacy.da() 拿到它，再调 getSystemService。
 *
 * 方法都继承自 Context（符号查找会沿父类链走），这里只占一个类名。
 */
#include <stddef.h>

#include <so_java/android/content/Context.h>
#include <so_java/java/lang/Class.h>

#include "Activity.h"

struct _Activity {
    JInt placeholder;
};

static JObject Activity_getSystemService(Activity self, JString name) {
    return Context_getSystemService((Context)self, name);
}

static JSymbol class_symbols[] = {
    JAVA_SYMBOL_METHOD("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", &Activity_getSystemService),
};

static JClassMeta class_meta = {
    .name          = "android/app/Activity",
    .super_name    = "android/content/Context",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = sizeof(struct _Activity),
    .dtor          = NULL,
};

void setup_android_app_Activity(void) {
    JavaCreateClass(&class_meta);
}
