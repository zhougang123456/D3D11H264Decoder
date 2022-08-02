#include <algorithm>
#include <limits>
#include <memory>

#include "h264_level_limits.hpp"
#include "h264_decoder.hpp"

H264Decoder::H264Accelerator::H264Accelerator() = default;

H264Decoder::H264Accelerator::~H264Accelerator() = default;

H264Decoder::H264Accelerator::Status H264Decoder::H264Accelerator::SetStream(const uint8_t * stream, size_t size) {
    return H264Decoder::H264Accelerator::Status::kNotSupported;
}

H264Decoder::H264Decoder(std::unique_ptr<H264Accelerator> accelerator,
    VideoCodecProfile profile,
    const VideoColorSpace& container_color_space)
    : state_(State::kNeedStreamMetadata),
    container_color_space_(container_color_space),
    max_frame_num_(0),
    max_pic_num_(0),
    max_long_term_frame_idx_(0),
    max_num_reorder_frames_(0),
    // TODO(hiroh): Set profile to UNKNOWN.
    profile_(profile),
    accelerator_(std::move(accelerator)) {
    //DCHECK(accelerator_);
    Reset();
}

H264Decoder::~H264Decoder() = default;

void H264Decoder::SetStream(int32_t id, uint8_t* buffer, size_t size) {
    const uint8_t* ptr = buffer;

    std::cout << "New input stream id: " << id << " at: " << (void*)ptr
        << " size: " << size << std::endl;
    stream_id_ = id;
    current_stream_ = ptr;
    current_stream_size_ = size;
    current_stream_has_been_changed_ = true;
    encrypted_sei_nalus_.clear();
    sei_subsamples_.clear();
    
    parser_.SetStream(ptr, size);
}

bool H264Decoder::Flush() {
    std::cout << "Decoder flush";

    if (!OutputAllRemainingPics())
        return false;

    ClearDPB();
    std::cout << "Decoder flush finished";
    return true;
}

void H264Decoder::Reset() {
    curr_pic_ = nullptr;
    curr_nalu_ = nullptr;
    curr_slice_hdr_ = nullptr;
    curr_sps_id_ = -1;
    curr_pps_id_ = -1;

    prev_frame_num_ = -1;
    prev_ref_frame_num_ = -1;
    prev_frame_num_offset_ = -1;
    prev_has_memmgmnt5_ = false;

    prev_ref_has_memmgmnt5_ = false;
    prev_ref_top_field_order_cnt_ = -1;
    prev_ref_pic_order_cnt_msb_ = -1;
    prev_ref_pic_order_cnt_lsb_ = -1;
    prev_ref_field_ = H264Picture::FIELD_NONE;

    ref_pic_list_p0_.clear();
    ref_pic_list_b0_.clear();
    ref_pic_list_b1_.clear();
    dpb_.Clear();
    parser_.Reset();
    accelerator_->Reset();
    last_output_poc_ = INT_MIN;

    encrypted_sei_nalus_.clear();
    sei_subsamples_.clear();

    recovery_frame_num_ = 0;
    recovery_frame_cnt_ = 0;

    // If we are in kDecoding, we can resume without processing an SPS.
    // The state becomes kDecoding again, (1) at the first IDR slice or (2) at
    // the first slice after the recovery point SEI.
    if (state_ == State::kDecoding)
        state_ = State::kAfterReset;
}

#define SET_ERROR_AND_RETURN()         \
  do {                                 \
    std::cout << "Error during decode"; \
    state_ = State::kError;            \
    return H264Decoder::kDecodeError;  \
  } while (0)

#define CHECK_ACCELERATOR_RESULT(func)             \
  do {                                             \
    H264Accelerator::Status result = (func);       \
    switch (result) {                              \
      case H264Accelerator::Status::kOk:           \
        break;                                     \
      case H264Accelerator::Status::kTryAgain:     \
        std::cout << #func " needs to try again" << std::endl;   \
        return H264Decoder::kTryAgain;             \
      case H264Accelerator::Status::kFail:         \
      case H264Accelerator::Status::kNotSupported: \
        SET_ERROR_AND_RETURN();                    \
    }                                              \
  } while (0)

H264Decoder::DecodeResult H264Decoder::Decode() {
    if (state_ == State::kError) {
        std::cout << "Decoder in error state";
        return kDecodeError;
    }

    if (current_stream_has_been_changed_) {
        // Calling H264Accelerator::SetStream() here instead of when the stream is
        // originally set in case the accelerator needs to return kTryAgain.
        H264Accelerator::Status result = accelerator_->SetStream(
            current_stream_, current_stream_size_);
        switch (result) {
        case H264Accelerator::Status::kOk:
        case H264Accelerator::Status::kNotSupported:
            // kNotSupported means the accelerator can't handle this stream,
            // so everything will be done through the parser.
            break;
        case H264Accelerator::Status::kTryAgain:
            std::cout << "SetStream() needs to try again";
            return H264Decoder::kTryAgain;
        case H264Accelerator::Status::kFail:
            SET_ERROR_AND_RETURN();
        }

        // Reset the flag so that this is only called again next time SetStream()
        // is called.
        current_stream_has_been_changed_ = false;
    }

    while (1) {
        H264Parser::Result par_res;

        if (!curr_nalu_) {
            curr_nalu_ = std::make_unique<H264NALU>();
            par_res = parser_.AdvanceToNextNALU(curr_nalu_.get());
            if (par_res == H264Parser::kEOStream) {
                CHECK_ACCELERATOR_RESULT(FinishPrevFrameIfPresent());
                return kRanOutOfStreamData;
            }
            else if (par_res != H264Parser::kOk) {
                SET_ERROR_AND_RETURN();
            }

            std::cout << "New NALU: " << static_cast<int>(curr_nalu_->nal_unit_type) << std::endl;  
        }

        switch (curr_nalu_->nal_unit_type) {
        case H264NALU::kNonIDRSlice:
            // We can't resume from a non-IDR slice unless recovery point SEI
            // process is going.
            if (state_ == State::kError ||
                (state_ == State::kAfterReset && !recovery_frame_cnt_))
                break;

        case H264NALU::kIDRSlice: {
            // TODO(posciak): the IDR may require an SPS that we don't have
            // available. For now we'd fail if that happens, but ideally we'd like
            // to keep going until the next SPS in the stream.
            if (state_ == State::kNeedStreamMetadata) {
                // We need an SPS, skip this IDR and keep looking.
                break;
            }

            // If after reset or waiting for a key, we should be able to recover
            // from an IDR. |state_|, |curr_slice_hdr_|, and |curr_pic_| are used
            // to keep track of what has previously been attempted, so that after
            // a retryable result is returned, subsequent calls to Decode() retry
            // the call that failed previously. If it succeeds (it may not if no
            // additional key has been provided, for example), then the remaining
            // steps will be executed.
            if (!curr_slice_hdr_) {
                curr_slice_hdr_ = std::make_unique<H264SliceHeader>();
                state_ = State::kParseSliceHeader;
            }

            if (state_ == State::kParseSliceHeader) {
                // Check if the slice header is encrypted.
                bool parsed_header = false;
                
                if (!parsed_header) {
                    par_res =
                        parser_.ParseSliceHeader(*curr_nalu_, curr_slice_hdr_.get());
                    if (par_res != H264Parser::kOk)
                        SET_ERROR_AND_RETURN();
                }
                state_ = State::kTryPreprocessCurrentSlice;
            }

            if (state_ == State::kTryPreprocessCurrentSlice) {
                CHECK_ACCELERATOR_RESULT(PreprocessCurrentSlice());
                state_ = State::kEnsurePicture;
            }

            if (state_ == State::kEnsurePicture) {
                if (curr_pic_) {
                    // |curr_pic_| already exists, so skip to ProcessCurrentSlice().
                    state_ = State::kTryCurrentSlice;
                }
                else {
                    // New picture/finished previous one, try to start a new one
                    // or tell the client we need more surfaces.
                    curr_pic_ = accelerator_->CreateH264Picture();
                    if (!curr_pic_)
                        return kRanOutOfSurfaces;
                  
                    state_ = State::kTryNewFrame;
                }
            }

            if (state_ == State::kTryNewFrame) {
                CHECK_ACCELERATOR_RESULT(StartNewFrame(curr_slice_hdr_.get()));
                state_ = State::kTryCurrentSlice;
            }

            if (state_ != State::kTryCurrentSlice) {
                return kDecodeError;
            }
            CHECK_ACCELERATOR_RESULT(ProcessCurrentSlice());
            curr_slice_hdr_.reset();
            state_ = State::kDecoding;
            break;
        }

        case H264NALU::kSPS: {
            int sps_id;

            CHECK_ACCELERATOR_RESULT(FinishPrevFrameIfPresent());
            par_res = parser_.ParseSPS(&sps_id);
            if (par_res != H264Parser::kOk)
                SET_ERROR_AND_RETURN();

            bool need_new_buffers = false;
            if (!ProcessSPS(sps_id, &need_new_buffers))
                SET_ERROR_AND_RETURN();

            last_sps_nalu_.assign(curr_nalu_->data,
                curr_nalu_->data + curr_nalu_->size);
            if (state_ == State::kNeedStreamMetadata)
                state_ = State::kAfterReset;

            if (need_new_buffers) {
                curr_pic_ = nullptr;
                curr_nalu_ = nullptr;
                ref_pic_list_p0_.clear();
                ref_pic_list_b0_.clear();
                ref_pic_list_b1_.clear();

                return kConfigChange;
            }
            break;
        }

        case H264NALU::kPPS: {
            CHECK_ACCELERATOR_RESULT(FinishPrevFrameIfPresent());
            par_res = parser_.ParsePPS(&last_parsed_pps_id_);
            if (par_res != H264Parser::kOk)
                SET_ERROR_AND_RETURN();

            last_pps_nalu_.assign(curr_nalu_->data,
                curr_nalu_->data + curr_nalu_->size);
            break;
        }

        case H264NALU::kAUD:
        case H264NALU::kEOSeq:
        case H264NALU::kEOStream:
            if (state_ != State::kDecoding)
                break;

            CHECK_ACCELERATOR_RESULT(FinishPrevFrameIfPresent());
            break;

        case H264NALU::kSEIMessage:
            
            if (state_ == State::kAfterReset && !recovery_frame_cnt_ &&
                !recovery_frame_num_) {
                // If we are after reset, we can also resume from a SEI recovery point
                // (spec D.2.8) if one is present. However, if we are already in the
                // process of handling one, skip any subsequent ones until we are done
                // processing.
                H264SEIMessage sei{};
                if (parser_.ParseSEI(&sei) != H264Parser::kOk)
                    SET_ERROR_AND_RETURN();

                if (sei.type == H264SEIMessage::kSEIRecoveryPoint) {
                    recovery_frame_cnt_ = sei.recovery_point.recovery_frame_cnt;
                    if (0 > recovery_frame_cnt_ ||
                        recovery_frame_cnt_ >= max_frame_num_) {
                        std::cout << "Invalid recovery_frame_cnt=" << recovery_frame_cnt_
                            << " (it must be [0, max_frame_num_-1="
                            << max_frame_num_ - 1 << "])" << std::endl;
                        SET_ERROR_AND_RETURN();
                    }
                    std::cout << "Recovery point SEI is found, recovery_frame_cnt_="
                        << recovery_frame_cnt_ << std::endl;
                    break;
                }
            }

        default:
            std::cout << "Skipping NALU type: " << curr_nalu_->nal_unit_type << std::endl;
            break;
        }

        std::cout << "NALU done" << std::endl;
        curr_nalu_.reset();
    }
}

Size H264Decoder::GetPicSize() const {
    return pic_size_;
}

Rect H264Decoder::GetVisibleRect() const {
    return visible_rect_;
}

VideoCodecProfile H264Decoder::GetProfile() const {
    return profile_;
}

uint8_t H264Decoder::GetBitDepth() const {
    return bit_depth_;
}

#define kMaxVideoFrames  4
size_t H264Decoder::GetRequiredNumOfPictures() const {
    constexpr size_t kPicsInPipeline = kMaxVideoFrames + 1;
    return GetNumReferenceFrames() + kPicsInPipeline;
}

size_t H264Decoder::GetNumReferenceFrames() const {
    // Use the maximum number of pictures in the Decoded Picture Buffer.
    return dpb_.max_num_pics();
}

void H264Decoder::SetVideoDecoder(ComD3D11VideoDecoder video_decoder)
{
    accelerator_->SetVideoDecoder(video_decoder);
}

// static
bool H264Decoder::IsNewPrimaryCodedPicture(const H264Picture* curr_pic,
    int curr_pps_id,
    const H264SPS* sps,
    const H264SliceHeader& slice_hdr) {
    if (!curr_pic)
        return true;

    // 7.4.1.2.4, assumes non-interlaced.
    if (slice_hdr.frame_num != curr_pic->frame_num ||
        slice_hdr.pic_parameter_set_id != curr_pps_id ||
        slice_hdr.nal_ref_idc != curr_pic->nal_ref_idc ||
        slice_hdr.idr_pic_flag != curr_pic->idr ||
        (slice_hdr.idr_pic_flag &&
            (slice_hdr.idr_pic_id != curr_pic->idr_pic_id ||
                // If we have two consecutive IDR slices, and the second one has
                // first_mb_in_slice == 0, treat it as a new picture.
                // Per spec, idr_pic_id should not be equal in this case (and we should
                // have hit the condition above instead, see spec 7.4.3 on idr_pic_id),
                // but some encoders neglect changing idr_pic_id for two consecutive
                // IDRs. Work around this by checking if the next slice contains the
                // zeroth macroblock, i.e. data that belongs to the next picture.
                slice_hdr.first_mb_in_slice == 0)))
        return true;

    if (!sps)
        return false;

    if (sps->pic_order_cnt_type == curr_pic->pic_order_cnt_type) {
        if (curr_pic->pic_order_cnt_type == 0) {
            if (slice_hdr.pic_order_cnt_lsb != curr_pic->pic_order_cnt_lsb ||
                slice_hdr.delta_pic_order_cnt_bottom !=
                curr_pic->delta_pic_order_cnt_bottom)
                return true;
        }
        else if (curr_pic->pic_order_cnt_type == 1) {
            if (slice_hdr.delta_pic_order_cnt0 != curr_pic->delta_pic_order_cnt0 ||
                slice_hdr.delta_pic_order_cnt1 != curr_pic->delta_pic_order_cnt1)
                return true;
        }
    }

    return false;
}

// static
bool H264Decoder::FillH264PictureFromSliceHeader(
    const H264SPS* sps,
    const H264SliceHeader& slice_hdr,
    H264Picture* pic) {
    //DCHECK(pic);

    pic->idr = slice_hdr.idr_pic_flag;
    if (pic->idr)
        pic->idr_pic_id = slice_hdr.idr_pic_id;

    if (slice_hdr.field_pic_flag) {
        pic->field = slice_hdr.bottom_field_flag ? H264Picture::FIELD_BOTTOM
            : H264Picture::FIELD_TOP;
    }
    else {
        pic->field = H264Picture::FIELD_NONE;
    }

    if (pic->field != H264Picture::FIELD_NONE) {
        std::cout << "Interlaced video not supported.";
        return false;
    }

    pic->nal_ref_idc = slice_hdr.nal_ref_idc;
    pic->ref = slice_hdr.nal_ref_idc != 0;
    // This assumes non-interlaced stream.
    pic->frame_num = pic->pic_num = slice_hdr.frame_num;

    if (!sps)
        return false;

    pic->pic_order_cnt_type = sps->pic_order_cnt_type;
    switch (pic->pic_order_cnt_type) {
    case 0:
        pic->pic_order_cnt_lsb = slice_hdr.pic_order_cnt_lsb;
        pic->delta_pic_order_cnt_bottom = slice_hdr.delta_pic_order_cnt_bottom;
        break;

    case 1:
        pic->delta_pic_order_cnt0 = slice_hdr.delta_pic_order_cnt0;
        pic->delta_pic_order_cnt1 = slice_hdr.delta_pic_order_cnt1;
        break;

    case 2:
        break;

    default:
        return false;
    }
    return true;
}

bool ParseBitDepth(const H264SPS& sps, uint8_t& bit_depth) {
    // Spec 7.4.2.1.1
    if (sps.bit_depth_luma_minus8 != sps.bit_depth_chroma_minus8) {
        std::cout << "H264Decoder doesn't support different bit depths between luma"
            << "and chroma, bit_depth_luma_minus8="
            << sps.bit_depth_luma_minus8
            << ", bit_depth_chroma_minus8=" << sps.bit_depth_chroma_minus8;
        return false;
    }
    if (sps.bit_depth_luma_minus8 < 0) {
        return false;
    }
    if (sps.bit_depth_luma_minus8 > 6) {
        return false;
    }
    switch (sps.bit_depth_luma_minus8) {
    case 0:
        bit_depth = 8u;
        break;
    case 2:
        bit_depth = 10u;
        break;
    case 4:
        bit_depth = 12u;
        break;
    case 6:
        bit_depth = 14u;
        break;
    default:
        std::cout << "Invalid bit depth: "
            << int(sps.bit_depth_luma_minus8 + 8);
        return false;
    }
    return true;
}

bool IsValidBitDepth(uint8_t bit_depth, VideoCodecProfile profile) {
    // Spec A.2.
    switch (profile) {
    case H264PROFILE_BASELINE:
    case H264PROFILE_MAIN:
    case H264PROFILE_EXTENDED:
    case H264PROFILE_HIGH:
        return bit_depth == 8u;
    case H264PROFILE_HIGH10PROFILE:
    case H264PROFILE_HIGH422PROFILE:
        return bit_depth == 8u || bit_depth == 10u;
    case H264PROFILE_HIGH444PREDICTIVEPROFILE:
        return bit_depth == 8u || bit_depth == 10u || bit_depth == 12u ||
            bit_depth == 14u;
    case H264PROFILE_SCALABLEBASELINE:
    case H264PROFILE_SCALABLEHIGH:
        // Spec G.10.1.
        return bit_depth == 8u;
    case H264PROFILE_STEREOHIGH:
    case H264PROFILE_MULTIVIEWHIGH:
        // Spec H.10.1.1 and H.10.1.2.
        return bit_depth == 8u;
    default:
        return false;
    }
}

bool IsYUV420Sequence(const H264SPS& sps) {
    // Spec 6.2
    return sps.chroma_format_idc == 1;
}


bool H264Decoder::ProcessSPS(int sps_id, bool* need_new_buffers) {
    std::cout << "Processing SPS id:" << sps_id << std::endl;

    const H264SPS* sps = parser_.GetSPS(sps_id);
    if (!sps)
        return false;

    *need_new_buffers = false;

    if (sps->frame_mbs_only_flag == 0) {
        std::cout << "frame_mbs_only_flag != 1 not supported";
        return false;
    }

    Size new_pic_size = sps->GetCodedSize();
    if (new_pic_size.IsEmpty()) {
        std::cout << "Invalid picture size";
        return false;
    }

    int width_mb = new_pic_size.width() / 16;
    int height_mb = new_pic_size.height() / 16;

    // Verify that the values are not too large before multiplying.
    if (INT_MAX / width_mb < height_mb) {
        std::cout << "Picture size is too big: ";
        return false;
    }

    // Spec A.3.1 and A.3.2
    // For Baseline, Constrained Baseline and Main profile, the indicated level is
    // Level 1b if level_idc is equal to 11 and constraint_set3_flag is equal to 1
    uint8_t level = uint8_t(sps->level_idc);
    if ((sps->profile_idc == H264SPS::kProfileIDCBaseline ||
        sps->profile_idc == H264SPS::kProfileIDCConstrainedBaseline ||
        sps->profile_idc == H264SPS::kProfileIDCMain) &&
        level == 11 && sps->constraint_set3_flag) {
        level = 9;  // Level 1b
    }
    int max_dpb_mbs = int(H264LevelToMaxDpbMbs(level));
    if (max_dpb_mbs == 0)
        return false;

    // MaxDpbFrames from level limits per spec.
    size_t max_dpb_frames = (std::min)(max_dpb_mbs / (width_mb * height_mb),
        static_cast<int>(H264DPB::kDPBMaxSize));
    std::cout << "MaxDpbFrames: " << max_dpb_frames
        << ", max_num_ref_frames: " << sps->max_num_ref_frames
        << ", max_dec_frame_buffering: " << sps->max_dec_frame_buffering;

    // Set DPB size to at least the level limit, or what the stream requires.
    size_t max_dpb_size =
        (std::max)(static_cast<int>(max_dpb_frames),
            (std::max)(sps->max_num_ref_frames, sps->max_dec_frame_buffering));
    // Some non-conforming streams specify more frames are needed than the current
    // level limit. Allow this, but only up to the maximum number of reference
    // frames allowed per spec.
    if (max_dpb_size > max_dpb_frames)
        std::cout << "Invalid stream, DPB size > MaxDpbFrames";
    if (max_dpb_size == 0 || max_dpb_size > H264DPB::kDPBMaxSize) {
        std::cout << "Invalid DPB size: " << max_dpb_size;
        return false;
    }
    if (!IsYUV420Sequence(*sps)) {
        std::cout << "Only YUV 4:2:0 is supported";
        return false;
    }

    VideoCodecProfile new_profile =
        H264Parser::ProfileIDCToVideoCodecProfile(sps->profile_idc);
    uint8_t new_bit_depth = 0;
    if (!ParseBitDepth(*sps, new_bit_depth))
        return false;
    if (!IsValidBitDepth(new_bit_depth, new_profile)) {
        std::cout << "Invalid bit depth=" << int(new_bit_depth)
            << ", profile=" << new_profile;
        return false;
    }

    if (pic_size_ != new_pic_size || dpb_.max_num_pics() != max_dpb_size ||
        profile_ != new_profile || bit_depth_ != new_bit_depth) {
        if (!Flush())
            return false;
        std::cout << "Codec profile: " << new_profile
            << ", level: " << level << ", DPB size: " << max_dpb_size
            << ", Picture size: " << new_pic_size.width()
            << ", bit depth: " << int(new_bit_depth);
        *need_new_buffers = true;
        profile_ = new_profile;
        bit_depth_ = new_bit_depth;
        pic_size_ = new_pic_size;
        dpb_.set_max_num_pics(max_dpb_size);
    }

    Rect new_visible_rect = sps->GetVisibleRect();
    if (visible_rect_ != new_visible_rect) {
        std::cout << "New visible rect: ";
        visible_rect_ = new_visible_rect;
    }

    if (!UpdateMaxNumReorderFrames(sps))
        return false;
    std::cout << "max_num_reorder_frames: " << max_num_reorder_frames_ << std::endl;

    return true;
}

H264Decoder::H264Accelerator::Status H264Decoder::PreprocessCurrentSlice() {
    const H264SliceHeader* slice_hdr = curr_slice_hdr_.get();

    if (IsNewPrimaryCodedPicture(curr_pic_, curr_pps_id_,
        parser_.GetSPS(curr_sps_id_), *slice_hdr)) {
        // New picture, so first finish the previous one before processing it.
        H264Accelerator::Status result = FinishPrevFrameIfPresent();
        if (result != H264Accelerator::Status::kOk)
            return result;


        if (slice_hdr->first_mb_in_slice != 0) {
            std::cout << "ASO/invalid stream, first_mb_in_slice: "
                << slice_hdr->first_mb_in_slice;
            return H264Accelerator::Status::kFail;
        }

        // If the new picture is an IDR, flush DPB.
        if (slice_hdr->idr_pic_flag) {
            // Output all remaining pictures, unless we are explicitly instructed
            // not to do so.
            if (!slice_hdr->no_output_of_prior_pics_flag) {
                if (!Flush())
                    return H264Accelerator::Status::kFail;
            }
            dpb_.Clear();
            last_output_poc_ = INT_MIN;
        }
    }

    return H264Accelerator::Status::kOk;
}

H264Decoder::H264Accelerator::Status H264Decoder::ProcessCurrentSlice() {
    const H264SliceHeader* slice_hdr = curr_slice_hdr_.get();

    if (slice_hdr->field_pic_flag == 0)
        max_pic_num_ = max_frame_num_;
    else
        max_pic_num_ = 2 * max_frame_num_;

    H264Picture::Vector ref_pic_list0, ref_pic_list1;
    // If we are using full sample encryption then we do not have the information
    // we need to update the ref pic lists here, but that's OK because the
    // accelerator doesn't actually need to submit them in this case.
    if (!slice_hdr->full_sample_encryption &&
        !ModifyReferencePicLists(slice_hdr, &ref_pic_list0, &ref_pic_list1)) {
        return H264Accelerator::Status::kFail;
    }

    const H264PPS* pps = parser_.GetPPS(curr_pps_id_);
    if (!pps)
        return H264Accelerator::Status::kFail;

    return accelerator_->SubmitSlice(pps, slice_hdr, ref_pic_list0, ref_pic_list1,
        curr_pic_, slice_hdr->nalu_data,
        slice_hdr->nalu_size,
        parser_.GetCurrentSubsamples());
}

bool H264Decoder::InitCurrPicture(const H264SliceHeader* slice_hdr) {
    if (!FillH264PictureFromSliceHeader(parser_.GetSPS(curr_sps_id_), *slice_hdr,
        curr_pic_)) {
        return false;
    }

    if (!CalculatePicOrderCounts(curr_pic_))
        return false;

    curr_pic_->long_term_reference_flag = slice_hdr->long_term_reference_flag;
    curr_pic_->adaptive_ref_pic_marking_mode_flag =
        slice_hdr->adaptive_ref_pic_marking_mode_flag;

    // If the slice header indicates we will have to perform reference marking
    // process after this picture is decoded, store required data for that
    // purpose.
    if (slice_hdr->adaptive_ref_pic_marking_mode_flag) {
        static_assert(sizeof(curr_pic_->ref_pic_marking) ==
            sizeof(slice_hdr->ref_pic_marking),
            "Array sizes of ref pic marking do not match.");
        memcpy(curr_pic_->ref_pic_marking, slice_hdr->ref_pic_marking,
            sizeof(curr_pic_->ref_pic_marking));
    }

    curr_pic_->set_visible_rect(visible_rect_);
    curr_pic_->set_bitstream_id(stream_id_);

    return true;
}

bool H264Decoder::InitNonexistingPicture(H264Picture* pic,
    int frame_num) {
    pic->nonexisting = true;
    pic->nal_ref_idc = 1;
    pic->frame_num = pic->pic_num = frame_num;
    pic->adaptive_ref_pic_marking_mode_flag = false;
    pic->ref = true;
    pic->long_term_reference_flag = false;
    pic->field = H264Picture::FIELD_NONE;

    return CalculatePicOrderCounts(pic);
}

bool H264Decoder::CalculatePicOrderCounts(H264Picture* pic) {
    const H264SPS* sps = parser_.GetSPS(curr_sps_id_);
    if (!sps)
        return false;

    switch (pic->pic_order_cnt_type) {
    case 0: {
        // See spec 8.2.1.1.
        int prev_pic_order_cnt_msb, prev_pic_order_cnt_lsb;

        if (pic->idr) {
            prev_pic_order_cnt_msb = prev_pic_order_cnt_lsb = 0;
        }
        else {
            if (prev_ref_has_memmgmnt5_) {
                if (prev_ref_field_ != H264Picture::FIELD_BOTTOM) {
                    prev_pic_order_cnt_msb = 0;
                    prev_pic_order_cnt_lsb = prev_ref_top_field_order_cnt_;
                }
                else {
                    prev_pic_order_cnt_msb = 0;
                    prev_pic_order_cnt_lsb = 0;
                }
            }
            else {
                prev_pic_order_cnt_msb = prev_ref_pic_order_cnt_msb_;
                prev_pic_order_cnt_lsb = prev_ref_pic_order_cnt_lsb_;
            }
        }

        int max_pic_order_cnt_lsb =
            1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
        if (max_pic_order_cnt_lsb == 0) {
            return false;
        }

        if ((pic->pic_order_cnt_lsb < prev_pic_order_cnt_lsb) &&
            (prev_pic_order_cnt_lsb - pic->pic_order_cnt_lsb >=
                max_pic_order_cnt_lsb / 2)) {
            pic->pic_order_cnt_msb = prev_pic_order_cnt_msb + max_pic_order_cnt_lsb;
        }
        else if ((pic->pic_order_cnt_lsb > prev_pic_order_cnt_lsb) &&
            (pic->pic_order_cnt_lsb - prev_pic_order_cnt_lsb >
                max_pic_order_cnt_lsb / 2)) {
            pic->pic_order_cnt_msb = prev_pic_order_cnt_msb - max_pic_order_cnt_lsb;
        }
        else {
            pic->pic_order_cnt_msb = prev_pic_order_cnt_msb;
        }

        if (pic->field != H264Picture::FIELD_BOTTOM) {
            pic->top_field_order_cnt =
                pic->pic_order_cnt_msb + pic->pic_order_cnt_lsb;
        }

        if (pic->field != H264Picture::FIELD_TOP) {
            if (pic->field == H264Picture::FIELD_NONE) {
                pic->bottom_field_order_cnt =
                    pic->top_field_order_cnt + pic->delta_pic_order_cnt_bottom;
            }
            else {
                pic->bottom_field_order_cnt =
                    pic->pic_order_cnt_msb + pic->pic_order_cnt_lsb;
            }
        }
        break;
    }

    case 1: {
        // See spec 8.2.1.2.
        if (prev_has_memmgmnt5_)
            prev_frame_num_offset_ = 0;

        if (pic->idr)
            pic->frame_num_offset = 0;
        else if (prev_frame_num_ > pic->frame_num)
            pic->frame_num_offset = prev_frame_num_offset_ + max_frame_num_;
        else
            pic->frame_num_offset = prev_frame_num_offset_;

        int abs_frame_num = 0;
        if (sps->num_ref_frames_in_pic_order_cnt_cycle != 0)
            abs_frame_num = pic->frame_num_offset + pic->frame_num;
        else
            abs_frame_num = 0;

        if (pic->nal_ref_idc == 0 && abs_frame_num > 0)
            --abs_frame_num;

        int expected_pic_order_cnt = 0;
        if (abs_frame_num > 0) {
            if (sps->num_ref_frames_in_pic_order_cnt_cycle == 0) {
                std::cout << "Invalid num_ref_frames_in_pic_order_cnt_cycle "
                    << "in stream";
                return false;
            }

            int pic_order_cnt_cycle_cnt =
                (abs_frame_num - 1) / sps->num_ref_frames_in_pic_order_cnt_cycle;
            int frame_num_in_pic_order_cnt_cycle =
                (abs_frame_num - 1) % sps->num_ref_frames_in_pic_order_cnt_cycle;

            expected_pic_order_cnt = pic_order_cnt_cycle_cnt *
                sps->expected_delta_per_pic_order_cnt_cycle;
            // frame_num_in_pic_order_cnt_cycle is verified < 255 in parser
            for (int i = 0; i <= frame_num_in_pic_order_cnt_cycle; ++i)
                expected_pic_order_cnt += sps->offset_for_ref_frame[i];
        }

        if (!pic->nal_ref_idc)
            expected_pic_order_cnt += sps->offset_for_non_ref_pic;

        if (pic->field == H264Picture::FIELD_NONE) {
            pic->top_field_order_cnt =
                expected_pic_order_cnt + pic->delta_pic_order_cnt0;
            pic->bottom_field_order_cnt = pic->top_field_order_cnt +
                sps->offset_for_top_to_bottom_field +
                pic->delta_pic_order_cnt1;
        }
        else if (pic->field != H264Picture::FIELD_BOTTOM) {
            pic->top_field_order_cnt =
                expected_pic_order_cnt + pic->delta_pic_order_cnt0;
        }
        else {
            pic->bottom_field_order_cnt = expected_pic_order_cnt +
                sps->offset_for_top_to_bottom_field +
                pic->delta_pic_order_cnt0;
        }
        break;
    }

    case 2: {
        // See spec 8.2.1.3.
        if (prev_has_memmgmnt5_)
            prev_frame_num_offset_ = 0;

        if (pic->idr)
            pic->frame_num_offset = 0;
        else if (prev_frame_num_ > pic->frame_num)
            pic->frame_num_offset = prev_frame_num_offset_ + max_frame_num_;
        else
            pic->frame_num_offset = prev_frame_num_offset_;

        int temp_pic_order_cnt;
        if (pic->idr) {
            temp_pic_order_cnt = 0;
        }
        else if (!pic->nal_ref_idc) {
            temp_pic_order_cnt = 2 * (pic->frame_num_offset + pic->frame_num) - 1;
        }
        else {
            temp_pic_order_cnt = 2 * (pic->frame_num_offset + pic->frame_num);
        }

        if (pic->field == H264Picture::FIELD_NONE) {
            pic->top_field_order_cnt = temp_pic_order_cnt;
            pic->bottom_field_order_cnt = temp_pic_order_cnt;
        }
        else if (pic->field == H264Picture::FIELD_BOTTOM) {
            pic->bottom_field_order_cnt = temp_pic_order_cnt;
        }
        else {
            pic->top_field_order_cnt = temp_pic_order_cnt;
        }
        break;
    }

    default:
        std::cout << "Invalid pic_order_cnt_type: " << sps->pic_order_cnt_type;
        return false;
    }

    switch (pic->field) {
    case H264Picture::FIELD_NONE:
        pic->pic_order_cnt =
            (std::min)(pic->top_field_order_cnt, pic->bottom_field_order_cnt);
        break;
    case H264Picture::FIELD_TOP:
        pic->pic_order_cnt = pic->top_field_order_cnt;
        break;
    case H264Picture::FIELD_BOTTOM:
        pic->pic_order_cnt = pic->bottom_field_order_cnt;
        break;
    }

    return true;
}

void H264Decoder::UpdatePicNums(int frame_num) {
    for (auto& pic : dpb_) {
        if (!pic->ref)
            continue;

        // 8.2.4.1. Assumes non-interlaced stream.
        if (pic->field != H264Picture::FIELD_NONE) {
            return;
        }
        if (pic->long_term) {
            pic->long_term_pic_num = pic->long_term_frame_idx;
        }
        else {
            if (pic->frame_num > frame_num)
                pic->frame_num_wrap = pic->frame_num - max_frame_num_;
            else
                pic->frame_num_wrap = pic->frame_num;

            pic->pic_num = pic->frame_num_wrap;
        }
    }
}

bool H264Decoder::UpdateMaxNumReorderFrames(const H264SPS* sps) {
    if (sps->vui_parameters_present_flag && sps->bitstream_restriction_flag) {
        max_num_reorder_frames_ =
            size_t(sps->max_num_reorder_frames);
        if (max_num_reorder_frames_ > dpb_.max_num_pics()) {
            std::cout
                << "max_num_reorder_frames present, but larger than MaxDpbFrames ("
                << max_num_reorder_frames_ << " > " << dpb_.max_num_pics() << ")";
            max_num_reorder_frames_ = 0;
            return false;
        }
        return true;
    }

    // max_num_reorder_frames not present, infer from profile/constraints
    // (see VUI semantics in spec).
    if (sps->constraint_set3_flag) {
        switch (sps->profile_idc) {
        case 44:
        case 86:
        case 100:
        case 110:
        case 122:
        case 244:
            max_num_reorder_frames_ = 0;
            break;
        default:
            max_num_reorder_frames_ = dpb_.max_num_pics();
            break;
        }
    }
    else {
        max_num_reorder_frames_ = dpb_.max_num_pics();
    }

    return true;
}

void H264Decoder::PrepareRefPicLists() {
    ConstructReferencePicListsP();
    ConstructReferencePicListsB();
}

bool H264Decoder::ModifyReferencePicLists(const H264SliceHeader* slice_hdr,
    H264Picture::Vector* ref_pic_list0,
    H264Picture::Vector* ref_pic_list1) {
    ref_pic_list0->clear();
    ref_pic_list1->clear();

    // Fill reference picture lists for B and S/SP slices.
    if (slice_hdr->IsPSlice() || slice_hdr->IsSPSlice()) {
        *ref_pic_list0 = ref_pic_list_p0_;
        return ModifyReferencePicList(slice_hdr, 0, ref_pic_list0);
    }
    else if (slice_hdr->IsBSlice()) {
        *ref_pic_list0 = ref_pic_list_b0_;
        *ref_pic_list1 = ref_pic_list_b1_;
        return ModifyReferencePicList(slice_hdr, 0, ref_pic_list0) &&
            ModifyReferencePicList(slice_hdr, 1, ref_pic_list1);
    }

    return true;
}

struct PicNumDescCompare {
    bool operator()(const  H264Picture* a,
        const  H264Picture* b) const {
        return a->pic_num > b->pic_num;
    }
};

struct LongTermPicNumAscCompare {
    bool operator()(const  H264Picture* a,
        const  H264Picture* b) const {
        return a->long_term_pic_num < b->long_term_pic_num;
    }
};

void H264Decoder::ConstructReferencePicListsP() {
    // RefPicList0 (8.2.4.2.1) [[1] [2]], where:
    // [1] shortterm ref pics sorted by descending pic_num,
    // [2] longterm ref pics by ascending long_term_pic_num.
    ref_pic_list_p0_.clear();

    // First get the short ref pics...
    dpb_.GetShortTermRefPicsAppending(&ref_pic_list_p0_);
    size_t num_short_refs = ref_pic_list_p0_.size();

    // and sort them to get [1].
    std::sort(ref_pic_list_p0_.begin(), ref_pic_list_p0_.end(),
        PicNumDescCompare());

    // Now get long term pics and sort them by long_term_pic_num to get [2].
    dpb_.GetLongTermRefPicsAppending(&ref_pic_list_p0_);
    std::sort(ref_pic_list_p0_.begin() + num_short_refs, ref_pic_list_p0_.end(),
        LongTermPicNumAscCompare());
}

struct POCAscCompare {
    bool operator()(const H264Picture* a,
        const H264Picture* b) const {
        return a->pic_order_cnt < b->pic_order_cnt;
    }
};

struct POCDescCompare {
    bool operator()(const H264Picture* a,
        const H264Picture* b) const {
        return a->pic_order_cnt > b->pic_order_cnt;
    }
};

void H264Decoder::ConstructReferencePicListsB() {
    // RefPicList0 (8.2.4.2.3) [[1] [2] [3]], where:
    // [1] shortterm ref pics with POC < curr_pic's POC sorted by descending POC,
    // [2] shortterm ref pics with POC > curr_pic's POC by ascending POC,
    // [3] longterm ref pics by ascending long_term_pic_num.
    ref_pic_list_b0_.clear();
    ref_pic_list_b1_.clear();
    dpb_.GetShortTermRefPicsAppending(&ref_pic_list_b0_);
    size_t num_short_refs = ref_pic_list_b0_.size();

    // First sort ascending, this will put [1] in right place and finish [2].
    std::sort(ref_pic_list_b0_.begin(), ref_pic_list_b0_.end(), POCAscCompare());

    // Find first with POC > curr_pic's POC to get first element in [2]...
    H264Picture::Vector::iterator iter;
    iter = std::upper_bound(ref_pic_list_b0_.begin(), ref_pic_list_b0_.end(),
        curr_pic_, POCAscCompare());

    // and sort [1] descending, thus finishing sequence [1] [2].
    std::sort(ref_pic_list_b0_.begin(), iter, POCDescCompare());

    // Now add [3] and sort by ascending long_term_pic_num.
    dpb_.GetLongTermRefPicsAppending(&ref_pic_list_b0_);
    std::sort(ref_pic_list_b0_.begin() + num_short_refs, ref_pic_list_b0_.end(),
        LongTermPicNumAscCompare());

    // RefPicList1 (8.2.4.2.4) [[1] [2] [3]], where:
    // [1] shortterm ref pics with POC > curr_pic's POC sorted by ascending POC,
    // [2] shortterm ref pics with POC < curr_pic's POC by descending POC,
    // [3] longterm ref pics by ascending long_term_pic_num.

    dpb_.GetShortTermRefPicsAppending(&ref_pic_list_b1_);
    num_short_refs = ref_pic_list_b1_.size();

    // First sort by descending POC.
    std::sort(ref_pic_list_b1_.begin(), ref_pic_list_b1_.end(), POCDescCompare());

    // Find first with POC < curr_pic's POC to get first element in [2]...
    iter = std::upper_bound(ref_pic_list_b1_.begin(), ref_pic_list_b1_.end(),
        curr_pic_, POCDescCompare());

    // and sort [1] ascending.
    std::sort(ref_pic_list_b1_.begin(), iter, POCAscCompare());

    // Now add [3] and sort by ascending long_term_pic_num
    dpb_.GetLongTermRefPicsAppending(&ref_pic_list_b1_);
    std::sort(ref_pic_list_b1_.begin() + num_short_refs, ref_pic_list_b1_.end(),
        LongTermPicNumAscCompare());

    // If lists identical, swap first two entries in RefPicList1 (spec 8.2.4.2.3)
    if (ref_pic_list_b1_.size() > 1 &&
        std::equal(ref_pic_list_b0_.begin(), ref_pic_list_b0_.end(),
            ref_pic_list_b1_.begin()))
        std::swap(ref_pic_list_b1_[0], ref_pic_list_b1_[1]);
}

// See 8.2.4
int H264Decoder::PicNumF(const H264Picture& pic) {
    if (!pic.long_term)
        return pic.pic_num;
    else
        return max_pic_num_;
}

// See 8.2.4
int H264Decoder::LongTermPicNumF(const H264Picture& pic) {
    if (pic.ref && pic.long_term)
        return pic.long_term_pic_num;
    else
        return 2 * (max_long_term_frame_idx_ + 1);
}

// Shift elements on the |v| starting from |from| to |to|, inclusive,
// one position to the right and insert pic at |from|.
static void ShiftRightAndInsert(H264Picture::Vector* v,
    int from,
    int to,
    H264Picture* pic) {
    // Security checks, do not disable in Debug mode.
    //CHECK(from <= to);
    //CHECK(to <= std::numeric_limits<int>::max() - 2);
    //// Additional checks. Debug mode ok.
    //DCHECK(v);
    //DCHECK(pic);
    //DCHECK((to + 1 == static_cast<int>(v->size())) ||
    //    (to + 2 == static_cast<int>(v->size())));

    v->resize(to + 2);

    for (int i = to + 1; i > from; --i)
        (*v)[i] = (*v)[i - 1];

    (*v)[from] = std::move(pic);
}

bool H264Decoder::ModifyReferencePicList(const H264SliceHeader* slice_hdr,
    int list,
    H264Picture::Vector* ref_pic_listx) {
    bool ref_pic_list_modification_flag_lX;
    int num_ref_idx_lX_active_minus1;
    const H264ModificationOfPicNum* list_mod;

    // This can process either ref_pic_list0 or ref_pic_list1, depending on
    // the list argument. Set up pointers to proper list to be processed here.
    if (list == 0) {
        ref_pic_list_modification_flag_lX =
            slice_hdr->ref_pic_list_modification_flag_l0;
        num_ref_idx_lX_active_minus1 = slice_hdr->num_ref_idx_l0_active_minus1;
        list_mod = slice_hdr->ref_list_l0_modifications;
    }
    else {
        ref_pic_list_modification_flag_lX =
            slice_hdr->ref_pic_list_modification_flag_l1;
        num_ref_idx_lX_active_minus1 = slice_hdr->num_ref_idx_l1_active_minus1;
        list_mod = slice_hdr->ref_list_l1_modifications;
    }

    // Resize the list to the size requested in the slice header.
    // Note that per 8.2.4.2 it's possible for num_ref_idx_lX_active_minus1 to
    // indicate there should be more ref pics on list than we constructed.
    // Those superfluous ones should be treated as non-reference and will be
    // initialized to nullptr, which must be handled by clients.
    if (num_ref_idx_lX_active_minus1 < 0) {
        return false;
    }
    ref_pic_listx->resize(num_ref_idx_lX_active_minus1 + 1);

    if (!ref_pic_list_modification_flag_lX)
        return true;

    // Spec 8.2.4.3:
    // Reorder pictures on the list in a way specified in the stream.
    int pic_num_lx_pred = curr_pic_->pic_num;
    int ref_idx_lx = 0;
    int pic_num_lx_no_wrap;
    int pic_num_lx;
    bool done = false;
    H264Picture* pic;
    for (int i = 0; i < H264SliceHeader::kRefListModSize && !done; ++i) {
        switch (list_mod->modification_of_pic_nums_idc) {
        case 0:
        case 1:
            // Modify short reference picture position.
            if (list_mod->modification_of_pic_nums_idc == 0) {
                // Subtract given value from predicted PicNum.
                pic_num_lx_no_wrap =
                    pic_num_lx_pred -
                    (static_cast<int>(list_mod->abs_diff_pic_num_minus1) + 1);
                // Wrap around max_pic_num_ if it becomes < 0 as result
                // of subtraction.
                if (pic_num_lx_no_wrap < 0)
                    pic_num_lx_no_wrap += max_pic_num_;
            }
            else {
                // Add given value to predicted PicNum.
                pic_num_lx_no_wrap =
                    pic_num_lx_pred +
                    (static_cast<int>(list_mod->abs_diff_pic_num_minus1) + 1);
                // Wrap around max_pic_num_ if it becomes >= max_pic_num_ as result
                // of the addition.
                if (pic_num_lx_no_wrap >= max_pic_num_)
                    pic_num_lx_no_wrap -= max_pic_num_;
            }

            // For use in next iteration.
            pic_num_lx_pred = pic_num_lx_no_wrap;

            if (pic_num_lx_no_wrap > curr_pic_->pic_num)
                pic_num_lx = pic_num_lx_no_wrap - max_pic_num_;
            else
                pic_num_lx = pic_num_lx_no_wrap;

            if (num_ref_idx_lX_active_minus1 + 1 >=
                H264SliceHeader::kRefListModSize) {
                return false;
            }
            pic = dpb_.GetShortRefPicByPicNum(pic_num_lx);
            if (!pic) {
                std::cout << "Malformed stream, no pic num " << pic_num_lx;
                return false;
            }

            if (ref_idx_lx > num_ref_idx_lX_active_minus1) {
                std::cout << "Bounds mismatch: expected " << ref_idx_lx
                    << " <= " << num_ref_idx_lX_active_minus1;
                return false;
            }

            ShiftRightAndInsert(ref_pic_listx, ref_idx_lx,
                num_ref_idx_lX_active_minus1, pic);
            ref_idx_lx++;

            for (int src = ref_idx_lx, dst = ref_idx_lx;
                src <= num_ref_idx_lX_active_minus1 + 1; ++src) {
                auto* src_pic = (*ref_pic_listx)[src];
                int src_pic_num_lx = src_pic ? PicNumF(*src_pic) : -1;
                if (src_pic_num_lx != pic_num_lx)
                    (*ref_pic_listx)[dst++] = (*ref_pic_listx)[src];
            }
            break;

        case 2:
            // Modify long term reference picture position.
            if (num_ref_idx_lX_active_minus1 + 1 >=
                H264SliceHeader::kRefListModSize) {
                return false;
            }
            pic = dpb_.GetLongRefPicByLongTermPicNum(list_mod->long_term_pic_num);
            if (!pic) {
                std::cout << "Malformed stream, no pic num "
                    << list_mod->long_term_pic_num;
                return false;
            }
            ShiftRightAndInsert(ref_pic_listx, ref_idx_lx,
                num_ref_idx_lX_active_minus1, pic);
            ref_idx_lx++;

            for (int src = ref_idx_lx, dst = ref_idx_lx;
                src <= num_ref_idx_lX_active_minus1 + 1; ++src) {
                if (LongTermPicNumF(*(*ref_pic_listx)[src]) !=
                    static_cast<int>(list_mod->long_term_pic_num))
                    (*ref_pic_listx)[dst++] = (*ref_pic_listx)[src];
            }
            break;

        case 3:
            // End of modification list.
            done = true;
            break;

        default:
            // May be recoverable.
            std::cout << "Invalid modification_of_pic_nums_idc="
                << list_mod->modification_of_pic_nums_idc << " in position "
                << i;
            break;
        }

        ++list_mod;
    }

    // Per NOTE 2 in 8.2.4.3.2, the ref_pic_listx size in the above loop is
    // temporarily made one element longer than the required final list.
    // Resize the list back to its required size.
    ref_pic_listx->resize(num_ref_idx_lX_active_minus1 + 1);

    return true;
}

bool H264Decoder::HandleMemoryManagementOps(H264Picture* pic) {
    // 8.2.5.4
    for (size_t i = 0; i < size_t(pic->ref_pic_marking); ++i) {
        // Code below does not support interlaced stream (per-field pictures).
        H264DecRefPicMarking* ref_pic_marking = &pic->ref_pic_marking[i];
        H264Picture* to_mark;
        int pic_num_x;

        switch (ref_pic_marking->memory_mgmnt_control_operation) {
        case 0:
            // Normal end of operations' specification.
            return true;

        case 1:
            // Mark a short term reference picture as unused so it can be removed
            // if outputted.
            pic_num_x =
                pic->pic_num - (ref_pic_marking->difference_of_pic_nums_minus1 + 1);
            to_mark = dpb_.GetShortRefPicByPicNum(pic_num_x);
            if (to_mark) {
                to_mark->ref = false;
            }
            else {
                std::cout << "Invalid short ref pic num to unmark";
                return false;
            }
            break;

        case 2:
            // Mark a long term reference picture as unused so it can be removed
            // if outputted.
            to_mark = dpb_.GetLongRefPicByLongTermPicNum(
                ref_pic_marking->long_term_pic_num);
            if (to_mark) {
                to_mark->ref = false;
            }
            else {
                std::cout << "Invalid long term ref pic num to unmark";
                return false;
            }
            break;

        case 3:
            // Mark a short term reference picture as long term reference.
            pic_num_x =
                pic->pic_num - (ref_pic_marking->difference_of_pic_nums_minus1 + 1);
            to_mark = dpb_.GetShortRefPicByPicNum(pic_num_x);
            if (to_mark) {
                if (!(to_mark->ref && !to_mark->long_term)) {
                    return false;
                }
                to_mark->long_term = true;
                to_mark->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
            }
            else {
                std::cout << "Invalid short term ref pic num to mark as long ref";
                return false;
            }
            break;

        case 4: {
            // Unmark all reference pictures with long_term_frame_idx over new max.
            max_long_term_frame_idx_ =
                ref_pic_marking->max_long_term_frame_idx_plus1 - 1;
            H264Picture::Vector long_terms;
            dpb_.GetLongTermRefPicsAppending(&long_terms);
            for (size_t long_term = 0; long_term < long_terms.size(); ++long_term) {
                H264Picture*& long_term_pic = long_terms[long_term];
                if (!(long_term_pic->ref && long_term_pic->long_term)) {
                    return false;
                }
                // Ok to cast, max_long_term_frame_idx is much smaller than 16bit.
                if (long_term_pic->long_term_frame_idx >
                    static_cast<int>(max_long_term_frame_idx_))
                    long_term_pic->ref = false;
            }
            break;
        }

        case 5:
            // Unmark all reference pictures.
            dpb_.MarkAllUnusedForRef();
            max_long_term_frame_idx_ = -1;
            pic->mem_mgmt_5 = true;
            break;

        case 6: {
            // Replace long term reference pictures with current picture.
            // First unmark if any existing with this long_term_frame_idx...
            H264Picture::Vector long_terms;
            dpb_.GetLongTermRefPicsAppending(&long_terms);
            for (size_t long_term = 0; long_term < long_terms.size(); ++long_term) {
                H264Picture*& long_term_pic = long_terms[long_term];
                if (!(long_term_pic->ref && long_term_pic->long_term)) {
                    return false;
                }
                // Ok to cast, long_term_frame_idx is much smaller than 16bit.
                if (long_term_pic->long_term_frame_idx ==
                    static_cast<int>(ref_pic_marking->long_term_frame_idx))
                    long_term_pic->ref = false;
            }

            // and mark the current one instead.
            pic->ref = true;
            pic->long_term = true;
            pic->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
            break;
        }

        default:
            break;
            // Would indicate a bug in parser.
        }
    }

    return true;
}

// This method ensures that DPB does not overflow, either by removing
// reference pictures as specified in the stream, or using a sliding window
// procedure to remove the oldest one.
// It also performs marking and unmarking pictures as reference.
// See spac 8.2.5.1.
bool H264Decoder::ReferencePictureMarking(H264Picture* pic) {
    // If the current picture is an IDR, all reference pictures are unmarked.
    if (pic->idr) {
        dpb_.MarkAllUnusedForRef();

        if (pic->long_term_reference_flag) {
            pic->long_term = true;
            pic->long_term_frame_idx = 0;
            max_long_term_frame_idx_ = 0;
        }
        else {
            pic->long_term = false;
            max_long_term_frame_idx_ = -1;
        }

        return true;
    }

    // Not an IDR. If the stream contains instructions on how to discard pictures
    // from DPB and how to mark/unmark existing reference pictures, do so.
    // Otherwise, fall back to default sliding window process.
    if (pic->adaptive_ref_pic_marking_mode_flag) {
        if (pic->nonexisting) {
            return false;
        }
        return HandleMemoryManagementOps(pic);
    }
    else {
        return SlidingWindowPictureMarking();
    }
}

bool H264Decoder::SlidingWindowPictureMarking() {
    const H264SPS* sps = parser_.GetSPS(curr_sps_id_);
    if (!sps)
        return false;

    // 8.2.5.3. Ensure the DPB doesn't overflow by discarding the oldest picture.
    int num_ref_pics = dpb_.CountRefPics();
    if (num_ref_pics > std::max<int>(sps->max_num_ref_frames, 1)) {
        return false;
    }
    if (num_ref_pics == std::max<int>(sps->max_num_ref_frames, 1)) {
        // Max number of reference pics reached, need to remove one of the short
        // term ones. Find smallest frame_num_wrap short reference picture and mark
        // it as unused.
        H264Picture* to_unmark =
            dpb_.GetLowestFrameNumWrapShortRefPic();
        if (!to_unmark) {
            std::cout << "Couldn't find a short ref picture to unmark";
            return false;
        }

        to_unmark->ref = false;
    }

    return true;
}

bool H264Decoder::HandleFrameNumGap(int frame_num) {
    const H264SPS* sps = parser_.GetSPS(curr_sps_id_);
    if (!sps)
        return false;

    if (!sps->gaps_in_frame_num_value_allowed_flag) {
        std::cout << "Invalid frame_num: " << frame_num;
        // TODO(b:129119729, b:146914440): Youtube android app sometimes sends an
        // invalid frame number after a seek. The sequence goes like:
        // Seek, SPS, PPS, IDR-frame, non-IDR, ... non-IDR with invalid number.
        // The only way to work around this reliably is to ignore this error.
        // Video playback is not affected, no artefacts are visible.
        // return false;
    }

    std::cout << "Handling frame_num gap: " << prev_ref_frame_num_ << "->"
        << frame_num;

    // 7.4.3/7-23
    int unused_short_term_frame_num = (prev_ref_frame_num_ + 1) % max_frame_num_;
    while (unused_short_term_frame_num != frame_num) {
        H264Picture* pic = new H264Picture();
        if (!InitNonexistingPicture(pic, unused_short_term_frame_num))
            return false;

        UpdatePicNums(unused_short_term_frame_num);

        if (!FinishPicture(pic))
            return false;

        unused_short_term_frame_num++;
        unused_short_term_frame_num %= max_frame_num_;
    }

    return true;
}

H264Decoder::H264Accelerator::Status H264Decoder::StartNewFrame(
    const H264SliceHeader* slice_hdr) {
    // TODO posciak: add handling of max_num_ref_frames per spec.


    curr_pps_id_ = slice_hdr->pic_parameter_set_id;
    const H264PPS* pps = parser_.GetPPS(curr_pps_id_);
    if (!pps)
        return H264Accelerator::Status::kFail;

    curr_sps_id_ = pps->seq_parameter_set_id;
    const H264SPS* sps = parser_.GetSPS(curr_sps_id_);
    if (!sps)
        return H264Accelerator::Status::kFail;

    max_frame_num_ = 1 << (sps->log2_max_frame_num_minus4 + 4);
    int frame_num = slice_hdr->frame_num;
    if (slice_hdr->idr_pic_flag)
        prev_ref_frame_num_ = 0;

    // 7.4.3
    if (frame_num != prev_ref_frame_num_ &&
        frame_num != (prev_ref_frame_num_ + 1) % max_frame_num_) {
        if (!HandleFrameNumGap(frame_num))
            return H264Accelerator::Status::kFail;
    }

    if (!InitCurrPicture(slice_hdr))
        return H264Accelerator::Status::kFail;

    UpdatePicNums(frame_num);
    PrepareRefPicLists();

    return accelerator_->SubmitFrameMetadata(sps, pps, dpb_, ref_pic_list_p0_,
        ref_pic_list_b0_, ref_pic_list_b1_,
        curr_pic_);
}

H264Decoder::H264Accelerator::Status H264Decoder::FinishPrevFrameIfPresent() {
    // If we already have a frame waiting to be decoded, decode it and finish.
    if (curr_pic_) {
        H264Accelerator::Status result = DecodePicture();
        if (result != H264Accelerator::Status::kOk)
            return result;

        H264Picture* pic = curr_pic_;
        curr_pic_ = nullptr;
        if (!FinishPicture(pic))
            return H264Accelerator::Status::kFail;
    }

    return H264Accelerator::Status::kOk;
}

bool H264Decoder::FinishPicture(H264Picture* pic) {
    // Finish processing the picture.
    // Start by storing previous picture data for later use.
    if (pic->ref) {
        ReferencePictureMarking(pic);
        prev_ref_has_memmgmnt5_ = pic->mem_mgmt_5;
        prev_ref_top_field_order_cnt_ = pic->top_field_order_cnt;
        prev_ref_pic_order_cnt_msb_ = pic->pic_order_cnt_msb;
        prev_ref_pic_order_cnt_lsb_ = pic->pic_order_cnt_lsb;
        prev_ref_field_ = pic->field;
        prev_ref_frame_num_ = pic->frame_num;
    }
    prev_frame_num_ = pic->frame_num;
    prev_has_memmgmnt5_ = pic->mem_mgmt_5;
    prev_frame_num_offset_ = pic->frame_num_offset;

    // Remove unused (for reference or later output) pictures from DPB, marking
    // them as such.
    dpb_.DeleteUnused();

    /*std::cout << "Finishing picture frame_num: " << pic->frame_num
        << ", entries in DPB: " << dpb_.size() << std::endl;*/

    if (dpb_.size() == 4) {
        //std::cout << "test" << std::endl;
    }
    if (recovery_frame_cnt_) {
        // This is the first picture after the recovery point SEI message. Computes
        // the frame_num of the frame that should be output from (Spec D.2.8).
        recovery_frame_num_ =
            (recovery_frame_cnt_ + pic->frame_num) % max_frame_num_;
        std::cout << "recovery_frame_num_" << recovery_frame_num_;
        recovery_frame_cnt_ = 0;
    }

    // The ownership of pic will either be transferred to DPB - if the picture is
    // still needed (for output and/or reference) - or we will release it
    // immediately if we manage to output it here and won't have to store it for
    // future reference.

    // Get all pictures that haven't been outputted yet.
    H264Picture::Vector not_outputted;
    dpb_.GetNotOutputtedPicsAppending(&not_outputted);
    // Include the one we've just decoded.
    not_outputted.push_back(pic);

    // Sort in output order.
    std::sort(not_outputted.begin(), not_outputted.end(), POCAscCompare());

    // Try to output as many pictures as we can. A picture can be output,
    // if the number of decoded and not yet outputted pictures that would remain
    // in DPB afterwards would at least be equal to max_num_reorder_frames.
    // If the outputted picture is not a reference picture, it doesn't have
    // to remain in the DPB and can be removed.
    auto output_candidate = not_outputted.begin();
    size_t num_remaining = not_outputted.size();
    while (num_remaining > max_num_reorder_frames_ ||
        // If the condition below is used, this is an invalid stream. We should
        // not be forced to output beyond max_num_reorder_frames in order to
        // make room in DPB to store the current picture (if we need to do so).
        // However, if this happens, ignore max_num_reorder_frames and try
        // to output more. This may cause out-of-order output, but is not
        // fatal, and better than failing instead.
        ((dpb_.IsFull() && (!pic->outputted || pic->ref)) && num_remaining)) {
        if (num_remaining <= max_num_reorder_frames_)
           std::cout << "Invalid stream: max_num_reorder_frames not preserved";

        if (!recovery_frame_num_ ||
            // If we are decoding ahead to reach a SEI recovery point, skip
            // outputting all pictures before it, to avoid outputting corrupted
            // frames.
            (*output_candidate)->frame_num == recovery_frame_num_) {
            recovery_frame_num_ = 0;
            if (!OutputPic(*output_candidate))
                return false;
        }

        if (!(*output_candidate)->ref) {
            // Current picture hasn't been inserted into DPB yet, so don't remove it
            // if we managed to output it immediately.
            int outputted_poc = (*output_candidate)->pic_order_cnt;
            if (outputted_poc != pic->pic_order_cnt)
                dpb_.DeleteByPOC(outputted_poc);
        }

        ++output_candidate;
        --num_remaining;
    }

    // If we haven't managed to output the picture that we just decoded, or if
    // it's a reference picture, we have to store it in DPB.
    if (!pic->outputted || pic->ref) {
        if (dpb_.IsFull()) {
            // If we haven't managed to output anything to free up space in DPB
            // to store this picture, it's an error in the stream.
            std::cout << "Could not free up space in DPB!";
            return false;
        }

        dpb_.StorePic(std::move(pic));
    }

    if (pic->outputted) {
        delete pic;
    }

    return true;
}

void H264Decoder::ClearDPB() {
    // Clear DPB contents, marking the pictures as unused first.
    dpb_.Clear();
    last_output_poc_ = INT_MIN;
}

H264Decoder::H264Accelerator::Status H264Decoder::DecodePicture() {

    return accelerator_->SubmitDecode(curr_pic_);
}

bool H264Decoder::OutputPic(H264Picture* pic) {
    //DCHECK(!pic->outputted);
    pic->outputted = true;

    VideoColorSpace colorspace_for_frame = container_color_space_;
    const H264SPS* sps = parser_.GetSPS(curr_sps_id_);
    if (sps && sps->GetColorSpace().IsSpecified())
        colorspace_for_frame = sps->GetColorSpace();
    pic->set_colorspace(colorspace_for_frame);

    if (pic->nonexisting) {
        std::cout << "Skipping output, non-existing frame_num: " << pic->frame_num;
        return true;
    }

    if (pic->pic_order_cnt < last_output_poc_)
        std::cout << "Outputting out of order, likely a broken stream: " << last_output_poc_
        << " -> " << pic->pic_order_cnt;
    last_output_poc_ = pic->pic_order_cnt;

    //std::cout << "Posting output task for POC: " << pic->pic_order_cnt << std::endl;
    return accelerator_->OutputPicture(pic);
}

bool H264Decoder::OutputAllRemainingPics() {
    // Output all pictures that are waiting to be outputted.
    if (FinishPrevFrameIfPresent() != H264Accelerator::Status::kOk)
        return false;
    H264Picture::Vector to_output;
    dpb_.GetNotOutputtedPicsAppending(&to_output);
    // Sort them by ascending POC to output in order.
    std::sort(to_output.begin(), to_output.end(), POCAscCompare());

    for (auto& pic : to_output) {
        if (!OutputPic(pic))
            return false;
    }
    return true;
}