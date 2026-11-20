#ifndef JAVA_DEVICE_H
#define JAVA_DEVICE_H

#include <so_jni/so_jni.h>
#include <so_linker/so_linker.h>

// com/gameloft/android/TBFV/GloftELHP/ML/GLUtils/Device
void setup_Device(so_module *mod);

// Android 侧 Device.init()：先填设备信息再进 native
void Device_init(void);

// 设备标识（16 位十六进制），引擎拿它当存档/DRM 的一部分
const char *Device_getDeviceId(void);

#endif
