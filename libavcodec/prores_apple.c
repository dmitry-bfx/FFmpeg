#include <stdbool.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavutil/internal.h"
#include "libavutil/imgutils.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/codec_internal.h"

#include "ProResDecoder.h"
#include "ProResProperties.h"

#if defined(_WIN32)
#include <windows.h>
#define LIBPRORES_NAME "ProRes64.dll"
static HMODULE libprores_handle = NULL;
#define LOAD_SYM(handle, name) GetProcAddress(handle, name)
#else
#include <dlfcn.h>
#if defined(__APPLE__)
#define LIBPRORES_NAME "libProRes64.dylib"
#else
#define LIBPRORES_NAME "libProRes64.so"
#endif
static void *libprores_handle = NULL;
#define LOAD_SYM(handle, name) dlsym(handle, name)
#endif

typedef PRDecoderRef (*PROpenDecoder_t)(uint32_t flags, void *reserved);
typedef void (*PRCloseDecoder_t)(PRDecoderRef decoder);
typedef int (*PRDecodeFrame_t)(PRDecoderRef decoder, const void *srcData, size_t srcSize, PRPixelBuffer *pixelBuffer, PRDownscaleMode downscale, bool enableMultithread);
typedef size_t (*PRBytesPerRowNeededInPixelBuffer_t)(uint32_t width, PRPixelFormat format, PRDownscaleMode downscale);

static PROpenDecoder_t PROpenDecoder_p = NULL;
static PRCloseDecoder_t PRCloseDecoder_p = NULL;
static PRDecodeFrame_t PRDecodeFrame_p = NULL;
static PRBytesPerRowNeededInPixelBuffer_t PRBytesPerRowNeededInPixelBuffer_p = NULL;

static int load_prores_sdk(void) {
    const char *override = getenv("LIBPRORES_PATH");
    const char *lib_path = override ? override : LIBPRORES_NAME;

#if defined(_WIN32)
    libprores_handle = LoadLibraryA(lib_path);
    if (!libprores_handle) return -1;
#else
    libprores_handle = dlopen(lib_path, RTLD_LAZY);
    if (!libprores_handle) return -1;
#endif
    PROpenDecoder_p = (PROpenDecoder_t)LOAD_SYM(libprores_handle, "PROpenDecoder");
    PRCloseDecoder_p = (PRCloseDecoder_t)LOAD_SYM(libprores_handle, "PRCloseDecoder");
    PRDecodeFrame_p = (PRDecodeFrame_t)LOAD_SYM(libprores_handle, "PRDecodeFrame");
    PRBytesPerRowNeededInPixelBuffer_p = (PRBytesPerRowNeededInPixelBuffer_t)LOAD_SYM(libprores_handle, "PRBytesPerRowNeededInPixelBuffer");
    return (PROpenDecoder_p && PRCloseDecoder_p && PRDecodeFrame_p && PRBytesPerRowNeededInPixelBuffer_p) ? 0 : -1;
}

static void unload_prores_sdk(void) {
#if defined(_WIN32)
    if (libprores_handle) FreeLibrary(libprores_handle);
#else
    if (libprores_handle) dlclose(libprores_handle);
#endif
    libprores_handle = NULL;
}

typedef struct ProResAppleContext {
    PRDecoderRef decoder;
} ProResAppleContext;

static av_cold int prores_apple_init(AVCodecContext *avctx) {
    if (load_prores_sdk() < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to load Apple ProRes SDK\n");
        return AVERROR_EXTERNAL;
    }
    ProResAppleContext *ctx = av_mallocz(sizeof(*ctx));
    if (!ctx) return AVERROR(ENOMEM);
    ctx->decoder = PROpenDecoder_p(0, NULL);
    if (!ctx->decoder) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open ProRes decoder\n");
        av_free(ctx);
        unload_prores_sdk();
        return AVERROR_EXTERNAL;
    }
    avctx->priv_data = ctx;
    avctx->pix_fmt   = AV_PIX_FMT_YUV422P10;
    return 0;
}

static av_cold int prores_apple_close(AVCodecContext *avctx) {
    ProResAppleContext *ctx = avctx->priv_data;
    if (ctx) {
        if (ctx->decoder) PRCloseDecoder_p(ctx->decoder);
        av_freep(&avctx->priv_data);
    }
    unload_prores_sdk();
    return 0;
}

// Full v210 unpack into planar YUV422P10
static void unpack_v210_line(const uint8_t *src,
                             uint16_t *dst_y, uint16_t *dst_u, uint16_t *dst_v,
                             int width, AVCodecContext *avctx, int row)
{
    int total_blocks = (width + 5) / 6;
    int total_y = total_blocks * 6;
    int total_uv = total_blocks * 3;

    uint16_t *temp_y = av_malloc_array(total_y, sizeof(uint16_t));
    uint16_t *temp_u = av_malloc_array(total_uv, sizeof(uint16_t));
    uint16_t *temp_v = av_malloc_array(total_uv, sizeof(uint16_t));
    if (!temp_y || !temp_u || !temp_v) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate temporary buffers\n");
        av_freep(&temp_y);
        av_freep(&temp_u);
        av_freep(&temp_v);
        return;
    }

    const uint32_t *src_words = (const uint32_t *)src;
    int index_y = 0;
    int index_uv = 0;

    for (int block = 0; block < total_blocks; block++) {
        uint32_t w0 = *src_words++;
        uint32_t w1 = *src_words++;
        uint32_t w2 = *src_words++;
        uint32_t w3 = *src_words++;

        if (index_y < total_y) temp_y[index_y++] = (w0 >> 10) & 0x3FF;
        if (index_y < total_y) temp_y[index_y++] = (w1 >>  0) & 0x3FF;
        if (index_y < total_y) temp_y[index_y++] = (w1 >> 20) & 0x3FF;
        if (index_y < total_y) temp_y[index_y++] = (w2 >> 10) & 0x3FF;
        if (index_y < total_y) temp_y[index_y++] = (w3 >>  0) & 0x3FF;
        if (index_y < total_y) temp_y[index_y++] = (w3 >> 20) & 0x3FF;

        if (index_uv < total_uv) {
            temp_u[index_uv] =  w0 & 0x3FF;
            temp_v[index_uv] = (w0 >> 20) & 0x3FF;
            index_uv++;
        }
        if (index_uv < total_uv) {
            temp_u[index_uv] = (w1 >> 10) & 0x3FF;
            temp_v[index_uv] =  w2 & 0x3FF;
            index_uv++;
        }
        if (index_uv < total_uv) {
            temp_u[index_uv] = (w2 >> 20) & 0x3FF;
            temp_v[index_uv] = (w3 >> 10) & 0x3FF;
            index_uv++;
        }
    }

    memcpy(dst_y, temp_y,      width    * sizeof(uint16_t));
    memcpy(dst_u, temp_u, (width/2) * sizeof(uint16_t));
    memcpy(dst_v, temp_v, (width/2) * sizeof(uint16_t));

    av_free(temp_y);
    av_free(temp_u);
    av_free(temp_v);
}

static int prores_apple_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *pkt) {
    ProResAppleContext *ctx = avctx->priv_data;
    AVFrame *frame      = data;
    if (!pkt || !pkt->data || pkt->size <= 0)
        return AVERROR_INVALIDDATA;

    const PRPixelFormat fmt       = kPRFormat_v210;
    const PRDownscaleMode downscale = kPRFullSize;
    int width  = avctx->width;
    int height = avctx->height;

    size_t min_rowbytes = PRBytesPerRowNeededInPixelBuffer_p(width, fmt, downscale);
    int rowbytes        = ((int)min_rowbytes + 15) & ~15;
    int buf_size        = rowbytes * height;
    uint8_t *decode_buf = av_malloc(buf_size);
    if (!decode_buf)
        return AVERROR(ENOMEM);

    PRPixelBuffer pb = { .baseAddr = decode_buf, .rowBytes = rowbytes,
                         .format = fmt, .width = width, .height = height };

    int decoded = PRDecodeFrame_p(ctx->decoder, pkt->data, pkt->size, &pb, downscale, true);
    if (decoded < 0) {
        av_free(decode_buf);
        return AVERROR_EXTERNAL;
    }

    frame->format = AV_PIX_FMT_YUV422P10;
    frame->width  = width;
    frame->height = height;

    int ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        av_free(decode_buf);
        return ret;
    }

    for (int y = 0; y < height; y++) {
        const uint8_t *src   = decode_buf + rowbytes * y;
        uint16_t *dY         = (uint16_t *)(frame->data[0] + y * frame->linesize[0]);
        uint16_t *dU         = (uint16_t *)(frame->data[1] + y * frame->linesize[1]);
        uint16_t *dV         = (uint16_t *)(frame->data[2] + y * frame->linesize[2]);
        unpack_v210_line(src, dY, dU, dV, width, avctx, y);
    }

    av_free(decode_buf);
    *got_frame = 1;
    return pkt->size;
}

const FFCodec ff_prores_apple_decoder = {
    .p.name         = "prores_apple",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Apple ProRes (SDK via dynamic loading)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PRORES,
    .p.capabilities = 0,
    .p.wrapper_name = "prores_sdk",
    .priv_data_size = sizeof(ProResAppleContext),
    .init           = prores_apple_init,
    .close          = prores_apple_close,
    .cb_type        = FF_CODEC_CB_TYPE_DECODE,
    .cb.decode      = prores_apple_decode_frame,
    .is_decoder     = 1,
};
