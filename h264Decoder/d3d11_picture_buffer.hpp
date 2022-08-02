#ifndef D3D11_PICTURE_BUBFFER_HPP_
#define D3D11_PICTURE_BUBFFER_HPP_
#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <vector>

#include "d3d11_texture_wrapper.hpp"

class Texture2DWrapper;

class D3D11PictureBuffer {
public:
    D3D11PictureBuffer(
        ComD3D11Texture2D texture,
        size_t array_slice,
        std::unique_ptr<Texture2DWrapper> texture_wrapper,
        Size size,
        size_t picture_index);

    bool Init(ComD3D11VideoDevice video_device,
              const GUID& decoder_guid);

    bool ProcessTexture(const VideoColorSpace& input_color_space,
                        VideoColorSpace* output_color_space);

    ComD3D11Texture2D Texture() const;
    ID3D11VideoDecoderOutputView* AcquireOutputView() const;

    const Size& size() const { return size_;  }
    size_t picture_index() const { return picture_index_; }

    // Is this PictureBuffer backing a VideoFrame right now?
    bool in_client_use() const { return in_client_use_ > 0; }

    // Is this PictureBuffer holding an image that's in use by the decoder?
    bool in_picture_use() const { return in_picture_use_; }

    void add_client_use() {
        in_client_use_++;
        if (in_client_use_ <= 0) {
            //warning
        }
    }
    void remove_client_use() {
        if (in_client_use_ <= 0) {
            //warning
        }
        in_client_use_--;
    }
    void set_in_picture_use(bool use) { in_picture_use_ = use; }

    Texture2DWrapper* texture_wrapper() const { return texture_wrapper_.get(); }

private:
    ~D3D11PictureBuffer();

    ComD3D11Texture2D texture_;
    uint32_t array_slice_;

    std::unique_ptr<Texture2DWrapper> texture_wrapper_;
    Size size_;
    bool in_picture_use_ = false;
    int in_client_use_ = 0;
    size_t picture_index_;

    ComD3D11VideoDecoderOutputView output_view_;
};

#endif // D3D11_PICTURE_BUBFFER_HPP_
