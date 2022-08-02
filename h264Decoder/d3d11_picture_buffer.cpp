#include "d3d11_picture_buffer.hpp"

#include <d3d11.h>
#include <d3d11_1.h>
#include <windows.h>
#include <wrl/client.h>

#include <memory>
#include <iostream>

#include "video_color_space.hpp"

D3D11PictureBuffer::D3D11PictureBuffer(
    ComD3D11Texture2D texture,
    size_t array_slice,
    std::unique_ptr<Texture2DWrapper> texture_wrapper,
    Size size,
    size_t picture_index)
    : texture_(std::move(texture)),
      array_slice_(array_slice),
      texture_wrapper_(std::move(texture_wrapper)),
      size_(size),
      picture_index_(picture_index) {}

D3D11PictureBuffer::~D3D11PictureBuffer() {}

bool D3D11PictureBuffer::Init(
    ComD3D11VideoDevice video_device,
    const GUID& decoder_guid) {
    D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC view_desc = {};
    view_desc.DecodeProfile = decoder_guid;
    view_desc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
    view_desc.Texture2D.ArraySlice = array_slice_;

    bool result =
        texture_wrapper_->Init(texture_, array_slice_);
    if (!result) {
        std::cout << "Failed to Initialize the wrapper";
        return result;
    }

    HRESULT hr = video_device->CreateVideoDecoderOutputView(
        Texture().Get(), &view_desc, &output_view_);

    if (!SUCCEEDED(hr)) {
        std::cout << "Failed to CreateVideoDecoderOutputView";
        return false;
    }

    return true;
}

bool D3D11PictureBuffer::ProcessTexture(
    const VideoColorSpace& input_color_space,
    VideoColorSpace* output_color_space) {
    return texture_wrapper_->ProcessTexture(input_color_space, output_color_space);
}

ComD3D11Texture2D D3D11PictureBuffer::Texture() const {
    return texture_;
}

ID3D11VideoDecoderOutputView* D3D11PictureBuffer::AcquireOutputView() const {
   
    return output_view_.Get();
}