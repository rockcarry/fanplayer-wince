// 包含头文件
#include "ffrender.h"
#include "adev.h"
#include "vdev.h"

#include "libavutil/time.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"

// 内部类型定义
typedef struct
{
    // adev & vdev
    void          *adev;
    void          *vdev;

    // swresampler & swscaler
    struct SwrContext *swr_context;
    struct SwsContext *sws_context;

    int            sample_rate;
    int            sample_fmt;
    int64_t        chan_layout;

    int            video_width;
    int            video_height;
    AVRational     frame_rate;
    int            pixel_fmt;

    int            adev_buf_avail;
    uint8_t       *adev_buf_cur;
    AUDIOBUF      *adev_hdr_cur;

    // video render rect
    int            rect_xcur;
    int            rect_ycur;
    int            rect_xnew;
    int            rect_ynew;
    int            rect_wcur;
    int            rect_hcur;
    int            rect_wnew;
    int            rect_hnew;

    // playback speed
    int            speed_value_cur;
    int            speed_value_new;
    int            speed_type_cur;
    int            speed_type_new;

    // render status
    #define RENDER_CLOSE       (1 << 0)
    #define RENDER_PAUSE       (1 << 1)
    #define RENDER_SNAPSHOT    (1 << 2)  // take snapshot
    #define RENDER_STEPFORWARD (1 << 3)  // step forward
    int            render_status;
} RENDER;

// 内部函数实现
static void render_setspeed(RENDER *render, int speed)
{
    if (speed > 0) {
        // set vdev playback speed
        vdev_setparam(render->vdev, PARAM_PLAY_SPEED_VALUE, &speed);

        // set speed_value_new to triger swr_context re-create
        render->speed_value_new = speed;
    }
}

// 函数实现
void* render_open(int adevtype, int srate, int sndfmt, int64_t ch_layout,
                  int vdevtype, void *surface, AVRational frate, int pixfmt, int w, int h)
{
    RENDER *render = (RENDER*)calloc(1, sizeof(RENDER));
    if (!render) {
        av_log(NULL, AV_LOG_ERROR, "failed to allocate render context !\n");
        exit(0);
    }

    // init for video
    render->video_width  = w;
    render->video_height = h;
    render->rect_wnew    = w;
    render->rect_hnew    = h;
    render->frame_rate   = frate;
    render->pixel_fmt    = pixfmt;
    if (render->pixel_fmt == AV_PIX_FMT_NONE) {
        render->pixel_fmt = AV_PIX_FMT_YUV420P;
    }

    // init for audio
    render->sample_rate  = srate;
    render->sample_fmt   = sndfmt;
    render->chan_layout  = ch_layout;

    // create adev & vdev
    render->adev = adev_create(adevtype, 0, (int)((double)ADEV_SAMPLE_RATE * frate.den / frate.num + 0.5) * 4);
    render->vdev = vdev_create(vdevtype, surface, 0, w, h, (int)((double)frate.num / frate.den + 0.5));

    // make adev & vdev sync together
    int64_t *papts = NULL;
    vdev_getavpts(render->vdev, &papts, NULL);
    adev_syncapts(render->adev,  papts);

#ifdef WIN32
    RECT rect; GetClientRect((HWND)surface, &rect);
    render_setrect(render, 0, rect.left, rect.top, rect.right, rect.bottom);
    render_setrect(render, 1, rect.left, rect.top, rect.right, rect.bottom);
#endif

    // set default playback speed
    render_setspeed(render, 100);
    return render;
}

void render_close(void *hrender)
{
    RENDER *render = (RENDER*)hrender;

    // wait visual effect thread exit
    render->render_status = RENDER_CLOSE;

    //++ audio ++//
    // destroy adev
    adev_destroy(render->adev);

    // free swr context
    swr_free(&render->swr_context);
    //-- audio --//

    //++ video ++//
    // destroy vdev
    vdev_destroy(render->vdev);

    // free sws context
    if (render->sws_context) {
        sws_freeContext(render->sws_context);
    }
    //-- video --//

    // free context
    free(render);
}

static int render_audio_swresample(RENDER *render, AVFrame *audio)
{
    int num_samp;

    if (render->adev_buf_avail == 0) {
        adev_lock(render->adev, &render->adev_hdr_cur);
        if (render->adev_hdr_cur) {
            render->adev_buf_avail = (int     )render->adev_hdr_cur->size;
            render->adev_buf_cur   = (uint8_t*)render->adev_hdr_cur->data;
        }
    }

    //++ do resample audio data ++//
    num_samp = swr_convert(render->swr_context,
        (uint8_t**)&render->adev_buf_cur, render->adev_buf_avail / 4,
        (const uint8_t**)audio->extended_data, audio->nb_samples);
    audio->extended_data    = NULL;
    audio->nb_samples       = 0;
    render->adev_buf_avail -= num_samp * 4;
    render->adev_buf_cur   += num_samp * 4;
    //-- do resample audio data --//

    if (render->adev_buf_avail == 0) {
        audio->pts += 10 * render->speed_value_cur * render->frame_rate.den / render->frame_rate.num;
        adev_unlock(render->adev, audio->pts);
    }

    return num_samp;
}

void render_audio(void *hrender, AVFrame *audio)
{
    if (!hrender) return;
    RENDER *render  = (RENDER*)hrender;
    int     sampnum;

    do {
        if (  render->speed_value_cur != render->speed_value_new
           || render->speed_type_cur  != render->speed_type_new ) {
            render->speed_value_cur = render->speed_value_new;
            render->speed_type_cur  = render->speed_type_new ;

            //++ allocate & init swr context
            int samprate = render->speed_type_cur ? ADEV_SAMPLE_RATE : (int)(ADEV_SAMPLE_RATE * 100.0 / render->speed_value_cur);
            if (render->swr_context) {
                swr_free(&render->swr_context);
            }
            render->swr_context = swr_alloc_set_opts(NULL, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, samprate,
                render->chan_layout, render->sample_fmt, render->sample_rate, 0, NULL);
            swr_init(render->swr_context);
            //-- allocate & init swr context
        }

        sampnum = render_audio_swresample(render, audio);
    } while (sampnum > 0);
}

void render_video(void *hrender, AVFrame *video)
{
    if (!hrender) return;
    RENDER *render = (RENDER*)hrender;

    do {
        VDEV_COMMON_CTXT *vdev = (VDEV_COMMON_CTXT*)render->vdev;
        if (  render->rect_xcur != render->rect_xnew
           || render->rect_ycur != render->rect_ynew
           || render->rect_wcur != render->rect_wnew
           || render->rect_hcur != render->rect_hnew ) {
            render->rect_xcur = render->rect_xnew;
            render->rect_ycur = render->rect_ynew;
            render->rect_wcur = render->rect_wnew;
            render->rect_hcur = render->rect_hnew;

            // vdev set rect
            vdev_setrect(render->vdev, render->rect_xcur, render->rect_ycur, render->rect_wcur, render->rect_hcur);

            // we need recreate sws
            if (!render->sws_context) {
                sws_freeContext(render->sws_context);
            }
            render->sws_context = sws_getContext(
                render->video_width, render->video_height, render->pixel_fmt,
                vdev->sw, vdev->sh, vdev->pixfmt,
                SWS_FAST_BILINEAR, 0, 0, 0);
        }

        if (1) {
            AVFrame picture;
            picture.data[0]     = NULL;
            picture.linesize[0] = 0;
            vdev_lock(render->vdev, picture.data, picture.linesize);
            if (picture.data[0] && video->pts != -1) {
                sws_scale(render->sws_context, (const uint8_t * const *)video->data, video->linesize, 0, render->video_height, picture.data, picture.linesize);
            }
            vdev_unlock(render->vdev, video->pts);
        }
    } while ((render->render_status & RENDER_PAUSE) && !(render->render_status & RENDER_STEPFORWARD));

    // clear step forward flag
    render->render_status &= ~RENDER_STEPFORWARD;
}

void render_setrect(void *hrender, int type, int x, int y, int w, int h)
{
    if (!hrender) return;
    RENDER *render = (RENDER*)hrender;
    switch (type) {
    case 0:
        render->rect_xnew = x;
        render->rect_ynew = y;
        render->rect_wnew = w > 1 ? w : 1;
        render->rect_hnew = h > 1 ? h : 1;
        break;
    }
}

void render_start(void *hrender)
{
    if (!hrender) return;
    RENDER *render = (RENDER*)hrender;
    render->render_status &=~RENDER_PAUSE;
    adev_pause(render->adev, 0);
    vdev_pause(render->vdev, 0);
}

void render_pause(void *hrender)
{
    if (!hrender) return;
    RENDER *render = (RENDER*)hrender;
    render->render_status |= RENDER_PAUSE;
    adev_pause(render->adev, 1);
    vdev_pause(render->vdev, 1);
}

void render_reset(void *hrender)
{
    if (!hrender) return;
    RENDER *render = (RENDER*)hrender;
    adev_reset(render->adev);
    vdev_reset(render->vdev);
    render->render_status = 0;
}

void render_setparam(void *hrender, int id, void *param)
{
    if (!hrender) return;
    RENDER *render = (RENDER*)hrender;
    switch (id)
    {
    case PARAM_AUDIO_VOLUME:
        adev_setparam(render->adev, id, param);
        break;
    case PARAM_PLAY_SPEED_VALUE:
        render_setspeed(render, *(int*)param);
        break;
    case PARAM_PLAY_SPEED_TYPE:
        render->speed_type_new = *(int*)param;
        break;
    case PARAM_AVSYNC_TIME_DIFF:
        vdev_setparam(render->vdev, id, param);
        break;
    case PARAM_RENDER_STEPFORWARD:
        render->render_status |= RENDER_STEPFORWARD;
        break;
    }
}

void render_getparam(void *hrender, int id, void *param)
{
    if (!hrender) return;
    RENDER         *render = (RENDER*)hrender;
    VDEV_COMMON_CTXT *vdev = (VDEV_COMMON_CTXT*)render->vdev;
    switch (id)
    {
    case PARAM_MEDIA_POSITION:
        if (vdev->status & VDEV_COMPLETED) {
            *(int64_t*)param  = -1; // means completed
        } else {
            if (vdev->apts >= 0 || vdev->vpts >= 0) {
                *(int64_t*)param = vdev->apts > vdev->vpts ? vdev->apts : vdev->vpts;
            } else {
                *(int64_t*)param = AV_NOPTS_VALUE;
            }
        }
        break;
    case PARAM_AUDIO_VOLUME:
        adev_getparam(render->adev, id, param);
        break;
    case PARAM_PLAY_SPEED_VALUE:
        *(int*)param = render->speed_value_cur ? render->speed_value_cur : render->speed_value_new;
        break;
    case PARAM_PLAY_SPEED_TYPE:
        *(int*)param = render->speed_type_cur;
        break;
    case PARAM_AVSYNC_TIME_DIFF:
        vdev_getparam(render->vdev, id, param);
        break;
    case PARAM_ADEV_GET_CONTEXT:
        *(void**)param = render->adev;
        break;
    case PARAM_VDEV_GET_CONTEXT:
        *(void**)param = render->vdev;
        break;
    }
}


