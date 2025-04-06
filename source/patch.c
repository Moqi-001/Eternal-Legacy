/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
// #include <string.h>
// #include <stdlib.h>
#include <so_util/so_util.h>
#include "utils/logger.h"
#include <falso_jni/FalsoJNI.h>

extern so_module so_mod;

void MC_knlPrintk(char* name)
{
    debugPrintf(name);
}

void* NVThreadGetCurrentJNIEnv()
{
    debugPrintf("NVThreadGetCurrentJNIEnv");
    return &jni;
}

void _ZN13ALicenseCheck15CallJNIFuncCharEP7_(int class,int id,char * out,int max)
{
    debugPrintf("_ZN13ALicenseCheck15CallJNIFuncCharEP7_");
    memset(out,0,max);
    jbyteArray v= CallStaticObjectMethod(jni,class,id);
    jint len= GetArrayLength(jni,v);
    GetByteArrayRegion(jni,v,0,len,out);
}

void so_patch(void) {
    // Sample hook
    //hook_addr((uintptr_t)so_symbol(&so_mod, "MC_knlPrintk"), (uintptr_t)&MC_knlPrintk);
    //hook_addr((uintptr_t)so_symbol(&so_mod, "_NVThreadGetCurrentJNIEnv_veneer"), (uintptr_t)&NVThreadGetCurrentJNIEnv);
    hook_addr((uintptr_t)so_symbol(&so_mod, "_ZN13ALicenseCheck15CallJNIFuncCharEP7_"), (uintptr_t)&_ZN13ALicenseCheck15CallJNIFuncCharEP7_);
    // char ** GVfs = (char **) so_symbol(&so_mod, "GVfs");
    // *GVfs = malloc(60);
    // //**GVfs = 0;
    // memset(*GVfs,0,60);
    //debugPrintf("set GVfs over");
    
}
