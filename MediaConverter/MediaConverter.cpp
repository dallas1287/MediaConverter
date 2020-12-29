// MediaConverter.cpp : Defines the exported functions for the DLL.
//
#include "pch.h"
#include "framework.h"
#include "MediaConverter.h"
#include <thread>

// This is the constructor of a class that has been exported.
CMediaConverter::CMediaConverter()
{
}

CMediaConverter::~CMediaConverter()
{
}

ErrorCode CMediaConverter::openVideoReader(const char* filename)
{
    return openVideoReader(&m_mrState, filename);
}

ErrorCode CMediaConverter::openVideoReader(MediaReaderState* state, const char* filename)
{
    auto& av_format_ctx = state->av_format_ctx;
    auto& av_codec_ctx = state->video_codec_ctx;
    auto& av_frame = state->av_frame;
    auto& av_packet = state->av_packet;
    auto& sws_scaler_ctx = state->sws_scaler_ctx;

    av_format_ctx = avformat_alloc_context();
    if (!av_format_ctx)
        return ErrorCode::NO_FMT_CTX;

    if (avformat_open_input(&av_format_ctx, filename, NULL, NULL)) //returns 0 on success
        return ErrorCode::FMT_UNOPENED;

    if (av_format_ctx->nb_streams < 1)
        return ErrorCode::NO_STREAMS;

    state->av_format_ctx->seek2any = 1;

    state->video_stream_index = -1;
    state->audio_stream_index = -1;
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
            state->video_stream_index = i;

            av_codec_ctx = avcodec_alloc_context3(av_codec);
            if (!av_codec_ctx)
                return ErrorCode::NO_CODEC_CTX;

            av_codec_ctx->thread_count = std::thread::hardware_concurrency();

            if (avcodec_parameters_to_context(av_codec_ctx, av_codec_params) < 0)
                return ErrorCode::CODEC_CTX_UNINIT;

            if (avcodec_open2(av_codec_ctx, av_codec, NULL) < 0)
                return ErrorCode::CODEC_UNOPENED;
        }
        else if (av_codec_params->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            state->audio_stream_index = i;
            state->audio_codec_ctx = avcodec_alloc_context3(av_codec);
            if (!state->audio_codec_ctx)
                return ErrorCode::NO_CODEC_CTX;

            state->audio_codec_ctx->thread_count = 8;

            if (avcodec_parameters_to_context(state->audio_codec_ctx, av_codec_params) < 0)
                return ErrorCode::CODEC_CTX_UNINIT;

            if (avcodec_open2(state->audio_codec_ctx, av_codec, NULL) < 0)
                return ErrorCode::CODEC_UNOPENED;

            if (state->audio_codec_ctx->channel_layout == 0)
                state->audio_codec_ctx->channel_layout = AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT;
        }
    }

    av_frame = av_frame_alloc();
    if (!av_frame)
        return ErrorCode::NO_FRAME;

    av_packet = av_packet_alloc();
    if (!av_packet)
        return ErrorCode::NO_PACKET;

    state->SetIsOpened();
    return ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::readVideoFrame(VideoBuffer& buffer)
{
    return readVideoFrame(&m_mrState, buffer);
}

ErrorCode CMediaConverter::readVideoFrame(MediaReaderState* state, VideoBuffer& buffer)
{
    int response = processVideoPacketsIntoFrames(state);

    if (response == AVERROR_EOF)
    {
        avcodec_flush_buffers(state->video_codec_ctx);
        return ErrorCode::FILE_EOF;
    }

    return (ErrorCode)outputToBuffer(state, buffer);
}

ErrorCode CMediaConverter::readAudioFrame(AudioBuffer& audioBuffer)
{
    return readAudioFrame(&m_mrState, audioBuffer);
}

ErrorCode CMediaConverter::readAudioFrame(MediaReaderState* state, AudioBuffer& audioBuffer)
{
    int response = processAudioPacketsIntoFrames(state);

    if (response == AVERROR_EOF)
    {
        avcodec_flush_buffers(state->audio_codec_ctx);
        return ErrorCode::FILE_EOF;
    }

    if(response == (int)ErrorCode::SUCCESS)
        return (ErrorCode)outputToAudioBuffer(state, audioBuffer);

    return (ErrorCode)response;
}

/*
* this returns int because the av_read_frame supercedes any other errors but those errors are sent back 
* and can be handled if returned. av_read_frame returns negative for errors and 0 for success.
* any errors for ErrorCode class are all positive
*/
int CMediaConverter::processVideoPacketsIntoFrames()
{
    return processVideoPacketsIntoFrames(&m_mrState);
}

int CMediaConverter::processVideoPacketsIntoFrames(MediaReaderState* state)
{
    //inital frame read, gives it an initial state to branch from
    int response = readFrame(state);

    if (response >= 0)
        return processVideoIntoFrames(state);

    return response;
}

int CMediaConverter::processAudioPacketsIntoFrames()
{
    return processAudioPacketsIntoFrames(&m_mrState);
}

int CMediaConverter::processAudioPacketsIntoFrames(MediaReaderState* state)
{
    int response = readFrame(state);

    if (response >= 0)
    {
        return processAudioIntoFrames(state);
    }

    return response;
}

int CMediaConverter::processVideoIntoFrames(MediaReaderState* state)
{
    if (!state->video_codec_ctx)
        return (int)ErrorCode::NO_CODEC_CTX;

    int response = 0;
    //send back and receive decoded frames, until frames can't be read
    do {
        if (state->av_packet->stream_index != state->video_stream_index)
            continue;

        response = avcodec_send_packet(state->video_codec_ctx, state->av_packet);
        if (response < 0)
            return (int)ErrorCode::PKT_NOT_DECODED;

        response = avcodec_receive_frame(state->video_codec_ctx, state->av_frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
            continue;
        else if (response < 0)
            return (int)ErrorCode::PKT_NOT_RECEIVED;

        av_packet_unref(state->av_packet);
        break;
    } while (readFrame(state) >= 0);

    //retrieve stats
    if(response == (int)ErrorCode::SUCCESS)
        state->videoFrameData.FillDataFromFrame(state->av_frame);

    return response;
}

int CMediaConverter::processAudioIntoFrames(MediaReaderState* state)
{
    if (!state->audio_codec_ctx)
        return (int)ErrorCode::NO_CODEC_CTX;

    int response = 0;
    //send back and receive decoded frames, until frames can't be read
    do {
        if (state->av_packet->stream_index != state->audio_stream_index)
            continue;

        response = avcodec_send_packet(state->audio_codec_ctx, state->av_packet);

        if (response < 0)
            return (int)ErrorCode::PKT_NOT_DECODED;

        response = avcodec_receive_frame(state->audio_codec_ctx, state->av_frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
            continue;
        else if (response < 0)
            return (int)ErrorCode::PKT_NOT_RECEIVED;

        av_packet_unref(state->av_packet);
        break;
    } while (readFrame(state) >= 0);

    //update frame data as necessary
    state->audioFrameData.FillDataFromFrame(state->av_frame);
    state->audioFrameData.UpdateBitRate(state->audio_codec_ctx);

    return response;
}

int CMediaConverter::readFrame()
{
    return readFrame(&m_mrState);
}

int CMediaConverter::readFrame(MediaReaderState* state)
{
    if (!state->av_format_ctx)
        return (int)ErrorCode::NO_FMT_CTX;
    int ret = av_read_frame(state->av_format_ctx, state->av_packet);
    //retrieve stats
    if(ret == (int)ErrorCode::SUCCESS)
        state->videoFrameData.FillDataFromPacket(state->av_packet);

    return ret;
}

int CMediaConverter::outputToBuffer(VideoBuffer& buffer)
{
    return outputToBuffer(&m_mrState, buffer);
}

int CMediaConverter::outputToBuffer(MediaReaderState* state, VideoBuffer& buffer)
{
    auto& sws_scaler_ctx = state->sws_scaler_ctx;
    auto& av_codec_ctx = state->video_codec_ctx;
    auto& av_frame = state->av_frame;
    if (!av_codec_ctx)
        return -1;
    //setup scaler
    if (!sws_scaler_ctx)
    {
        sws_scaler_ctx = sws_getContext(state->VideoWidth(), state->VideoHeight(), av_codec_ctx->pix_fmt, //input
            state->VideoWidth(), state->VideoHeight(), AV_PIX_FMT_RGB0, //output
            SWS_BILINEAR, NULL, NULL, NULL); //options
    }
    if (!sws_scaler_ctx)
        return (int)ErrorCode::NO_SCALER;
    uint64_t w = av_frame->width;
    uint64_t h = av_frame->height;
    uint64_t size = w * h * 4;

    if (size == 0)
        return -1;

    buffer.resize(size);

    //using 4 here because RGB0 designates 4 channels of values
    unsigned char* dest[4] = { &buffer[0], NULL, NULL, NULL };
    int dest_linesize[4] = { state->VideoWidth() * 4, 0, 0, 0 };

    sws_scale(sws_scaler_ctx, av_frame->data, av_frame->linesize, 0, state->VideoHeight(), dest, dest_linesize);

    av_frame_unref(av_frame);

    return (int)ErrorCode::SUCCESS;
}

int CMediaConverter::outputToAudioBuffer(AudioBuffer& audioBuffer)
{
    return outputToAudioBuffer(&m_mrState, audioBuffer);
}

int CMediaConverter::outputToAudioBuffer(MediaReaderState* state, AudioBuffer& audioBuffer)
{
    if (audioBuffer.size() != state->AudioBufferSize() && state->AudioBufferSize() > 0)
        audioBuffer.resize(state->AudioBufferSize());
    else
        return (int)ErrorCode::NO_DATA_AVAIL;

    auto ptr = &audioBuffer[0];
    
    state->swr_ctx = swr_alloc_set_opts(nullptr, AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT, AV_SAMPLE_FMT_FLT, state->SampleRate(),
        state->audio_codec_ctx->channel_layout, state->AudioSampleFormat(), state->SampleRate(), 0, nullptr);

    if (!state->swr_ctx)
        return (int)ErrorCode::NO_SWR_CTX;

    swr_init(state->swr_ctx);

    int got_samples = swr_convert(state->swr_ctx, &ptr, state->NumSamples(), (const uint8_t**)state->av_frame->extended_data, state->av_frame->nb_samples);

    if(got_samples < 0)
        return (int)ErrorCode::NO_SWR_CONVERT;

    while (got_samples > 0)
    {
        got_samples = swr_convert(state->swr_ctx, &ptr, state->NumSamples(), nullptr, 0);
        if (got_samples < 0)
            return (int)ErrorCode::NO_SWR_CONVERT;
    }

    av_frame_unref(state->av_frame);

    return (int)ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::trackToFrame(int64_t targetPts)
{
    return trackToFrame(&m_mrState, targetPts);
}

ErrorCode CMediaConverter::trackToFrame(MediaReaderState* state, int64_t targetPts)
{
    seekToFrame(state, targetPts);
    auto ret = processVideoPacketsIntoFrames(state);
    if (ret != (int)ErrorCode::SUCCESS)
        return (ErrorCode)ret;
    int64_t interval = state->VideoFrameInterval() * state->FPS(); // interval starts at 1 second previous
    int64_t previous = state->VideoFramePts();
    while (!WithinTolerance(targetPts, state->VideoFramePts(), state->VideoFrameInterval() - 10))
    {
        if (state->VideoFramePts() < targetPts)
        {
            processVideoPacketsIntoFrames(state);
        }
        else
        {
            seekToFrame(state, state->VideoFramePts() - interval);
            auto ret = processVideoPacketsIntoFrames(state);
            if (ret != (int)ErrorCode::SUCCESS)
                return (ErrorCode)ret;
            interval *= 2;
        }

        if (previous == state->VideoFramePts())
            return ErrorCode::SUCCESS;
        previous = state->VideoFramePts();
    }

    return ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::trackToAudioFrame(int64_t targetPts)
{
    return trackToAudioFrame(&m_mrState, targetPts);
}

ErrorCode CMediaConverter::trackToAudioFrame(MediaReaderState* state, int64_t targetPts)
{
    if (!state->audio_codec_ctx)
        return ErrorCode::NO_CODEC_CTX;
    if (state->audio_stream_index < 0)
        return ErrorCode::NO_STREAMS;

    seekToAudioFrame(state, targetPts);
    auto ret = processAudioPacketsIntoFrames(state);
    if (ret != (int)ErrorCode::SUCCESS)
        return (ErrorCode)ret;
    int64_t interval = state->AudioFrameInterval() * state->FPS(); // interval starts at 1 second previous
    int64_t previous = state->AudioPts();
    while (!WithinTolerance(targetPts, state->AudioPts(), state->AudioFrameInterval() - 10))
    {
        if (state->AudioPts() < targetPts)
        {
            processAudioPacketsIntoFrames(state);
        }
        else
        {
            seekToAudioFrame(state, state->AudioPts() - interval);
            auto ret = processAudioPacketsIntoFrames(state);
            if (ret != (int)ErrorCode::SUCCESS)
                return (ErrorCode)ret;
        }

        if (previous == state->AudioPts())
            return ErrorCode::SUCCESS;
        previous = state->AudioPts();
    }

    return ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::seekToFrame(int64_t targetPts)
{
    return seekToFrame(&m_mrState, targetPts);
}

ErrorCode CMediaConverter::seekToFrame(MediaReaderState* state, int64_t targetPts)
{
    if (av_seek_frame(state->av_format_ctx, state->video_stream_index, targetPts, AVSEEK_FLAG_BACKWARD) >= 0)
    {
        avcodec_flush_buffers(state->video_codec_ctx);
        return ErrorCode::SUCCESS;
    }
    return ErrorCode::SEEK_FAILED;
}

ErrorCode CMediaConverter::seekToAudioFrame(int64_t targetPts)
{
    return seekToAudioFrame(&m_mrState, targetPts);
}

ErrorCode CMediaConverter::seekToAudioFrame(MediaReaderState* state, int64_t targetPts)
{
    if (av_seek_frame(state->av_format_ctx, state->audio_stream_index, targetPts, AVSEEK_FLAG_BACKWARD) >= 0)
    {
        avcodec_flush_buffers(state->audio_codec_ctx);
        return ErrorCode::SUCCESS;
    }
    return ErrorCode::SEEK_FAILED;
}

ErrorCode CMediaConverter::seekToStart()
{
    return seekToStart(&m_mrState);
}

ErrorCode CMediaConverter::seekToStart(MediaReaderState* state)
{
    if (av_seek_frame(state->av_format_ctx, state->video_stream_index, 0, 0) >= 0)
    {
        avcodec_flush_buffers(state->video_codec_ctx);
        return ErrorCode::SUCCESS;
    }
    return ErrorCode::SEEK_FAILED;
}

ErrorCode CMediaConverter::seekToAudioStart()
{
    return seekToAudioStart(&m_mrState);
}

ErrorCode CMediaConverter::seekToAudioStart(MediaReaderState* state)
{
    if (av_seek_frame(state->av_format_ctx, state->audio_stream_index, 0, 0) >= 0)
    {
        avcodec_flush_buffers(state->audio_codec_ctx);
        return ErrorCode::SUCCESS;
    }
    return ErrorCode::SEEK_FAILED;
}

ErrorCode CMediaConverter::closeVideoReader()
{
    return closeVideoReader(&m_mrState);
}

ErrorCode CMediaConverter::closeVideoReader(MediaReaderState* state)
{
    sws_freeContext(state->sws_scaler_ctx);
    swr_free(&state->swr_ctx);
    avformat_close_input(&state->av_format_ctx);
    avformat_free_context(state->av_format_ctx);
    avcodec_free_context(&state->video_codec_ctx);
    avcodec_free_context(&state->audio_codec_ctx);
    av_frame_free(&state->av_frame);
    av_packet_free(&state->av_packet);
    state->SetIsOpened(false);
    return ErrorCode::SUCCESS;
}

bool CMediaConverter::WithinTolerance(int64_t referencePts, int64_t targetPts, int64_t tolerance)
{
    return std::abs(targetPts - referencePts) < tolerance;
}

ErrorCode CMediaConverter::readVideoReaderFrame(unsigned char** frameBuffer, bool requestFlush)
{
    return readVideoReaderFrame(&m_mrState, frameBuffer, requestFlush);
}

ErrorCode CMediaConverter::readVideoReaderFrame(MediaReaderState* state, unsigned char** frameBuffer, bool requestFlush)
{
    auto& av_format_ctx = state->av_format_ctx;
    auto& av_codec_ctx = state->video_codec_ctx;
    auto& av_frame = state->av_frame;
    auto& av_packet = state->av_packet;
    auto& vid_str_idx = state->video_stream_index;
    auto& sws_scaler_ctx = state->sws_scaler_ctx;

    int response = processVideoPacketsIntoFrames(state);

    if (response == AVERROR_EOF)
    {
        avcodec_flush_buffers(av_codec_ctx);
        return ErrorCode::FILE_EOF;
    }

    //setup scaler
    if (!sws_scaler_ctx)
    {
        sws_scaler_ctx = sws_getContext(state->VideoWidth(), state->VideoHeight(), av_codec_ctx->pix_fmt, //input
            state->VideoWidth(), state->VideoHeight(), AV_PIX_FMT_RGB0, //output
            SWS_BILINEAR, NULL, NULL, NULL); //options
    }
    if (!sws_scaler_ctx)
        return ErrorCode::NO_SCALER;
    uint64_t w = av_frame->width;
    uint64_t h = av_frame->height;
    uint64_t size = w * h * 4;
    unsigned char* output = new unsigned char[size];

    //using 4 here because RGB0 designates 4 channels of values
    unsigned char* dest[4] = { output, NULL, NULL, NULL };
    int dest_linesize[4] = { state->VideoWidth() * 4, 0, 0, 0 };

    sws_scale(sws_scaler_ctx, av_frame->data, av_frame->linesize, 0, state->VideoHeight(), dest, dest_linesize);

    *frameBuffer = output;

    return ErrorCode::SUCCESS;
}

ErrorCode CMediaConverter::encodeMedia(const char* inFile, const char* outFile)
{
    return encodeMedia(inFile, outFile, &m_mrState);
}

ErrorCode CMediaConverter::encodeMedia(const char* inFile, const char* outFile, MediaReaderState* state)
{
    if (avformat_open_input(&state->av_format_ctx, inFile, nullptr, nullptr) < 0)
        return ErrorCode::FMT_UNOPENED;

    if (avformat_find_stream_info(state->av_format_ctx, nullptr) < 0)
        return ErrorCode::NO_STREAMS;

    AVFormatContext* out_ctx = nullptr;
    avformat_alloc_output_context2(&out_ctx, nullptr, nullptr, outFile);
    if (!out_ctx)
        return ErrorCode::NO_CODEC_CTX;

    int num_streams = state->av_format_ctx->nb_streams;
    if (num_streams <= 0)
        return ErrorCode::NO_STREAMS;

    int* streams_list = nullptr;
    streams_list = (int*)av_mallocz_array(num_streams, sizeof(*streams_list));
    if (!streams_list)
        return ErrorCode::NO_STREAMS;

    int stream_index = 0;
    for (unsigned int i = 0; i < state->av_format_ctx->nb_streams; ++i)
    {
        AVStream* out_stream = nullptr;
        AVStream* in_stream = state->av_format_ctx->streams[i];
        AVCodecParameters* in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
        {
            streams_list[i] = -1;
            continue;
        }

        streams_list[i] = stream_index++;
        out_stream = avformat_new_stream(out_ctx, nullptr);
        if (!out_stream)
            return ErrorCode::NO_CODEC_CTX;

        if (avcodec_parameters_copy(out_stream->codecpar, in_codecpar) < 0)
            return ErrorCode::NO_CODEC_CTX;
    }

    av_dump_format(out_ctx, 0, outFile, 1);

    if (!(out_ctx->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&out_ctx->pb, outFile, AVIO_FLAG_WRITE) < 0)
            return ErrorCode::NO_OUTPUT_FILE;
    }

    AVDictionary* opts = nullptr;
    //av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);

    if (avformat_write_header(out_ctx, &opts) < 0)
        return ErrorCode::NO_OUTPUT_FILE;

    AVPacket pkt;
    while (1) //this will end up linking up with the start and stop frames of the files
    {
        AVStream* in_stream = nullptr;
        AVStream* out_stream = nullptr;
        if (av_read_frame(state->av_format_ctx, &pkt) < 0)
            break;

        in_stream = state->av_format_ctx->streams[pkt.stream_index];
        if (pkt.stream_index > num_streams || streams_list[pkt.stream_index] < 0)
        {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = streams_list[pkt.stream_index];
        out_stream = out_ctx->streams[pkt.stream_index];
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;

        av_interleaved_write_frame(out_ctx, &pkt);

        av_packet_unref(&pkt);  
    }

    av_write_trailer(out_ctx);

    avformat_close_input(&state->av_format_ctx);
    if (out_ctx && !(out_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&out_ctx->pb);

    avformat_free_context(out_ctx);
    av_freep(&streams_list);
    return ErrorCode::SUCCESS;
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