#include "d3d11_decoder_configurator.hpp"

#include <d3d11.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <iostream>

#include "size.hpp"


D3D11DecoderConfigurator::D3D11DecoderConfigurator(
    DXGI_FORMAT decoder_output_dxgifmt,
    GUID decoder_guid,
    Size coded_size,
    bool supports_swap_chain)
    : dxgi_format_(decoder_output_dxgifmt),
    decoder_guid_(decoder_guid),
    supports_swap_chain_(supports_swap_chain) {
    SetUpDecoderDescriptor(coded_size);
    SetUpTextureDescriptor();
}

// static
std::unique_ptr<D3D11DecoderConfigurator> D3D11DecoderConfigurator::Create(
    Size coded_size,
    uint8_t bit_depth,
    bool use_shared_handle) {
    // Decoder swap chains do not support shared resources. More info in
    // https://crbug.com/911847. To enable Kaby Lake+ systems for using shared
    // handle, we disable decode swap chain support if shared handle is enabled.
    const bool supports_nv12_decode_swap_chain =
        //gl::DirectCompositionSurfaceWin::IsDecodeSwapChainSupported() &&
        !use_shared_handle;
    const auto decoder_dxgi_format = 
        bit_depth == 8 ? DXGI_FORMAT_NV12 : DXGI_FORMAT_P010;
    GUID decoder_guid = {};
    decoder_guid = D3D11_DECODER_PROFILE_H264_VLD_NOFGT;
    
    return std::make_unique<D3D11DecoderConfigurator>(
        decoder_dxgi_format, decoder_guid, coded_size, supports_nv12_decode_swap_chain);
}

bool D3D11DecoderConfigurator::SupportsDevice(
    ComD3D11VideoDevice video_device) {
    for (UINT i = video_device->GetVideoDecoderProfileCount(); i--;) {
        GUID profile = {};
        if (SUCCEEDED(video_device->GetVideoDecoderProfile(i, &profile))) {
            if (profile == decoder_guid_)
                return true;
        }
    }
    return false;
}

ComD3D11Texture2D D3D11DecoderConfigurator::CreateOutputTexture(
    ComD3D11Device device,
    Size size,
    uint32_t array_size,
    bool use_shared_handle) {
    output_texture_desc_.Width = size.width();
    output_texture_desc_.Height = size.height();
    output_texture_desc_.ArraySize = array_size;

    if (use_shared_handle) {
        // Update the decoder output texture usage to support shared handle and
        // keyed_mutex if required. SwapChain should be disabled and the frame
        // shouldn't be encrypted.
        output_texture_desc_.MiscFlags = 0;
           // D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    }
    else if (supports_swap_chain_) {
        // Decode swap chains do not support shared resources.
        // TODO(sunnyps): Find a workaround for when the decoder moves to its own
        // thread and D3D device.  See https://crbug.com/911847
        // TODO(liberato): This depends on the configuration of the TextureSelector,
        // to some degree. We should unset the flag only if it's binding and the
        // decode swap chain is supported, as Intel driver is buggy on Gen9 and
        // older devices without the flag. See https://crbug.com/1107403
        output_texture_desc_.MiscFlags = 0;
    }
    else {
        // Create non-shareable texture for d3d11 video decoder.
        output_texture_desc_.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    }
    //output_texture_desc_.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComD3D11Texture2D texture;
    HRESULT hr =
        device->CreateTexture2D(&output_texture_desc_, nullptr, &texture);
    if (FAILED(hr)) {
        return nullptr;
    }
    
    return texture;
}

// private
void D3D11DecoderConfigurator::SetUpDecoderDescriptor(
    const Size& coded_size) {
    decoder_desc_ = {};
    decoder_desc_.Guid = decoder_guid_;
    decoder_desc_.SampleWidth = coded_size.width();
    decoder_desc_.SampleHeight = coded_size.height();
    decoder_desc_.OutputFormat = dxgi_format_;
}

// private
void D3D11DecoderConfigurator::SetUpTextureDescriptor() {
    output_texture_desc_ = {};
    output_texture_desc_.MipLevels = 1;
    output_texture_desc_.Format = dxgi_format_;
    output_texture_desc_.SampleDesc.Count = 1;
    output_texture_desc_.Usage = D3D11_USAGE_DEFAULT;
    output_texture_desc_.BindFlags = D3D11_BIND_DECODER | D3D11_BIND_SHADER_RESOURCE;
}