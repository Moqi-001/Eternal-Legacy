/*
 * com/gameloft/android/TBFV/GloftELHP/ML/GLUtils/Device
 *
 * Java 侧这些方法原本从 TelephonyManager / Build / WebView 取设备信息，
 * 结果被引擎拿去拼统计和 DRM 串。Vita 上没有电话和 WebView，这里给固定值：
 * 只要格式合法（比如 User-Agent 形状、运营商字符串可为空），引擎就能继续。
 */
#include <string.h>

#include <so_common/so_logger.h>
#include <so_java/java/lang/Array.h>
#include <so_java/java/lang/Class.h>
#include <so_java/java/lang/Object.h>
#include <so_java/java/lang/String.h>

#include "Device.h"

#define DEVICE_VERSION    "1.0.1"
#define DEVICE_MODEL      "Vita"
#define DEVICE_USER_AGENT "Mozilla/5.0 (Linux; Android 4.4.2; Vita) AppleWebKit/537.36"

static JClass sClass      = NULL;
static JInt   sUniqueCode = 1234;

static void (*native_Init)(JNIEnv *env, jclass clazz);

const char *Device_getDeviceId(void) {
    return "0123456789abcdef";
}

// 引擎把字符串当字节串用（Java 侧 a()/b()/c() 都是 getBytes()）
static JObject Device_bytesFromString(const char *text) {
    JInt len = (JInt)strlen(text);

    JObject array = Array_new(JTYPE_BYTE, len);
    if (!array) {
        return NULL;
    }

    Array_setRegion(array, 0, len, text);
    return array;
}

static JString Device_getUserAgent(void) {
    return String_newUTF(DEVICE_USER_AGENT);
}

static JString Device_getHostName(void) {
    return String_newUTF(DEVICE_MODEL);
}

static JObject Device_getHostNameBytes(void) {
    return Device_bytesFromString(DEVICE_MODEL);
}

static JString Device_getIMEI(void) {
    return String_newUTF(Device_getDeviceId());
}

static JString Device_getLineNumber(void) {
    return String_newUTF("00");
}

static JString Device_getOperatorName(void) {
    return String_newUTF("");
}

static JString Device_getPhoneModel(void) {
    return String_newUTF(DEVICE_MODEL);
}

static JString Device_getDemoCode(void) {
    return String_newUTF("ELHP");
}

static JString Device_getGameVersion(void) {
    return String_newUTF(DEVICE_VERSION);
}

static JObject Device_getGameVersionBytes(void) {
    return Device_bytesFromString(DEVICE_VERSION);
}

static JObject Device_getDeviceIdBytes(void) {
    return Device_bytesFromString(Device_getDeviceId());
}

static JObject Device_getUserAgentBytes(void) {
    return Device_bytesFromString(DEVICE_USER_AGENT);
}

static JObject Device_getOperatorNameBytes(void) {
    return Device_bytesFromString("");
}

static JObject Device_getLineNumberBytes(void) {
    return Device_bytesFromString("00");
}

static JBoolean Device_IsWifiEnable(void) {
    // 引擎用它在"有 Wi-Fi"时走另一条设备 ID 分支，这里报无 Wi-Fi
    return JAVA_FALSE;
}

static JInt Device_getUniqueCode(void) {
    return sUniqueCode;
}

static void Device_setUniqueCode(JInt code) {
    sUniqueCode = code;
}

static void Device_nativeInit(void) {
    if (!native_Init) {
        return;
    }

    ENTER_JNI_CALL(0)
    native_Init(&_jni_env, JniNewLocalRefFromJava(sClass));
    EXIT_JNI_CALL(NULL)
}

void Device_init(void) {
    Device_nativeInit();
}

static JSymbol class_symbols[] = {
    JAVA_SYMBOL_STATIC_NATIVE("nativeInit", "()V", &native_Init),

    JAVA_SYMBOL_STATIC_METHOD("getUserAgent", "()Ljava/lang/String;", &Device_getUserAgent),
    JAVA_SYMBOL_STATIC_METHOD("getIMEI", "()Ljava/lang/String;", &Device_getIMEI),
    JAVA_SYMBOL_STATIC_METHOD("getLineNumber", "()Ljava/lang/String;", &Device_getLineNumber),
    JAVA_SYMBOL_STATIC_METHOD("getOperatorName", "()Ljava/lang/String;", &Device_getOperatorName),
    JAVA_SYMBOL_STATIC_METHOD("getPhoneModel", "()Ljava/lang/String;", &Device_getPhoneModel),
    JAVA_SYMBOL_STATIC_METHOD("getDemoCode", "()Ljava/lang/String;", &Device_getDemoCode),
    JAVA_SYMBOL_STATIC_METHOD("getGameVersion", "()Ljava/lang/String;", &Device_getGameVersion),
    JAVA_SYMBOL_STATIC_METHOD("getHostName", "()Ljava/lang/String;", &Device_getHostName),
    JAVA_SYMBOL_STATIC_METHOD("IsWifiEnable", "()Z", &Device_IsWifiEnable),
    JAVA_SYMBOL_STATIC_METHOD("getUniqueCode", "()I", &Device_getUniqueCode),
    JAVA_SYMBOL_STATIC_METHOD("setUniqueCode", "(I)V", &Device_setUniqueCode),

    // Java 侧按字节数组暴露的那几个
    JAVA_SYMBOL_STATIC_METHOD("a", "()[B", &Device_getDeviceIdBytes),
    JAVA_SYMBOL_STATIC_METHOD("b", "()[B", &Device_getOperatorNameBytes),
    JAVA_SYMBOL_STATIC_METHOD("c", "()[B", &Device_getLineNumberBytes),
    JAVA_SYMBOL_STATIC_METHOD("d", "()[B", &Device_getGameVersionBytes),
    JAVA_SYMBOL_STATIC_METHOD("f", "()[B", &Device_getUserAgentBytes),
    JAVA_SYMBOL_STATIC_METHOD("getHostNameBytes", "()[B", &Device_getHostNameBytes),
};

static JClassMeta class_meta = {
    .name          = "com/gameloft/android/TBFV/GloftELHP/ML/GLUtils/Device",
    .super_name    = "java/lang/Object",
    .symbols       = class_symbols,
    .num_symbols   = sizeof(class_symbols) / sizeof(class_symbols[0]),
    .instance_size = sizeof(JInt),
    .dtor          = NULL,
};

void setup_Device(so_module *mod) {
    sClass = JavaCreateClass(&class_meta);
    if (!sClass) {
        so_loge("[JAVA] Device: create class failed");
        return;
    }

    native_Init = so_module_find_export(mod, "Java_com_gameloft_android_TBFV_GloftELHP_ML_GLUtils_Device_nativeInit");
}
