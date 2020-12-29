#pragma once
#ifdef MEDIACONVERTER_EXPORTS
#define MEDIACONVERTER_API __declspec(dllexport)
#else
#define MEDIACONVERTER_API __declspec(dllimport)
#endif

//ffmpeg includes
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/timestamp.h>
}

struct VideoFrameData
{
public:
	VideoFrameData();
	VideoFrameData(const VideoFrameData& other);
	~VideoFrameData();
	VideoFrameData& operator=(const VideoFrameData& other);
	bool operator==(const VideoFrameData& other);

	bool FillDataFromFrame(AVFrame* frame);
	bool FillDataFromPacket(AVPacket* packet);
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
	AudioFrameData();
	AudioFrameData(const AudioFrameData& other);
	~AudioFrameData();
	AudioFrameData& operator=(const AudioFrameData& other);
	bool operator==(const AudioFrameData& other);

	bool FillDataFromFrame(AVFrame* frame);
	bool UpdateBitRate(AVCodecContext* ctx);
	int BufferSize(AVSampleFormat fmt) const;

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

class MEDIACONVERTER_API MediaReaderState
{
public:
	MediaReaderState();
	MediaReaderState(const MediaReaderState& other);
	~MediaReaderState();

	bool IsEqual(const MediaReaderState& other);
	int FPS() const;
	int64_t VideoFrameInterval() const;
	int64_t AudioFrameInterval() const;
	AVCodecContext* GetCodecCtxFromPkt();
	AVCodecContext* GetCodecCtxFromPkt(AVPacket* pkt);

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
	double AudioTotalSeconds() const { return AudioDuration() / (double)AudioFrameInterval(); }

	//VideoFrameData accessors - these change per frame 
	int VideoFrameNumber() { return videoFrameData.FrameNumber(); }
	int64_t PktPts() { return videoFrameData.PktPts(); }
	int64_t VideoFramePts() { return videoFrameData.FramePts(); }
	int64_t PktDts() { return videoFrameData.PktDts(); }
	int64_t FramePktDts() { return videoFrameData.FramePktDts(); }
	int KeyFrame() { return videoFrameData.KeyFrame(); }
	size_t VideoPktSize() { return videoFrameData.PktSize(); }
	double VideoTotalSeconds() const { return VideoDuration() / (double)VideoFrameInterval(); }

	//File data accessors - these are constant and read when the file is opened
	int64_t VideoDuration() const;
	int64_t VideoStartTime() const;
	AVRational VideoAvgFrameRate() const;
	double VideoAvgFrameRateDbl() const;
	AVRational VideoTimebase() const;
	double VideoTimebaseDbl() const;
	int64_t VideoFrameCt() const;
	int VideoWidth() const;
	int VideoHeight() const;

	int64_t AudioDuration() const;
	int64_t AudioStartTime() const;
	AVRational AudioAvgFrameRate() const;
	double AudioAvgFrameRateDbl() const;
	AVRational AudioTimebase() const;
	double AudioTimebaseDbl() const;
	int AudioFrameSize() const;

	const char* CodecName();
	AVSampleFormat AudioSampleFormat() const;
	void SetAudioFrameInterval(int64_t interval);
	bool IsRationalValid(const AVRational& rational) const;

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