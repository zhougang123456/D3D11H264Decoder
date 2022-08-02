#ifndef ACCELERATED_VIDEO_DECODER_HPP_
#define ACCELERATED_VIDEO_DECODER_HPP_
#include <stddef.h>
#include <stdint.h>
#include "video_codecs.hpp"
#include "rect.hpp"
class AcceleratedVideoDecoder
{
public:
	AcceleratedVideoDecoder() {}
	virtual ~AcceleratedVideoDecoder() {}
	virtual void SetStream(int32_t id, uint8_t* buffer, size_t size) = 0;
	virtual bool Flush() = 0;
	virtual void Reset() = 0;
	enum DecodeResult
	{
		kDecodeError,
		kConfigChange,
		kRanOutOfStreamData,
		kRanOutOfSurfaces,
		kNeedContextUpdate,
		kTryAgain,
	};
	virtual DecodeResult Decode() = 0;
	virtual Size GetPicSize() const = 0;
	virtual Rect GetVisibleRect() const = 0;
	virtual VideoCodecProfile GetProfile() const = 0;
	virtual uint8_t GetBitDepth() const = 0;
	virtual size_t GetRequiredNumOfPictures() const = 0;
	virtual size_t GetNumReferenceFrames() const = 0;
private:

};

#endif // ACCELERATED_VIDEO_DECODER_HPP_
