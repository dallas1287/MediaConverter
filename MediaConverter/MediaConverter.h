// The following ifdef block is the standard way of creating macros which make exporting
// from a DLL simpler. All files within this DLL are compiled with the MEDIACONVERTER_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see
// MEDIACONVERTER_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef MEDIACONVERTER_EXPORTS
#define MEDIACONVERTER_API __declspec(dllexport)
#else
#define MEDIACONVERTER_API __declspec(dllimport)
#endif
#include <iostream>

//ffmpeg includes
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

enum class ErrorCode : int
{
	FILE_EOF = -1,
	SUCCESS,
	NO_FMT_CTX,
	FMT_UNOPENED,
	NO_CODEC,
	CODEC_UNOPENED,
	NO_STREAMS,
	NO_VID_STREAM,
	NO_CODEC_CTX,
	CODEC_CTX_UNINIT,
	NO_FRAME,
	NO_PACKET,
	PKT_NOT_DECODED,
	PKT_NOT_RECEIVED,
	NO_SCALER,
	SEEK_FAILED
};

class MEDIACONVERTER_API VideoReaderState
{
public:
	VideoReaderState() {}
	VideoReaderState(const VideoReaderState& other)
	{
		width = other.width;
		height = other.height;
		frame_buffer = other.frame_buffer;
		av_format_ctx = other.av_format_ctx;
		av_codec_ctx = other.av_codec_ctx;
		av_frame = other.av_frame;
		av_packet = other.av_packet;
		sws_scaler_ctx = other.sws_scaler_ctx;
		video_stream_index = other.video_stream_index;
		timebase.num = other.timebase.num;
		timebase.den = other.timebase.den;
		codecName = other.codecName;
		frame_number = other.frame_number;
		pts = other.pts;
		pkt_dts = other.pkt_dts;
		key_frame = other.key_frame;
		start_time = other.start_time;
		duration = other.duration;
		avg_frame_rate.num = other.avg_frame_rate.num;
		avg_frame_rate.den = other.avg_frame_rate.den;
		pkt_size = other.pkt_size;
	}
	~VideoReaderState() {}
	bool IsEqual(const VideoReaderState& other)
	{
		return width == other.width &&
			height == other.height &&
			frame_buffer == other.frame_buffer &&
			av_format_ctx == other.av_format_ctx &&
			av_codec_ctx == other.av_codec_ctx &&
			av_frame == other.av_frame &&
			av_packet == other.av_packet &&
			sws_scaler_ctx == other.sws_scaler_ctx &&
			video_stream_index == other.video_stream_index &&
			timebase.num == other.timebase.num &&
			timebase.den == other.timebase.den &&
			codecName == other.codecName &&
			frame_number == other.frame_number &&
			pts == other.pts &&
			pkt_dts == other.pkt_dts &&
			key_frame == other.key_frame &&
			start_time == other.start_time &&
			duration == other.duration &&
			avg_frame_rate.num == other.avg_frame_rate.num &&
			avg_frame_rate.den == other.avg_frame_rate.den &&
			pkt_size == other.pkt_size;
	}

	int64_t FrameInterval()
	{
		if (timebase.num <= 0 || timebase.den <= 0 ||
			avg_frame_rate.num <= 0 || avg_frame_rate.den <= 0)
			return 0;

		return timebase.den / avg_frame_rate.num;
	}

	int width = 0;
	int height = 0;
	uint8_t* frame_buffer = nullptr;

	AVFormatContext* av_format_ctx = nullptr;
	AVCodecContext* av_codec_ctx = nullptr;
	AVFrame* av_frame = nullptr;
	AVPacket* av_packet = nullptr;
	SwsContext* sws_scaler_ctx = nullptr;
	int video_stream_index = -1;
	AVRational timebase = { -1, -1 };

	std::string codecName;
	int frame_number = -1;
	int64_t pts = -1;
	int64_t pkt_dts = -1;
	int key_frame = -1;
	int64_t start_time = -1;
	int64_t duration = -1;
	AVRational avg_frame_rate = { -1, -1 };
	size_t pkt_size = -1;
};

// This class is exported from the dll
class MEDIACONVERTER_API CMediaConverter 
{
	typedef std::unique_ptr<unsigned char[]> FBPtr;

public:
	CMediaConverter(void);
	ErrorCode loadFrame(const char* filename, int& width, int& height, unsigned char** data);
	ErrorCode openVideoReader(VideoReaderState* state, const char* filename);
	ErrorCode readVideoReaderFrame(VideoReaderState* state, FBPtr& fb_ptr, bool requestFlush = false);
	ErrorCode readVideoReaderFrame(VideoReaderState* state, unsigned char** frameBuffer, bool requestFlush = false); //unmanaged data version, creates heap data in function
	ErrorCode readVideoReaderFrameAt(VideoReaderState* state, FBPtr& fb_ptr, int64_t targetPts, bool requestFlush = false);
	int processPacketsIntoFrames(VideoReaderState* state, bool requestFlush = false);
	ErrorCode seekToFrame(VideoReaderState* state, int64_t targetPts);
	ErrorCode seekToStart(VideoReaderState* state);
	ErrorCode rewindFrame(VideoReaderState* state, FBPtr& fb_ptr);
	ErrorCode rewindFrame(VideoReaderState* state, unsigned char** frame_buffer); //unmanaged data version, creates heap data in function
	ErrorCode rewindToBuffer(VideoReaderState* state, FBPtr& fbptr);
	ErrorCode rewindToBuffer(VideoReaderState* state, unsigned char** frame_buffer); //unmanaged data version, creates heap data in function
	ErrorCode closeVideoReader(VideoReaderState* state);
};
