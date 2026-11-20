#ifndef SOLOADER_CONTROLS_H
#define SOLOADER_CONTROLS_H

#ifdef __cplusplus
extern "C" {
#endif

// 手柄初始化；之后每帧调用一次 controls_poll() 把按键沿转成 Android KeyEvent
int  controls_create(void);
void controls_poll(void);

#ifdef __cplusplus
};
#endif

#endif
