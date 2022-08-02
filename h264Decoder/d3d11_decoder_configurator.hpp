#ifndef D3D11_DECODER_CONFIGURATOR_HPP_
#define D3D11_DECODER_CONFIGURATOR_HPP_

#include <d3d11.h>
#include <wrl.h>
#include <memory>
#include <vector>

#include "d3d11_picture_buffer.hpp"
#include "size.hpp"

class D3D11DecoderConfigurator
{
public:
	D3D11DecoderConfigurator(DXGI_FORMAT decoder_output_dxgifmt,
                             GUID decoder_guid,
                             Size coded_size,
                             bool supports_swap_chain);
	virtual ~D3D11DecoderConfigurator() = default;

    static std::unique_ptr<D3D11DecoderConfigurator> Create(
        Size coded_size, uint8_t bit_depth, bool use_shared_handle);

    bool SupportsDevice(ComD3D11VideoDevice video_device);

    // Create the decoder's output texture.
    ComD3D11Texture2D CreateOutputTexture(ComD3D11Device device,
                                          Size size,
                                          uint32_t array_size,
                                          bool use_shared_handle);

    const D3D11_VIDEO_DECODER_DESC* DecoderDescriptor() const {
        return &decoder_desc_;
    }

    const GUID DecoderGuid() const { return decoder_guid_; }

    DXGI_FORMAT TextureFormat() const { return dxgi_format_; }

    static constexpr size_t BUFFER_COUNT = 20;

private:
    // Set up instances of the parameter structs for D3D11 Functions
    void SetUpDecoderDescriptor(const Size& coded_size);
    void SetUpTextureDescriptor();

    D3D11_TEXTURE2D_DESC output_texture_desc_;
    D3D11_VIDEO_DECODER_DESC decoder_desc_;

    const DXGI_FORMAT dxgi_format_;
    const GUID decoder_guid_;

    const bool supports_swap_chain_;
};

#endif // D3D11_DECODER_CONFIGURATOR_HPP_
