#ifndef D3D11_H264_ACCELERATOR_HPP_
#define D3D11_H264_ACCELERATOR_HPP_
#include <d3d11_1.h>
#include <d3d9.h>
#include <dxva.h>
#include <wrl/client.h>

#include <vector>

#include "h264_decoder.hpp"
#include "h264_dpb.hpp"
#include "d3d11_com_defs.hpp"
#include "d3d11_video_decoder_client.hpp"
#include "d3d11_video_context_wrapper.hpp"

constexpr int kRefFrameMaxCount = 16;

class D3D11H264Accelerator : public H264Decoder::H264Accelerator
{
public:
	D3D11H264Accelerator(D3D11VideoDecoderClient* client,
						 ComD3D11VideoDevice video_device,
						 std::unique_ptr<VideoContextWrapper> video_context);


	~D3D11H264Accelerator() override;

    // H264Decoder::H264Accelerator implementation.
    H264Picture* CreateH264Picture() override;
    Status SubmitFrameMetadata(const H264SPS* sps,
        const H264PPS* pps,
        const H264DPB& dpb,
        const H264Picture::Vector& ref_pic_listp0,
        const H264Picture::Vector& ref_pic_listb0,
        const H264Picture::Vector& ref_pic_listb1,
        H264Picture* pic) override;
    Status SubmitSlice(const H264PPS* pps,
        const H264SliceHeader* slice_hdr,
        const H264Picture::Vector& ref_pic_list0,
        const H264Picture::Vector& ref_pic_list1,
        H264Picture* pic,
        const uint8_t* data,
        size_t size,
        const std::vector<SubsampleEntry>& subsamples) override;
    Status SubmitDecode(H264Picture* pic) override;
    void Reset() override;
    bool OutputPicture(H264Picture* pic) override;
    void SetVideoDecoder(ComD3D11VideoDecoder video_decoder) override;

    // Gets a pic params struct with the constant fields set.
    void FillPicParamsWithConstants(DXVA_PicParams_H264* pic_param);

    // Populate the pic params with fields from the SPS structure.
    void PicParamsFromSPS(DXVA_PicParams_H264* pic_param,
        const H264SPS* sps,
        bool field_pic);

    // Populate the pic params with fields from the PPS structure.
    bool PicParamsFromPPS(DXVA_PicParams_H264* pic_param, const H264PPS* pps);

    // Populate the pic params with fields from the slice header structure.
    void PicParamsFromSliceHeader(DXVA_PicParams_H264* pic_param,
        const H264SliceHeader* pps);

    void PicParamsFromPic(DXVA_PicParams_H264* pic_param, D3D11H264Picture* pic);

private:
    bool SubmitSliceData();
    bool RetrieveBitstreamBuffer();

    D3D11VideoDecoderClient* client_;

    ComD3D11VideoDecoder video_decoder_;
    ComD3D11VideoDevice video_device_;
    std::unique_ptr<VideoContextWrapper> video_context_;

    // This information set at the beginning of a frame and saved for processing
    // all the slices.
    DXVA_PicEntry_H264 ref_frame_list_[kRefFrameMaxCount];
    H264SPS sps_;
    INT field_order_cnt_list_[kRefFrameMaxCount][2];
    USHORT frame_num_list_[kRefFrameMaxCount];
    UINT used_for_reference_flags_;
    USHORT non_existing_frame_flags_;

    // Information that's accumulated during slices and submitted at the end
    std::vector<DXVA_Slice_H264_Short> slice_info_;
    size_t current_offset_ = 0;
    size_t bitstream_buffer_size_ = 0;
    uint8_t* bitstream_buffer_bytes_ = nullptr;

    // This contains the subsamples (clear and encrypted) of the slice data
    // in D3D11_VIDEO_DECODER_BUFFER_BITSTREAM buffer.
    std::vector<D3D11_VIDEO_DECODER_SUB_SAMPLE_MAPPING_BLOCK> subsamples_;
    // IV for the current frame.
    std::vector<uint8_t> frame_iv_;

};

#endif // D3D11_H264_ACCELERATOR_HPP_
