/*
 * android/content/Context：只做引擎真正会走到的那部分。
 *
 * 引擎（ADevice::Init）拿 Activity 实例调 getSystemService(TELEPHONY_SERVICE)
 * 再取 TelephonyManager 上的设备信息，所以这里要给出真实可用对象，而不是 NULL。
 */
#include <string.h>

#include <so_common/so_logger.h>
#include <so_java/android/telephony/TelephonyManager.h>
#include <so_java/java/lang/Class.h>
#include <so_java/java/lang/Object.h>
#include <so_java/java/lang/String.h>

#include "Context.h"

struct _Context {
    JInt placeholder;
};

static JClass sClass = NULL;

// Context 的服务名常量（Java 侧是 static final String）
static JObject s_TELEPHONY_SERVICE    = NULL;
static JObject s_WIFI_SERVICE         = NULL;
static JObject s_CONNECTIVITY_SERVICE = NULL;
static JObject s_ACTIVITY_SERVICE     = NULL;
static JObject s_SENSOR_SERVICE       = NULL;

static JObject Context_newObject(void) {
    return Object_newWithoutClass(sizeof(JInt), NULL);
}

JObject Context_getSystemService(Context context, JString name) {
    if (!name) {
        return NULL;
    }

    const char *service = String_getUTFChars(name);
    if (!service) {
        return NULL;
    }

    if (strcmp(service, "phone") == 0) {
        return Object_retain(TelephonyManager_instance());
    }

    // 其余服务引擎目前只取对象本身，给一个空对象让它继续跑
    so_logd("[JAVA] Context.getSystemService(%s)", service);
    return Context_newObject();
}

JObject Context_retainInstance(JObject context) {
    return Object_retain(context);
}

static JSymbol class_symbols[] = {
    JAVA_SYMBOL_METHOD("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", &Context_getSystemService),

    JAVA_SYMBOL_STATIC_FIELD("TELEPHONY_SERVICE", "Ljava/lang/String;", &s_TELEPHONY_SERVICE),
    JAVA_SYMBOL_STATIC_FIELD("WIFI_SERVICE", "Ljava/lang/String;", &s_WIFI_SERVICE),
    JAVA_SYMBOL_STATIC_FIELD("CONNECTIVITY_SERVICE", "Ljava/lang/String;", &s_CONNECTIVITY_SERVICE),
    JAVA_SYMBOL_STATIC_FIELD("ACTIVITY_SERVICE", "Ljava/lang/String;", &s_ACTIVITY_SERVICE),
    JAVA_SYMBOL_STATIC_FIELD("SENSOR_SERVICE", "Ljava/lang/String;", &s_SENSOR_SERVICE),
};

static JClassMeta class_meta = {
    .name          = "android/content/Context",
    .super_name    = "java/lang/Object",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = sizeof(struct _Context),
    .dtor          = NULL,
};

void setup_android_content_Context(void) {
    sClass = JavaCreateClass(&class_meta);

    s_TELEPHONY_SERVICE    = String_newUTF("phone");
    s_WIFI_SERVICE         = String_newUTF("wifi");
    s_CONNECTIVITY_SERVICE = String_newUTF("connectivity");
    s_ACTIVITY_SERVICE     = String_newUTF("activity");
    s_SENSOR_SERVICE       = String_newUTF("sensor");
}
