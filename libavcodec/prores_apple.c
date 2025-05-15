// prores_apple_wrapper.c – FFmpeg‑side wrapper
// -------------------------------------------------------------
// Exposes ff_prores_apple_decoder that forwards every call to a
// separate bridge library (libprores_bridge.so / prores_bridge.dll).
// FFmpeg therefore never links against Apple’s proprietary SDK.
// -------------------------------------------------------------
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "libavutil/mem.h"
#include "libavutil/internal.h"
#include "libavutil/pixfmt.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/codec_internal.h"

#if defined(_WIN32)
    #include <windows.h>
    #define LIBBRIDGE_NAME  "prores_bridge.dll"
    static HMODULE bridge_handle = NULL;
    #define LOAD_SYM(h, s)  GetProcAddress(h, s)
    #define OPEN_LIB(p)     LoadLibraryA(p)
    #define CLOSE_LIB(h)    FreeLibrary(h)
#else
    #include <dlfcn.h>
    #define LIBBRIDGE_NAME  "libprores_bridge.so"
    static void *bridge_handle = NULL;
    #define LOAD_SYM(h, s)  dlsym(h, s)
    #define OPEN_LIB(p)     dlopen(p, RTLD_LAZY | RTLD_LOCAL)
    #define CLOSE_LIB(h)    dlclose(h)
#endif

// Bridge‑library public interface (keep in sync with prores_bridge.c)
typedef void *bridge_ctx;
typedef bridge_ctx (*bridge_open_t)(uint32_t flags);
typedef void       (*bridge_close_t)(bridge_ctx ctx);
typedef int        (*bridge_decode_t)(bridge_ctx ctx,
                                      const void *src, size_t src_size,
                                      int w, int h,
                                      uint8_t **out_buf, int *out_rowbytes);
typedef void       (*bridge_free_buf_t)(uint8_t *buf);

static bridge_open_t     bridge_open_p      = NULL;
static bridge_close_t    bridge_close_p     = NULL;
static bridge_decode_t   bridge_decode_p    = NULL;
static bridge_free_buf_t bridge_free_buf_p  = NULL;

#if defined(WIN32)
#define DEBUG_PRINT(fmt, ...) \
{\
    char buffer[1000]; \
    snprintf(buffer, 1000, "[prores_wrapper] " fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(buffer); \
}
#else
#define DEBUG_PRINT(fmt, ...)  fprintf(stderr, "[prores_wrapper] " fmt "\n", ##__VA_ARGS__)
#endif

static int load_bridge_library(void)
{
#if defined(WIN32)
    const char *path = NULL;

    char override[32000] = {0};
    GetEnvironmentVariableA("PRORES_BRIDGE_PATH", override, 32000);
    if( strlen(override) > 0 )
      path = override;
    else
      path = LIBBRIDGE_NAME;
#else
    const char *override = getenv("PRORES_BRIDGE_PATH");
    const char *path     = override ? override : LIBBRIDGE_NAME;
#endif

    DEBUG_PRINT("Loading bridge library: %s", path);
    bridge_handle = OPEN_LIB(path);
    if (!bridge_handle) {
        DEBUG_PRINT("Failed to open bridge library");
        return AVERROR_EXTERNAL;
    }

    bridge_open_p      = (bridge_open_t)     LOAD_SYM(bridge_handle, "bridge_open_decoder");
    bridge_close_p     = (bridge_close_t)    LOAD_SYM(bridge_handle, "bridge_close_decoder");
    bridge_decode_p    = (bridge_decode_t)   LOAD_SYM(bridge_handle, "bridge_decode_frame");
    bridge_free_buf_p  = (bridge_free_buf_t) LOAD_SYM(bridge_handle, "bridge_free_buffer");

    if (!bridge_open_p || !bridge_close_p || !bridge_decode_p || !bridge_free_buf_p) {
        DEBUG_PRINT("Missing symbol(s) in bridge library");
        CLOSE_LIB(bridge_handle);
        bridge_handle = NULL;
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static void unload_bridge_library(void)
{
    if (bridge_handle) {
        DEBUG_PRINT("Unloading bridge library");
        CLOSE_LIB(bridge_handle);
        bridge_handle = NULL;
    }
}

// v210 → planar 10‑bit unpack helpers --------------------------------
static inline void unpack_block(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3,
                                uint16_t *y, uint16_t *u, uint16_t *v)
{
    y[0] = (w0 >> 10) & 0x3FF;  y[1] = (w1 >> 0) & 0x3FF;   y[2] = (w1 >> 20) & 0x3FF;
    y[3] = (w2 >> 10) & 0x3FF;  y[4] = (w3 >> 0) & 0x3FF;   y[5] = (w3 >> 20) & 0x3FF;
    u[0] =  w0 & 0x3FF;         u[1] = (w1 >> 10) & 0x3FF;  u[2] = (w2 >> 20) & 0x3FF;
    v[0] = (w0 >> 20) & 0x3FF;  v[1] =  w2 & 0x3FF;         v[2] = (w3 >> 10) & 0x3FF;
}

static void unpack_v210_line(const uint8_t *src, uint16_t *dst_y,
                             uint16_t *dst_u, uint16_t *dst_v, int width)
{
    const uint32_t *p = (const uint32_t *)src;
    int blocks        = (width + 5) / 6;
    for (int i = 0; i < blocks; ++i) {
        uint32_t w0 = *p++, w1 = *p++, w2 = *p++, w3 = *p++;
        unpack_block(w0, w1, w2, w3, dst_y, dst_u, dst_v);
        dst_y += 6; dst_u += 3; dst_v += 3;
    }
}

// FFmpeg context -----------------------------------------------------
typedef struct ProResAppleCtx { bridge_ctx dec; } ProResAppleCtx;

static av_cold int prores_apple_init(AVCodecContext *avctx)
{
    if (load_bridge_library() < 0) return AVERROR_EXTERNAL;

    ProResAppleCtx *ctx = av_mallocz(sizeof(*ctx));
    if (!ctx) return AVERROR(ENOMEM);

    ctx->dec = bridge_open_p(0);
    if (!ctx->dec) {
        av_free(ctx); unload_bridge_library();
        return AVERROR_EXTERNAL;
    }

    avctx->priv_data = ctx;
    avctx->pix_fmt   = AV_PIX_FMT_YUV422P10;
    return 0;
}

static av_cold int prores_apple_close(AVCodecContext *avctx)
{
    ProResAppleCtx *ctx = avctx->priv_data;
    if (ctx) {
        if (ctx->dec) bridge_close_p(ctx->dec);
        av_freep(&avctx->priv_data);
    }
    unload_bridge_library();
    return 0;
}

static int prores_apple_decode_frame(AVCodecContext *avctx, AVFrame *data,
                                     int *got_frame, AVPacket *pkt)
{
    if (!pkt || pkt->size <= 0) return AVERROR_INVALIDDATA;

    ProResAppleCtx *ctx = avctx->priv_data;
    AVFrame *frame      = data;
    uint8_t *raw_v210   = NULL; int row_bytes = 0;

    int ret = bridge_decode_p(ctx->dec, pkt->data, pkt->size,
                              avctx->width, avctx->height,
                              &raw_v210, &row_bytes);
    if (ret < 0) return AVERROR_EXTERNAL;

    frame->format = AV_PIX_FMT_YUV422P10;
    frame->width  = avctx->width;
    frame->height = avctx->height;
    if ((ret = av_frame_get_buffer(frame, 32)) < 0) {
        bridge_free_buf_p(raw_v210); return ret;
    }

    for (int y = 0; y < frame->height; ++y) {
        const uint8_t *src = raw_v210 + y * row_bytes;
        uint16_t *dY = (uint16_t *)(frame->data[0] + y * frame->linesize[0]);
        uint16_t *dU = (uint16_t *)(frame->data[1] + y * frame->linesize[1]);
        uint16_t *dV = (uint16_t *)(frame->data[2] + y * frame->linesize[2]);
        unpack_v210_line(src, dY, dU, dV, frame->width);
    }

    bridge_free_buf_p(raw_v210);
    *got_frame = 1;
    return pkt->size;
}

const FFCodec ff_prores_apple_decoder = {
    .p.name         = "prores_apple",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Apple ProRes (bridge decoder)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PRORES,
    .p.capabilities = 0,
    .p.wrapper_name = "prores_bridge",
    .priv_data_size = sizeof(ProResAppleCtx),
    .init           = prores_apple_init,
    .close          = prores_apple_close,
    .cb_type        = FF_CODEC_CB_TYPE_DECODE,
    .cb.decode      = prores_apple_decode_frame,
};
