/* Public domain. No rights reserved. Wholly AI-authored. */
/* FFmpeg reads through host files, never media URLs. Frames retain their planes. */
#include "os2libs/video_decode.h"
#include "hal/host_fs.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if TWOINE_VIDEO
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/common.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
struct LxVideoDecoder
{
    LxHostFile *file;
    AVFormatContext *format;
    AVIOContext *io;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    struct SwsContext *scale;
    int stream, eof, sent_eof;
    int64_t start, seek_ms, next_ms;
    LxVideoInfo info;
    AVCodecContext *audio_codec;
    AVFrame *audio_frame;
    SwrContext *resampler;
    int audio_stream;
    int64_t audio_next_sample, video_origin_sample;
    float *audio_buffer;
    size_t audio_capacity;
    LxVideoAudioSink audio_sink;
    void *audio_context;
};
static int read_packet(void *opaque, uint8_t *buffer, int size)
{
    size_t actual = 0;
    LxVideoDecoder *d = opaque;
    if (size < 0 || lx_host_file_read(d->file, buffer, (size_t)size, &actual) != LX_HOST_FS_OK)
        return AVERROR(EIO);
    return actual ? (int)actual : AVERROR_EOF;
}
static int64_t seek_packet(void *opaque, int64_t offset, int whence)
{
    LxVideoDecoder *d = opaque;
    if (whence == AVSEEK_SIZE)
    {
        LxHostFileInfo info;
        return lx_host_file_info(d->file, &info) == LX_HOST_FS_OK && info.size <= INT64_MAX
                   ? (int64_t)info.size
                   : AVERROR(EIO);
    }
    uint64_t position = 0;
    int origin = whence & ~AVSEEK_FORCE;
    if (origin < 0 || origin > 2 ||
        lx_host_file_seek(d->file, offset, (unsigned)origin, &position) != LX_HOST_FS_OK ||
        position > INT64_MAX)
        return AVERROR(EIO);
    return (int64_t)position;
}
void lx_video_decoder_close(LxVideoDecoder *d)
{
    if (!d)
        return;
    sws_freeContext(d->scale);
    swr_free(&d->resampler);
    avcodec_free_context(&d->audio_codec);
    av_frame_free(&d->audio_frame);
    free(d->audio_buffer);
    av_frame_free(&d->frame);
    av_packet_free(&d->packet);
    avcodec_free_context(&d->codec);
    avformat_close_input(&d->format);
    if (d->io)
    {
        av_freep(&d->io->buffer);
        avio_context_free(&d->io);
    }
    lx_host_file_close(d->file);
    free(d);
}
LxVideoDecoder *lx_video_decoder_open_audio(const char *path, LxVideoInfo *info, int audio)
{
    if (!path || !info)
        return NULL;
    LxVideoDecoder *d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
    LxHostFsError error;
    d->file = lx_host_file_open(path, LX_HOST_FILE_READ | LX_HOST_FILE_NOINHERIT, 1, &error);
    if (!d->file)
        goto fail;
    d->format = avformat_alloc_context();
    unsigned char *buffer = av_malloc(65536);
    if (!d->format || !buffer)
    {
        av_free(buffer);
        goto fail;
    }
    d->io = avio_alloc_context(buffer, 65536, 0, d, read_packet, NULL, seek_packet);
    if (!d->io)
    {
        av_free(buffer);
        goto fail;
    }
    d->format->pb = d->io;
    d->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    AVDictionary *options = NULL;
    av_dict_set(&options, "protocol_whitelist", "", 0);
    av_dict_set(&options, "enable_drefs", "0", 0);
    int result = avformat_open_input(&d->format, NULL, NULL, &options);
    av_dict_free(&options);
    if (result < 0 || avformat_find_stream_info(d->format, NULL) < 0)
        goto fail;
    const AVCodec *codec = NULL;
    d->stream = av_find_best_stream(d->format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (d->stream < 0)
        goto fail;
    AVStream *stream = d->format->streams[d->stream];
    AVCodecParameters *parameters = stream->codecpar;
    if (parameters->width <= 0 || parameters->height <= 0 || parameters->width > 16384 ||
        parameters->height > 16384 || (uint64_t)parameters->width * parameters->height > 33554432)
        goto fail;
    d->codec = avcodec_alloc_context3(codec);
    if (!d->codec || avcodec_parameters_to_context(d->codec, parameters) < 0)
        goto fail;
    d->codec->thread_count = 2;
    if (avcodec_open2(d->codec, codec, NULL) < 0)
        goto fail;
    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->packet || !d->frame)
        goto fail;
    AVRational rate = av_guess_frame_rate(d->format, stream, NULL);
    if (rate.num <= 0 || rate.den <= 0)
        rate = (AVRational){25, 1};
    d->info = (LxVideoInfo){.width = parameters->width,
                            .height = parameters->height,
                            .duration_ms = d->format->duration != AV_NOPTS_VALUE
                                               ? av_rescale(d->format->duration, 1000, AV_TIME_BASE)
                                               : 0,
                            .rate_numerator = rate.num,
                            .rate_denominator = rate.den};
    if (d->info.duration_ms < 0 || d->info.duration_ms > UINT32_MAX)
        goto fail;
    for (unsigned i = 0; i < d->format->nb_streams; i++)
        d->info.audio_present |=
            audio && d->format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
    d->start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    d->video_origin_sample = av_rescale_q(d->start, stream->time_base, (AVRational){1, 48000});
    const AVCodec *audio_codec = NULL;
    d->audio_stream = av_find_best_stream(d->format, AVMEDIA_TYPE_AUDIO, -1, -1, &audio_codec, 0);
    if (!audio)
        d->audio_stream = -1;
    if (d->info.audio_present)
    {
        if (d->audio_stream < 0 || !audio_codec)
            goto fail;
        d->audio_codec = avcodec_alloc_context3(audio_codec);
        if (!d->audio_codec ||
            avcodec_parameters_to_context(d->audio_codec,
                                          d->format->streams[d->audio_stream]->codecpar) < 0 ||
            avcodec_open2(d->audio_codec, audio_codec, NULL) < 0)
            goto fail;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (d->audio_codec->sample_rate < 4000 || d->audio_codec->sample_rate > 384000 ||
            d->audio_codec->ch_layout.nb_channels > 8 ||
            swr_alloc_set_opts2(&d->resampler, &stereo, AV_SAMPLE_FMT_FLT, 48000,
                                &d->audio_codec->ch_layout, d->audio_codec->sample_fmt,
                                d->audio_codec->sample_rate, 0, NULL) < 0 ||
            swr_init(d->resampler) < 0)
            goto fail;
        d->audio_frame = av_frame_alloc();
        if (!d->audio_frame)
            goto fail;
    }
    *info = d->info;
    return d;
fail:
    lx_video_decoder_close(d);
    return NULL;
}
void lx_video_frame_release(LxDecodedVideoFrame *frame)
{
    if (!frame)
        return;
    AVFrame *owner = frame->owner;
    av_frame_free(&owner);
    memset(frame, 0, sizeof *frame);
}
static int export_frame(LxVideoDecoder *d, LxDecodedVideoFrame *output)
{
    if (d->frame->width <= 0 || d->frame->height <= 0 || d->frame->width > 16384 ||
        d->frame->height > 16384 || (uint64_t)d->frame->width * d->frame->height > 33554432)
        return 0;
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return 0;
    av_frame_move_ref(frame, d->frame);
    enum AVPixelFormat format = frame->format;
    int direct = format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P ||
                 format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_BGRA;
    if (format != AV_PIX_FMT_BGRA && frame->colorspace != AVCOL_SPC_UNSPECIFIED &&
        frame->colorspace != AVCOL_SPC_BT709 && frame->colorspace != AVCOL_SPC_BT470BG &&
        frame->colorspace != AVCOL_SPC_SMPTE170M)
        direct = 0;
    for (unsigned i = 0; i < 3; i++)
        if (frame->data[i] && frame->linesize[i] <= 0)
            direct = 0;
    if (!direct)
    {
        AVFrame *converted = av_frame_alloc();
        if (!converted)
            goto fail;
        converted->format = AV_PIX_FMT_BGRA;
        converted->width = frame->width;
        converted->height = frame->height;
        if (av_frame_get_buffer(converted, 32) < 0)
        {
            av_frame_free(&converted);
            goto fail;
        }
        d->scale =
            sws_getCachedContext(d->scale, frame->width, frame->height, format, frame->width,
                                 frame->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
        int matrix =
            frame->colorspace == AVCOL_SPC_BT709       ? SWS_CS_ITU709
            : frame->colorspace == AVCOL_SPC_FCC       ? SWS_CS_FCC
            : frame->colorspace == AVCOL_SPC_SMPTE240M ? SWS_CS_SMPTE240M
            : frame->colorspace == AVCOL_SPC_BT2020_NCL || frame->colorspace == AVCOL_SPC_BT2020_CL
                ? SWS_CS_BT2020
                : SWS_CS_ITU601;
        const int *coefficients = sws_getCoefficients(matrix);
        if (!d->scale ||
            sws_setColorspaceDetails(d->scale, coefficients,
                                     frame->color_range == AVCOL_RANGE_JPEG ||
                                         format == AV_PIX_FMT_YUVJ420P,
                                     coefficients, 1, 0, 1 << 16, 1 << 16) < 0 ||
            sws_scale(d->scale, (const uint8_t *const *)frame->data, frame->linesize, 0,
                      frame->height, converted->data, converted->linesize) != frame->height)
        {
            av_frame_free(&converted);
            goto fail;
        }
        if (av_frame_copy_props(converted, frame) < 0)
        {
            av_frame_free(&converted);
            goto fail;
        }
        av_frame_free(&frame);
        frame = converted;
        format = AV_PIX_FMT_BGRA;
    }
    output->image = (LxHostVideoFrame){
        .width = frame->width,
        .height = frame->height,
        .format = format == AV_PIX_FMT_NV12   ? LX_HOST_FRAME_NV12
                  : format == AV_PIX_FMT_BGRA ? LX_HOST_FRAME_BGRA
                                              : LX_HOST_FRAME_YUV420P,
        .colors = frame->colorspace == AVCOL_SPC_BT709
                      ? (frame->color_range == AVCOL_RANGE_JPEG ? LX_HOST_FRAME_BT709_FULL
                                                                : LX_HOST_FRAME_BT709_LIMITED)
                      : (frame->color_range == AVCOL_RANGE_JPEG || format == AV_PIX_FMT_YUVJ420P
                             ? LX_HOST_FRAME_BT601_FULL
                             : LX_HOST_FRAME_BT601_LIMITED)};
    unsigned planes = output->image.format == LX_HOST_FRAME_BGRA   ? 1
                      : output->image.format == LX_HOST_FRAME_NV12 ? 2
                                                                   : 3;
    for (unsigned i = 0; i < planes; i++)
    {
        output->image.planes[i] = frame->data[i];
        output->image.strides[i] = frame->linesize[i];
        unsigned height = i ? (frame->height + 1) / 2 : frame->height;
        output->image.lengths[i] = (size_t)height * frame->linesize[i];
    }
    AVStream *stream = d->format->streams[d->stream];
    output->duration_ms =
        frame->duration > 0
            ? av_rescale_q(frame->duration, stream->time_base, (AVRational){1, 1000})
            : av_rescale(d->info.rate_denominator, 1000, d->info.rate_numerator);
    if (output->duration_ms < 1)
        output->duration_ms = 1;
    output->milliseconds = frame->best_effort_timestamp == AV_NOPTS_VALUE
                               ? d->next_ms
                               : av_rescale_q(av_sat_sub64(frame->best_effort_timestamp, d->start),
                                              stream->time_base, (AVRational){1, 1000});
    if (output->milliseconds < 0)
        output->milliseconds = 0;
    if (output->milliseconds > UINT32_MAX || output->duration_ms > UINT32_MAX ||
        output->milliseconds > (int64_t)UINT32_MAX - output->duration_ms)
        goto fail;
    d->next_ms = output->milliseconds + output->duration_ms;
    output->owner = frame;
    return 1;
fail:
    av_frame_free(&frame);
    return 0;
}
void lx_video_decoder_audio_sink(LxVideoDecoder *d, LxVideoAudioSink sink, void *context)
{
    if (d)
    {
        d->audio_sink = sink;
        d->audio_context = context;
    }
}
static int audio_output(LxVideoDecoder *d, AVFrame *frame)
{
    int rate = d->audio_codec->sample_rate;
    int64_t delay = swr_get_delay(d->resampler, rate);
    int64_t needed = av_rescale_rnd(av_sat_add64(delay, frame ? frame->nb_samples : 0), 48000, rate,
                                    AV_ROUND_UP);
    if (needed < 0 || needed > 480000)
        return 0;
    if (!needed)
        return 1;
    if ((uint64_t)needed > d->audio_capacity)
    {
        float *buffer = realloc(d->audio_buffer, (size_t)needed * 2 * sizeof(float));
        if (!buffer)
            return 0;
        d->audio_buffer = buffer;
        d->audio_capacity = (size_t)needed;
    }
    if (frame && frame->best_effort_timestamp != AV_NOPTS_VALUE)
    {
        AVStream *stream = d->format->streams[d->audio_stream];
        d->audio_next_sample =
            av_sat_sub64(av_sat_sub64(av_rescale_q(frame->best_effort_timestamp, stream->time_base,
                                                   (AVRational){1, 48000}),
                                      d->video_origin_sample),
                         av_rescale(delay, 48000, rate));
    }
    if (d->audio_next_sample < -480000 || d->audio_next_sample > (int64_t)UINT32_MAX * 48 - needed)
        return 0;
    uint8_t *output = (uint8_t *)d->audio_buffer;
    int count = swr_convert(d->resampler, &output, (int)needed,
                            frame ? (const uint8_t **)frame->extended_data : NULL,
                            frame ? frame->nb_samples : 0);
    if (count < 0)
        return 0;
    if (count && d->audio_sink &&
        !d->audio_sink(d->audio_context, d->audio_buffer, (unsigned)count, d->audio_next_sample))
        return 0;
    d->audio_next_sample += count;
    return 1;
}
static int audio_packet(LxVideoDecoder *d, AVPacket *packet)
{
    if (!d->audio_codec)
        return 1;
    int result = avcodec_send_packet(d->audio_codec, packet);
    if (result < 0 && result != AVERROR_EOF)
        return 0;
    while (!(result = avcodec_receive_frame(d->audio_codec, d->audio_frame)))
    {
        int ok = audio_output(d, d->audio_frame);
        av_frame_unref(d->audio_frame);
        if (!ok)
            return 0;
    }
    return result == AVERROR(EAGAIN) || result == AVERROR_EOF;
}
int lx_video_decoder_next(LxVideoDecoder *d, LxDecodedVideoFrame *output)
{
    if (!d || !output)
        return -1;
    memset(output, 0, sizeof *output);
    for (;;)
    {
        int result = avcodec_receive_frame(d->codec, d->frame);
        if (!result)
        {
            if (!export_frame(d, output))
                return -1;
            if (output->milliseconds < d->seek_ms)
            {
                lx_video_frame_release(output);
                continue;
            }
            return 1;
        }
        if (result == AVERROR_EOF)
            return 0;
        if (result != AVERROR(EAGAIN))
            return -1;
        if (d->eof)
        {
            if (d->sent_eof)
                return -1;
            result = avcodec_send_packet(d->codec, NULL);
            if (result < 0 && result != AVERROR_EOF)
                return -1;
            d->sent_eof = 1;
            continue;
        }
        for (;;)
        {
            result = av_read_frame(d->format, d->packet);
            if (result < 0)
            {
                if (result != AVERROR_EOF)
                    return -1;
                if (!audio_packet(d, NULL) || (d->audio_codec && !audio_output(d, NULL)))
                    return -1;
                d->eof = 1;
                break;
            }
            if (d->packet->stream_index == d->stream)
            {
                result = avcodec_send_packet(d->codec, d->packet);
                av_packet_unref(d->packet);
                if (result < 0)
                    return -1;
                break;
            }
            if (d->packet->stream_index == d->audio_stream && !audio_packet(d, d->packet))
            {
                av_packet_unref(d->packet);
                return -1;
            }
            av_packet_unref(d->packet);
        }
    }
}
int lx_video_decoder_seek(LxVideoDecoder *d, int64_t milliseconds)
{
    if (!d || milliseconds < 0 || milliseconds > UINT32_MAX)
        return 0;
    AVStream *stream = d->format->streams[d->stream];
    int64_t target = av_sat_add64(
        d->start, av_rescale_q(milliseconds, (AVRational){1, 1000}, stream->time_base));
    if (av_seek_frame(d->format, d->stream, target, AVSEEK_FLAG_BACKWARD) < 0)
        return 0;
    avcodec_flush_buffers(d->codec);
    if (d->audio_codec)
    {
        avcodec_flush_buffers(d->audio_codec);
        av_frame_unref(d->audio_frame);
        swr_close(d->resampler);
        if (swr_init(d->resampler) < 0)
            return 0;
        d->audio_next_sample = milliseconds * 48;
    }
    av_packet_unref(d->packet);
    av_frame_unref(d->frame);
    d->eof = d->sent_eof = 0;
    d->seek_ms = d->next_ms = milliseconds;
    return 1;
}
#else
struct LxVideoDecoder
{
    int unused;
};
void lx_video_decoder_audio_sink(LxVideoDecoder *d, LxVideoAudioSink sink, void *context)
{
    (void)d;
    (void)sink;
    (void)context;
}
LxVideoDecoder *lx_video_decoder_open_audio(const char *path, LxVideoInfo *info, int audio)
{
    (void)path;
    (void)info;
    (void)audio;
    return NULL;
}
int lx_video_decoder_next(LxVideoDecoder *d, LxDecodedVideoFrame *frame)
{
    (void)d;
    (void)frame;
    return -1;
}
int lx_video_decoder_seek(LxVideoDecoder *d, int64_t milliseconds)
{
    (void)d;
    (void)milliseconds;
    return 0;
}
void lx_video_frame_release(LxDecodedVideoFrame *frame)
{
    (void)frame;
}
void lx_video_decoder_close(LxVideoDecoder *d)
{
    free(d);
}
#endif
LxVideoDecoder *lx_video_decoder_open(const char *path, LxVideoInfo *info)
{
    return lx_video_decoder_open_audio(path, info, 1);
}
