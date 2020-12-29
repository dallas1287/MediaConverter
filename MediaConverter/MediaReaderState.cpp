#include "pch.h"
#include "framework.h"
#include "MediaReaderState.h"
#include <algorithm>
#include <cmath>

MediaReaderState::MediaReaderState()
{
}

MediaReaderState::MediaReaderState(const MediaReaderState& other) : 
av_format_ctx(other.av_format_ctx), 
video_codec_ctx(other.video_codec_ctx),
av_frame(other.av_frame),
av_packet(other.av_packet),
sws_scaler_ctx(other.sws_scaler_ctx),
video_stream_index(other.video_stream_index),
audio_codec_ctx(other.audio_codec_ctx),
audio_stream_index(other.audio_stream_index)
{
}

MediaReaderState::~MediaReaderState()
{
}

bool MediaReaderState::IsEqual(const MediaReaderState& other)
{
	return av_format_ctx == other.av_format_ctx &&
		video_codec_ctx == other.video_codec_ctx &&
		av_frame == other.av_frame &&
		av_packet == other.av_packet &&
		sws_scaler_ctx == other.sws_scaler_ctx &&
		video_stream_index == other.video_stream_index &&
		audio_codec_ctx == other.audio_codec_ctx &&
		audio_stream_index == other.audio_stream_index;
}

int MediaReaderState::FPS() const
{
	if (!IsRationalValid(VideoAvgFrameRate()))
		return 0;

	//using round here because i have seen avg_frame_rates report strange values that end up @ 30.03 fps
	return (int)std::round(VideoAvgFrameRateDbl());
}

int64_t MediaReaderState::VideoFrameInterval() const
{
	if (!IsRationalValid(VideoTimebase()))
		return 1;
	auto ret = VideoTimebase().den / (double)VideoTimebase().num / VideoAvgFrameRateDbl();
	if (ret == 0)
		return 1;
	return (int64_t)ret;
}

int64_t MediaReaderState::AudioFrameInterval() const
{
	if (!IsRationalValid(AudioTimebase()) || !IsRationalValid(AudioAvgFrameRate()))
		return (std::max)((int64_t)1, audio_frame_interval);
	auto ret = AudioTimebase().den / (double)AudioTimebase().num / AudioAvgFrameRateDbl();
	if (ret == 0)
		return audio_frame_interval;
	return (int64_t)ret;
}

AVCodecContext* MediaReaderState::GetCodecCtxFromPkt()
{
	return GetCodecCtxFromPkt(av_packet);
}

AVCodecContext* MediaReaderState::GetCodecCtxFromPkt(AVPacket* pkt)
{
	if (pkt->stream_index == video_stream_index)
		return video_codec_ctx;
	else if (pkt->stream_index == audio_stream_index)
		return audio_codec_ctx;

	return nullptr;
}

int64_t MediaReaderState::VideoDuration() const
{
	if (!HasVideoStream())
		return 0;
	return av_format_ctx->streams[video_stream_index]->duration;
}

int64_t MediaReaderState::VideoStartTime() const
{
	if (!HasVideoStream())
		return 0;
	return av_format_ctx->streams[video_stream_index]->start_time;
}

AVRational MediaReaderState::VideoAvgFrameRate() const
{
	if (!HasVideoStream())
		return av_make_q(-1, -1); //returns invalid values 

	return av_format_ctx->streams[video_stream_index]->avg_frame_rate;
}

double MediaReaderState::VideoAvgFrameRateDbl() const
{
	if (!HasVideoStream())
		return 0.0;
	return av_q2d(VideoAvgFrameRate());
}

AVRational MediaReaderState::VideoTimebase() const
{
	if (!HasVideoStream())
		return av_make_q(-1, -1); //returns invalid values 

	return av_format_ctx->streams[video_stream_index]->time_base;
}

double MediaReaderState::VideoTimebaseDbl() const
{
	if (!HasVideoStream())
		return 0.0;
	return av_q2d(VideoTimebase());
}

int64_t MediaReaderState::VideoFrameCt() const
{
	if (!HasVideoStream())
		return 0;
	return av_format_ctx->streams[video_stream_index]->nb_frames;
}

int MediaReaderState::VideoWidth() const
{
	if (!HasVideoStream())
		return 0;
	return av_format_ctx->streams[video_stream_index]->codecpar->width;
}

int MediaReaderState::VideoHeight() const
{
	if (!HasVideoStream())
		return 0;
	return av_format_ctx->streams[video_stream_index]->codecpar->height;
}

int64_t MediaReaderState::AudioDuration() const
{
	if (!HasAudioStream())
		return 0;
	return av_format_ctx->streams[audio_stream_index]->duration;
}

int64_t MediaReaderState::AudioStartTime() const
{
	if (!HasAudioStream())
		return 0;
	return av_format_ctx->streams[audio_stream_index]->start_time;
}

AVRational MediaReaderState::AudioAvgFrameRate() const
{
	if (!HasAudioStream())
		return av_make_q(-1, -1); //returns invalid values 

	return av_format_ctx->streams[audio_stream_index]->avg_frame_rate;
}

double MediaReaderState::AudioAvgFrameRateDbl() const
{
	if (!HasAudioStream())
		return 0.0;
	if (!IsRationalValid(AudioAvgFrameRate()))
		return 0.0;
	return av_q2d(AudioAvgFrameRate());
}

AVRational MediaReaderState::AudioTimebase() const
{
	if (!HasAudioStream())
		return av_make_q(-1, -1); //returns invalid values 
	return av_format_ctx->streams[audio_stream_index]->time_base;
}

double MediaReaderState::AudioTimebaseDbl() const
{
	if (!HasAudioStream())
		return 0.0;
	return av_q2d(AudioTimebase());
}

int MediaReaderState::AudioFrameSize() const
{
	if (!HasAudioStream() || !audio_codec_ctx)
		return 0;

	return audio_codec_ctx->frame_size;
}

const char* MediaReaderState::CodecName()
{
	int streamIndex = HasVideoStream() ? video_stream_index : HasAudioStream() ? audio_stream_index : -1;
	if (streamIndex == -1)
		return nullptr;

	AVCodecParameters* av_codec_params = av_format_ctx->streams[streamIndex]->codecpar;
	if (!av_codec_params)
		return nullptr;
	AVCodec* av_codec = avcodec_find_decoder(av_codec_params->codec_id);
	if (!av_codec)
		return nullptr;

	return av_codec->long_name;
}

AVSampleFormat MediaReaderState::AudioSampleFormat() const
{
	if (!HasAudioStream() || !audio_codec_ctx)
		return AV_SAMPLE_FMT_NONE;

	return audio_codec_ctx->sample_fmt;
}

void MediaReaderState::SetAudioFrameInterval(int64_t interval)
{
	if (interval < 0)
		return;
	audio_frame_interval = interval;
}

bool MediaReaderState::IsRationalValid(const AVRational& rational) const
{
	//num can be 0 but den can't 
	//generally if one reports -1 then they both will 
	return (rational.den > 0 && rational.num >= 0);
}

VideoFrameData::VideoFrameData()
{
}

VideoFrameData::VideoFrameData(const VideoFrameData& other) 
{ 
	*this = other; 
}

VideoFrameData::~VideoFrameData()
{
}

VideoFrameData& VideoFrameData::operator=(const VideoFrameData& other)
{
	frame_number = other.frame_number;
	pkt_pts = other.pkt_pts;
	frame_pts = other.frame_pts;
	pkt_dts = other.pkt_dts;
	frame_pkt_dts = other.frame_pkt_dts;
	key_frame = other.key_frame;
	pkt_size = other.pkt_size;
	return *this;
}

bool VideoFrameData::operator==(const VideoFrameData& other)
{
	return frame_number == other.frame_number &&
		pkt_pts == other.pkt_pts &&
		frame_pts == other.frame_pts &&
		pkt_dts == other.pkt_dts &&
		frame_pkt_dts == other.frame_pkt_dts &&
		key_frame == other.key_frame &&
		pkt_size == other.pkt_size;
}

bool VideoFrameData::FillDataFromFrame(AVFrame* frame)
{
	if (!frame)
		return false;
	frame_number = frame->coded_picture_number;
	frame_pts = frame->pts;
	frame_pkt_dts = frame->pkt_dts;
	key_frame = frame->key_frame;
	pkt_size = frame->pkt_size;
	return true;
}

bool VideoFrameData::FillDataFromPacket(AVPacket* packet)
{
	if (!packet)
		return false;
	pkt_pts = packet->pts;
	pkt_dts = packet->dts;
	return true;
}

AudioFrameData::AudioFrameData()
{
}

AudioFrameData::AudioFrameData(const AudioFrameData& other) 
{ 
	*this = other; 
}

AudioFrameData::~AudioFrameData()
{
}

AudioFrameData& AudioFrameData::operator=(const AudioFrameData& other)
{
	num_channels = other.num_channels;
	sample_rate = other.sample_rate;
	line_size = other.line_size;
	num_samples = other.num_samples;
	audio_pts = other.audio_pts;
	bit_rate = other.bit_rate;
	return *this;
}

bool AudioFrameData::operator==(const AudioFrameData& other)
{
	return num_channels == other.num_channels &&
		sample_rate == other.sample_rate &&
		line_size == other.line_size &&
		num_samples == other.num_samples &&
		audio_pts == other.audio_pts &&
		bit_rate == other.bit_rate;
}

//the data provided by the frame isn't necessarily the same for each frame
//so we selectively update values based on certain needs 
bool AudioFrameData::FillDataFromFrame(AVFrame* frame)
{
	if (!frame)
		return false;

	if (line_size < 0 && frame->linesize[0] > 0)
		line_size = frame->linesize[0];
	if (num_channels < 0 && frame->channels > 0)
		num_channels = frame->channels;
	if (sample_rate < 0 && frame->sample_rate > 0)
		sample_rate = frame->sample_rate;
	if (num_samples < frame->nb_samples)
		num_samples = frame->nb_samples;

	audio_pts = frame->pts;
	audio_dts = frame->pkt_dts;
	best_effort_ts = frame->best_effort_timestamp;
	return true;
}

bool AudioFrameData::UpdateBitRate(AVCodecContext* ctx)
{
	if (!ctx)
		return false;

	if (bit_rate < 0 || ctx->bit_rate > 0)
		bit_rate = ctx->bit_rate;

	return true;
}

int AudioFrameData::BufferSize(AVSampleFormat fmt) const
{
	auto packed = av_get_packed_sample_fmt(fmt);
	int linesize = 0;
	return av_samples_get_buffer_size(&linesize, num_channels, num_samples, packed, 1);
}