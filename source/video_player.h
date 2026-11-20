#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

int  video_open(const char *path, float volume);
int  video_play(void);
int  video_pause(void);
int  video_stop();
void video_close(void);

int video_is_playing(void);
int video_is_active(void);

// 在黑底上等比缩放画出当前帧（含上一帧，避免帧间闪烁）；没在播返回 0
int video_draw(void);

#ifdef __cplusplus
};
#endif

#endif
