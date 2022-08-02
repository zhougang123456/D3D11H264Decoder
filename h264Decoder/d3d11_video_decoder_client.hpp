#ifndef D3D_VIDEO_DECODER_CLIENT_HPP_
#define D3D_VIDEO_DECODER_CLIENT_HPP_
class CodecPicture;
class D3D11PictureBuffer;

class D3D11VideoDecoderClient {
public:
    virtual D3D11PictureBuffer* GetPicture() = 0;
    virtual void UpdateTimestamp(D3D11PictureBuffer* picture_buffer) = 0;
    virtual bool OutputResult(const CodecPicture* picture,
                              D3D11PictureBuffer* picture_buffer) = 0;

protected:
    virtual ~D3D11VideoDecoderClient() = default;
};
#endif // D3D_VIDEO_DECODER_CLIENT_HPP_
