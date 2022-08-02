#include <stdio.h>
#include "d3d11_video_decoder.hpp"
int main() {
    std::unique_ptr<VideoDecoder> video_decoder = D3D11VideoDecoder::Create();
    VideoColorSpace color_space = VideoColorSpace::REC709();
    Size code_size(600, 480);
    video_decoder->Initialize(color_space, code_size);
    const char* filename = "oceans.h264";
    FILE* fd = fopen(filename, "r");
    fseek(fd, 0, SEEK_END);
    int size = ftell(fd);
    printf("stream size is %d\n", size);
    uint8_t* stream = (uint8_t*)malloc(sizeof(uint8_t) * size);
    fseek(fd, 0, SEEK_SET);
    fread(stream, 1, size, fd);
    video_decoder->Decode(stream, size);
    return 0;
}
//#include "h264_parser.hpp"
//enum class State {
//    // After initialization, need an SPS.
//    kNeedStreamMetadata,
//    // Ready to decode from any point.
//    kDecoding,
//    // After Reset(), need a resume point.
//    kAfterReset,
//    // The following keep track of what step is next in Decode() processing
//    // in order to resume properly after H264Decoder::kTryAgain (or another
//    // retryable error) is returned. The next time Decode() is called the call
//    // that previously failed will be retried and execution continues from
//    // there (if possible).
//    kParseSliceHeader,
//    kTryPreprocessCurrentSlice,
//    kEnsurePicture,
//    kTryNewFrame,
//    kTryCurrentSlice,
//    // Error in decode, can't continue.
//    kError,
//};
//int main() {
//    H264Parser* h264_parser = new H264Parser();
//    const char* filename = "oceans.h264";
//    FILE* fd = fopen(filename, "r");
//    fseek(fd, 0, SEEK_END);
//    int size = ftell(fd);
//    printf("stream size is %d\n", size);
//    uint8_t* stream = (uint8_t*)malloc(sizeof(uint8_t) * size);
//    fseek(fd, 0, SEEK_SET);
//    fread(stream, 1, size, fd);
//
//    h264_parser->SetStream(stream, size);
//    
//    State state_ = State::kNeedStreamMetadata;
//    std::unique_ptr<H264NALU> curr_nalu_;
//    std::unique_ptr<H264SliceHeader> curr_slice_hdr_;
//    int last_parsed_pps_id_;
//
//    while (1) {
//        H264Parser::Result par_res;
//
//        if (!curr_nalu_) {
//            curr_nalu_ = std::make_unique<H264NALU>();
//            par_res = h264_parser->AdvanceToNextNALU(curr_nalu_.get());
//            if (par_res == H264Parser::kEOStream) {
//                //CHECK_ACCELERATOR_RESULT(FinishPrevFrameIfPresent());
//                //return kRanOutOfStreamData;
//                printf("kRanOutOfStreamData\n");
//                return 0;
//            }
//            else if (par_res != H264Parser::kOk) {
//                //SET_ERROR_AND_RETURN();
//                printf("ERROR\n");
//                return 0;
//            }
//
//            printf("New NALU: %d\n", static_cast<int>(curr_nalu_->nal_unit_type));
//        }
//
//        switch (curr_nalu_->nal_unit_type) {
//        case H264NALU::kNonIDRSlice:
//           
//        case H264NALU::kIDRSlice: {
//            // TODO(posciak): the IDR may require an SPS that we don't have
//            // available. For now we'd fail if that happens, but ideally we'd like
//            // to keep going until the next SPS in the stream.
//            if (state_ == State::kNeedStreamMetadata) {
//                // We need an SPS, skip this IDR and keep looking.
//                break;
//            }
//
//            // If after reset or waiting for a key, we should be able to recover
//            // from an IDR. |state_|, |curr_slice_hdr_|, and |curr_pic_| are used
//            // to keep track of what has previously been attempted, so that after
//            // a retryable result is returned, subsequent calls to Decode() retry
//            // the call that failed previously. If it succeeds (it may not if no
//            // additional key has been provided, for example), then the remaining
//            // steps will be executed.
//            if (!curr_slice_hdr_) {
//                curr_slice_hdr_ = std::make_unique<H264SliceHeader>();
//                state_ = State::kParseSliceHeader;
//            }
//
//            if (state_ == State::kParseSliceHeader) {
//                // Check if the slice header is encrypted.
//                bool parsed_header = false;
//                
//                if (!parsed_header) {
//                    par_res =
//                        h264_parser->ParseSliceHeader(*curr_nalu_, curr_slice_hdr_.get());
//                    if (par_res != H264Parser::kOk) {
//
//                    }
//                }
//                state_ = State::kTryPreprocessCurrentSlice;
//            }
//
//            if (state_ == State::kTryPreprocessCurrentSlice) {
//                state_ = State::kEnsurePicture;
//            }
//
//            if (state_ == State::kEnsurePicture) {
//                if (1) {
//                    // |curr_pic_| already exists, so skip to ProcessCurrentSlice().
//                    state_ = State::kTryCurrentSlice;
//                }
//                else {
//                    // New picture/finished previous one, try to start a new one
//                    // or tell the client we need more surfaces.
//                
//                    state_ = State::kTryNewFrame;
//                }
//            }
//
//            if (state_ == State::kTryNewFrame) {
//                state_ = State::kTryCurrentSlice;
//            }
//
//            curr_slice_hdr_.reset();
//            state_ = State::kDecoding;
//            break;
//        }
//
//        case H264NALU::kSPS: {
//            int sps_id;
//
//            par_res = h264_parser->ParseSPS(&sps_id);
//            if (par_res != H264Parser::kOk) {
//            }
//
//            bool need_new_buffers = false;
//            /*if (!ProcessSPS(sps_id, &need_new_buffers))
//
//            last_sps_nalu_.assign(curr_nalu_->data,
//                curr_nalu_->data + curr_nalu_->size);
//            if (state_ == State::kNeedStreamMetadata)*/
//                state_ = State::kAfterReset;
//
//            if (need_new_buffers) {
//               
//            }
//            break;
//        }
//
//        case H264NALU::kPPS: {
//            par_res = h264_parser->ParsePPS(&last_parsed_pps_id_);
//            if (par_res != H264Parser::kOk) {
//            }
//
//           /* last_pps_nalu_.assign(curr_nalu_->data,
//                curr_nalu_->data + curr_nalu_->size);*/
//            break;
//        }
//
//        case H264NALU::kAUD:
//        case H264NALU::kEOSeq:
//        case H264NALU::kEOStream:
//            if (state_ != State::kDecoding)
//                break;
//
//            break;
//
//        case H264NALU::kSEIMessage:
//            
//            
//
//        default:
//            printf("Skipping NALU type: %d\n ", curr_nalu_->nal_unit_type);
//            break;
//        }
//
//        curr_nalu_.reset();
//    }
//    return 0;
//}