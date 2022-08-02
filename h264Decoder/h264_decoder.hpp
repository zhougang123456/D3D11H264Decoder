#ifndef H264_DECODER_HPP_
#define H264_DECODER_HPP_
#include "accelerated_video_decoder.hpp"
#include "h264_dpb.hpp"
#include "d3d11_com_defs.hpp"

class H264Decoder : public AcceleratedVideoDecoder
{
public:
	class H264Accelerator
	{
	public:
		enum class Status
		{
			kOk,
			kFail,
			kTryAgain,
			kNotSupported,
		};
		H264Accelerator();
		virtual ~H264Accelerator();
		virtual H264Picture* CreateH264Picture() = 0;
		virtual Status SubmitFrameMetadata(
			const H264SPS* sps,
			const H264PPS* pps,
			const H264DPB& dpb,
			const H264Picture::Vector& ref_pic_listp0,
			const H264Picture::Vector& ref_pic_listb0,
			const H264Picture::Vector& ref_pic_listb1,
			H264Picture* pic) = 0;
		virtual Status SubmitSlice(
			const H264PPS* pps,
			const H264SliceHeader* slice_hdr,
			const H264Picture::Vector& ref_pic_list0,
			const H264Picture::Vector& ref_pic_list1,
			H264Picture* pic,
			const uint8_t* data,
			size_t size,
			const std::vector<SubsampleEntry>& subsamples) = 0;

		virtual Status SubmitDecode(H264Picture* pic) = 0;
		virtual bool OutputPicture(H264Picture* pic) = 0;
		virtual void Reset() = 0;
		virtual Status SetStream(const uint8_t* stream, size_t size);
		virtual void SetVideoDecoder(ComD3D11VideoDecoder video_decoder) = 0;
	};

	H264Decoder(std::unique_ptr<H264Accelerator> accelerator,
				VideoCodecProfile profile,
				const VideoColorSpace& container_color_space = VideoColorSpace());
	~H264Decoder() override;

	void SetStream(int32_t id, uint8_t* buffer, size_t size) override;
	bool Flush() override;
	void Reset() override;
	DecodeResult Decode() override;
	Size GetPicSize() const override;
	Rect GetVisibleRect() const override;
	VideoCodecProfile GetProfile() const override;
	uint8_t GetBitDepth() const override;
	size_t GetRequiredNumOfPictures() const override;
	size_t GetNumReferenceFrames() const override;

	static bool IsNewPrimaryCodedPicture(const H264Picture* curr_pic,
										 int curr_pps_id,
										 const H264SPS* sps,
										 const H264SliceHeader& slice_hdr);
	static bool FillH264PictureFromSliceHeader(const H264SPS* sps,
											   const H264SliceHeader& slice_hdr,
											   H264Picture* pic);

	void SetVideoDecoder(ComD3D11VideoDecoder video_decoder);
private:
	enum class State
	{
		kNeedStreamMetadata,
		kDecoding,
		kAfterReset,
		kParseSliceHeader,
		kTryPreprocessCurrentSlice,
		kEnsurePicture,
		kTryNewFrame,
		kTryCurrentSlice,
		kError,
	};

	bool ProcessSPS(int sps_id, bool* need_new_buffers);

	// Processes a CENCv1 encrypted slice header and fills in |curr_slice_hdr_|
	// with the relevant parsed fields.
	//H264Accelerator::Status ProcessEncryptedSliceHeader(
	//	const std::vector<SubsampleEntry>& subsamples);

	// Process current slice header to discover if we need to start a new picture,
	// finishing up the current one.
	H264Accelerator::Status PreprocessCurrentSlice();
	// Process current slice as a slice of the current picture.
	H264Accelerator::Status ProcessCurrentSlice();

	// Initialize the current picture according to data in |slice_hdr|.
	bool InitCurrPicture(const H264SliceHeader* slice_hdr);

	// Initialize |pic| as a "non-existing" picture (see spec) with |frame_num|,
	// to be used for frame gap concealment.
	bool InitNonexistingPicture(H264Picture* pic, int frame_num);

	// Calculate picture order counts for |pic| on initialization
	// of a new frame (see spec).
	bool CalculatePicOrderCounts(H264Picture* pic);

	// Update PicNum values in pictures stored in DPB on creation of
	// a picture with |frame_num|.
	void UpdatePicNums(int frame_num);

	bool UpdateMaxNumReorderFrames(const H264SPS* sps);

	// Prepare reference picture lists for the current frame.
	void PrepareRefPicLists();
	// Prepare reference picture lists for the given slice.
	bool ModifyReferencePicLists(const H264SliceHeader* slice_hdr,
		H264Picture::Vector* ref_pic_list0,
		H264Picture::Vector* ref_pic_list1);

	// Construct initial reference picture lists for use in decoding of
	// P and B pictures (see 8.2.4 in spec).
	void ConstructReferencePicListsP();
	void ConstructReferencePicListsB();

	// Helper functions for reference list construction, per spec.
	int PicNumF(const H264Picture& pic);
	int LongTermPicNumF(const H264Picture& pic);

	// Perform the reference picture lists' modification (reordering), as
	// specified in spec (8.2.4).
	//
	// |list| indicates list number and should be either 0 or 1.
	bool ModifyReferencePicList(const H264SliceHeader* slice_hdr,
		int list,
		H264Picture::Vector* ref_pic_listx);

	// Perform reference picture memory management operations (marking/unmarking
	// of reference pictures, long term picture management, discarding, etc.).
	// See 8.2.5 in spec.
	bool HandleMemoryManagementOps(H264Picture* pic);
	bool ReferencePictureMarking(H264Picture* pic);
	bool SlidingWindowPictureMarking();

	// Handle a gap in frame_num in the stream up to |frame_num|, by creating
	// "non-existing" pictures (see spec).
	bool HandleFrameNumGap(int frame_num);

	// Start processing a new frame.
	H264Accelerator::Status StartNewFrame(const H264SliceHeader* slice_hdr);

	// All data for a frame received, process it and decode.
	H264Accelerator::Status FinishPrevFrameIfPresent();

	// Called after we are done processing |pic|. Performs all operations to be
	// done after decoding, including DPB management, reference picture marking
	// and memory management operations.
	// This will also output pictures if any have become ready to be outputted
	// after processing |pic|.
	bool FinishPicture(H264Picture* pic);

	// Clear DPB contents and remove all surfaces in DPB from *in_use_ list.
	// Cleared pictures will be made available for decode, unless they are
	// at client waiting to be displayed.
	void ClearDPB();

	// Commits all pending data for HW decoder and starts HW decoder.
	H264Accelerator::Status DecodePicture();

	// Notifies client that a picture is ready for output.
	bool OutputPic(H264Picture* pic);

	// Output all pictures in DPB that have not been outputted yet.
	bool OutputAllRemainingPics();

	// Decoder state.
	State state_;

	// The colorspace for the h264 container.
	const VideoColorSpace container_color_space_;

	// Parser in use.
	H264Parser parser_;

	// Most recent call to SetStream().
	const uint8_t* current_stream_ = nullptr;
	size_t current_stream_size_ = 0;

	// Decrypting config for the most recent data passed to SetStream().
	//std::unique_ptr<DecryptConfig> current_decrypt_config_;

	// Keep track of when SetStream() is called so that
	// H264Accelerator::SetStream() can be called.
	bool current_stream_has_been_changed_ = false;

	// DPB in use.
	H264DPB dpb_;

	// Current stream buffer id; to be assigned to pictures decoded from it.
	int32_t stream_id_ = -1;

	// Picture currently being processed/decoded.
	H264Picture* curr_pic_;

	// Reference picture lists, constructed for each frame.
	H264Picture::Vector ref_pic_list_p0_;
	H264Picture::Vector ref_pic_list_b0_;
	H264Picture::Vector ref_pic_list_b1_;

	// Global state values, needed in decoding. See spec.
	int max_frame_num_;
	int max_pic_num_;
	int max_long_term_frame_idx_;
	size_t max_num_reorder_frames_;

	int prev_frame_num_;
	int prev_ref_frame_num_;
	int prev_frame_num_offset_;
	bool prev_has_memmgmnt5_;

	// Values related to previously decoded reference picture.
	bool prev_ref_has_memmgmnt5_;
	int prev_ref_top_field_order_cnt_;
	int prev_ref_pic_order_cnt_msb_;
	int prev_ref_pic_order_cnt_lsb_;
	H264Picture::Field prev_ref_field_;

	// Currently active SPS and PPS.
	int curr_sps_id_;
	int curr_pps_id_;

	// Last PPS that was parsed. Used for full sample encryption, which has the
	// assumption this is streaming content which does not switch between
	// different PPSes in the stream (they are present once in the container for
	// the stream).
	int last_parsed_pps_id_;

	// Copies of the last SPS and PPS NALUs, used for full sample encryption.
	std::vector<uint8_t> last_sps_nalu_;
	std::vector<uint8_t> last_pps_nalu_;

	// Current NALU and slice header being processed.
	std::unique_ptr<H264NALU> curr_nalu_;
	std::unique_ptr<H264SliceHeader> curr_slice_hdr_;

	// Encrypted SEI NALUs preceding a fully encrypted slice NALU. We need to
	// save these that are part of a single sample so they can all be decrypted
	// together.
	std::vector<const uint8_t*> encrypted_sei_nalus_;
	std::vector<SubsampleEntry> sei_subsamples_;

	// These are absl::nullopt unless get recovery point SEI message after Reset.
	// A frame_num of the frame at output order that is correct in content.
	int recovery_frame_num_;
	// A value in the recovery point SEI message to compute |recovery_frame_num_|
	// later.
	int recovery_frame_cnt_;

	// Output picture size.
	Size pic_size_;
	// Output visible cropping rect.
	Rect visible_rect_;

	// Profile of input bitstream.
	VideoCodecProfile profile_;
	// Bit depth of input bitstream.
	uint8_t bit_depth_ = 0;

	// PicOrderCount of the previously outputted frame.
	int last_output_poc_;

	const std::unique_ptr<H264Accelerator> accelerator_;
};

#endif // H264_DECODER_HPP_
