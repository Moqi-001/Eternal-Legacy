#ifndef SOLOADER_CONTROLS_H
#define SOLOADER_CONTROLS_H

#ifdef __cplusplus
extern "C" {
#endif

// 手柄初始化；之后每帧调用一次 soloader_poll_controls() 把按键沿转成 Android KeyEvent
int  soloader_setup_controls(void);
void soloader_poll_controls(void);

#ifdef __cplusplus
};
#endif

#endif
