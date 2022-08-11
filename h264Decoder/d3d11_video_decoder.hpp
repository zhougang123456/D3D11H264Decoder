#ifndef D3D11_VIDEO_DECODER_HPP_
#define D3D11_VIDEO_DECODER_HPP_

#include <memory>

#include "video_decoder.hpp"
#include "d3d11_video_decoder_client.hpp"
#include "d3d11_com_defs.hpp"
#include "d3d11_h264_accelerator.hpp"
#include "d3d11_texture_selector.hpp"
#include "d3d11_decoder_configurator.hpp"
#include "OutputManager.h"

class D3D11VideoDecoder : public VideoDecoder,
                          public D3D11VideoDecoderClient{
public:
    static std::unique_ptr<VideoDecoder> Create();
    void Initialize(VideoColorSpace color_space, Size coded_size, HWND hWnd) override;
    void Decode(uint8_t* buffer, int size) override;

    D3D11PictureBuffer* GetPicture() override;
    void UpdateTimestamp(D3D11PictureBuffer* picture_buffer) override;
    bool OutputResult(const CodecPicture* picture,
        D3D11PictureBuffer* picture_buffer) override;

    static bool GetD3D11FeatureLevel(
        ComD3D11Device dev,
        D3D_FEATURE_LEVEL* feature_level);

protected:
    // Owners should call Destroy(). This is automatic via
    // std::default_delete<media::VideoDecoder> when held by a
    // std::unique_ptr<media::VideoDecoder>.
    ~D3D11VideoDecoder() override;

private:
    D3D11VideoDecoder();

    enum class State {
        // Initializing resources required to create a codec.
        kInitializing,

        // Initialization has completed and we're running. This is the only state
        // in which |codec_| might be non-null. If |codec_| is null, a codec
        // creation is pending.
        kRunning,

        // A fatal error occurred. A terminal state.
        kError,
    };

    // Receive |buffer|, that is now unused by the client.
    void ReceivePictureBufferFromClient(D3D11PictureBuffer* buffer);

    HRESULT InitializeAcceleratedDecoder(ComD3D11VideoDecoder video_decoder,VideoColorSpace color_space);

    // Create new PictureBuffers.  Currently, this completes synchronously, but
    // really should have an async interface since it must do some work on the
    // gpu main thread.
    void CreatePictureBuffers();

    // Create a D3D11VideoDecoder, if possible, based on the current config.
    ComD3D11VideoDecoder CreateD3D11Decoder();

    ComD3D11Device device_;
    ComD3D11DeviceContext device_context_;
    ComD3D11VideoDevice video_device_;
    OUTPUTMANAGER output_msg_;

    // D3D11 version on this device.
    D3D_FEATURE_LEVEL usable_feature_level_;

    std::unique_ptr<AcceleratedVideoDecoder> accelerated_video_decoder_;

    std::unique_ptr<D3D11DecoderConfigurator> decoder_configurator_;

    std::unique_ptr<TextureSelector> texture_selector_;

    // It would be nice to unique_ptr these, but we give a ref to the VideoFrame
    // so that the texture is retained until the mailbox is opened.
    std::vector<D3D11PictureBuffer*> picture_buffers_;

    // Profile of the video being decoded.
    VideoCodecProfile profile_ = VIDEO_CODEC_PROFILE_UNKNOWN;

    // The currently configured bit depth for the decoder. When this changes we
    // need to recreate the decoder.
    uint8_t bit_depth_ = 8u;

    // Should we use multiple single textures for the decoder output(true) or one
    // texture with multiple array slices (false)?
    bool use_single_video_decoder_texture_ = false;

    Size code_size_;

    State state_ = State::kInitializing;

    uint8_t* current_buffer_;

    int current_buffer_size_;
};
#endif // D3D_VIDEO_DECODER_HPP_
