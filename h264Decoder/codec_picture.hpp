#ifndef CODEC_PICTURE_HPP_
#define CODEC_PICTURE_HPP_
#include <vector>
#include "video_color_space.hpp"
#include "rect.hpp"

class CodecPicture{
public:
    CodecPicture();

    int32_t bitstream_id() const { return bitstream_id_; }
    void set_bitstream_id(int32_t bitstream_id) { bitstream_id_ = bitstream_id; }

    const Rect visible_rect() const { return visible_rect_; }
    void set_visible_rect(const Rect& rect) { visible_rect_ = rect; }

    // Populate with an unspecified colorspace by default.
    const VideoColorSpace& get_colorspace() const { return colorspace_; }
    void set_colorspace(const VideoColorSpace& colorspace) {
        colorspace_ = colorspace;
    }

    virtual ~CodecPicture();

private:
    int32_t bitstream_id_ = -1;
    Rect visible_rect_;
    VideoColorSpace colorspace_;

};
#endif