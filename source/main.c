#include <string.h>

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <so_common/so_logger.h>

#include "config.h"
#include "loader.h"

int _newlib_heap_size_user = MEMORY_NEWLIB_MB * 1024 * 1024;

int main(int argc, char *argv[]) {
    SceCommonDialogConfigParam cmnDlgCfgParam;
    SceAppUtilInitParam        appUtilParam;
    SceAppUtilBootParam        appUtilBootParam;
    memset(&appUtilParam, 0, sizeof(SceAppUtilInitParam));
    memset(&appUtilBootParam, 0, sizeof(SceAppUtilBootParam));
    sceAppUtilInit(&appUtilParam, &appUtilBootParam);
    sceCommonDialogConfigParamInit(&cmnDlgCfgParam);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, (int *)&cmnDlgCfgParam.language);
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int *)&cmnDlgCfgParam.enterButtonAssign);
    sceCommonDialogSetConfigParam(&cmnDlgCfgParam);

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);

    int arm_clock = scePowerSetArmClockFrequency(444);
    int bus_clock = scePowerSetBusClockFrequency(222);
    int gpu_clock = scePowerSetGpuClockFrequency(222);
    int xbar_clock = scePowerSetGpuXbarClockFrequency(166);
    so_loga("[CLOCK] requested=444/222/222/166 actual=%d/%d/%d/%d status=%d/%d/%d/%d",
            scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
            scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency(),
            arm_clock, bus_clock, gpu_clock, xbar_clock);

    soloader_init();
    soloader_run();
    soloader_term();

    return sceKernelExitProcess(0);
}
