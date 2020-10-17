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
#include <vector>
#include <iostream>

//ffmpeg includes
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

enum class ErrorCode : int
{
	AGAIN = -2,
	FILE_EOF = -1,
	SUCCESS,
	NO_FMT_CTX,
	FMT_UNOPENED,
	NO_CODEC,
	CODEC_UNOPENED,
	NO_STREAMS,
	NO_VID_STREAM,
	NO_AUDIO_STREAM,
	NO_CODEC_CTX,
	CODEC_CTX_UNINIT,
	NO_SWR_CTX,
	NO_SWR_CONVERT,
	NO_FRAME,
	NO_PACKET,
	PKT_NOT_DECODED,
	PKT_NOT_RECEIVED,
	NO_SCALER,
	SEEK_FAILED,
	NO_DATA_AVAIL,
	REPEATING_FRAME,
	NO_AUDIO_DEVICES
};

class MEDIACONVERTER_API VideoReaderState
{
public:
	struct FrameData
	{
	public:
		FrameData() {}
		FrameData(const FrameData& other) { *this = other; }
		~FrameData() {}
		FrameData& operator=(const FrameData& other)
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
		bool operator==(const FrameData& other)
		{
			return frame_number == other.frame_number &&
				pkt_pts == other.pkt_pts &&
				frame_pts == other.frame_pts &&
				pkt_dts == other.pkt_dts &&
				frame_pkt_dts == other.frame_pkt_dts &&
				key_frame == other.key_frame &&
				pkt_size == other.pkt_size;
		}

		bool FillDataFromFrame(AVFrame* frame)
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

		bool FillDataFromPacket(AVPacket* packet)
		{
			if (!packet)
				return false;
			pkt_pts = packet->pts;
			pkt_dts = packet->dts;
			return true;
		}

		int frame_number = -1;
		int64_t pkt_pts = -1;
		int64_t frame_pts = -1;
		int64_t pkt_dts = -1;
		int64_t frame_pkt_dts = -1; //frame copies pkt_dts when it grabs frame data
		int key_frame = -1;
		size_t pkt_size = -1;
	};

	VideoReaderState() {}
	VideoReaderState(const VideoReaderState& other)
	{
		av_format_ctx = other.av_format_ctx;
		video_codec_ctx = other.video_codec_ctx;
		av_frame = other.av_frame;
		av_packet = other.av_packet;
		sws_scaler_ctx = other.sws_scaler_ctx;
		video_stream_index = other.video_stream_index;
		audio_codec_ctx = other.audio_codec_ctx;
		audio_stream_index = other.audio_stream_index;
		sample_fmt = other.sample_fmt;
		sample_rate = other.sample_rate;
		frame_size = other.frame_size;
		num_channels = other.num_channels;
		data_size = other.data_size;
	}
	~VideoReaderState() {}
	bool IsEqual(const VideoReaderState& other)
	{
		return av_format_ctx == other.av_format_ctx &&
			video_codec_ctx == other.video_codec_ctx &&
			av_frame == other.av_frame &&
			av_packet == other.av_packet &&
			sws_scaler_ctx == other.sws_scaler_ctx &&
			video_stream_index == other.video_stream_index &&
			audio_codec_ctx == other.audio_codec_ctx &&
			audio_stream_index == other.audio_stream_index &&
			sample_fmt == other.sample_fmt &&
			sample_rate == other.sample_rate &&
			frame_size == other.frame_size &&
			num_channels == other.num_channels &&
			data_size == other.data_size;
	}

	int FPS() const
	{
		if(!IsRationalValid(VideoAvgFrameRate()))
			return 1;

		//using round here because i have seen avg_frame_rates report strange values that end up @ 30.03 fps
		return (int)std::round(VideoAvgFrameRateDbl());
	}

	int64_t FrameInterval() const
	{
		if(!IsRationalValid(VideoTimebase()))
			return 1;
		auto ret = VideoTimebase().den / (double)VideoTimebase().num / VideoAvgFrameRateDbl();
		if (ret == 0)
			return 1;
		return (int64_t)ret;
	}

	AVCodecContext* GetCodecCtxFromPkt()
	{
		return GetCodecCtxFromPkt(av_packet);
	}

	AVCodecContext* GetCodecCtxFromPkt(AVPacket* pkt)
	{
		if (pkt->stream_index == video_stream_index)
			return video_codec_ctx;
		else if (pkt->stream_index == audio_stream_index)
			return audio_codec_ctx;

		return nullptr;
	}

	bool HasVideoStream() const { return video_stream_index >= 0; }
	bool HasAudioStream() const { return audio_stream_index >= 0; }
	int SampleRate() { return sample_rate; }
	int BufferSize() { return buffer_size; }
	int Channels() { return num_channels; }
	int NumSamples() { return num_samples; }
	int LineSize() { return line_size; }
	int BytesPerSample() { return av_get_bytes_per_sample(sample_fmt); }
	int64_t VideoDuration() const
	{ 
		if (!HasVideoStream())
			return 0;
		return av_format_ctx->streams[video_stream_index]->duration;
	}
	int64_t VideoStartTime()
	{
		if (!HasVideoStream())
			return 0;
		return av_format_ctx->streams[video_stream_index]->start_time;
	}
	AVRational VideoAvgFrameRate() const
	{
		if (!HasVideoStream())
			return av_make_q(-1, -1); //returns invalid values 

		return av_format_ctx->streams[video_stream_index]->avg_frame_rate;
	}
	double VideoAvgFrameRateDbl() const
	{
		if (!HasVideoStream())
			return 0.0;
		return av_q2d(VideoAvgFrameRate());
	}
	AVRational VideoTimebase() const
	{
		if (!HasVideoStream())
			return av_make_q(-1, -1); //returns invalid values 

		return av_format_ctx->streams[video_stream_index]->time_base;
	}
	double VideoTimebaseDbl()
	{
		if (!HasVideoStream())
			return 0.0;
		return av_q2d(VideoTimebase());
	}
	int64_t VideoFrameCt() const
	{
		if (!HasVideoStream())
			return 0;
		return av_format_ctx->streams[video_stream_index]->nb_frames;
	}
	int VideoWidth() const
	{
		if (!HasVideoStream())
			return 0;
		return av_format_ctx->streams[video_stream_index]->codecpar->width;
	}
	int VideoHeight() const
	{
		if (!HasVideoStream())
			return 0;
		return av_format_ctx->streams[video_stream_index]->codecpar->height;
	}

	int64_t AudioDuration()
	{
		if (!HasAudioStream())
			return 0;
		return av_format_ctx->streams[audio_stream_index]->duration;
	}
	int64_t AudioStartTime()
	{
		if (!HasAudioStream())
			return 0;
		return av_format_ctx->streams[audio_stream_index]->start_time;
	}
	AVRational AudioAvgFrameRate()
	{
		if (!HasAudioStream())
			return av_make_q(-1, -1); //returns invalid values 

		return av_format_ctx->streams[audio_stream_index]->avg_frame_rate;
	}
	double AudioAvgFrameRateDbl()
	{
		if (!HasAudioStream())
			return 0.0;
		return av_q2d(AudioAvgFrameRate());
	}
	AVRational AudioTimebase()
	{
		if (!HasAudioStream())
			return av_make_q(-1, -1); //returns invalid values 
		return av_format_ctx->streams[audio_stream_index]->time_base;
	}
	double AudioTimebaseDbl()
	{
		if (!HasAudioStream())
			return 0.0;
		return av_q2d(AudioTimebase());
	}

	const char* CodecName()
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

	bool IsRationalValid(const AVRational& rational) const
	{
		//num can be 0 but den can't 
		//generally if one reports -1 then they both will 
		return (rational.den > 0 && rational.num >= 0);
	}

	AVFrame* av_frame = nullptr;
	AVPacket* av_packet = nullptr;

	AVFormatContext* av_format_ctx = nullptr;
	AVCodecContext* video_codec_ctx = nullptr;
	SwsContext* sws_scaler_ctx = nullptr;
	int video_stream_index = -1;

	FrameData frameData;

	//Audio details
	AVCodecContext* audio_codec_ctx = nullptr;
	SwrContext* swr_ctx = nullptr;
	int got_picture = -1;
	int audio_stream_index = -1;
	AVSampleFormat sample_fmt = AV_SAMPLE_FMT_NONE;
	int frame_size = 0;
	int num_channels = -1;
	int sample_rate = -1;
	int data_size = 0;
	int buffer_size = -1;
	int line_size = -1;
	int num_samples = -1;
	int64_t audio_pts = -1;
};

// This class is exported from the dll
class MEDIACONVERTER_API CMediaConverter 
{
	typedef std::unique_ptr<unsigned char[]> FBPtr;
	typedef std::vector<uint8_t> AudioBuffer;

public:
	CMediaConverter(void);
	ErrorCode loadFrame(const char* filename, int& width, int& height, unsigned char** data);

	ErrorCode openVideoReader(const char* filename);
	ErrorCode openVideoReader(VideoReaderState* state, const char* filename);

	ErrorCode readVideoFrame(VideoReaderState* state, FBPtr& fb_ptr);
	ErrorCode readVideoFrame(FBPtr& fb_ptr);

	ErrorCode readAudioFrame(VideoReaderState* state, AudioBuffer& audioBuffer);
	ErrorCode readAudioFrame(AudioBuffer& audioBuffer);

	int readFrame();
	int readFrame(VideoReaderState* state);

	int outputToBuffer(FBPtr& fb_ptr);
	int outputToBuffer(VideoReaderState*, FBPtr& fb_ptr);

	int outputToAudioBuffer(AudioBuffer& ab_ptr);
	int outputToAudioBuffer(VideoReaderState*, AudioBuffer& ab_ptr);

	int processVideoIntoFrames(VideoReaderState* state);
	int processAudioIntoFrames(VideoReaderState* state);

	int processVideoPacketsIntoFrames();
	int processVideoPacketsIntoFrames(VideoReaderState* state);

	int processAudioPacketsIntoFrames();
	int processAudioPacketsIntoFrames(VideoReaderState* state);

	ErrorCode trackToFrame(VideoReaderState* state, int64_t targetPts);
	ErrorCode trackToFrame(int64_t targetPts);

	ErrorCode seekToFrame(VideoReaderState* state, int64_t targetPts, bool inReverse = false);
	ErrorCode seekToFrame(int64_t targetPts, bool inReverse = false);

	ErrorCode seekToStart(VideoReaderState* state);
	ErrorCode seekToStart();

	ErrorCode closeVideoReader(VideoReaderState* state);
	ErrorCode closeVideoReader();

	ErrorCode readVideoReaderFrame(VideoReaderState* state, unsigned char** frameBuffer, bool requestFlush = false); //unmanaged data version, creates heap data in function
	ErrorCode readVideoReaderFrame(unsigned char** frameBuffer, bool requestFlush = false); //unmanaged data version, creates heap data in function

	VideoReaderState& VrState() { return m_vrState; }
private:
	bool WithinTolerance(int64_t referencePts, int64_t targetPts, int64_t tolerance);
	VideoReaderState m_vrState;
};
