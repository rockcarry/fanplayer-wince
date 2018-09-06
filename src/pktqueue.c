// 包含头文件
#include "pktqueue.h"

// 内部常量定义
#define DEF_PKT_QUEUE_SIZE 256 // important!! size must be a power of 2

// 内部类型定义
typedef struct {
    int        fsize;
    int        asize;
    int        vsize;
    int        fhead;
    int        ftail;
    int        ahead;
    int        atail;
    int        vhead;
    int        vtail;
    HANDLE     fsem;
    HANDLE     asem;
    HANDLE     vsem;
    AVPacket  *bpkts; // packet buffers
    AVPacket **fpkts; // free packets
    AVPacket **apkts; // audio packets
    AVPacket **vpkts; // video packets
    CRITICAL_SECTION lock;
} PKTQUEUE;

// 函数实现
void* pktqueue_create(int size)
{
    int i;
    PKTQUEUE *ppq = (PKTQUEUE*)calloc(1, sizeof(PKTQUEUE));
    if (!ppq) {
        av_log(NULL, AV_LOG_ERROR, "failed to allocate pktqueue context !\n");
        exit(0);
    }

    ppq->fsize = size ? size : DEF_PKT_QUEUE_SIZE;
    ppq->asize = ppq->vsize = ppq->fsize;

    // alloc buffer & semaphore
    ppq->bpkts = (AVPacket* )calloc(ppq->fsize, sizeof(AVPacket ));
    ppq->fpkts = (AVPacket**)calloc(ppq->fsize, sizeof(AVPacket*));
    ppq->apkts = (AVPacket**)calloc(ppq->asize, sizeof(AVPacket*));
    ppq->vpkts = (AVPacket**)calloc(ppq->vsize, sizeof(AVPacket*));
    ppq->fsem = CreateSemaphore(NULL, ppq->fsize, ppq->fsize, NULL);
    ppq->asem = CreateSemaphore(NULL, 0         , ppq->fsize, NULL);
    ppq->vsem = CreateSemaphore(NULL, 0         , ppq->fsize, NULL);
    InitializeCriticalSection(&ppq->lock);

    // check invalid
    if (!ppq->bpkts || !ppq->fpkts || !ppq->apkts || !ppq->vpkts) {
        av_log(NULL, AV_LOG_ERROR, "failed to allocate resources for pktqueue !\n");
        exit(0);
    }

    // init fpkts
    for (i=0; i<ppq->fsize; i++) {
        ppq->fpkts[i] = &ppq->bpkts[i];
    }

    return ppq;
}

void pktqueue_destroy(void *ctxt)
{
    PKTQUEUE *ppq = (PKTQUEUE*)ctxt;

    // reset to unref packets
    pktqueue_reset(ctxt);

    // close
    CloseHandle(ppq->fsem);
    CloseHandle(ppq->asem);
    CloseHandle(ppq->vsem);
    DeleteCriticalSection(&ppq->lock);

    // free
    free(ppq->bpkts);
    free(ppq->fpkts);
    free(ppq->apkts);
    free(ppq->vpkts);
    free(ppq);
}

void pktqueue_reset(void *ctxt)
{
    PKTQUEUE *ppq    = (PKTQUEUE*)ctxt;
    AVPacket *packet = NULL;

    while (NULL != (packet = pktqueue_audio_dequeue(ctxt))) {
        av_packet_unref(packet);
        pktqueue_free_enqueue(ctxt, packet);
    }

    while (NULL != (packet = pktqueue_video_dequeue(ctxt))) {
        av_packet_unref(packet);
        pktqueue_free_enqueue(ctxt, packet);
    }

    ppq->fhead = ppq->ftail = 0;
    ppq->ahead = ppq->atail = 0;
    ppq->vhead = ppq->vtail = 0;
}

void pktqueue_free_enqueue(void *ctxt, AVPacket *pkt)
{
    PKTQUEUE *ppq = (PKTQUEUE*)ctxt;
    EnterCriticalSection(&ppq->lock);
    ppq->fpkts[ppq->ftail++ & (ppq->fsize - 1)] = pkt;
    LeaveCriticalSection(&ppq->lock);
    ReleaseSemaphore(ppq->fsem, 1, NULL);
}

AVPacket* pktqueue_free_dequeue(void *ctxt)
{
    PKTQUEUE *ppq = (PKTQUEUE*)ctxt;
    if (WAIT_OBJECT_0 != WaitForSingleObject(ppq->fsem, 100)) return NULL;
    return ppq->fpkts[ppq->fhead++ & (ppq->fsize - 1)];
}

void pktqueue_free_cancel(void *ctxt, AVPacket *pkt)
{
    PKTQUEUE *ppq = (PKTQUEUE*)ctxt;
    EnterCriticalSection(&ppq->lock);
    ppq->fpkts[ppq->ftail++ & (ppq->fsize - 1)] = pkt;
    LeaveCriticalSection(&ppq->lock);
    ReleaseSemaphore(ppq->fsem, 1, NULL);
}

void pktqueue_audio_enqueue(void *ctxt, AVPacket *pkt)
{
    PKTQUEUE *ppq = (PKTQUEUE*)ctxt;
    ppq->apkts[ppq->atail++ & (ppq->asize - 1)] = pkt;
    ReleaseSemaphore(ppq->asem, 1, NULL);
}

AVPacket* pktqueue_audio_dequeue(void *ctxt)
{
    PKTQUEUE *ppq = (PKTQUEUE*)ctxt;
    if (WAIT_OBJECT_0 != WaitForSingleObject(ppq->asem, 100)) return NULL;
    return ppq->apkts[ppq->ahead++ & (ppq->asize - 1)];
}

void pktqueue_video_enqueue(void *ctxt, AVPacket *pkt)
{
    PKTQUEUE *ppq = (PKTQUEUE*)ctxt;
    ppq->vpkts[ppq->vtail++ & (ppq->vsize - 1)] = pkt;
    ReleaseSemaphore(ppq->vsem, 1, NULL);
}

AVPacket* pktqueue_video_dequeue(void *ctxt)
{
    PKTQUEUE *ppq = (PKTQUEUE*)ctxt;
    if (WAIT_OBJECT_0 != WaitForSingleObject(ppq->vsem, 100)) return NULL;
    return ppq->vpkts[ppq->vhead++ & (ppq->vsize - 1)];
}




