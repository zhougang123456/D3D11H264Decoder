#include "d3d11_video_decoder.hpp"

#include <d3d11_4.h>
#include <memory>
#include <utility>

#include "video_codecs.hpp"
#include "d3d11_picture_buffer.hpp"
#include "d3d11_video_context_wrapper.hpp"
#include "d3d11_decoder_configurator.hpp"
#include <cassert>

std::unique_ptr<VideoDecoder> D3D11VideoDecoder::Create()
{
    return std::unique_ptr<VideoDecoder>(new D3D11VideoDecoder());
}

void D3D11VideoDecoder::Initialize(VideoColorSpace color_space, Size coded_size)
{
    code_size_ = coded_size;
    state_ = State::kInitializing;

    D3D_DRIVER_TYPE DriverTypes[] =
    {
        D3D_DRIVER_TYPE_HARDWARE,
        D3D_DRIVER_TYPE_WARP,
        D3D_DRIVER_TYPE_REFERENCE,
    };
    UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

    // Feature levels supported
    D3D_FEATURE_LEVEL FeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_1
    };
    UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

    D3D_FEATURE_LEVEL FeatureLevel;

    HRESULT hr;
    for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
    {
        hr = D3D11CreateDevice(NULL, DriverTypes[DriverTypeIndex], NULL, 0, FeatureLevels, NumFeatureLevels, D3D11_SDK_VERSION, &device_, &FeatureLevel, &device_context_);
        if (SUCCEEDED(hr))
        {
            break;
        }
    }

    if (!GetD3D11FeatureLevel(device_, &usable_feature_level_)) {
        std::cout << "D3D11 feature level not supported" << std::endl;
        return;
    }

    hr = device_.As(&video_device_);
    if (!SUCCEEDED(hr)) {
        std::cout <<"Failed to get video device" << std::endl;
        return;

    }
    auto video_decoder = CreateD3D11Decoder();
    
    hr = InitializeAcceleratedDecoder(video_decoder, color_space);
    if (!SUCCEEDED(hr)) {
        std::cout << "Failed to get device context" << std::endl;
        return;
    }

}

void D3D11VideoDecoder::Decode(uint8_t* buffer, int size)
{
    current_buffer_ = buffer; 
    current_buffer_size_ = size;

    accelerated_video_decoder_->SetStream(-1, current_buffer_, current_buffer_size_);

    while (true) {
        if (state_ == State::kError)
            return;
        if (!current_buffer_)
            break;
        AcceleratedVideoDecoder::DecodeResult result = accelerated_video_decoder_->Decode();
        if (result == AcceleratedVideoDecoder::kRanOutOfStreamData) {
            current_buffer_ = nullptr;
            std::cout << "decode end" << std::endl;
            break;
        }
        else if (result == AcceleratedVideoDecoder::kRanOutOfSurfaces) {
            // At this point, we know the picture size.
            // If we haven't allocated picture buffers yet, then allocate some now.
            // Otherwise, stop here.  We'll restart when a picture comes back.
            if (picture_buffers_.size())
                return;

            CreatePictureBuffers();
        }
        else if (result == AcceleratedVideoDecoder::kConfigChange) {
            // Before the first frame, we get a config change that we should ignore.
            // We only want to take action if this is a mid-stream config change.  We
            // could wait until now to allocate the first D3D11VideoDecoder, but we
            // don't, so that init can fail rather than decoding if there's a problem
            // creating it.  We could also unconditionally re-allocate the decoder,
            // but we keep it if it's ready to go.
            const auto new_bit_depth = accelerated_video_decoder_->GetBitDepth();
            const auto new_profile = accelerated_video_decoder_->GetProfile();
            const auto new_coded_size = accelerated_video_decoder_->GetPicSize();
            if (new_profile == profile_ &&
                new_coded_size == code_size_ &&
                new_bit_depth == bit_depth_ && !picture_buffers_.size()) {
                continue;
            }

            // Update the config.
            std::cout
                << "D3D11VideoDecoder config change: profile: "
                << static_cast<int>(new_profile) << " coded_size: ("
                << new_coded_size.width() << ", " << new_coded_size.height() << ")";
            profile_ = new_profile;
            code_size_ = new_coded_size;

            // Replace the decoder, and clear any picture buffers we have.  It's okay
            // if we don't have any picture buffer yet; this might be before the
            // accelerated decoder asked for any.
            auto video_decoder_or_error = CreateD3D11Decoder();
            if (video_decoder_or_error == nullptr) {
                std::wcout << "video decoder error" << std::endl;
                return;
            }
            
            H264Decoder* decoder = dynamic_cast<H264Decoder*>(accelerated_video_decoder_.get());
            
            decoder->SetVideoDecoder(video_decoder_or_error);

            picture_buffers_.clear();
        }
        else if (result == AcceleratedVideoDecoder::kTryAgain) {
            std::cout << "Try again is not supported" << std::endl;
            return;
        }
        else {
            std::cout << "VDA Error " << result << std::endl;
            return;
        }
    }
}

D3D11VideoDecoder::D3D11VideoDecoder()
{

}

D3D11VideoDecoder::~D3D11VideoDecoder() {

    // Explicitly destroy the decoder, since it can reference picture buffers.
    accelerated_video_decoder_.reset();
   
}

D3D11PictureBuffer* D3D11VideoDecoder::GetPicture() {

    for (auto& buffer : picture_buffers_) {
        if (!buffer->in_client_use() && !buffer->in_picture_use()) {
            return buffer;
        }
    }

    return nullptr;
}

void D3D11VideoDecoder::UpdateTimestamp(D3D11PictureBuffer* picture_buffer) {
    
}

void saveBmpFile(const char* fileName, unsigned char* pImgData, int imgLength, int width, int height)
{
    BITMAPFILEHEADER bmheader;
    memset(&bmheader, 0, sizeof(bmheader));
    bmheader.bfType = 0x4d42;     //图像格式。必须为'BM'格式。  
    bmheader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER); //从文件开头到数据的偏移量  
    bmheader.bfSize = imgLength + bmheader.bfOffBits;//文件大小  

    BITMAPINFOHEADER bmInfo;
    memset(&bmInfo, 0, sizeof(bmInfo));
    bmInfo.biSize = sizeof(bmInfo);
    bmInfo.biWidth = width;
    bmInfo.biHeight = height;
    bmInfo.biPlanes = 1;
    bmInfo.biBitCount = 32;
    bmInfo.biCompression = BI_RGB;

    HANDLE hFile = CreateFileA(fileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD dwWritten;
        BOOL bRet = WriteFile(hFile, &bmheader, sizeof(BITMAPFILEHEADER), &dwWritten, NULL);
        assert(TRUE == bRet);
        bRet = WriteFile(hFile, &bmInfo, sizeof(BITMAPINFOHEADER), &dwWritten, NULL);
        assert(TRUE == bRet);
        bRet = WriteFile(hFile, pImgData, imgLength, &dwWritten, NULL);
        assert(TRUE == bRet);
        CloseHandle(hFile);
    }
}

void NV12_T_RGB(unsigned int width, unsigned int height, unsigned char* Y, unsigned char* UV, unsigned char* rgb)
{
    int r, g, b;
    int y, u, v;
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            y = Y[i * width + j];
            u = UV[(i / 2 * width + j / 2 * 2)];
            v = UV[(i / 2 * width + j / 2 * 2) + 1];
            // TRACE("yuv(%d, %d, %d)\n", i * width + j, (i / 2 * width + j / 2 * 2), (i / 2 * width + j / 2 * 2) + 1);
            r = y + 1.4075 * (v - 128);  //r
            g = y - 0.344 * (u - 128) - 0.714 * (v - 128); //g
            b = y + 1.770 * (u - 128); //b

            if (r > 255)   r = 255;
            if (g > 255)   g = 255;
            if (b > 255)   b = 255;
            if (r < 0)     r = 0;
            if (g < 0)     g = 0;
            if (b < 0)     b = 0;

            rgb[(i * width + j) * 4 + 0] = (unsigned char)b;
            rgb[(i * width + j) * 4 + 1] = (unsigned char)g;
            rgb[(i * width + j) * 4 + 2] = (unsigned char)r;
            rgb[(i * width + j) * 4 + 3] = 255;
        }
    }
}


bool D3D11VideoDecoder::OutputResult(const CodecPicture* picture,
    D3D11PictureBuffer* picture_buffer) {

    picture_buffer->add_client_use();

    // Note: The pixel format doesn't matter.
    Rect visible_rect = picture->visible_rect();

    ReceivePictureBufferFromClient(picture_buffer);

    ComD3D11Texture2D texture = picture_buffer->Texture();

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    //desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    ID3D11Texture2D* des;
    device_->CreateTexture2D(&desc, NULL, &des);
    if (!des) {
        return false;
    }
    device_context_->CopyResource(des, texture.Get());

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = device_context_->Map(des, 0, D3D11_MAP_READ, 0, &mappedResource);

    size_t imageSize = desc.Width * desc.Height * 4;
    uint8_t* rgba = (uint8_t*)malloc(imageSize);
    memset(rgba, 0, imageSize);
    uint8_t* pData = (uint8_t*)mappedResource.pData;
   /* for (int i = 0; i < desc.Height; i++) {
        memcpy(rgba + desc.Width * 4 * i, rgba + i * mappedResource.RowPitch, desc.Width * 4);
    }*/
    
    NV12_T_RGB(desc.Width, desc.Height, pData, pData + mappedResource.RowPitch * desc.Height, rgba);
    static int image_id = 0;
    char buf[10] = { 0 };
    sprintf_s(buf, "%d.bmp", image_id++);
    saveBmpFile(buf, rgba, imageSize, desc.Width, desc.Height);
    free(rgba);
    device_context_->Unmap(des, 0);
    des->Release();
    return true;
}

// static
bool D3D11VideoDecoder::GetD3D11FeatureLevel(
    ComD3D11Device dev,
    D3D_FEATURE_LEVEL* feature_level) {
    if (!dev || !feature_level)
        return false;

    *feature_level = dev->GetFeatureLevel();
    if (*feature_level < D3D_FEATURE_LEVEL_11_0)
        return false;

    return true;
}

void  D3D11VideoDecoder::ReceivePictureBufferFromClient(D3D11PictureBuffer* buffer)
{
    buffer->remove_client_use();
}

HRESULT D3D11VideoDecoder::InitializeAcceleratedDecoder(
    ComD3D11VideoDecoder video_decoder,
    VideoColorSpace color_space) {
    std::cout << "gpu ""D3D11VideoDecoder::InitializeAcceleratedDecoder" << std::endl;
    // If we got an 11.1 D3D11 Device, we can use a |ID3D11VideoContext1|,
    // otherwise we have to make sure we only use a |ID3D11VideoContext|.
    HRESULT hr;

    // |device_context_| is the primary display context, but currently
    // we share it for decoding purposes.
    auto video_context = VideoContextWrapper::CreateWrapper(usable_feature_level_,
        device_context_, &hr);

    if (!SUCCEEDED(hr))
        return hr;

    accelerated_video_decoder_ = std::make_unique<H264Decoder>(
                std::make_unique<D3D11H264Accelerator>(
                this, video_device_, std::move(video_context)),
                profile_, color_space);

    return hr;
}

void D3D11VideoDecoder::CreatePictureBuffers() {
    // TODO(liberato): When we run off the gpu main thread, this call will need
    // to signal success / failure asynchronously.  We'll need to transition into
    // a "waiting for pictures" state, since D3D11PictureBuffer will post the gpu
    // thread work.
    std::cout << "gpu""D3D11VideoDecoder::CreatePictureBuffers" << std::endl;
    Size size = accelerated_video_decoder_->GetPicSize();

    if (decoder_configurator_->TextureFormat() == DXGI_FORMAT_P010) {
        // For HDR formats, try to get the display metadata.  This may fail, which
        // is okay.  We'll just skip sending the metadata.
      
    }

    // Drop any old pictures.
    /*for (auto& buffer : picture_buffers_)
        DCHECK(!buffer->in_picture_use());*/
    picture_buffers_.clear();

    ComD3D11Texture2D in_texture;

    // Create each picture buffer.
    for (size_t i = 0; i < D3D11DecoderConfigurator::BUFFER_COUNT; i++) {
        // Create an input texture / texture array if we haven't already.
        if (!in_texture) {
            auto result = decoder_configurator_->CreateOutputTexture(
                device_, size,
                use_single_video_decoder_texture_
                ? 1
                : D3D11DecoderConfigurator::BUFFER_COUNT,
                true);
            if (result) {
                in_texture = std::move(result);
            }
            else {
                //NotifyError(std::move(result).error().AddHere());
                return;
            }
        }

        if (!!!in_texture) {
            return;
        }

        auto tex_wrapper = texture_selector_->CreateTextureWrapper(device_, size);
        if (!tex_wrapper) {
            std::cout << "StatusCode::kAllocateTextureForCopyingWrapperFailed" << std::endl;
            return;
        }

        const size_t array_slice = use_single_video_decoder_texture_ ? 0 : i;
        picture_buffers_.push_back(
            new D3D11PictureBuffer(in_texture, array_slice,
                std::move(tex_wrapper), size, i /* level */));
        bool result = picture_buffers_[i]->Init(
            video_device_,
            decoder_configurator_->DecoderGuid());
        if (!result) {
            //NotifyError(std::move(result).AddHere());
            return;
        }

        // If we're using one texture per buffer, rather than an array, then clear
        // the ref to it so that we allocate a new one above.
        if (use_single_video_decoder_texture_)
            in_texture = nullptr;
    }
}

ComD3D11VideoDecoder D3D11VideoDecoder::CreateD3D11Decoder() {
    // By default we assume outputs are 8-bit for SDR color spaces and 10 bit for
    // HDR color spaces (or VP9.2). We'll get a config change once we know the
    // real bit depth if this turns out to be wrong.
    bit_depth_ = 8;

    // OS prevent read any content from encrypted video frame. No need to support
    // shared handle and keyed_mutex system for the encrypted frame.
    const bool use_shared_handle = true;

    // TODO: supported check?
    decoder_configurator_ = D3D11DecoderConfigurator::Create(
        code_size_, bit_depth_,
        use_shared_handle);
    if (!decoder_configurator_)
        return nullptr;

    if (!decoder_configurator_->SupportsDevice(video_device_))
        return nullptr;

    // Use IsHDRSupported to guess whether the compositor can output HDR textures.
    // See TextureSelector for notes about why the decoder should not care.
    texture_selector_ = TextureSelector::Create(
        decoder_configurator_->TextureFormat(),
        TextureSelector::HDRMode::kSDROnly,
        video_device_, device_context_,
        use_shared_handle);
    if (!texture_selector_)
        return nullptr;

    UINT config_count = 0;
    auto hr = video_device_->GetVideoDecoderConfigCount(
        decoder_configurator_->DecoderDescriptor(), &config_count);
    if (FAILED(hr)) {
        return nullptr;
    }

    if (config_count == 0)
        return nullptr;

    D3D11_VIDEO_DECODER_CONFIG dec_config = {};
    bool found = false;

    for (UINT i = 0; i < config_count; i++) {
        hr = video_device_->GetVideoDecoderConfig(
            decoder_configurator_->DecoderDescriptor(), i, &dec_config);
        if (FAILED(hr)) {
            return nullptr;
        }

        if (dec_config.ConfigBitstreamRaw == 2) {
            // ConfigBitstreamRaw == 2 means the decoder uses DXVA_Slice_H264_Short.
            found = true;
            break;
        }
    }
    if (!found)
        return nullptr;

    // Prefer whatever the config tells us about whether to use one Texture2D with
    // multiple array slices, or multiple Texture2Ds with one slice each.  If bit
    // 14 is clear, then it's the former, else it's the latter.
    //
    // Let the workaround override array texture mode, if enabled.
    // TODO(crbug.com/971952): Ignore |use_single_video_decoder_texture_| here,
    // since it might be the case that it's not actually the right fix.  Instead,
    // We use this workaround to force a copy later.  The workaround will be
    // renamed if this turns out to fix the issue, but we might need to merge back
    // and smaller changes are better.
    //
    // For more information, please see:
    // https://download.microsoft.com/download/9/2/A/92A4E198-67E0-4ABD-9DB7-635D711C2752/DXVA_VPx.pdf
    // https://download.microsoft.com/download/5/f/c/5fc4ec5c-bd8c-4624-8034-319c1bab7671/DXVA_H264.pdf
    //
    // When creating output texture with shared handle supports, we can't use a
    // texture array. Because the keyed mutex applies on the entire texture array
    // causing a deadlock when multiple threads try to use different slots of the
    // array. More info here: https://crbug.com/1238943
    use_single_video_decoder_texture_ =
        !!(dec_config.ConfigDecoderSpecific & (1 << 14)) || use_shared_handle;
    if (use_single_video_decoder_texture_)
        std::cout << "D3D11VideoDecoder is using single textures" << std::endl;
    else
        std::cout << "D3D11VideoDecoder is using array texture" << std::endl;

    Microsoft::WRL::ComPtr<ID3D11VideoDecoder> video_decoder;
    hr = video_device_->CreateVideoDecoder(
        decoder_configurator_->DecoderDescriptor(), &dec_config, &video_decoder);

    if (!video_decoder.Get())
        return nullptr;

    if (FAILED(hr)) {
        return nullptr;
    }

    return { std::move(video_decoder) };
}