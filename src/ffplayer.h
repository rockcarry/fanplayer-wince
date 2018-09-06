#ifndef __FANPLAYER_FFPLAYER_H__
#define __FANPLAYER_FFPLAYER_H__

// ����ͷ�ļ�
#include "stdefine.h"

#ifdef __cplusplus
extern "C" {
#endif

// ��������
// message
#define MSG_FANPLAYER         (WM_APP + 1)
#define MSG_OPEN_DONE         (('O' << 24) | ('P' << 16) | ('E' << 8) | ('N' << 0))
#define MSG_OPEN_FAILED       (('F' << 24) | ('A' << 16) | ('I' << 8) | ('L' << 0))
#define MSG_PLAY_COMPLETED    (('E' << 24) | ('N' << 16) | ('D' << 8) | (' ' << 0))
#define MSG_STREAM_CONNECTED  (('C' << 24) | ('N' << 16) | ('C' << 8) | ('T' << 0))
#define MSG_STREAM_DISCONNECT (('D' << 24) | ('I' << 16) | ('S' << 8) | ('C' << 0))

// adev render type
enum {
    ADEV_RENDER_TYPE_WAVEOUT,
    ADEV_RENDER_TYPE_MAX_NUM,
};

// vdev render type
enum {
    VDEV_RENDER_TYPE_GDI,
    VDEV_RENDER_TYPE_MAX_NUM,
};

// render mode
enum {
    VIDEO_MODE_LETTERBOX,
    VIDEO_MODE_STRETCHED,
    VIDEO_MODE_MAX_NUM,
};

// visual effect
enum {
    VISUAL_EFFECT_DISABLE,
    VISUAL_EFFECT_WAVEFORM,
    VISUAL_EFFECT_SPECTRUM,
    VISUAL_EFFECT_MAX_NUM,
};

// seek flags
enum {
    SEEK_STEP_FORWARD = 1,
    SEEK_STEP_BACKWARD,
};

// param
enum {
    //++ public
    // duration & position
    PARAM_MEDIA_DURATION = 0x1000,
    PARAM_MEDIA_POSITION,

    // media detail info
    PARAM_VIDEO_WIDTH,
    PARAM_VIDEO_HEIGHT,

    // video display mode
    PARAM_VIDEO_MODE,

    // audio volume control
    PARAM_AUDIO_VOLUME,

    // playback speed control
    PARAM_PLAY_SPEED_VALUE,
    PARAM_PLAY_SPEED_TYPE,

    // visual effect mode
    PARAM_VISUAL_EFFECT,

    // audio/video sync diff
    PARAM_AVSYNC_TIME_DIFF,

    // get player init params
    PARAM_PLAYER_INIT_PARAMS,
    //-- public

    //++ for adev
    PARAM_ADEV_GET_CONTEXT = 0x2000,
    //-- for adev

    //++ for vdev
    PARAM_VDEV_GET_CONTEXT = 0x3000,
    //-- for vdev

    //++ for render
    PARAM_RENDER_GET_CONTEXT = 0x4000,
    PARAM_RENDER_STEPFORWARD,
    //-- for render
};

// ��ʼ������˵��
// PLAYER_INIT_PARAMS Ϊ��������ʼ���������� player_open ʱ���룬���ɻ����Ƶ�ļ��򿪺��һЩ������Ϣ
// player_open ����ֻ���ȡ����ĳ�ʼ�����������ݣ�������ı������ݡ���Ҫ��ȡ��Ƶ�ļ��򿪺���²���ֵ
// ��Ҫͨ�� player_getparam(PARAM_PLAYER_INIT_PARAMS, &params); �����Ľӿ�
// �ṹ���� r ��ʾ����ֻ����w ��ʾ����ֻд��wr ��ʾ���������ã��������Ƿ�ɹ����� player_open ���ȡ�ж�
// ע�⣺�����첽�򿪷�ʽ��open_syncmode = 0��ʱ��player_open �������غ󣬻�ȡ����һЩ�������ܲ�������Ч
// �ģ����� video_vwidth, video_vheight������Ҫ�ȵ����յ� MSG_OPEN_DONE ��Ϣ����ܻ�ȡ��Ч�Ĳ�����ͬ����
// ����ʽ�򲻻���������⣬��Ϊ player_open ��ɺ��Ѿ�������ȫ���ĳ�ʼ��������
typedef struct {
    int  video_vwidth;             // wr video actual width
    int  video_vheight;            // wr video actual height
    int  video_owidth;             // r  video output width  (after rotate)
    int  video_oheight;            // r  video output height (after rotate)
    int  video_frame_rate;         // wr ��Ƶ֡��
    int  video_stream_total;       // r  ��Ƶ������
    int  video_stream_cur;         // wr ��ǰ��Ƶ��
    int  video_thread_count;       // wr ��Ƶ�����߳���
    int  video_hwaccel;            // wr ��ƵӲ����ʹ��
    int  video_deinterlace;        // wr ��Ƶ������ʹ��
    int  video_rotate;             // wr ��Ƶ��ת�Ƕ�

    int  audio_channels;           // r  ��Ƶͨ����
    int  audio_sample_rate;        // r  ��Ƶ������
    int  audio_stream_total;       // r  ��Ƶ������
    int  audio_stream_cur;         // wr ��ǰ��Ƶ��

    int  subtitle_stream_total;    // r  ��Ļ������
    int  subtitle_stream_cur;      // wr ��ǰ��Ļ��

    int  vdev_render_type;         // w  vdev ����
    int  adev_render_type;         // w  adev ����

    int  init_timeout;             // w  ��������ʼ����ʱ����λ ms����������ý��ʱ����������ֹ����
    int  open_syncmode;            // w  ��������ͬ����ʽ�򿪣����� player_open ���ȴ���������ʼ���ɹ�
    int  auto_reconnect;           // w  ������ý��ʱ�Զ������ĳ�ʱʱ�䣬����Ϊ��λ

    char filter_string[256];       // w  �Զ���� video filter string
} PLAYER_INIT_PARAMS;
// video_stream_cur �� audio_stream_cur �������������������Ϊ -1 ���Խ�ֹ��Ӧ�Ľ��붯��
// Ӧ�ó�����������Ƶʱ�������˵���̨��������ֻ�������������Խ� video_stream_cur ����Ϊ -1
//           ������������ֻ������Ƶ����������Ƶ���ɼ��� cpu ��ʹ����


// ��������
void* player_open    (TCHAR *file, void *appdata, PLAYER_INIT_PARAMS *params);
void  player_close   (void *hplayer);
void  player_play    (void *hplayer);
void  player_pause   (void *hplayer);
void  player_seek    (void *hplayer, int64_t ms, int type);
void  player_setrect (void *hplayer, int type, int x, int y, int w, int h); // type: 0 - video rect, 1 - visual effect rect
void  player_setparam(void *hplayer, int id, void *param);
void  player_getparam(void *hplayer, int id, void *param);

// internal helper function
void  player_send_message(void *extra, int32_t msg, uint32_t param);
void  player_load_params (PLAYER_INIT_PARAMS *params, char *str);

// ����˵��
/*
player_open     ����һ�� player ����
    file        - �ļ�·����������������ý��� URL��
    appdata     - win32 ƽ̨���봰�ھ����android ƽ̨���� MediaPlayer �����
    params      - ��������ʼ������
    ����ֵ      - void* ָ�����ͣ�ָ�� player ����

player_close    �رղ�����
    hplayer     - ָ�� player_open ���ص� player ����

player_play     ��ʼ����
    hplayer     - ָ�� player_open ���ص� player ����

player_pause    ��ͣ����
    hplayer     - ָ�� player_open ���ص� player ����

player_seek     ��ת��ָ��λ��
    hplayer     - ָ�� player_open ���ص� player ����
    ms          - ָ��λ�ã��Ժ���Ϊ��λ
    type        - ָ�����ͣ�0 / SEEK_STEP_FORWARD / SEEK_STEP_BACKWARD
    ��� type Ϊ 0 ���������� seek ������ms ָ������ seek ����λ�ã�����Ϊ��λ
    ��� type Ϊ SEEK_STEP_FORWARD����ᵥ����ǰ����һ֡Ȼ����ͣ
    ��� type Ϊ SEEK_STEP_BACKWARD����ᵥ�����˲���һ֡Ȼ����ͣ
    ʹ�� SEEK_STEP_BACKWARD �� seek ��ʽʱ����Ҫ���� ms ��������������ĺ�����
    ����������ʱ�䷶Χ��ͨ������´��� -1 ���ɡ����Ƕ���һЩ������Ƶ������ -1
    ����û����ȷִ�л��˲��������Գ����޸�Ϊ -500 �������

player_setrect  ������ʾ������������ʾ������Ƶ��ʾ�����Ӿ�Ч����ʾ��
    hplayer     - ָ�� player_open ���ص� player ����
    type        - ָ����������  0 - video rect, 1 - visual effect rect
    x,y,w,h     - ָ����ʾ����

player_setparam ���ò���
    hplayer     - ָ�� player_open ���ص� player ����
    id          - ���� id
    param       - ����ָ��

player_getparam ��ȡ����
    hplayer     - ָ�� player_open ���ص� player ����
    id          - ���� id
    param       - ����ָ��
 */

// ��̬����˵��
/*
PARAM_MEDIA_DURATION �� PARAM_MEDIA_POSITION
���ڻ�ȡ��ý���ļ����ܳ��Ⱥ͵�ǰ����λ�ã�����Ϊ��λ��
LONGLONG total = 1, pos = 0;
player_getparam(g_hplayer, PARAM_MEDIA_DURATION, &total);
player_getparam(g_hplayer, PARAM_MEDIA_POSITION, &pos  );

PARAM_VIDEO_WIDTH �� PARAM_VIDEO_HEIGHT
���ڻ�ȡ��ý���ļ�����Ƶ��Ⱥ͸߶ȣ�����Ϊ��λ��
int vw = 0, vh = 0;
player_getparam(g_hplayer, PARAM_VIDEO_WIDTH , &vw);
player_getparam(g_hplayer, PARAM_VIDEO_HEIGHT, &vh);

PARAM_VIDEO_MODE
���ڻ�ȡ��������Ƶ��ʾ��ʽ�������ַ�ʽ��ѡ��
    1. VIDEO_MODE_LETTERBOX - ���������ŵ���ʾ����
    2. VIDEO_MODE_STRETCHED - ���쵽��ʾ����
��ע����Ƶ��ʾ������ player_setrect �����趨��
int mode = 0;
player_getparam(g_hplayer, PARAM_VIDEO_MODE, &mode);
mode = VIDEO_MODE_STRETCHED;
player_setparam(g_hplayer, PARAM_VIDEO_MODE, &mode);

PARAM_AUDIO_VOLUME
�������ò�����������ͬ��ϵͳ������fanplayer �ڲ�����һ�� -30dB �� +12dB ������������Ƶ�Ԫ
������Χ��[-182, 73]��-182 ��Ӧ -30dB��73 ��Ӧ +12dB
����ֵ  ��0 ��Ӧ 0dB ���棬-255 ��Ӧ������+255 ��Ӧ�������
int volume = -0;
player_setparam(g_hplayer, PARAM_AUDIO_VOLUME, &volume);

PARAM_PLAY_SPEED_VALUE
�������ò����ٶȣ�fanplayer ֧�ֱ��ٲ���
int speed = 150;
player_setparam(g_hplayer, PARAM_PLAY_SPEED_VALUE, &speed);
���� speed Ϊ�ٷֱ��ٶȣ�150 ��ʾ�� 150% ���в���
�ٶ�û�����޺����ޣ�����Ϊ 0 û�����壬�ڲ��ᴦ��Ϊ 1%
�����ٶȵ�ʵ�����ޣ��ɴ������Ĵ��������������������������������Ż���ֿ�������

PARAM_PLAY_SPEED_TYPE
�������ñ��ٲ��ŵı������ͣ�Ŀǰ֧���������ͣ�
0 - ���ٻ��������� swresample �ı��������ʵ�ֱ���
1 - ���ٲ���������� soundtouch ��Ƶ�����ʵ�ֱ���
int type = 1;
player_setparam(g_hplayer, PARAM_PLAY_SPEED_TYPE, &type);

PARAM_AVSYNC_TIME_DIFF
�������� audio �� video ��ʱ��ͬ����ֵ������Ϊ��λ��
int diff = 100;
player_setparam(g_hplayer, PARAM_AVSYNC_TIME_DIFF, &diff);
����Ϊ 100 ����Ƶ������Ƶ�� 100ms������Ϊ -100 ���� 100ms

PARAM_PLAYER_INIT_PARAMS
���ڻ�ȡ��������ʼ��������ʹ�÷�����
PLAYER_INIT_PARAMS params;
player_getparam(g_hplayer, PARAM_PLAYER_INIT_PARAMS, &params);
�������������������ʼ������˵��

���еĲ��������ǿ��� get �ģ������������еĲ��������� set����Ϊ��Щ������ֻ���ġ�


// �� avdevice �����豸��֧��
windows ƽ̨��֧�� dshow gdigrab vfwcap ���������豸
�򿪷�ʽ������
player_open("vfwcap", hwnd, 0, 0, NULL); // ���� vfw ��ʽ������ͷ����Ԥ��
player_open("gdigrab://desktop", hwnd, 0, 0, NULL); // ���� gdigrab ��ʽ���������Ԥ��
player_open("dshow://video=Integrated Camera", hwnd, 0, 0, NULL); // ���� dshow ��ʽ�� Integrated Camera

 */

#ifdef __cplusplus
}
#endif

#endif



