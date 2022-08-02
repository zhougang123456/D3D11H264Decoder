#ifndef D3D11_TEXTURE_WRAPPER_HPP_
#define D3D11_TEXTURE_WRAPPER_HPP_

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

#include "d3d11_com_defs.hpp"
#include "video_color_space.hpp"
#include "size.hpp"

class Texture2DWrapper
{
public:
	Texture2DWrapper();
	virtual ~Texture2DWrapper();

	virtual bool Init(ComD3D11Texture2D texture, size_t array_size) = 0;

	virtual bool ProcessTexture(const VideoColorSpace& input_color_space,
								VideoColorSpace* output_color_space) = 0;
};

class DefaultTexture2DWrapper : public Texture2DWrapper {
public:
	DefaultTexture2DWrapper(const Size& size, DXGI_FORMAT dxgi_format);
	~DefaultTexture2DWrapper() override;

	bool Init(ComD3D11Texture2D texture, size_t array_size) override;
	bool ProcessTexture(const VideoColorSpace& input_color_space,
						VideoColorSpace* output_color_space) override;
private:
	Size size_;
	DXGI_FORMAT dxgi_format_;

};

#endif // D3D11_TEXTURE_WRAPPER_HPP_
