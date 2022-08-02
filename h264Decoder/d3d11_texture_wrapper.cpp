#include "d3d11_texture_wrapper.hpp"

#include <list>
#include <memory>
#include <utility>
#include <vector>

bool SupportsFormat(DXGI_FORMAT dxgi_format) {
    switch (dxgi_format) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return true;
    default:
        return false;
    }
}

size_t NumPlanes(DXGI_FORMAT dxgi_format) {
    switch (dxgi_format) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
        return 2;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return 1;
    default:
        
        return 0;
    }
}

Texture2DWrapper::Texture2DWrapper() = default;

Texture2DWrapper::~Texture2DWrapper() = default;

DefaultTexture2DWrapper::DefaultTexture2DWrapper(const Size & size, DXGI_FORMAT dxgi_format)
       : size_(size), dxgi_format_(dxgi_format) {}

DefaultTexture2DWrapper::~DefaultTexture2DWrapper() = default;

bool DefaultTexture2DWrapper::Init(ComD3D11Texture2D texture, size_t array_size)
{
    if (!SupportsFormat(dxgi_format_)) {
        return false;
    }

    if (texture) {
        D3D11_TEXTURE2D_DESC desc = {};
        texture->GetDesc(&desc);

    }
}

bool DefaultTexture2DWrapper::ProcessTexture(const VideoColorSpace& input_color_space,
    VideoColorSpace* output_color_space) 
{
    return true;
}