// ����ͷ�ļ�
#include "pktqueue.h"
#include "ffrender.h"
#include "ffplayer.h"
#include "vdev.h"

#include "libavutil/time.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

// �ڲ����Ͷ���
typedef struct {
    // format
    AVFormatContext *avformat_context;

    // audio
    AVCodecContext  *acodec_context;
    int              astream_index;
    AVRational       astream_timebase;
    AVFrame          aframe;

    // video
    AVCodecContext  *vcodec_context;
    int              vstream_index;
    AVRational       vstream_timebase;
    AVFrame          vframe;

    // pktqueue
    void            *pktqueue;

    // render
    void            *render;
    RECT             vdrect;
    int              vdmode;

    // thread
    #define PS_A_PAUSE    (1 << 0)  // audio decoding pause
    #define PS_V_PAUSE    (1 << 1)  // video decoding pause
    #define PS_R_PAUSE    (1 << 2)  // rendering pause
    #define PS_F_SEEK     (1 << 3)  // seek flag
    #define PS_A_SEEK     (1 << 4)  // seek audio
    #define PS_V_SEEK     (1 << 5)  // seek video
    #define PS_CLOSE      (1 << 6)  // close player
    int              player_status;
    int              seek_req ;
    int64_t          seek_pos ;
    int64_t          seek_dest;
    int64_t          seek_vpts;
    int              seek_diff;
    int              seek_sidx;
    int64_t          start_pts;

    HANDLE           avdemux_thread;
    HANDLE           adecode_thread;
    HANDLE           vdecode_thread;

    AVFilterGraph   *vfilter_graph;
    AVFilterContext *vfilter_src_ctx;
    AVFilterContext *vfilter_sink_ctx;

    // player init timeout, and init params
    int64_t            init_timetick;
    int64_t            init_timeout;
    PLAYER_INIT_PARAMS init_params;
    int64_t            tick_demux;

    // save url and appdata
    char             url[PATH_MAX];
    void            *appdata;
} PLAYER;

// �ڲ���������
static const AVRational TIMEBASE_MS = { 1, 1000 };

// �ڲ�����ʵ��
static FILE *g_fp = NULL;
static void avlog_callback(void* ptr, int level, const char *fmt, va_list vl) {
    DO_USE_VAR(ptr);
    if (level <= av_log_get_level() && g_fp) {
#ifdef WIN32
        char str[1024];
        vsprintf(str, fmt, vl);
        fputs(str, g_fp);
        fflush(g_fp);
#endif
    }
}

static int interrupt_callback(void *param)
{
    PLAYER *player = (PLAYER*)param;
    if (player->init_timeout == -1) return 0;
    return av_gettime_relative() - player->init_timetick > player->init_timeout ? AVERROR_EOF : 0;
}

//++ for filter graph
static void vfilter_graph_init(PLAYER *player)
{
    if (!player->vcodec_context) return;
    AVFilter       *filter_src  = avfilter_get_by_name("buffer"    );
    AVFilter       *filter_sink = avfilter_get_by_name("buffersink");
    AVCodecContext *vdec_ctx    = player->vcodec_context;

    char temp[256];
    char fstr[256];
    int  ret;

    //++ check if no filter used
    if (  !player->init_params.video_deinterlace
       && !player->init_params.video_rotate
       && !player->init_params.filter_string[0] ) {
        return;
    }
    //-- check if no filter used

    player->vfilter_graph = avfilter_graph_alloc();
    if (!player->vfilter_graph) return;

    //++ create in & out filter
    sprintf(temp, "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            vdec_ctx->width, vdec_ctx->height, vdec_ctx->pix_fmt,
            vdec_ctx->time_base.num, vdec_ctx->time_base.den,
            vdec_ctx->sample_aspect_ratio.num, vdec_ctx->sample_aspect_ratio.den);
    avfilter_graph_create_filter(&player->vfilter_src_ctx, filter_src, "in", temp, NULL, player->vfilter_graph);

    enum AVPixelFormat pixfmts[] = { vdec_ctx->pix_fmt, AV_PIX_FMT_NONE };
    AVBufferSinkParams params    = { pixfmts };
    avfilter_graph_create_filter(&player->vfilter_sink_ctx, filter_sink, "out", NULL, &params, player->vfilter_graph);
    //-- create in & out filter

    //++ generate filter string according to deinterlace and rotation
    if (player->init_params.video_rotate) {
        int ow = abs((int)(vdec_ctx->width  * cos(player->init_params.video_rotate * M_PI / 180)))
               + abs((int)(vdec_ctx->height * sin(player->init_params.video_rotate * M_PI / 180)));
        int oh = abs((int)(vdec_ctx->width  * sin(player->init_params.video_rotate * M_PI / 180)))
               + abs((int)(vdec_ctx->height * cos(player->init_params.video_rotate * M_PI / 180)));
        player->init_params.video_owidth  = ow;
        player->init_params.video_oheight = oh;
        sprintf(temp, "rotate=%d*PI/180:%d:%d", player->init_params.video_rotate, ow, oh);
    }
    strcpy(fstr, player->init_params.video_deinterlace ? "yadif=0:-1:1" : "");
    strcat(fstr, player->init_params.video_deinterlace && player->init_params.video_rotate ? "[a];[a]" : "");
    strcat(fstr, player->init_params.video_rotate ? temp : "");
    //-- generate filter string according to deinterlace and rotation

    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterInOut *outputs = avfilter_inout_alloc();
    inputs->name        = av_strdup("out");
    inputs->filter_ctx  = player->vfilter_sink_ctx;
    inputs->pad_idx     = 0;
    inputs->next        = NULL;
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = player->vfilter_src_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
    ret = avfilter_graph_parse_ptr(player->vfilter_graph, player->init_params.filter_string[0] ? player->init_params.filter_string : fstr, &inputs, &outputs, NULL);
    avfilter_inout_free(&inputs );
    avfilter_inout_free(&outputs);
    if (ret < 0) {
        av_log(NULL, AV_LOG_WARNING, "avfilter_graph_parse_ptr failed !\n");
        goto failed;
    }

    // config filter graph
    ret = avfilter_graph_config(player->vfilter_graph, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_WARNING, "avfilter_graph_config failed !\n");
        goto failed;
    }

failed:
    if (ret < 0) {
        avfilter_graph_free(&player->vfilter_graph);
        player->vfilter_graph    = NULL;
        player->vfilter_src_ctx  = NULL;
        player->vfilter_sink_ctx = NULL;
    }
}

static void vfilter_graph_free(PLAYER *player)
{
    if (!player->vfilter_graph) return;
    avfilter_graph_free(&player->vfilter_graph);
    player->vfilter_graph    = NULL;
    player->vfilter_src_ctx  = NULL;
    player->vfilter_sink_ctx = NULL;
}

static void vfilter_graph_input(PLAYER *player, AVFrame *frame)
{
    if (!player->vfilter_graph) return;
    int ret = av_buffersrc_add_frame(player->vfilter_src_ctx, frame);
    if (ret != 0) {
        av_log(NULL, AV_LOG_WARNING, "av_buffersrc_add_frame_flags failed !\n");
    }
}

static int vfilter_graph_output(PLAYER *player, AVFrame *frame)
{
    if (player->vfilter_graph) {
        return av_buffersink_get_frame(player->vfilter_sink_ctx, frame);
    } else {
        return 0;
    }
}
//-- for filter graph

static int init_stream(PLAYER *player, enum AVMediaType type, int sel) {
    AVCodec *decoder = NULL;
    int     idx = -1, cur = -1, i;

    if (sel == -1) return -1;
    for (i=0; i<(int)player->avformat_context->nb_streams; i++) {
        if (player->avformat_context->streams[i]->codec->codec_type == type) {
            idx = i; if (++cur == sel) break;
        }
    }
    if (idx == -1) return -1;

    switch (type) {
    case AVMEDIA_TYPE_AUDIO:
        // get new acodec_context & astream_timebase
        player->acodec_context   = player->avformat_context->streams[idx]->codec;
        player->astream_timebase = player->avformat_context->streams[idx]->time_base;

        // reopen codec
        decoder = avcodec_find_decoder(player->acodec_context->codec_id);
        if (decoder && avcodec_open2(player->acodec_context, decoder, NULL) == 0) {
            player->astream_index = idx;
        } else {
            av_log(NULL, AV_LOG_WARNING, "failed to find or open decoder for audio !\n");
        }
        break;

    case AVMEDIA_TYPE_VIDEO:
        // get new vcodec_context & vstream_timebase
        player->vcodec_context   = player->avformat_context->streams[idx]->codec;
        player->vstream_timebase = player->avformat_context->streams[idx]->time_base;

        //++ open codec
        if (!decoder) {
            //+ try to set video decoding thread count
            if (player->init_params.video_thread_count > 0) {
                player->vcodec_context->thread_count = player->init_params.video_thread_count;
            }
            //- try to set video decoding thread count
            decoder = avcodec_find_decoder(player->vcodec_context->codec_id);
            if (decoder && avcodec_open2(player->vcodec_context, decoder, NULL) == 0) {
                player->vstream_index = idx;
            } else {
                av_log(NULL, AV_LOG_WARNING, "failed to find or open decoder for video !\n");
            }
            // get the actual video decoding thread count
            player->init_params.video_thread_count = player->vcodec_context->thread_count;
        }
        //-- open codec
        break;

    case AVMEDIA_TYPE_SUBTITLE:
        return -1; // todo...
    default:
        return -1;
    }

    return 0;
}

static int get_stream_total(PLAYER *player, enum AVMediaType type) {
    int total, i;
    for (i=0,total=0; i<(int)player->avformat_context->nb_streams; i++) {
        if (player->avformat_context->streams[i]->codec->codec_type == type) {
            total++;
        }
    }
    return total;
}

static int player_prepare(PLAYER *player)
{
    char          *url    = player->url;
    AVInputFormat *fmt    = NULL;
    int           arate   = 0;
    int           aformat = 0;
    uint64_t      alayout = 0;
    AVRational    vrate   = { 20, 1 };
    int           vformat = AV_PIX_FMT_YUV420P;
    AVDictionary *opts    = NULL;
    int           ret     = -1;

    // allocate avformat_context
    player->avformat_context = avformat_alloc_context();
    if (!player->avformat_context) goto done;

    // setup interrupt_callback
    player->avformat_context->interrupt_callback.callback = interrupt_callback;
    player->avformat_context->interrupt_callback.opaque   = player;

    // open input file
    /*
    av_dict_set(&opts, "rtsp_transport", "udp", 0);
    av_dict_set(&opts, "buffer_size", "1048576", 0);
    av_dict_set(&opts, "fpsprobesize", "2", 0);
    */
    if (player->init_params.video_vwidth != 0 && player->init_params.video_vheight != 0) {
        char vsize[64];
        sprintf(vsize, "%dx%d", player->init_params.video_vwidth, player->init_params.video_vheight);
        av_dict_set(&opts, "video_size", vsize, 0);
    }
    if (player->init_params.video_frame_rate != 0) {
        char frate[64];
        sprintf(frate, "%d", player->init_params.video_frame_rate);
        av_dict_set(&opts, "framerate" , frate, 0);
    }

    // set init_timetick & init_timeout
    player->init_timetick = av_gettime_relative();
    player->init_timeout  = player->init_params.init_timeout ? player->init_params.init_timeout * 1000 : -1;

    while (1) {
        if (avformat_open_input(&player->avformat_context, url, fmt, &opts) != 0) {
            if (player->init_params.auto_reconnect > 0 && !(player->player_status & PS_CLOSE)) {
                av_log(NULL, AV_LOG_INFO, "retry to open url: %s ...\n", url);
                av_usleep(100*1000);
            } else {
                av_log(NULL, AV_LOG_ERROR, "failed to open url: %s !\n", url);
                goto done;
            }
        } else {
            av_log(NULL, AV_LOG_INFO, "successed to open url: %s !\n", url);
            break;
        }
    }

    // set init_timeout to -1
    player->init_timeout = -1;

    // find stream info
    if (avformat_find_stream_info(player->avformat_context, NULL) < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to find stream info !\n");
        goto done;
    }

    // get start_pts
    if (player->avformat_context->start_time > 0) {
        player->start_pts = player->avformat_context->start_time * 1000 / AV_TIME_BASE;
    }

    // set current audio & video stream
    player->astream_index = -1; init_stream(player, AVMEDIA_TYPE_AUDIO, player->init_params.audio_stream_cur);
    player->vstream_index = -1; init_stream(player, AVMEDIA_TYPE_VIDEO, player->init_params.video_stream_cur);
    if (player->astream_index != -1) player->seek_req |= PS_A_SEEK;
    if (player->vstream_index != -1) player->seek_req |= PS_V_SEEK;

    // for audio
    if (player->astream_index != -1) {
        arate   = player->acodec_context->sample_rate;
        aformat = player->acodec_context->sample_fmt;
        alayout = player->acodec_context->channel_layout;
        //++ fix audio channel layout issue
        if (alayout == 0) {
            alayout = av_get_default_channel_layout(player->acodec_context->channels);
        }
        //-- fix audio channel layout issue
    }

    // for video
    if (player->vstream_index != -1) {
        vrate = player->avformat_context->streams[player->vstream_index]->r_frame_rate;
        if (vrate.num / vrate.den > 100) {
            vrate.num = 20;
            vrate.den = 1;
        }
        vformat = player->vcodec_context->pix_fmt;
        player->init_params.video_vwidth = player->init_params.video_owidth  = player->vcodec_context->width;
        player->init_params.video_vheight= player->init_params.video_oheight = player->vcodec_context->height;
    }

    // init avfilter graph
    vfilter_graph_init(player);

    // open render
    player->render = render_open(
        player->init_params.adev_render_type, arate, aformat, alayout,
        player->init_params.vdev_render_type, player->appdata, vrate, vformat,
        player->init_params.video_owidth, player->init_params.video_oheight);

    if (player->vstream_index == -1) {
        int effect = VISUAL_EFFECT_WAVEFORM;
        render_setparam(player->render, PARAM_VISUAL_EFFECT, &effect);
    }

    // for player init params
    player->init_params.video_frame_rate     = vrate.num / vrate.den;
    player->init_params.video_stream_total   = get_stream_total(player, AVMEDIA_TYPE_VIDEO);
    player->init_params.audio_channels       = av_get_channel_layout_nb_channels(alayout);
    player->init_params.audio_sample_rate    = arate;
    player->init_params.audio_stream_total   = get_stream_total(player, AVMEDIA_TYPE_AUDIO);
    player->init_params.subtitle_stream_total= get_stream_total(player, AVMEDIA_TYPE_SUBTITLE);
    ret = 0; // prepare ok

done:
    // send player init message
    player_send_message(player->appdata, ret ? MSG_OPEN_FAILED : MSG_OPEN_DONE, (uint32_t)player);
    return ret;
}

static void handle_fseek_or_reconnect(PLAYER *player, int reconnect)
{
    int PAUSE_REQ = 0;
    int PAUSE_ACK = 0;

    if (player->astream_index != -1) { PAUSE_REQ |= PS_A_PAUSE; PAUSE_ACK |= PS_A_PAUSE << 16; }
    if (player->vstream_index != -1) { PAUSE_REQ |= PS_V_PAUSE; PAUSE_ACK |= PS_V_PAUSE << 16; }

    // set audio & video decoding pause flags
    player->player_status |= PAUSE_REQ | player->seek_req;
    player->player_status &=~PAUSE_ACK;

    // make render run
    render_start(player->render);

    // wait for pause done
    while ((player->player_status & PAUSE_ACK) != PAUSE_ACK) {
        if (player->player_status & PS_CLOSE) return;
        av_usleep(20*1000);
    }

    if (reconnect) {
        vfilter_graph_free(player);
        if (player->acodec_context  ) { avcodec_close(player->acodec_context); player->acodec_context = NULL; }
        if (player->vcodec_context  ) { avcodec_close(player->vcodec_context); player->vcodec_context = NULL; }
        if (player->avformat_context) { avformat_close_input(&player->avformat_context); }
        av_frame_unref(&player->aframe); player->aframe.pts = -1;
        av_frame_unref(&player->vframe); player->vframe.pts = -1;

        VDEV_COMMON_CTXT *vdev = NULL;
        player_getparam(player, PARAM_VDEV_GET_CONTEXT, &vdev);
        if (vdev) vdev->status |= VDEV_ERASE_BG1;
        player_send_message(player->appdata, MSG_STREAM_DISCONNECT, (uint32_t)player);
        player_prepare(player);
        player_send_message(player->appdata, MSG_STREAM_CONNECTED , (uint32_t)player);
        if (vdev) vdev->status &=~VDEV_ERASE_BG1;
    } else {
        av_seek_frame(player->avformat_context, player->seek_sidx, player->seek_pos, AVSEEK_FLAG_BACKWARD);
        if (player->astream_index != -1) avcodec_flush_buffers(player->acodec_context);
        if (player->vstream_index != -1) avcodec_flush_buffers(player->vcodec_context);
    }

    pktqueue_reset(player->pktqueue); // reset pktqueue
    render_reset  (player->render  ); // reset render

    // make audio & video decoding thread resume
    player->player_status &= ~(PAUSE_REQ|PAUSE_ACK);
}

static DWORD WINAPI av_demux_thread_proc(void *param)
{
    PLAYER   *player = (PLAYER*)param;
    AVPacket *packet = NULL;
    int       retv   = 0;

    // async prepare player
    if (!player->init_params.open_syncmode) {
        retv = player_prepare(player);
        if (retv != 0) goto done;
    }

    while (!(player->player_status & PS_CLOSE)) {
        //++ when player seek ++//
        if (player->player_status & PS_F_SEEK) {
            player->player_status &= ~PS_F_SEEK;
            handle_fseek_or_reconnect(player, 0);
        }
        //-- when player seek --//

        packet = pktqueue_free_dequeue(player->pktqueue);
        if (packet == NULL) { av_usleep(20*1000); continue; }

        retv = av_read_frame(player->avformat_context, packet);
        if (retv < 0) {
            av_packet_unref(packet); // free packet
            if (  player->init_params.auto_reconnect > 0
               && av_gettime_relative() - player->tick_demux > player->init_params.auto_reconnect * 1000) {
                handle_fseek_or_reconnect(player, 1);
            } else {
                pktqueue_free_cancel(player->pktqueue, packet);
                av_usleep(20*1000);
            }
            continue;
        } else {
            player->tick_demux = av_gettime_relative();
        }

        // audio
        if (packet->stream_index == player->astream_index) {
            pktqueue_audio_enqueue(player->pktqueue, packet);
        }

        // video
        if (packet->stream_index == player->vstream_index) {
            pktqueue_video_enqueue(player->pktqueue, packet);
        }

        if (  packet->stream_index != player->astream_index
           && packet->stream_index != player->vstream_index) {
            av_packet_unref(packet); // free packet
            pktqueue_free_cancel(player->pktqueue, packet);
        }
    }

done:
    return 0;
}

static DWORD WINAPI audio_decode_thread_proc(void *param)
{
    PLAYER   *player = (PLAYER*)param;
    AVPacket *packet = NULL;
    int64_t   apts;

    player->aframe.pts = -1;
    while (!(player->player_status & PS_CLOSE))
    {
        //++ when audio decode pause ++//
        if (player->player_status & PS_A_PAUSE) {
            player->player_status |= (PS_A_PAUSE << 16);
            av_usleep(20*1000); continue;
        }
        //-- when audio decode pause --//

        // dequeue audio packet
        packet = pktqueue_audio_dequeue(player->pktqueue);
        if (packet == NULL) {
//          render_audio(player->render, &player->aframe);
            av_usleep(20*1000); continue;
        }

        //++ decode audio packet ++//
        apts = AV_NOPTS_VALUE;
        while (packet->size > 0 && !(player->player_status & (PS_A_PAUSE|PS_CLOSE))) {
            int consumed = 0;
            int gotaudio = 0;

            consumed = avcodec_decode_audio4(player->acodec_context, &player->aframe, &gotaudio, packet);
            if (consumed < 0) {
                av_log(NULL, AV_LOG_WARNING, "an error occurred during decoding audio.\n");
                break;
            }

            if (gotaudio) {
                AVRational tb_sample_rate = { 1, player->acodec_context->sample_rate };
                if (apts == AV_NOPTS_VALUE) {
                    apts  = av_rescale_q(player->aframe.pts, player->astream_timebase, tb_sample_rate);
                } else {
                    apts += player->aframe.nb_samples;
                }
                player->aframe.pts = av_rescale_q(apts, tb_sample_rate, TIMEBASE_MS);
                //++ for seek operation
                if (player->player_status & PS_A_SEEK) {
                    if (player->seek_dest - player->aframe.pts <= player->seek_diff) {
                        player->player_status &= ~PS_A_SEEK;
                    }
                    if ((player->player_status & PS_R_PAUSE) && player->vstream_index == -1) {
                        render_pause(player->render);
                    }
                }
                //-- for seek operation
                if (!(player->player_status & PS_A_SEEK)) render_audio(player->render, &player->aframe);
            }

            packet->data += consumed;
            packet->size -= consumed;
        }
        //-- decode audio packet --//

        // free packet
        av_packet_unref(packet);

        pktqueue_free_enqueue(player->pktqueue, packet);
    }

    av_frame_unref(&player->aframe);
    return 0;
}

static DWORD WINAPI video_decode_thread_proc(void *param)
{
    PLAYER   *player = (PLAYER*)param;
    AVPacket *packet = NULL;

    player->vframe.pts = -1;
    while (!(player->player_status & PS_CLOSE)) {
        //++ when video decode pause ++//
        if (player->player_status & PS_V_PAUSE) {
            player->player_status |= (PS_V_PAUSE << 16);
            av_usleep(20*1000); continue;
        }
        //-- when video decode pause --//

        // dequeue video packet
        packet = pktqueue_video_dequeue(player->pktqueue);
        if (packet == NULL) {
            render_video(player->render, &player->vframe);
            av_usleep(20*1000); continue;
        }

        //++ decode video packet ++//
        while (packet->size > 0 && !(player->player_status & (PS_V_PAUSE|PS_CLOSE))) {
            int consumed = 0;
            int gotvideo = 0;

            consumed = avcodec_decode_video2(player->vcodec_context, &player->vframe, &gotvideo, packet);
            if (consumed < 0) {
                av_log(NULL, AV_LOG_WARNING, "an error occurred during decoding video.\n");
                break;
            }

            if (gotvideo) {
                vfilter_graph_input(player, &player->vframe);
                do {
                    if (vfilter_graph_output(player, &player->vframe) < 0) break;
//                  player->seek_vpts = av_frame_get_best_effort_timestamp(&player->vframe);
                    player->seek_vpts = player->vframe.pkt_dts; // if rtmp has problem, try to use this code
                    player->vframe.pts= av_rescale_q(player->seek_vpts, player->vstream_timebase, TIMEBASE_MS);
                    //++ for seek operation
                    if (player->player_status & PS_V_SEEK) {
                        if (player->seek_dest - player->vframe.pts <= player->seek_diff) {
                            player->player_status &= ~PS_V_SEEK;
                            if (player->player_status & PS_R_PAUSE) {
                                render_pause(player->render);
                            }
                        }
                    }
                    //-- for seek operation
                    if (!(player->player_status & PS_V_SEEK)) render_video(player->render, &player->vframe);
                } while (player->vfilter_graph);
            }

            packet->data += packet->size;
            packet->size -= packet->size;
        }
        //-- decode video packet --//

        // free packet
        av_packet_unref(packet);

        pktqueue_free_enqueue(player->pktqueue, packet);
    }

    av_frame_unref(&player->vframe);
    return 0;
}

// ����ʵ��
void* player_open(char *file, void *appdata, PLAYER_INIT_PARAMS *params)
{
    PLAYER *player = NULL;

    // av register all
    av_register_all();
    avfilter_register_all();
    avformat_network_init();

    // setup log
    g_fp = fopen("fanplayer.log", "wb");
    av_log_set_level   (AV_LOG_ERROR);
    av_log_set_callback(avlog_callback);

    // alloc player context
    player = (PLAYER*)calloc(1, sizeof(PLAYER));
    if (!player) return NULL;

    // create packet queue
    player->pktqueue = pktqueue_create(0);
    if (!player->pktqueue) {
        av_log(NULL, AV_LOG_ERROR, "failed to create packet queue !\n");
        goto error_handler;
    }

    // for player init params
    if (params) {
        memcpy(&player->init_params, params, sizeof(PLAYER_INIT_PARAMS));
    }

    //++ for player_prepare
    strcpy(player->url, file);
#ifdef WIN32
    player->appdata = appdata;
#endif
    //-- for player_prepare

    if (player->init_params.open_syncmode && player_prepare(player) == -1) {
        av_log(NULL, AV_LOG_ERROR, "failed to prepare player !\n");
        goto error_handler;
    }

    // make sure player status paused
    player->player_status  = (PS_A_PAUSE|PS_V_PAUSE|PS_R_PAUSE);
    player->avdemux_thread = CreateThread(NULL, 0, av_demux_thread_proc    , player, 0, NULL);
    player->adecode_thread = CreateThread(NULL, 0, audio_decode_thread_proc, player, 0, NULL);
    player->vdecode_thread = CreateThread(NULL, 0, video_decode_thread_proc, player, 0, NULL);
    return player; // return

error_handler:
    player_close(player);
    return NULL;
}

void player_close(void *hplayer)
{
    if (!hplayer) return;
    PLAYER *player = (PLAYER*)hplayer;

    //++ fix noise sound issue when player_close on android platform
    int vol = -255; player_setparam(player, PARAM_AUDIO_VOLUME, &vol);
    //-- fix noise sound issue when player_close on android platform

    // set init_timeout to 0
    player->init_timeout = 0;

    // set close flag
    player->player_status |= PS_CLOSE;
    render_start(player->render);

    // wait audio/video demuxing thread exit
    WaitForSingleObject(player->avdemux_thread, -1);
    CloseHandle(player->avdemux_thread);

    // wait audio decoding thread exit
    WaitForSingleObject(player->adecode_thread, -1);
    CloseHandle(player->adecode_thread);

    // wait video decoding thread exit
    WaitForSingleObject(player->vdecode_thread, -1);
    CloseHandle(player->vdecode_thread);

    // free avfilter graph
    vfilter_graph_free(player);

    // destroy packet queue
    pktqueue_destroy(player->pktqueue);

    if (player->render          ) render_close (player->render);
    if (player->acodec_context  ) avcodec_close(player->acodec_context);
    if (player->vcodec_context  ) avcodec_close(player->vcodec_context);
    if (player->avformat_context) avformat_close_input(&player->avformat_context);

    free(player);

    // deinit network
    avformat_network_deinit();
    if (g_fp) fclose(g_fp);
}

void player_play(void *hplayer)
{
    if (!hplayer) return;
    PLAYER *player = (PLAYER*)hplayer;
    player->player_status &= PS_CLOSE;
    render_start(player->render);
}

void player_pause(void *hplayer)
{
    if (!hplayer) return;
    PLAYER *player = (PLAYER*)hplayer;
    player->player_status |= PS_R_PAUSE;
    render_pause(player->render);
}

void player_setrect(void *hplayer, int type, int x, int y, int w, int h)
{
    if (!hplayer) return;
    PLAYER *player = (PLAYER*)hplayer;

    //++ if set visual effect rect
    if (type == 1) {
        render_setrect(player->render, type, x, y, w, h);
        return;
    }
    //-- if set visual effect rect

    int vw = player->init_params.video_owidth ;
    int vh = player->init_params.video_oheight;
    int rw = 0, rh = 0;
    if (!vw || !vh) return;

    player->vdrect.left   = x;
    player->vdrect.top    = y;
    player->vdrect.right  = x + w;
    player->vdrect.bottom = y + h;

    switch (player->vdmode)
    {
    case VIDEO_MODE_LETTERBOX:
        if (w * vh < h * vw) { rw = w; rh = rw * vh / vw; }
        else                 { rh = h; rw = rh * vw / vh; }
        break;
    case VIDEO_MODE_STRETCHED: rw = w; rh = h; break;
    }

    if (rw <= 0) rw = 1;
    if (rh <= 0) rh = 1;
    render_setrect(player->render, type, x + (w - rw) / 2, y + (h - rh) / 2, rw, rh);
}

void player_seek(void *hplayer, int64_t ms, int type)
{
    if (!hplayer) return;
    PLAYER    *player = (PLAYER*)hplayer;
    AVRational frate;

    if (player->player_status & (PS_F_SEEK | player->seek_req)) {
        av_log(NULL, AV_LOG_WARNING, "seek busy !\n");
        return;
    }

    switch (type) {
    case SEEK_STEP_FORWARD:
        render_pause(player->render);
        render_setparam(player->render, PARAM_RENDER_STEPFORWARD, NULL);
        return;
    case SEEK_STEP_BACKWARD:
        frate = player->avformat_context->streams[player->vstream_index]->r_frame_rate;
        player->seek_dest = av_rescale_q(player->seek_vpts, player->vstream_timebase, TIMEBASE_MS) - 1000 * frate.den / frate.num - 1;
        player->seek_pos  = player->seek_vpts + av_rescale_q(ms, TIMEBASE_MS, player->vstream_timebase);
        player->seek_diff = 0;
        player->seek_sidx = player->vstream_index;
        player->player_status |= PS_R_PAUSE;
        break;
    default:
        player->seek_dest =  player->start_pts + ms;
        player->seek_pos  = (player->start_pts + ms) * AV_TIME_BASE / 1000;
        player->seek_diff = 100;
        player->seek_sidx = -1;
        break;
    }

    // set PS_F_SEEK flag
    player->player_status |= PS_F_SEEK;

}

void player_setparam(void *hplayer, int id, void *param)
{
    if (!hplayer) return;
    PLAYER *player = (PLAYER*)hplayer;

    switch (id)
    {
    case PARAM_VIDEO_MODE:
        player->vdmode = *(int*)param;
        player_setrect(hplayer, 0,
            player->vdrect.left, player->vdrect.top,
            player->vdrect.right - player->vdrect.left,
            player->vdrect.bottom - player->vdrect.top);
        break;
    default:
        render_setparam(player->render, id, param);
        break;
    }
}

void player_getparam(void *hplayer, int id, void *param)
{
    if (!hplayer || !param) return;
    PLAYER *player = (PLAYER*)hplayer;

    switch (id)
    {
    case PARAM_MEDIA_DURATION:
        if (!player->avformat_context) *(int64_t*)param = 1;
        else *(int64_t*)param = (player->avformat_context->duration * 1000 / AV_TIME_BASE);
        if (*(int64_t*)param <= 0) *(int64_t*)param = 1;
        break;
    case PARAM_MEDIA_POSITION:
        if ((player->player_status & PS_F_SEEK) || (player->player_status & player->seek_req) == player->seek_req) {
            *(int64_t*)param = player->seek_dest - player->start_pts;
        } else {
            int64_t pos = 0; render_getparam(player->render, id, &pos);
            switch (pos) {
            case -1:             *(int64_t*)param = -1; break;
            case AV_NOPTS_VALUE: *(int64_t*)param = player->seek_dest - player->start_pts; break;
            default:             *(int64_t*)param = pos - player->start_pts; break;
            }
        }
        break;
    case PARAM_VIDEO_WIDTH:
        if (!player->vcodec_context) *(int*)param = 0;
        else *(int*)param = player->init_params.video_owidth;
        break;
    case PARAM_VIDEO_HEIGHT:
        if (!player->vcodec_context) *(int*)param = 0;
        else *(int*)param = player->init_params.video_oheight;
        break;
    case PARAM_VIDEO_MODE:
        *(int*)param = player->vdmode;
        break;
    case PARAM_RENDER_GET_CONTEXT:
        *(void**)param = player->render;
        break;
    case PARAM_PLAYER_INIT_PARAMS:
        memcpy(param, &player->init_params, sizeof(PLAYER_INIT_PARAMS));
        break;
    default:
        render_getparam(player->render, id, param);
        break;
    }
}

void player_send_message(void *extra, int32_t msg, uint32_t param) {
#ifdef WIN32
    PostMessage((HWND)extra, MSG_FANPLAYER, msg, (LPARAM)param);
#endif
}

//++ load player init params from string
static char* parse_params(const char *str, const char *key, char *val, int len)
{
    char *p = (char*)strstr(str, key);
    int   i;

    if (!p) return NULL;
    p += strlen(key);
    if (*p == '\0') return NULL;

    while (1) {
        if (*p != ' ' && *p != '=' && *p != ':') break;
        else p++;
    }

    for (i=0; i<len; i++) {
        if (*p == '\\') {
            p++;
        } else if (*p == ';' || *p == '\n' || *p == '\0') {
            break;
        }
        val[i] = *p++;
    }
    val[i] = val[len-1] = '\0';
    return val;
}

void player_load_params(PLAYER_INIT_PARAMS *params, char *str)
{
    char value[16];
    params->video_stream_cur    = atoi(parse_params(str, "video_stream_cur"   , value, sizeof(value)) ? value : "0");
    params->video_thread_count  = atoi(parse_params(str, "video_thread_count" , value, sizeof(value)) ? value : "0");
    params->video_hwaccel       = atoi(parse_params(str, "video_hwaccel"      , value, sizeof(value)) ? value : "0");
    params->video_deinterlace   = atoi(parse_params(str, "video_deinterlace"  , value, sizeof(value)) ? value : "0");
    params->video_rotate        = atoi(parse_params(str, "video_rotate"       , value, sizeof(value)) ? value : "0");
    params->audio_stream_cur    = atoi(parse_params(str, "audio_stream_cur"   , value, sizeof(value)) ? value : "0");
    params->subtitle_stream_cur = atoi(parse_params(str, "subtitle_stream_cur", value, sizeof(value)) ? value : "0");
    params->vdev_render_type    = atoi(parse_params(str, "vdev_render_type"   , value, sizeof(value)) ? value : "0");
    params->adev_render_type    = atoi(parse_params(str, "adev_render_type"   , value, sizeof(value)) ? value : "0");
    params->init_timeout        = atoi(parse_params(str, "init_timeout"       , value, sizeof(value)) ? value : "0");
    params->open_syncmode       = atoi(parse_params(str, "open_syncmode"      , value, sizeof(value)) ? value : "0");
    params->auto_reconnect      = atoi(parse_params(str, "auto_reconnect"     , value, sizeof(value)) ? value : "0");
    parse_params(str, "filter_string", params->filter_string, sizeof(params->filter_string));
}
//-- load player init params from string