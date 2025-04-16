#include <stdbool.h>
#include "libavutil/mem.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/codec_internal.h"
#include "libavutil/internal.h"
#include "libavutil/imgutils.h"
#include <string.h>  // for memcpy

#include "ProResDecoder.h"
#include "ProResProperties.h"

typedef struct ProResAppleContext {
    PRDecoderRef decoder;
    AVPacket     last_pkt;
    int          have_packet;
} ProResAppleContext;

static av_cold int prores_apple_init(AVCodecContext *avctx) {
    ProResAppleContext *ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    ctx->decoder = PROpenDecoder(0, NULL);
    if (!ctx->decoder) {
        av_log(avctx, AV_LOG_ERROR, "Failed to open ProRes decoder\n");
        av_freep(&ctx);
        return AVERROR_EXTERNAL;
    }

    avctx->priv_data = ctx;

    avctx->pix_fmt = AV_PIX_FMT_YUV422P10;

    // Initialize packet to empty state
    av_init_packet(&ctx->last_pkt);
    ctx->last_pkt.data = NULL;
    ctx->last_pkt.size = 0;
    ctx->have_packet = 0;

    av_log(avctx, AV_LOG_INFO, "ProRes Apple decoder initialized\n");
    return 0;
}

/*static int prores_apple_receive_frame(AVCodecContext *avctx, AVFrame *frame) {
    av_log(avctx, AV_LOG_WARNING, "Stub decoder receive_frame called\n");
    return AVERROR(EAGAIN);
}*/
/*
static int prores_apple_receive_frame(AVCodecContext *avctx, AVFrame *frame) {
    av_log(avctx, AV_LOG_WARNING, "Stub decoder receive_frame called\n");
    *frame = (AVFrame){0};  // Zero-initialize to pretend we decoded something
    return 0;  // SUCCESS
}

static int prores_apple_receive_frame(AVCodecContext *avctx, AVFrame *frame) {
    av_log(avctx, AV_LOG_WARNING, "Stub decoder receive_frame called\n");
    *frame = (AVFrame){0}; // just zero init
    return 0;
}*/

static void unpack_v210_line1(const uint8_t *src, uint16_t *dst_y, uint16_t *dst_u, uint16_t *dst_v, int width, AVCodecContext *avctx, int row) {
    //av_assert0(width % 6 == 0);  // Only full 6-pixel blocks supported

    const uint32_t *src_words = (const uint32_t *)src;
    int blocks = width / 6;

    av_log(avctx, AV_LOG_DEBUG, "unpack_v210_line: row %d, width=%d, src=%p dst_y=%p dst_u=%p dst_v=%p\n",
           row, width, src, dst_y, dst_u, dst_v);

    for (int i = 0; i < blocks; i++) {
        uint32_t w0 = *src_words++;
        uint32_t w1 = *src_words++;
        uint32_t w2 = *src_words++;
        uint32_t w3 = *src_words++;
        uint32_t w4 = *src_words++;

        *dst_u++ =  w0 & 0x3FF;
        *dst_y++ = (w0 >> 10) & 0x3FF;
        *dst_v++ = (w0 >> 20) & 0x3FF;

        *dst_y++ =  w1 & 0x3FF;
        *dst_u++ = (w1 >> 10) & 0x3FF;
        *dst_y++ = (w1 >> 20) & 0x3FF;

        *dst_v++ =  w2 & 0x3FF;
        *dst_y++ = (w2 >> 10) & 0x3FF;
        *dst_u++ = (w2 >> 20) & 0x3FF;

        *dst_y++ =  w3 & 0x3FF;
        *dst_v++ = (w3 >> 10) & 0x3FF;
        *dst_y++ = (w3 >> 20) & 0x3FF;

        *dst_u++ =  w4 & 0x3FF;
        *dst_y++ = (w4 >> 10) & 0x3FF;
        *dst_v++ = (w4 >> 20) & 0x3FF;
    }
}

static void unpack_v210_line2(const uint8_t *src, uint16_t *dst_y, uint16_t *dst_u, uint16_t *dst_v, int width, AVCodecContext *avctx, int row) {
    if (width % 6 != 0) {
        av_log(avctx, AV_LOG_ERROR, "v210 decoding requires width to be a multiple of 6, got %d\n", width);
        return;
    }

    const uint32_t *src_words = (const uint32_t *)src;
    int blocks = width / 6;

    av_log(avctx, AV_LOG_DEBUG, "unpack_v210_line: row %d, width=%d, blocks=%d, src=%p\n", row, width, blocks, src);

    for (int i = 0; i < blocks; i++) {
        uint32_t w0 = *src_words++;
        uint32_t w1 = *src_words++;
        uint32_t w2 = *src_words++;
        uint32_t w3 = *src_words++;

        // Word 0
        *dst_u++ =  (w0 >>  0) & 0x3FF;
        *dst_y++ =  (w0 >> 10) & 0x3FF;
        *dst_v++ =  (w0 >> 20) & 0x3FF;

        // Word 1
        *dst_y++ =  (w1 >>  0) & 0x3FF;
        *dst_u++ =  (w1 >> 10) & 0x3FF;
        *dst_y++ =  (w1 >> 20) & 0x3FF;

        // Word 2
        *dst_v++ =  (w2 >>  0) & 0x3FF;
        *dst_y++ =  (w2 >> 10) & 0x3FF;
        *dst_u++ =  (w2 >> 20) & 0x3FF;

        // Word 3
        *dst_y++ =  (w3 >>  0) & 0x3FF;
        *dst_v++ =  (w3 >> 10) & 0x3FF;
        *dst_y++ =  (w3 >> 20) & 0x3FF;
    }
}

static void unpack_v210_line(const uint8_t *src,
                             uint16_t *dst_y, uint16_t *dst_u, uint16_t *dst_v,
                             int width, AVCodecContext *avctx, int row)
{
    /* Compute how many 16-byte blocks the encoded row occupies.
     * A full block always decodes 6 luma (Y) and 3 chroma samples.
     * If width is not a multiple of 6, the row was padded.
     */
    int total_blocks = (width + 5) / 6;
    int total_y = total_blocks * 6;      // Decoded Y samples available
    int total_uv = total_blocks * 3;     // Decoded U and V samples available

    /* Allocate temporary buffers to hold the decoded line.
     * (In real code you might use a stack-allocated VLA if width is known small,
     *  or reuse a scratch buffer.)
     */
    uint16_t *temp_y = av_malloc_array(total_y, sizeof(uint16_t));
    uint16_t *temp_u = av_malloc_array(total_uv, sizeof(uint16_t));
    uint16_t *temp_v = av_malloc_array(total_uv, sizeof(uint16_t));
    if (!temp_y || !temp_u || !temp_v) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate temporary buffers\n");
        av_free(temp_y);
        av_free(temp_u);
        av_free(temp_v);
        return;
    }

    /* In v210 the standard ordering per 16-byte block is:
     *
     *   Byte offset:            0       4       8       12
     *   32-bit word contents:   w0      w1      w2      w3
     *
     *   From w0:  bits[0:9] = U0,  bits[10:19] = Y0, bits[20:29] = V0
     *   From w1:  bits[0:9]  = Y1, bits[10:19] = U1, bits[20:29] = Y2
     *   From w2:  bits[0:9]  = V1, bits[10:19] = Y3, bits[20:29] = U2
     *   From w3:  bits[0:9]  = Y4, bits[10:19] = V2, bits[20:29] = Y5
     *
     * In planar form the luma values are:
     *   Y0, Y1, Y2, Y3, Y4, Y5
     *
     * And the chroma samples for 4:2:2 (each pair of pixels shares one U/V) are:
     *   For pixels 0-1: U0, V0
     *   For pixels 2-3: U1, V1
     *   For pixels 4-5: U2, V2
     */

    const uint32_t *src_words = (const uint32_t *)src;
    int index_y = 0;
    int index_uv = 0;

    av_log(avctx, AV_LOG_DEBUG,
           "unpack_v210_line: row %d, width=%d, total_blocks=%d, src=%p\n",
           row, width, total_blocks, src);

    for (int block = 0; block < total_blocks; block++) {
        uint32_t w0 = *src_words++;
        uint32_t w1 = *src_words++;
        uint32_t w2 = *src_words++;
        uint32_t w3 = *src_words++;

        /* Extract luma (Y) samples */
        if (index_y < total_y) temp_y[index_y++] = (w0 >> 10) & 0x3FF; // Y0
        if (index_y < total_y) temp_y[index_y++] = (w1 >> 0)  & 0x3FF; // Y1
        if (index_y < total_y) temp_y[index_y++] = (w1 >> 20) & 0x3FF; // Y2
        if (index_y < total_y) temp_y[index_y++] = (w2 >> 10) & 0x3FF; // Y3
        if (index_y < total_y) temp_y[index_y++] = (w3 >> 0)  & 0x3FF; // Y4
        if (index_y < total_y) temp_y[index_y++] = (w3 >> 20) & 0x3FF; // Y5

        /* Extract chroma samples.
         * Every block supplies three chroma samples.
         */
        if (index_uv < total_uv) {
            temp_u[index_uv] = (w0 >> 0)  & 0x3FF;  // U0 for pixel pair 0
            temp_v[index_uv] = (w0 >> 20) & 0x3FF;  // V0
            index_uv++;
        }
        if (index_uv < total_uv) {
            temp_u[index_uv] = (w1 >> 10) & 0x3FF;  // U1 for pixel pair 1
            temp_v[index_uv] = (w2 >> 0)  & 0x3FF;  // V1
            index_uv++;
        }
        if (index_uv < total_uv) {
            temp_u[index_uv] = (w2 >> 20) & 0x3FF;  // U2 for pixel pair 2
            temp_v[index_uv] = (w3 >> 10) & 0x3FF;  // V2
            index_uv++;
        }
    }

    /* Copy only the valid output samples:
     *  - The destination luma buffer gets the first "width" samples.
     *  - For planar 4:2:2, the chroma buffers get "width/2" samples (for an even width).
     */
    memcpy(dst_y, temp_y, width * sizeof(uint16_t));
    memcpy(dst_u, temp_u, (width/2) * sizeof(uint16_t));
    memcpy(dst_v, temp_v, (width/2) * sizeof(uint16_t));

    av_free(temp_y);
    av_free(temp_u);
    av_free(temp_v);
}



static av_cold int prores_apple_close(AVCodecContext *avctx) {
    ProResAppleContext *ctx = avctx->priv_data;
    if (ctx) {
        if (ctx->decoder)
            PRCloseDecoder(ctx->decoder);
        av_packet_unref(&ctx->last_pkt);
        av_freep(&avctx->priv_data);
    }
    return 0;
}


static int prores_apple_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *pkt) {
    ProResAppleContext *ctx = avctx->priv_data;
    AVFrame *frame = data;

    if (!pkt || !pkt->data || pkt->size <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid packet received\n");
        return AVERROR_INVALIDDATA;
    }

    const PRPixelFormat fmt = kPRFormat_v210;
    const PRDownscaleMode downscale = kPRFullSize;
    const int width = avctx->width;
    const int height = avctx->height;

    const int min_rowbytes = PRBytesPerRowNeededInPixelBuffer(width, fmt, downscale);
    const int aligned_rowbytes = (min_rowbytes + 15) & ~15;
    int buf_size = aligned_rowbytes * height;

    uint8_t *decode_buf = av_malloc(buf_size);
    if (!decode_buf) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate decoder output buffer\n");
        return AVERROR(ENOMEM);
    }

    PRPixelBuffer pb = {
        .baseAddr = decode_buf,
        .rowBytes = aligned_rowbytes,
        .format   = fmt,
        .width    = width,
        .height   = height
    };

    int decoded = PRDecodeFrame(ctx->decoder, pkt->data, pkt->size, &pb, downscale, true);
    if (decoded < 0) {
        av_log(avctx, AV_LOG_ERROR, "Apple ProRes decode failed: %d\n", decoded);
        av_free(decode_buf);
        return AVERROR_EXTERNAL;
    }

    frame->format = AV_PIX_FMT_YUV422P10;
    frame->width = width;
    frame->height = height;

    int ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate AVFrame buffer\n");
        av_free(decode_buf);
        return ret;
    }

    for (int y = 0; y < height; y++) {
        const uint8_t *src = decode_buf + pb.rowBytes * y;
        uint16_t *dst_y = (uint16_t *)(frame->data[0] + y * frame->linesize[0]);
        uint16_t *dst_u = (uint16_t *)(frame->data[1] + y * frame->linesize[1]);
        uint16_t *dst_v = (uint16_t *)(frame->data[2] + y * frame->linesize[2]);

        unpack_v210_line(src, dst_y, dst_u, dst_v, width, avctx, y);
    }

    av_free(decode_buf);

    *got_frame = 1;
    return pkt->size;
}

const FFCodec ff_prores_apple_decoder = {
    .p.name           = "prores_apple",
    .p.long_name      = "Apple ProRes (Apple SDK stub)",
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_PRORES,
    .p.capabilities   = 0/*AV_CODEC_CAP_DR1*/,
    .p.wrapper_name   = "prores_sdk",
    .priv_data_size   = sizeof(ProResAppleContext),
    .init             = prores_apple_init,
    .close            = prores_apple_close,
    .cb_type          = FF_CODEC_CB_TYPE_DECODE,
    .cb.decode        = prores_apple_decode_frame,
    .is_decoder       = 1,
};
