#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI.h>
#include "utils/logger.h"
#include "reimpl/io.h"
#include <dirent.h>
/*
 * JNI Methods
*/

// jlong GetCurrentTime(jmethodID id, va_list args) {
//     int time=current_timestamp_ms();
//     debugPrintf("GetCurrentTime(%d)",time);
//     return (int64_t) time;
// }

jlong GetCurrentTime(jmethodID id, va_list args) {
    return (int64_t) current_timestamp_ms();
}

jint Get_PhoneLanguage(jmethodID id, va_list args) {
    debugPrintf("Get_PhoneLanguage(1)");
    return 1;
}

jint isMediaPlaying(jmethodID id, va_list args) {
    debugPrintf("isMediaPlaying(1)");
    return 1;
}

jint detectPhoneLang(jmethodID id, va_list args) {
    debugPrintf("detectPhoneLang(1)");
    return 1;
}

jint Get_DeviceLockState(jmethodID id, va_list args) {
    debugPrintf("Get_DeviceLockState(12000000)");
    return 12000000;
}

jint Get_logoVideoState(jmethodID id, va_list args) {

    debugPrintf("Get_logoVideoState(1)");
    return 0;
}

jint Get_PhoneManufacturer(jmethodID id, va_list args) {

    debugPrintf("Get_PhoneManufacturer(10)");
    return 10;
}

jint loadMovie(jmethodID id, va_list args) {
    const char * value = va_arg(args, char*);
    debugPrintf("loadMovie(%s)",value);
    return 0;
}

void OpenGLive(jmethodID id, va_list args) {
    debugPrintf("OpenGLive()");
}

long get_file_size(char *filename){
    char fopen_path_real[1024];
    memset(fopen_path_real,0,1024);
    int count =strlen(filename);
    sceClibSnprintf(fopen_path_real, sizeof(fopen_path_real), "ux0:data/EternalLegacy/GloftELHP/%s", filename );
    FILE *fp = NULL;
    if((fp = fopen(fopen_path_real,"r")) == NULL){
        printf("获取文件失败");
        return -1;
    }
    //从文件末尾偏移，偏移0位
    fseek(fp,0,SEEK_END);
    long file_size = ftell(fp);//ftell()存储当前文件描述符的读取的偏移位置，这里就是文件末尾
    fclose(fp);
    return file_size;
}

jint getResourceLength(jmethodID id, va_list args) {
    const char * value = va_arg(args, char*);
    long length=get_file_size(value);
    debugPrintf("getResourceLength(%s) %d",value,length);
    return (jint)length;
}

void* getData(char* filename,int * num)
{
    char fopen_path_real[1024];
    memset(fopen_path_real,0,1024);
    int count =strlen(filename);
    sceClibSnprintf(fopen_path_real, sizeof(fopen_path_real), "ux0:data/EternalLegacy/GloftELHP/%s", filename );
    //replace(fopen_path_real,'/','~',1022);
    FILE *fp = fopen(fopen_path_real, "r");
    if (fp == NULL) {

        return NULL;
     }
    fseek(fp, 0, SEEK_END);
    int file_size = (int)ftell(fp);
    *num=file_size;
    char a[file_size]; 
    int i = 0;
    fseek(fp, 0, SEEK_SET);

    while (i<file_size&&fscanf(fp, "%d", &a[i]) != EOF) {
        i++;
    }
    fclose(fp);
    return a;
}

jobject getResourceBytes(jmethodID id, va_list args) {
    const char * value = va_arg(args, char*);
    debugPrintf("getResourceBytes(%s)",value);
    int num;
    return (jobject)getData(value,&num);
}



void sendAppToBackground(jmethodID id, va_list args) {

    debugPrintf("sendAppToBackground()");
}

void Exit(jmethodID id, va_list args) {
    debugPrintf("Exit()");
    //exit(0);
}

jint getHeight(jmethodID id, va_list args) {
    debugPrintf("getHeight()");
    return 544;
}

jint getWidth(jmethodID id, va_list args) {
    debugPrintf("getWidth()");
    return 960;
}

jobject getResourceFull(jmethodID id, va_list args) {
    const char* value = va_arg(args, char*);
    debugPrintf("getResourceFull(%s)",value);
    return NULL;
}

jint isSoundLoaded(jmethodID id, va_list args)
{
    debugPrintf("isSoundLoaded(1)");
    return 1;
}

jint isSoundLoadedBig(jmethodID id, va_list args)
{
    debugPrintf("isSoundLoadedBig(1)");
    return 1;
}


void unloadSound(jmethodID id, va_list args) {
    int v1 = va_arg(args, int);
    int v2 = va_arg(args, int);
    debugPrintf("unloadSound(%d,%d)",v1,v2);
}

void unloadSoundBig(jmethodID id, va_list args) {
    int v1 = va_arg(args, int);
    debugPrintf("unloadSoundBig(%d)",v1);
}

void loadSound(jmethodID id, va_list args) {
    debugPrintf("loadSound()");
}

void loadSoundBig(jmethodID id, va_list args) {
    debugPrintf("loadSoundBig()");
}

void playSound(jmethodID id, va_list args) {
    debugPrintf("playSound()");
}

void playSoundBig(jmethodID id, va_list args) {
    debugPrintf("playSoundBig()");
}

void pauseSound(jmethodID id, va_list args) {
    debugPrintf("pauseSound()");
}

void pauseSoundBig(jmethodID id, va_list args) {
    debugPrintf("pauseSoundBig()");
}

void resumeSound(jmethodID id, va_list args) {
    debugPrintf("resumeSound()");
}

void resumeSoundBig(jmethodID id, va_list args) {
    debugPrintf("resumeSoundBig()");
}

void stopSound(jmethodID id, va_list args) {
    debugPrintf("stopSound()");
}

void stopSoundBig(jmethodID id, va_list args) {
    debugPrintf("stopSoundBig()");
}

void setVolume(jmethodID id, va_list args) {
    debugPrintf("setVolume()");
}

void setVolumeBig(jmethodID id, va_list args) {
    debugPrintf("setVolumeBig()");
}

void resetSound(jmethodID id, va_list args) {
    debugPrintf("resetSound()");
}

void setPitch(jmethodID id, va_list args) {
    debugPrintf("setPitch()");
}

void stopAllSounds(jmethodID id, va_list args) {
    debugPrintf("stopAllSounds()");
}

void stopAllPool(jmethodID id, va_list args) {
    debugPrintf("stopAllPool()");
}

void stopAllBig(jmethodID id, va_list args) {
    debugPrintf("stopAllBig()");
}

void destroySoundPool(jmethodID id, va_list args) {
    debugPrintf("destroySoundPool()");
}

void initSoundPoolArray(jmethodID id, va_list args) {
    debugPrintf("initSoundPoolArray()");
}

jobject GetRealDeviceMacAddress(jmethodID id, va_list args) {
    debugPrintf("GetRealDeviceMacAddress(02:00:00:00:00:00)");
    return "02:00:00:00:00:00";
}

void OpenIGP(jmethodID id, va_list args) {
    debugPrintf("OpenIGP()");
}

void openBrowser(jmethodID id, va_list args)
{
    debugPrintf("openBrowser()");
}

void d(jmethodID id, va_list args)//mLicense
{
    debugPrintf("d()");
}

jobject da(jmethodID id, va_list args)//Activity
{
    debugPrintf("da()");
    return NULL;
}

jobject db(jmethodID id, va_list args)//mFileConfig
{
    int num;
    char* job= getData("ux0:data/EternalLegacy/raw/igli.bin",&num);
    jbyteArray igli=NewByteArray(&jni,num);
    for (size_t i = 0; i < num; i++)
    {
        SetObjectArrayElement(&jni,igli,i,job[i]);
    }
    return igli;
}

jobject dc(jmethodID id, va_list args)//mKEY
{
    int num;
    char* job= getData("ux0:data/EternalLegacy/raw/serialkey.txt",&num);
    jbyteArray serialkey=NewByteArray(&jni,num);
    for (size_t i = 0; i < num; i++)
    {
        SetObjectArrayElement(&jni,serialkey,i,job[i]);
    }
    return serialkey;
}

jobject getSystemService(jmethodID id, va_list args)
{
    const char * value = va_arg(args, char*);
    debugPrintf("getSystemService(%s)",value);
    return "SystemService";
}

jobject getDeviceId(jmethodID id, va_list args)
{
    debugPrintf("getDeviceId()");
    return "1234567891011";
}

NameToMethodID nameToMethodId[] = {
    { 1, "OpenGLive", METHOD_TYPE_VOID ,OpenGLive}, // ()V
    { 2, "sendAppToBackground", METHOD_TYPE_VOID ,sendAppToBackground}, // ()V
    { 3, "destroySoundPool", METHOD_TYPE_VOID ,destroySoundPool}, // (I)V
    { 4, "playSound", METHOD_TYPE_VOID ,playSound}, // (I)V
    { 5, "pauseSound", METHOD_TYPE_LONG ,pauseSound}, // ()J
    { 6, "stopSound", METHOD_TYPE_INT,stopSound }, // ()I
    { 7, "loadSound", METHOD_TYPE_VOID ,loadSound}, // (Ljava/lang/String;)V
    { 8, "setVolume", METHOD_TYPE_OBJECT ,setVolume}, // ()Ljava/lang/String;
    { 9, "setPitch", METHOD_TYPE_OBJECT,setPitch }, // ()Ljava/lang/String;
    { 10, "resetSound", METHOD_TYPE_OBJECT ,resetSound}, // ()Ljava/lang/String;
    { 11, "stopAllSounds", METHOD_TYPE_FLOAT ,stopAllSounds}, // ()F
    { 12, "unloadSoundBig", METHOD_TYPE_OBJECT ,unloadSoundBig}, // ()Ljava/lang/String;
    { 13, "OpenIGP", METHOD_TYPE_VOID,OpenIGP }, // (I)V
    { 14, "initSoundPoolArray", METHOD_TYPE_INT,initSoundPoolArray }, // ()I
    { 15, "openBrowser", METHOD_TYPE_INT ,openBrowser}, // ([BII)I
    { 16, "GetRealDeviceMacAddress", METHOD_TYPE_INT,GetRealDeviceMacAddress }, // ([BII)I
    { 17, "stopAllBig", METHOD_TYPE_INT ,stopAllBig}, // ([BII)I
    { 18, "getResourceFull", METHOD_TYPE_INT ,getResourceFull}, // ([BII)I
    { 19, "getHeight", METHOD_TYPE_INT ,getHeight}, // ([BII)I
    { 20, "getWidth", METHOD_TYPE_INT ,getWidth}, // ([BII)I
    { 21, "unloadSound", METHOD_TYPE_INT ,unloadSound}, // ([BII)I
    { 22, "GetCurrentTime", METHOD_TYPE_LONG ,NULL}, // ([BII)I
    { 23, "stopAllPool", METHOD_TYPE_INT ,stopAllPool}, // ([BII)I
    { 24, "isSoundLoaded", METHOD_TYPE_INT ,isSoundLoaded}, // ([BII)I
    { 26, "Get_DeviceLockState", METHOD_TYPE_INT ,Get_DeviceLockState}, // ([BII)I
    { 27, "Get_logoVideoState", METHOD_TYPE_INT ,Get_logoVideoState}, // ([BII)I
    { 28, "Exit", METHOD_TYPE_INT ,Exit}, // ([BII)I
    { 29, "detectPhoneLang", METHOD_TYPE_INT ,detectPhoneLang}, // ([BII)I
    { 30, "getResourceBytes", METHOD_TYPE_INT ,getResourceBytes}, // ([BII)I
    { 31, "getResourceLength", METHOD_TYPE_INT ,getResourceLength}, // ([BII)I
    { 32, "loadSoundBig", METHOD_TYPE_INT ,loadSoundBig}, // ([BII)I
    { 33, "resumeSound", METHOD_TYPE_INT ,resumeSound}, // ([BII)I
    { 34, "Get_PhoneManufacturer", METHOD_TYPE_INT ,Get_PhoneManufacturer}, // ([BII)I
    { 35, "loadMovie", METHOD_TYPE_INT ,loadMovie}, 
    { 36, "isMediaPlaying", METHOD_TYPE_INT ,isMediaPlaying}, 
    { 37, "Get_PhoneLanguage", METHOD_TYPE_INT ,Get_PhoneLanguage}, 
    { 38, "setVolumeBig", METHOD_TYPE_INT ,setVolumeBig}, 
    { 39, "stopSoundBig", METHOD_TYPE_INT ,stopSoundBig}, 
    { 40, "resumeSoundBig", METHOD_TYPE_INT ,resumeSoundBig}, 
    { 41, "pauseSoundBig", METHOD_TYPE_INT ,pauseSoundBig}, 
    { 42, "playSoundBig", METHOD_TYPE_INT ,playSoundBig}, 
    { 43, "isSoundLoadedBig", METHOD_TYPE_INT ,isSoundLoadedBig}, 
    { 44, "dc", METHOD_TYPE_INT ,dc}, 
    { 45, "db", METHOD_TYPE_INT ,db}, 
    { 46, "d", METHOD_TYPE_INT ,d}, 
    { 47, "da", METHOD_TYPE_INT ,da}, 
    { 48, "getSystemService", METHOD_TYPE_INT ,getSystemService}, 
    { 49, "getDeviceId", METHOD_TYPE_INT ,getDeviceId}, 
};

MethodsBoolean methodsBoolean[] = {};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {

};
MethodsFloat methodsFloat[] = {

};
MethodsInt methodsInt[] = {};
MethodsLong methodsLong[] = {
};
MethodsObject methodsObject[] = {};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {};

/*
 * JNI Fields
*/

// System-wide constant that applications sometimes request
// https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE
char WINDOW_SERVICE[] = "window";
char TELEPHONY_SERVICE[] = "TELEPHONY_SERVICE";

// System-wide constant that's often used to determine Android version
// https://developer.android.com/reference/android/os/Build.VERSION.html#SDK_INT
// Possible values: https://developer.android.com/reference/android/os/Build.VERSION_CODES
const int SDK_INT = 19; // Android 4.4 / KitKat
const int descriptor = 110; // Android 4.4 / KitKat
NameToFieldID nameToFieldId[] = {
		{ 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT }, 
		{ 1, "SDK_INT", FIELD_TYPE_INT },
        { 2, "descriptor", FIELD_TYPE_INT },
        { 3, "TELEPHONY_SERVICE", FIELD_TYPE_OBJECT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
		{ 1, SDK_INT },
        { 2, descriptor },
};
FieldsObject fieldsObject[] = {
		{ 0, WINDOW_SERVICE },
		{ 3, TELEPHONY_SERVICE },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
