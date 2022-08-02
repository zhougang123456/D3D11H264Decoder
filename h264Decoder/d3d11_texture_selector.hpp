#ifndef D3D11_TEXTURE_SELECTOR_HPP_
#define D3D11_TEXTURE_SELECTOR_HPP_

#include <d3d11.h>
#include <wrl.h>
#include <memory>
#include <vector>

#include "d3d11_picture_buffer.hpp"
#include "video_color_space.hpp"
#include "size.hpp"
#include "video_pixel_format.hpp"

class TextureSelector
{
public:
	enum class HDRMode
	{
		kSDROnly = 0,
		kSDROrHDR = 1,
	};
	TextureSelector(VideoPixelFormat pixfmt,
					DXGI_FORMAT output_dxgifmt,
					ComD3D11VideoDevice video_device,
		            ComD3D11DeviceContext d3d11_device_context,
		            bool use_shared_handle);
    virtual ~TextureSelector();

    static std::unique_ptr<TextureSelector> Create(
        DXGI_FORMAT decoder_output_format,
        HDRMode hdr_output_mode,
        ComD3D11VideoDevice video_device,
        ComD3D11DeviceContext device_context,
        bool shared_image_use_shared_handle = false);

    virtual std::unique_ptr<Texture2DWrapper> CreateTextureWrapper(
        ComD3D11Device device,
        Size size);

    virtual bool DoesDecoderOutputUseSharedHandle() const;

    VideoPixelFormat PixelFormat() const { return pixel_format_; }
    DXGI_FORMAT OutputDXGIFormat() const { return output_dxgifmt_; }
    bool DoesSharedImageUseSharedHandle() const {
        return shared_image_use_shared_handle_;
    }

    virtual bool WillCopyForTesting() const;

protected:
    const ComD3D11VideoDevice& video_device() const { return video_device_; }

    const ComD3D11DeviceContext& device_context() const {
        return device_context_;
    }

private:
    friend class CopyTextureSelector;

    const VideoPixelFormat pixel_format_;
    const DXGI_FORMAT output_dxgifmt_;

    ComD3D11VideoDevice video_device_;
    ComD3D11DeviceContext device_context_;

    bool shared_image_use_shared_handle_;
};

class CopyTextureSelector : public TextureSelector {
public:
    // TODO(liberato): do we need |input_dxgifmt| here?
    CopyTextureSelector(VideoPixelFormat pixfmt,
        DXGI_FORMAT input_dxgifmt,
        DXGI_FORMAT output_dxgifmt,
        VideoColorSpace output_color_space,
        ComD3D11VideoDevice video_device,
        ComD3D11DeviceContext d3d11_device_context,
        bool use_shared_handle);
    ~CopyTextureSelector() override;

    std::unique_ptr<Texture2DWrapper> CreateTextureWrapper(
        ComD3D11Device device,
        Size size) override;

    bool DoesDecoderOutputUseSharedHandle() const override;

    bool WillCopyForTesting() const override;

private:
    VideoColorSpace output_color_space_;
};


#endif // D3D11_TEXTURE_SELECTOR_HPP_
