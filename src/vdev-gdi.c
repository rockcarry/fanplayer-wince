// 包含头文件
#include <tchar.h>
#include "vdev.h"
#include "libavformat/avformat.h"

// 内部常量定义
#define DEF_VDEV_BUF_NUM  3

// 内部类型定义
typedef struct {
    // common members
    VDEV_COMMON_MEMBERS

    HDC      hdcsrc;
    HDC      hdcdst;
    HBITMAP *hbitmaps;
    BYTE   **pbmpbufs;
    HFONT    hfont;
} VDEVGDICTXT;

// 内部函数实现
static DWORD WINAPI video_render_thread_proc(void *param)
{
    VDEVGDICTXT *c = (VDEVGDICTXT*)param;

    while (1) {
        WaitForSingleObject(c->semr, -1);
        if (c->status & VDEV_CLOSE) break;

        if (vdev_refresh_background(c) && c->ppts[c->head] != -1) {
            SelectObject(c->hdcsrc, c->hbitmaps[c->head]);
            BitBlt(c->hdcdst, c->x, c->y, c->w, c->h, c->hdcsrc, 0, 0, SRCCOPY);
            c->vpts = c->ppts[c->head];
        }

        av_log(NULL, AV_LOG_DEBUG, "vpts: %lld\n", c->vpts);
        if (++c->head == c->bufnum) c->head = 0;
        ReleaseSemaphore(c->semw, 1, NULL);

        // handle av-sync & frame rate & complete
        vdev_avsync_and_complete(c);
    }

    return 0;
}

static void vdev_gdi_lock(void *ctxt, uint8_t *buffer[8], int linesize[8])
{
    VDEVGDICTXT *c = (VDEVGDICTXT*)ctxt;

    WaitForSingleObject(c->semw, -1);

    BITMAP bitmap;
    int bmpw = 0;
    int bmph = 0;
    if (c->hbitmaps[c->tail]) {
        GetObject(c->hbitmaps[c->tail], sizeof(BITMAP), &bitmap);
        bmpw = bitmap.bmWidth ;
        bmph = bitmap.bmHeight;
    }

    if (bmpw != c->w || bmph != c->h) {
        c->sw = c->w; c->sh = c->h;
        if (c->hbitmaps[c->tail]) {
            DeleteObject(c->hbitmaps[c->tail]);
        }

        BYTE        buffer[sizeof(BITMAPINFOHEADER) + sizeof(DWORD) * 256] = {0};
        BITMAPINFO *bmpinfo = (BITMAPINFO*)buffer;
        bmpinfo->bmiHeader.biSize        =  sizeof(BITMAPINFOHEADER);
        bmpinfo->bmiHeader.biWidth       =  c->w;
        bmpinfo->bmiHeader.biHeight      = -c->h;
        bmpinfo->bmiHeader.biPlanes      =  1;
        bmpinfo->bmiHeader.biBitCount    =  16;
        bmpinfo->bmiHeader.biCompression =  BI_BITFIELDS;
        ((DWORD*)bmpinfo->bmiColors)[0]  = 0xF800;
        ((DWORD*)bmpinfo->bmiColors)[1]  = 0x07E0;
        ((DWORD*)bmpinfo->bmiColors)[2]  = 0x001F;
        c->hbitmaps[c->tail] = CreateDIBSection(c->hdcsrc, bmpinfo, DIB_RGB_COLORS,
                                        (void**)&c->pbmpbufs[c->tail], NULL, 0);
        GetObject(c->hbitmaps[c->tail], sizeof(BITMAP), &bitmap);
    }

    if (buffer  ) buffer[0]   = c->pbmpbufs[c->tail];
    if (linesize) linesize[0] = bitmap.bmWidthBytes ;
}

static void vdev_gdi_unlock(void *ctxt, int64_t pts)
{
    VDEVGDICTXT *c = (VDEVGDICTXT*)ctxt;
    c->ppts[c->tail] = pts;
    if (++c->tail == c->bufnum) c->tail = 0;
    ReleaseSemaphore(c->semr, 1, NULL);
}

static void vdev_gdi_setrect(void *ctxt, int x, int y, int w, int h)
{
    VDEV_COMMON_CTXT *c = (VDEV_COMMON_CTXT*)ctxt;
    c->sw = w; c->sh = h;
}

static void vdev_gdi_destroy(void *ctxt)
{
    int i;
    VDEVGDICTXT *c = (VDEVGDICTXT*)ctxt;

    // make visual effect & rendering thread safely exit
    c->status = VDEV_CLOSE;
    ReleaseSemaphore(c->semr, 1, NULL);
    WaitForSingleObject(c->thread, -1);
    CloseHandle(c->thread);

    //++ for video
    DeleteDC (c->hdcsrc);
    ReleaseDC((HWND)c->surface, c->hdcdst);
    for (i=0; i<c->bufnum; i++) {
        if (c->hbitmaps[i]) {
            DeleteObject(c->hbitmaps[i]);
        }
    }
    //-- for video

    // delete font
    DeleteObject(c->hfont);

    // close semaphore
    CloseHandle(c->semr);
    CloseHandle(c->semw);

    // free memory
    free(c->ppts    );
    free(c->hbitmaps);
    free(c->pbmpbufs);
    free(c);
}

// 接口函数实现
void* vdev_gdi_create(void *surface, int bufnum, int w, int h, int frate)
{
    VDEVGDICTXT *ctxt = (VDEVGDICTXT*)calloc(1, sizeof(VDEVGDICTXT));
    if (!ctxt) {
        av_log(NULL, AV_LOG_ERROR, "failed to allocate gdi vdev context !\n");
        exit(0);
    }

    // init vdev context
    bufnum          = bufnum ? bufnum : DEF_VDEV_BUF_NUM;
    ctxt->surface   = surface;
    ctxt->bufnum    = bufnum;
    ctxt->pixfmt    = AV_PIX_FMT_RGB565;
    ctxt->w         = w;
    ctxt->h         = h;
    ctxt->sw        = w;
    ctxt->sh        = h;
    ctxt->tickframe = 1000 / frate;
    ctxt->ticksleep = ctxt->tickframe;
    ctxt->apts      = -1;
    ctxt->vpts      = -1;
    ctxt->lock      = vdev_gdi_lock;
    ctxt->unlock    = vdev_gdi_unlock;
    ctxt->setrect   = vdev_gdi_setrect;
    ctxt->destroy   = vdev_gdi_destroy;

    // alloc buffer & semaphore
    ctxt->ppts     = (int64_t*)calloc(bufnum, sizeof(int64_t));
    ctxt->hbitmaps = (HBITMAP*)calloc(bufnum, sizeof(HBITMAP));
    ctxt->pbmpbufs = (BYTE**  )calloc(bufnum, sizeof(BYTE*  ));

    // create semaphore
    ctxt->semr = CreateSemaphore(NULL, 0     , bufnum, NULL);
    ctxt->semw = CreateSemaphore(NULL, bufnum, bufnum, NULL);

    ctxt->hdcdst = GetDC((HWND)ctxt->surface);
    ctxt->hdcsrc = CreateCompatibleDC(ctxt->hdcdst);
    if (!ctxt->ppts || !ctxt->hbitmaps || !ctxt->pbmpbufs || !ctxt->semr || !ctxt->semw || !ctxt->hdcdst || !ctxt->hdcsrc) {
        av_log(NULL, AV_LOG_ERROR, "failed to allocate resources for vdev-gdi !\n");
        exit(0);
    }
    SetBkMode(ctxt->hdcsrc, TRANSPARENT);

    // create video rendering thread
    ctxt->thread = CreateThread(NULL, 0, video_render_thread_proc, ctxt, 0, NULL);
    return ctxt;
}
