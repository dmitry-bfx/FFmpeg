#ifndef AVCODEC_PRORES_APPLE_H
#define AVCODEC_PRORES_APPLE_H

#include "libavutil/frame.h"

typedef void *bridge_ctx;

// FFmpeg context -----------------------------------------------------
typedef struct ProResAppleCtx {
    bridge_ctx dec;
    AVFrame *frame;
}ProResAppleCtx;


#endif AVCODEC_PRORES_APPLE_H
