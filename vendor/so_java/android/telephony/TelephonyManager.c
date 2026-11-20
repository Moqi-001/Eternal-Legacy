/*
 * android/telephony/TelephonyManager：游戏拿它算设备标识和运营商信息。
 *
 * Vita 没有电话模块，这里给一组固定但格式合法的值：设备 ID 是 16 位十六进制
 * （安卓 ANDROID_ID 的形状，游戏会把它当字符串用），运营商信息取空串。
 */
#include <stddef.h>

#include <so_common/so_logger.h>
#include <so_java/java/lang/Class.h>
#include <so_java/java/lang/Object.h>
#include <so_java/java/lang/String.h>

#include "TelephonyManager.h"

struct _TelephonyManager {
    JInt placeholder;
};

static JClass  sClass    = NULL;
static JObject sInstance = NULL;

#define TELEPHONY_DEVICE_ID "0123456789abcdef"

static JString TelephonyManager_getDeviceId(TelephonyManager self) {
    (void)self;
    return String_newUTF(TELEPHONY_DEVICE_ID);
}

static JString TelephonyManager_getEmpty(TelephonyManager self) {
    (void)self;
    return String_newUTF("");
}

static JBoolean TelephonyManager_isNetworkRoaming(TelephonyManager self) {
    (void)self;
    return JAVA_FALSE;
}

static JSymbol class_symbols[] = {
    JAVA_SYMBOL_METHOD("getDeviceId", "()Ljava/lang/String;", &TelephonyManager_getDeviceId),
    JAVA_SYMBOL_METHOD("getImei", "()Ljava/lang/String;", &TelephonyManager_getDeviceId),
    JAVA_SYMBOL_METHOD("getMeid", "()Ljava/lang/String;", &TelephonyManager_getDeviceId),
    JAVA_SYMBOL_METHOD("getSubscriberId", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("getLine1Number", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("getNetworkOperator", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("getNetworkOperatorName", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("getSimOperator", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("getSimOperatorName", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("getNetworkCountryIso", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("getSimCountryIso", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("getVoiceMailNumber", "()Ljava/lang/String;", &TelephonyManager_getEmpty),
    JAVA_SYMBOL_METHOD("isNetworkRoaming", "()Z", &TelephonyManager_isNetworkRoaming),
};

static JClassMeta class_meta = {
    .name          = "android/telephony/TelephonyManager",
    .super_name    = "java/lang/Object",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = sizeof(struct _TelephonyManager),
    .dtor          = NULL,
};

JObject TelephonyManager_instance(void) {
    return sInstance;
}

void setup_android_telephony_TelephonyManager(void) {
    sClass = JavaCreateClass(&class_meta);
    if (sClass) {
        sInstance = Object_new(sClass);
    }
}
