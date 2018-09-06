#ifndef __FANPLAYER_FFRENDER_H__
#define __FANPLAYER_FFRENDER_H__

// 包含头文件
#include "ffplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"

// 函数声明
void*render_open(int adevtype, int srate, int sndfmt, int64_t ch_layout,
                 int vdevtype, void *surface, AVRational frate, int pixfmt, int w, int h);
void render_close   (void *hrender);
void render_audio   (void *hrender, AVFrame *audio);
void render_video   (void *hrender, AVFrame *video);
void render_setrect (void *hrender, int type, int x, int y, int w, int h);
void render_start   (void *hrender);
void render_pause   (void *hrender);
void render_reset   (void *hrender);
void render_setparam(void *hrender, int id, void *param);
void render_getparam(void *hrender, int id, void *param);

#ifdef __cplusplus
}
#endif

#endif















