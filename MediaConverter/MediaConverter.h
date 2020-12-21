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
#include <cmath>
#include <memory>

//ffmpeg includes
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/timestamp.h>
}

enum class MEDIACONVERTER_API ErrorCode : int
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
	NO_AUDIO_DEVICES,
	NO_OUTPUT_FILE
};

class MEDIACONVERTER_API MediaReaderState
{
private:
	struct VideoFrameData
	{
	public:
		VideoFrameData() {}
		VideoFrameData(const VideoFrameData& other) { *this = other; }
		~VideoFrameData() {}
		VideoFrameData& operator=(const VideoFrameData& other)
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
		bool operator==(const VideoFrameData& other)
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
		int FrameNumber() { return frame_number; }
		int64_t PktPts() { return pkt_pts; }
		int64_t FramePts() { return frame_pts; }
		int64_t PktDts() { return pkt_dts; }
		int64_t FramePktDts() { return frame_pkt_dts; }
		int KeyFrame() { return key_frame; }
		size_t PktSize() { return pkt_size; }
	private:
		int frame_number = -1;
		int64_t pkt_pts = -1;
		int64_t frame_pts = -1;
		int64_t pkt_dts = -1;
		int64_t frame_pkt_dts = -1; //frame copies pkt_dts when it grabs frame data
		int key_frame = -1;
		size_t pkt_size = -1;
	};

	struct AudioFrameData
	{
		AudioFrameData() {}
		AudioFrameData(const AudioFrameData& other) { *this = other; }
		~AudioFrameData() {}
		AudioFrameData& operator=(const AudioFrameData& other)
		{
			num_channels = other.num_channels;
			sample_rate = other.sample_rate;
			line_size = other.line_size;
			num_samples = other.num_samples;
			audio_pts = other.audio_pts;
			bit_rate = other.bit_rate;
			return *this;
		}
		bool operator==(const AudioFrameData& other)
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
		bool FillDataFromFrame(AVFrame* frame)
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

		bool UpdateBitRate(AVCodecContext* ctx)
		{
			if (!ctx)
				return false;

			if (bit_rate < 0 || ctx->bit_rate > 0)
				bit_rate = ctx->bit_rate;

			return true;
		}

		int BufferSize(AVSampleFormat fmt) const
		{
			auto packed = av_get_packed_sample_fmt(fmt);
			int linesize = 0;
			return av_samples_get_buffer_size(&linesize, num_channels, num_samples, packed, 1);
		}

		int Channels() const { return num_channels; }
		int SampleRate() const { return sample_rate; }
		int LineSize() const { return line_size; }
		int NumSamples() const { return num_samples; }
		int64_t AudioPts() const { return audio_pts; }
		int64_t AudioDts() const { return audio_dts; }
		int64_t BestEffortTs() const { return best_effort_ts; }
		int64_t BitRate() const { return bit_rate; }

	private:
		int num_channels = -1;
		int sample_rate = -1;
		int line_size = -1;
		int num_samples = -1;
		int64_t audio_pts = -1;
		int64_t audio_dts = -1;
		int64_t best_effort_ts = -1;
		int64_t bit_rate = -1;
	};

public:
	MediaReaderState() {}
	MediaReaderState(const MediaReaderState& other)
	{
		av_format_ctx = other.av_format_ctx;
		video_codec_ctx = other.video_codec_ctx;
		av_frame = other.av_frame;
		av_packet = other.av_packet;
		sws_scaler_ctx = other.sws_scaler_ctx;
		video_stream_index = other.video_stream_index;
		audio_codec_ctx = other.audio_codec_ctx;
		audio_stream_index = other.audio_stream_index;
	}
	~MediaReaderState() {}
	bool IsEqual(const MediaReaderState& other)
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

	int FPS() const
	{
		if(!IsRationalValid(VideoAvgFrameRate()))
			return 0;

		//using round here because i have seen avg_frame_rates report strange values that end up @ 30.03 fps
		return (int)std::round(VideoAvgFrameRateDbl());
	}

	int64_t VideoFrameInterval() const
	{
		if(!IsRationalValid(VideoTimebase()))
			return 1;
		auto ret = VideoTimebase().den / (double)VideoTimebase().num / VideoAvgFrameRateDbl();
		if (ret == 0)
			return 1;
		return (int64_t)ret;
	}

	int64_t AudioFrameInterval() const
	{
		if (!IsRationalValid(AudioTimebase()) || !IsRationalValid(AudioAvgFrameRate()))
			return (std::max)((int64_t)1, audio_frame_interval);
		auto ret = AudioTimebase().den / (double)AudioTimebase().num / AudioAvgFrameRateDbl();
		if (ret == 0)
			return audio_frame_interval;
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

	//AudioFrameData accessors - these change per frame
	int SampleRate() const { return audioFrameData.SampleRate(); }
	int AudioBufferSize() const { return audioFrameData.BufferSize(AudioSampleFormat()); }
	int Channels() const { return audioFrameData.Channels(); }
	int NumSamples() const { return audioFrameData.NumSamples(); }
	int LineSize() const { return audioFrameData.LineSize(); }
	int BytesPerSample() const { return av_get_bytes_per_sample(AudioSampleFormat()); }
	int64_t AudioPts() const { return audioFrameData.AudioPts(); }
	int64_t AudioDts() const { return audioFrameData.AudioDts(); }
	int64_t BestEffortTs() const { return audioFrameData.BestEffortTs(); }
	int64_t BitRate() const { return audioFrameData.BitRate(); }
	double AudioTotalSeconds() const
	{
		return AudioDuration() / (double)AudioFrameInterval();
	}


	//VideoFrameData accessors - these change per frame 
	int VideoFrameNumber() { return videoFrameData.FrameNumber(); }
	int64_t PktPts() { return videoFrameData.PktPts(); }
	int64_t VideoFramePts() { return videoFrameData.FramePts(); }
	int64_t PktDts() { return videoFrameData.PktDts(); }
	int64_t FramePktDts() { return videoFrameData.FramePktDts(); }
	int KeyFrame() { return videoFrameData.KeyFrame(); }
	size_t VideoPktSize() { return videoFrameData.PktSize(); }
	double VideoTotalSeconds() const
	{
		return VideoDuration() / (double)VideoFrameInterval();
	}

	//File data accessors - these are constant and read when the file is opened
	int64_t VideoDuration() const
	{ 
		if (!HasVideoStream())
			return 0;
		return av_format_ctx->streams[video_stream_index]->duration;
	}
	int64_t VideoStartTime() const
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
	double VideoTimebaseDbl() const
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

	int64_t AudioDuration() const
	{
		if (!HasAudioStream())
			return 0;
		return av_format_ctx->streams[audio_stream_index]->duration;
	}
	int64_t AudioStartTime() const
	{
		if (!HasAudioStream())
			return 0;
		return av_format_ctx->streams[audio_stream_index]->start_time;
	}
	AVRational AudioAvgFrameRate() const
	{
		if (!HasAudioStream())
			return av_make_q(-1, -1); //returns invalid values 

		return av_format_ctx->streams[audio_stream_index]->avg_frame_rate;
	}
	double AudioAvgFrameRateDbl() const
	{
		if (!HasAudioStream())
			return 0.0;
		if (!IsRationalValid(AudioAvgFrameRate()))
			return 0.0;
		return av_q2d(AudioAvgFrameRate());
	}
	AVRational AudioTimebase() const
	{
		if (!HasAudioStream())
			return av_make_q(-1, -1); //returns invalid values 
		return av_format_ctx->streams[audio_stream_index]->time_base;
	}
	double AudioTimebaseDbl() const 
	{
		if (!HasAudioStream())
			return 0.0;
		return av_q2d(AudioTimebase());
	}
	int AudioFrameSize() const
	{
		if (!HasAudioStream() || !audio_codec_ctx)
			return 0;

		return audio_codec_ctx->frame_size;
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

	AVSampleFormat AudioSampleFormat() const
	{
		if (!HasAudioStream() || !audio_codec_ctx)
			return AV_SAMPLE_FMT_NONE;

		return audio_codec_ctx->sample_fmt;
	}

	void SetAudioFrameInterval(int64_t interval)
	{
		if (interval < 0)
			return;
		audio_frame_interval = interval;
	}

	bool IsRationalValid(const AVRational& rational) const
	{
		//num can be 0 but den can't 
		//generally if one reports -1 then they both will 
		return (rational.den > 0 && rational.num >= 0);
	}

	bool IsOpened() const { return is_opened; }
	void SetIsOpened(bool opened = true) { is_opened = opened; }

	bool is_opened = false;

	AVFrame* av_frame = nullptr;
	AVPacket* av_packet = nullptr;

	AVFormatContext* av_format_ctx = nullptr;
	AVCodecContext* video_codec_ctx = nullptr;
	SwsContext* sws_scaler_ctx = nullptr;
	int video_stream_index = -1;

	VideoFrameData videoFrameData;

	//Audio details
	AVCodecContext* audio_codec_ctx = nullptr;
	SwrContext* swr_ctx = nullptr;
	int audio_stream_index = -1;

	AudioFrameData audioFrameData;
	int64_t audio_frame_interval = 0; //this is calculated manually from the buffer since it isn't known prior through ffmpeg
};

// This class is exported from the dll
class MEDIACONVERTER_API CMediaConverter 
{
	typedef std::unique_ptr<uint8_t[]> FBPtr;
	typedef std::vector<uint8_t> AudioBuffer;

public:
	CMediaConverter(void);
	~CMediaConverter();
	ErrorCode loadFrame(const char* filename, int& width, int& height, unsigned char** data);

	ErrorCode openVideoReader(const char* filename);
	ErrorCode openVideoReader(MediaReaderState* state, const char* filename);

	ErrorCode readVideoFrame(MediaReaderState* state, FBPtr& fb_ptr);
	ErrorCode readVideoFrame(FBPtr& fb_ptr);

	ErrorCode readAudioFrame(MediaReaderState* state, AudioBuffer& audioBuffer);
	ErrorCode readAudioFrame(AudioBuffer& audioBuffer);

	int readFrame();
	int readFrame(MediaReaderState* state);

	int outputToBuffer(FBPtr& fb_ptr);
	int outputToBuffer(MediaReaderState*, FBPtr& fb_ptr);

	int outputToAudioBuffer(AudioBuffer& ab_ptr);
	int outputToAudioBuffer(MediaReaderState*, AudioBuffer& ab_ptr);

	int processVideoIntoFrames(MediaReaderState* state);
	int processAudioIntoFrames(MediaReaderState* state);

	int processVideoPacketsIntoFrames();
	int processVideoPacketsIntoFrames(MediaReaderState* state);

	int processAudioPacketsIntoFrames();
	int processAudioPacketsIntoFrames(MediaReaderState* state);

	ErrorCode trackToFrame(MediaReaderState* state, int64_t targetPts);
	ErrorCode trackToFrame(int64_t targetPts);

	ErrorCode trackToAudioFrame(MediaReaderState* state, int64_t targetPts);
	ErrorCode trackToAudioFrame(int64_t targetPts);

	ErrorCode seekToFrame(MediaReaderState* state, int64_t targetPts);
	ErrorCode seekToFrame(int64_t targetPts);

	ErrorCode seekToAudioFrame(MediaReaderState* state, int64_t targetPts);
	ErrorCode seekToAudioFrame(int64_t targetPts);

	ErrorCode seekToStart(MediaReaderState* state);
	ErrorCode seekToStart();

	ErrorCode seekToAudioStart(MediaReaderState* state);
	ErrorCode seekToAudioStart();

	ErrorCode closeVideoReader(MediaReaderState* state);
	ErrorCode closeVideoReader();

	ErrorCode encodeMedia(const char* inFile, const char* outFile);
	ErrorCode encodeMedia(const char* inFile, const char* outFile, MediaReaderState* state);

	ErrorCode readVideoReaderFrame(MediaReaderState* state, unsigned char** frameBuffer, bool requestFlush = false); //unmanaged data version, creates heap data in function
	ErrorCode readVideoReaderFrame(unsigned char** frameBuffer, bool requestFlush = false); //unmanaged data version, creates heap data in function

	const MediaReaderState& MRState() const { return m_mrState; }
	MediaReaderState& MRState() { return m_mrState; }
private:
	bool WithinTolerance(int64_t referencePts, int64_t targetPts, int64_t tolerance);
	MediaReaderState m_mrState;
};
