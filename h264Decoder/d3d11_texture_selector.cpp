#include "d3d11_texture_selector.hpp"

#include <d3d11.h>
#include <iostream>

TextureSelector::TextureSelector(VideoPixelFormat pixfmt,
    DXGI_FORMAT output_dxgifmt,
    ComD3D11VideoDevice video_device,
    ComD3D11DeviceContext device_context,
    bool shared_image_use_shared_handle)
    : pixel_format_(pixfmt),
    output_dxgifmt_(output_dxgifmt),
    video_device_(std::move(video_device)),
    device_context_(std::move(device_context)),
    shared_image_use_shared_handle_(shared_image_use_shared_handle) {}

TextureSelector::~TextureSelector() = default;

// static
std::unique_ptr<TextureSelector> TextureSelector::Create(
    DXGI_FORMAT decoder_output_format,
    TextureSelector::HDRMode hdr_output_mode,
    ComD3D11VideoDevice video_device,
    ComD3D11DeviceContext device_context,
    bool shared_image_use_shared_handle) {
    VideoPixelFormat output_pixel_format;
    DXGI_FORMAT output_dxgi_format;
    VideoColorSpace output_color_space;

    bool needs_texture_copy = true;

    // TODO(liberato): add other options here, like "copy to rgb" for NV12.
    switch (decoder_output_format) {
    case DXGI_FORMAT_NV12: {
        std::cout << "D3D11VideoDecoder producing NV12" << std::endl;
        if (!needs_texture_copy ) {
            output_pixel_format = PIXEL_FORMAT_NV12;
            output_dxgi_format = DXGI_FORMAT_NV12;
            // Leave |output_color_space| the same, since we'll bind either the
            // original or the copy. Downstream will handle it, either in the
            // shaders or in the overlay, if needed.
            std::cout << "D3D11VideoDecoder: Selected NV12" << std::endl;
        } else {
            output_pixel_format = PIXEL_FORMAT_ARGB;
            output_dxgi_format = DXGI_FORMAT_B8G8R8A8_UNORM;
            std::cout << "D3D11VideoDecoder: Selected ARGB" << std::endl;
        }
        
        break;
    }
   
    default: {
        // TODO(tmathmeyer) support other profiles in the future.
        std::cout << "D3D11VideoDecoder does not support " << decoder_output_format << std::endl;
        return nullptr;
    }
    }

    // If we're trying to produce an output texture that's different from what
    // the decoder is providing, then we need to copy it. If sharing decoder
    // textures is not allowed, then copy either way.
    needs_texture_copy |= (decoder_output_format != output_dxgi_format);

    std::cout << "D3D11VideoDecoder output color space: " << std::endl;

    if (needs_texture_copy) {
        std::cout << "D3D11VideoDecoder is copying textures" << std::endl;
        return std::make_unique<CopyTextureSelector>(
            output_pixel_format, decoder_output_format, output_dxgi_format,
            output_color_space, std::move(video_device), std::move(device_context),
            shared_image_use_shared_handle);
    }
    else {
        std::cout << "D3D11VideoDecoder is binding textures" << std::endl;
        // Binding can't change the color space. The consumer has to do it, if they
        // want to.
        return std::make_unique<TextureSelector>(
            output_pixel_format, output_dxgi_format, std::move(video_device),
            std::move(device_context), shared_image_use_shared_handle);
    }
}

std::unique_ptr<Texture2DWrapper> TextureSelector::CreateTextureWrapper(
    ComD3D11Device device,
    Size size) {
    // TODO(liberato): If the output format is rgb, then create a pbuffer wrapper.
    return std::make_unique<DefaultTexture2DWrapper>(size, OutputDXGIFormat());
}

bool TextureSelector::DoesDecoderOutputUseSharedHandle() const {
    return shared_image_use_shared_handle_;
}

bool TextureSelector::WillCopyForTesting() const {
    return false;
}

CopyTextureSelector::CopyTextureSelector(
    VideoPixelFormat pixfmt,
    DXGI_FORMAT input_dxgifmt,
    DXGI_FORMAT output_dxgifmt,
    VideoColorSpace output_color_space,
    ComD3D11VideoDevice video_device,
    ComD3D11DeviceContext device_context,
    bool shared_image_use_shared_handle)
    : TextureSelector(pixfmt,
        output_dxgifmt,
        std::move(video_device),
        std::move(device_context),
        shared_image_use_shared_handle),
    output_color_space_(std::move(output_color_space)) {}

CopyTextureSelector::~CopyTextureSelector() = default;

std::unique_ptr<Texture2DWrapper> CopyTextureSelector::CreateTextureWrapper(
    ComD3D11Device device,
    Size size) {
    D3D11_TEXTURE2D_DESC texture_desc = {};
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;// 0;
    texture_desc.Format = output_dxgifmt_;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = 0;
        //D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    texture_desc.Width = size.width();
    texture_desc.Height = size.height();
    if (DoesSharedImageUseSharedHandle()) {
        texture_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
            D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    }

    ComD3D11Texture2D out_texture;
    if (FAILED(device->CreateTexture2D(&texture_desc, nullptr, &out_texture)))
        return nullptr;

  
    return std::make_unique<DefaultTexture2DWrapper>(size, OutputDXGIFormat());
}

bool CopyTextureSelector::DoesDecoderOutputUseSharedHandle() const {
    return false;
}

bool CopyTextureSelector::WillCopyForTesting() const {
    return true;
}