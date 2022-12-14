#ifndef VIDEO_DECODER_HPP_
#define VIDEO_DECODER_HPP_
#include "video_color_space.hpp"
#include "size.hpp"
#include <Windows.h>

class VideoDecoder
{
public:
	VideoDecoder();
	~VideoDecoder();
	virtual void Initialize(VideoColorSpace color_space, Size coded_size, HWND hWnd) = 0;
	virtual void Decode(uint8_t* buffer, int size) = 0;
private:

};

#endif // VIDEO_DECODER_HPP_
