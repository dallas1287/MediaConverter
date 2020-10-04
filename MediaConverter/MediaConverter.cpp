// MediaConverter.cpp : Defines the exported functions for the DLL.
//
#include "pch.h"
#include "framework.h"
#include "MediaConverter.h"

// This is the constructor of a class that has been exported.
CMediaConverter::CMediaConverter()
{
}

ErrorCode CMediaConverter::loadFrame(const char* filename, int& width, int& height, unsigned char** data)
{
    AVFormatContext* av_format_ctx = avformat_alloc_context();
    if (!av_format_ctx)
        return ErrorCode::NO_FMT_CTX;

    if (avformat_open_input(&av_format_ctx, filename, NULL, NULL)) //returns 0 on success
        return ErrorCode::FMT_UNOPENED;

    if (av_format_ctx->nb_streams < 1)
        return ErrorCode::NO_STREAMS;

    int vid_str_idx = -1;
    AVCodec* av_codec = nullptr;
    AVCodecParameters* av_codec_params = nullptr;

    for (unsigned int i = 0; i < av_format_ctx->nb_streams; ++i)
    {
        auto stream = av_format_ctx->streams[i];
        av_codec_params = av_format_ctx->streams[i]->codecpar;

        av_codec = avcodec_find_decoder(av_codec_params->codec_id);
        if (!av_codec)
            return ErrorCode::NO_CODEC;

        if (av_codec_params->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            vid_str_idx = i;
            break;
        }
    }

    if (vid_str_idx == -1)
        return ErrorCode::NO_VID_STREAM;

    AVCodecContext* av_codec_ctx = avcodec_alloc_context3(av_codec);
    if (!av_codec_ctx)
        return ErrorCode::NO_CODEC_CTX;

    if (avcodec_parameters_to_context(av_codec_ctx, av_codec_params) < 0)
        return ErrorCode::CODEC_CTX_UNINIT;

    if (avcodec_open2(av_codec_ctx, av_codec, NULL) < 0)
        return ErrorCode::CODEC_UNOPENED;

    AVFrame* av_frame = av_frame_alloc();
    if (!av_frame)
        return ErrorCode::NO_FRAME;

    AVPacket* av_packet = av_packet_alloc();
    if (!av_packet)
        return ErrorCode::NO_PACKET;

    int response;
    while (av_read_frame(av_format_ctx, av_packet) >= 0)
    {
        if (av_packet->stream_index != vid_str_idx)
            continue;

        response = avcodec_send_packet(av_codec_ctx, av_packet);
        if (response < 0)
            return ErrorCode::PKT_NOT_DECODED;

        response = avcodec_receive_frame(av_codec_ctx, av_frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
            continue;
        else if (response < 0)
            return ErrorCode::PKT_NOT_RECEIVED;

        av_packet_unref(av_packet);
        break;
    }

    unsigned char* output = new unsigned char[static_cast<unsigned long>(av_frame->width) * static_cast<unsigned long>(av_frame->height) * 4];

    SwsContext* sws_scaler_ctx = sws_getContext(av_frame->width, av_frame->height, av_codec_ctx->pix_fmt, //input
                                                av_frame->width, av_frame->height, AV_PIX_FMT_RGB0, //output
                                                SWS_BILINEAR, NULL, NULL, NULL); //options

    if (!sws_scaler_ctx)
        return ErrorCode::NO_SCALER;

    unsigned char* dest[4] = { output, NULL, NULL, NULL };
    int dest_linesize[4] = { av_frame->width * 4, 0, 0, 0 };

    sws_scale(sws_scaler_ctx, av_frame->data, av_frame->linesize, 0, av_frame->height, dest, dest_linesize);
    sws_freeContext(sws_scaler_ctx);

    width = av_frame->width;
    height = av_frame->height;
    *data = output;

    return ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::openVideoReader(const char* filename)
{
    return openVideoReader(&m_vrState, filename);
}

ErrorCode CMediaConverter::openVideoReader(VideoReaderState* state, const char* filename)
{
    auto& width = state->width;
    auto& height = state->height;
    auto& av_format_ctx = state->av_format_ctx;
    auto& av_codec_ctx = state->av_codec_ctx;
    auto& av_frame = state->av_frame;
    auto& av_packet = state->av_packet;
    auto& vid_str_idx = state->video_stream_index;
    auto& sws_scaler_ctx = state->sws_scaler_ctx;
    auto& timebase = state->timebase;
    auto& frame_ct = state->frame_ct;

    av_format_ctx = avformat_alloc_context();
    if (!av_format_ctx)
        return ErrorCode::NO_FMT_CTX;

    if (avformat_open_input(&av_format_ctx, filename, NULL, NULL)) //returns 0 on success
        return ErrorCode::FMT_UNOPENED;

    if (av_format_ctx->nb_streams < 1)
        return ErrorCode::NO_STREAMS;

    state->av_format_ctx->seek2any = 1;

    vid_str_idx = -1;
    AVCodec* av_codec = nullptr;
    AVCodecParameters* av_codec_params = nullptr;

    for (unsigned int i = 0; i < av_format_ctx->nb_streams; ++i)
    {
        auto stream = av_format_ctx->streams[i];
        av_codec_params = av_format_ctx->streams[i]->codecpar;

        av_codec = avcodec_find_decoder(av_codec_params->codec_id);
        if (!av_codec)
            return ErrorCode::NO_CODEC;

        if (av_codec_params->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            vid_str_idx = i;
            width = av_codec_params->width;
            height = av_codec_params->height;
            timebase = av_format_ctx->streams[i]->time_base;
            frame_ct = av_format_ctx->streams[i]->nb_frames;
            break;
        }
    }

    if (vid_str_idx == -1)
        return ErrorCode::NO_VID_STREAM;

    state->codecName = av_codec->long_name;
    state->start_time = av_format_ctx->streams[vid_str_idx]->start_time;
    state->duration = av_format_ctx->streams[vid_str_idx]->duration;
    state->avg_frame_rate = av_format_ctx->streams[vid_str_idx]->avg_frame_rate;

    av_codec_ctx = avcodec_alloc_context3(av_codec);
    if (!av_codec_ctx)
        return ErrorCode::NO_CODEC_CTX;

    av_codec_ctx->thread_count = 8;

    if (avcodec_parameters_to_context(av_codec_ctx, av_codec_params) < 0)
        return ErrorCode::CODEC_CTX_UNINIT;

    if (avcodec_open2(av_codec_ctx, av_codec, NULL) < 0)
        return ErrorCode::CODEC_UNOPENED;

    av_frame = av_frame_alloc();
    if (!av_frame)
        return ErrorCode::NO_FRAME;

    av_packet = av_packet_alloc();
    if (!av_packet)
        return ErrorCode::NO_PACKET;

    return ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::readVideoReaderFrame(FBPtr& fb_ptr, bool requestFlush)
{
    return readVideoReaderFrame(&m_vrState, fb_ptr, requestFlush);
}

ErrorCode CMediaConverter::readVideoReaderFrame(VideoReaderState* state, FBPtr& fb_ptr, bool requestFlush)
{
    auto& width = state->width;
    auto& height = state->height;
    auto& av_format_ctx = state->av_format_ctx;
    auto& av_codec_ctx = state->av_codec_ctx;
    auto& av_frame = state->av_frame;
    auto& av_packet = state->av_packet;
    auto& vid_str_idx = state->video_stream_index;
    auto& sws_scaler_ctx = state->sws_scaler_ctx;

    int response = processPacketsIntoFrames(state);

    if (response == AVERROR_EOF)
    {
        avcodec_flush_buffers(av_codec_ctx);
        return ErrorCode::FILE_EOF;
    }

    return (ErrorCode)outputToBuffer(state, fb_ptr);
}

ErrorCode CMediaConverter::readVideoReaderFrame(unsigned char** frameBuffer, bool requestFlush)
{
    return readVideoReaderFrame(&m_vrState, frameBuffer, requestFlush);
}

ErrorCode CMediaConverter::readVideoReaderFrame(VideoReaderState* state, unsigned char** frameBuffer, bool requestFlush)
{
    auto& width = state->width;
    auto& height = state->height;
    auto& av_format_ctx = state->av_format_ctx;
    auto& av_codec_ctx = state->av_codec_ctx;
    auto& av_frame = state->av_frame;
    auto& av_packet = state->av_packet;
    auto& vid_str_idx = state->video_stream_index;
    auto& sws_scaler_ctx = state->sws_scaler_ctx;

    int response = processPacketsIntoFrames(state);

    if (response == AVERROR_EOF)
    {
        avcodec_flush_buffers(av_codec_ctx);
        return ErrorCode::FILE_EOF;
    }

    //setup scaler
    if (!sws_scaler_ctx)
    {
        sws_scaler_ctx = sws_getContext(width, height, av_codec_ctx->pix_fmt, //input
            width, height, AV_PIX_FMT_RGB0, //output
            SWS_BILINEAR, NULL, NULL, NULL); //options
    }
    if (!sws_scaler_ctx)
        return ErrorCode::NO_SCALER;
    uint64_t w = av_frame->width;
    uint64_t h = av_frame->height;
    uint64_t size = w * h * 4;
    unsigned char* output = new unsigned char[size];

    unsigned char* dest[4] = {output, NULL, NULL, NULL };
    int dest_linesize[4] = { width * 4, 0, 0, 0 };

    sws_scale(sws_scaler_ctx, av_frame->data, av_frame->linesize, 0, height, dest, dest_linesize);

    *frameBuffer = output;

    return ErrorCode::SUCCESS;
}

/*
* this returns int because the av_read_frame supercedes any other errors but those errors are sent back 
* and can be handled if returned. av_read_frame returns negative for errors and 0 for success.
* any errors for ErrorCode class are all positive
*/
int CMediaConverter::processPacketsIntoFrames(bool requestFlush)
{
    return processPacketsIntoFrames(&m_vrState, requestFlush);
}

int CMediaConverter::processPacketsIntoFrames(VideoReaderState* state, bool requestFlush)
{
    //inital frame read, gives it an initial state to branch from
    //int response = av_read_frame(state->av_format_ctx, state->av_packet);
    int response = readFrame(state);

    if (response >= 0)
    {
        //send back and receive decoded frames, until frames can't be read
        do {
            if (state->av_packet->stream_index != state->video_stream_index)
                continue;

            response = avcodec_send_packet(state->av_codec_ctx, requestFlush ? nullptr : state->av_packet);
            if (response < 0)
                return (int)ErrorCode::PKT_NOT_DECODED;

            response = avcodec_receive_frame(state->av_codec_ctx, state->av_frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
                continue;
            else if (response < 0)
                return (int)ErrorCode::PKT_NOT_RECEIVED;

            av_packet_unref(state->av_packet);
            break;
        } while (av_read_frame(state->av_format_ctx, state->av_packet) >= 0);
    }

    //retrieve stats
    state->frame_number = state->av_frame->coded_picture_number;
    state->frame_pts = state->av_frame->pts;
    state->frame_pkt_dts = state->av_frame->pkt_dts;
    state->key_frame = state->av_frame->key_frame;
    state->pkt_size = state->av_frame->pkt_size;

    return response;
}

int CMediaConverter::readFrame()
{
    return readFrame(&m_vrState);
}

int CMediaConverter::readFrame(VideoReaderState* state)
{
    int ret = av_read_frame(state->av_format_ctx, state->av_packet);
    //retrieve stats
    state->pkt_pts = state->av_packet->pts;
    state->pkt_dts = state->av_packet->dts;

    return ret;
}

int CMediaConverter::sendPacket()
{
    return sendPacket(&m_vrState);
}

int CMediaConverter::sendPacket(VideoReaderState* state)
{
    if (state->av_packet->stream_index != state->video_stream_index)
        return (int)ErrorCode::NO_VID_STREAM;

    int response = avcodec_send_packet(state->av_codec_ctx, state->av_packet);
    if (response < 0)
        return (int)ErrorCode::PKT_NOT_DECODED;

    return response;
}

int CMediaConverter::receiveFrame()
{
    return receiveFrame(&m_vrState);
}

int CMediaConverter::receiveFrame(VideoReaderState* state)
{
    return avcodec_receive_frame(state->av_codec_ctx, state->av_frame);
}

int CMediaConverter::outputToBuffer(FBPtr& fb_ptr)
{
    return outputToBuffer(&m_vrState, fb_ptr);
}

int CMediaConverter::outputToBuffer(VideoReaderState* state, FBPtr& fb_ptr)
{
    auto& sws_scaler_ctx = state->sws_scaler_ctx;
    auto& av_codec_ctx = state->av_codec_ctx;
    auto& width = state->width;
    auto& height = state->height;
    auto& av_frame = state->av_frame;

    //setup scaler
    if (!sws_scaler_ctx)
    {
        sws_scaler_ctx = sws_getContext(width, height, av_codec_ctx->pix_fmt, //input
            width, height, AV_PIX_FMT_RGB0, //output
            SWS_BILINEAR, NULL, NULL, NULL); //options
    }
    if (!sws_scaler_ctx)
        return (int)ErrorCode::NO_SCALER;
    uint64_t w = av_frame->width;
    uint64_t h = av_frame->height;
    uint64_t size = w * h * 4;
    fb_ptr.reset(new unsigned char[size]);

    unsigned char* dest[4] = { fb_ptr.get(), NULL, NULL, NULL };
    int dest_linesize[4] = { width * 4, 0, 0, 0 };

    sws_scale(sws_scaler_ctx, av_frame->data, av_frame->linesize, 0, height, dest, dest_linesize);

    return (int)ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::trackToFrame(int64_t targetPts)
{
    return trackToFrame(&m_vrState, targetPts);
}

ErrorCode CMediaConverter::trackToFrame(VideoReaderState* state, int64_t targetPts)
{
    seekToFrame(state, targetPts, true);
    auto ret = processPacketsIntoFrames(state);
    if (ret != (int)ErrorCode::SUCCESS)
        return (ErrorCode)ret;
    int64_t interval = state->FrameInterval() * state->FPS(); // interval starts at 1 second previous
    while (!WithinTolerance(targetPts, state->frame_pts, state->FrameInterval() - 10))
    {
        if (state->frame_pts < targetPts)
        {
            processPacketsIntoFrames(state);
        }
        else
        {
            interval *= 2; //double interval each time through to speed up seek
            seekToFrame(state, state->frame_pts - interval, true);
            auto ret = processPacketsIntoFrames(state);
            if (ret != (int)ErrorCode::SUCCESS)
                return (ErrorCode)ret;
        }
    }

    return ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::seekToFrame(int64_t targetPts, bool inReverse)
{
    return seekToFrame(&m_vrState, targetPts, inReverse);
}

ErrorCode CMediaConverter::seekToFrame(VideoReaderState* state, int64_t targetPts, bool inReverse)
{
    if (av_seek_frame(state->av_format_ctx, state->video_stream_index, targetPts, inReverse ? AVSEEK_FLAG_BACKWARD : AVSEEK_FLAG_ANY) >= 0)
    {
        avcodec_flush_buffers(state->av_codec_ctx);
        return ErrorCode::SUCCESS;
    }
    return ErrorCode::SEEK_FAILED;
}

ErrorCode CMediaConverter::seekToStart()
{
    return seekToStart(&m_vrState);
}

ErrorCode CMediaConverter::seekToStart(VideoReaderState* state)
{
    if (av_seek_frame(state->av_format_ctx, state->video_stream_index, 0, 0) >= 0)
    {
        avcodec_flush_buffers(state->av_codec_ctx);
        return ErrorCode::SUCCESS;
    }
    return ErrorCode::SEEK_FAILED;
}

ErrorCode CMediaConverter::closeVideoReader()
{
    return closeVideoReader(&m_vrState);
}

ErrorCode CMediaConverter::closeVideoReader(VideoReaderState* state)
{
    sws_freeContext(state->sws_scaler_ctx);
    avformat_close_input(&state->av_format_ctx);
    avformat_free_context(state->av_format_ctx);
    av_frame_free(&state->av_frame);
    av_packet_free(&state->av_packet);
    return ErrorCode::SUCCESS;
}

bool CMediaConverter::WithinTolerance(int64_t referencePts, int64_t targetPts, int64_t tolerance)
{
    return std::abs(targetPts - referencePts) < tolerance;
}