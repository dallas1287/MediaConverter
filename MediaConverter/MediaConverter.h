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

#include "MediaReaderState.h"
#include <vector>
#include <memory>

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

// This class is exported from the dll
class MEDIACONVERTER_API CMediaConverter 
{
	typedef std::vector<uint8_t> VideoBuffer;
	typedef std::vector<uint8_t> AudioBuffer;

public:
	CMediaConverter();
	~CMediaConverter();
	ErrorCode loadFrame(const char* filename, int& width, int& height, unsigned char** data);

	ErrorCode openVideoReader(const char* filename);
	ErrorCode openVideoReader(MediaReaderState* state, const char* filename);

	ErrorCode readVideoFrame(MediaReaderState* state, VideoBuffer& buffer);
	ErrorCode readVideoFrame(VideoBuffer& buffer);

	ErrorCode readAudioFrame(MediaReaderState* state, AudioBuffer& audioBuffer);
	ErrorCode readAudioFrame(AudioBuffer& audioBuffer);

	int readFrame();
	int readFrame(MediaReaderState* state);

	int outputToBuffer(VideoBuffer& buffer);
	int outputToBuffer(MediaReaderState*, VideoBuffer& buffer);

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
