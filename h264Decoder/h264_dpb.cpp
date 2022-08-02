#include <string.h>
#include <algorithm>

#include "h264_dpb.hpp"

H264Picture::H264Picture()
    : pic_order_cnt_type(0),
    top_field_order_cnt(0),
    bottom_field_order_cnt(0),
    pic_order_cnt(0),
    pic_order_cnt_msb(0),
    pic_order_cnt_lsb(0),
    delta_pic_order_cnt_bottom(0),
    delta_pic_order_cnt0(0),
    delta_pic_order_cnt1(0),
    pic_num(0),
    long_term_pic_num(0),
    frame_num(0),
    frame_num_offset(0),
    frame_num_wrap(0),
    long_term_frame_idx(0),
    type(H264SliceHeader::kPSlice),
    nal_ref_idc(0),
    idr(false),
    idr_pic_id(0),
    ref(false),
    ref_pic_list_modification_flag_l0(0),
    abs_diff_pic_num_minus1(0),
    long_term(false),
    outputted(false),
    mem_mgmt_5(false),
    nonexisting(false),
    field(FIELD_NONE),
    long_term_reference_flag(false),
    adaptive_ref_pic_marking_mode_flag(false),
    dpb_position(0) {
    memset(&ref_pic_marking, 0, sizeof(ref_pic_marking));
}

H264Picture::~H264Picture() = default;

D3D11H264Picture* H264Picture::AsD3D11H264Picture() {
    return nullptr;
}

H264DPB::H264DPB() : max_num_pics_(0) {}
H264DPB::~H264DPB() = default;

void H264DPB::Clear() {
    for (auto it = pics_.begin(); it != pics_.end(); ++it) {
        delete (*it);
    }
    pics_.clear();
}

void H264DPB::set_max_num_pics(size_t max_num_pics) {
    if (max_num_pics > static_cast<size_t>(kDPBMaxSize))
    {
        return;
    }
    max_num_pics_ = max_num_pics;
    if (pics_.size() > max_num_pics_)
        pics_.resize(max_num_pics_);
}

void H264DPB::UpdatePicPositions() {
    size_t i = 0;
    for (auto& pic : pics_) {
        pic->dpb_position = i;
        ++i;
    }
}

void H264DPB::DeleteByPOC(int poc) {
    for (auto it = pics_.begin(); it != pics_.end(); ++it) {
        if ((*it)->pic_order_cnt == poc) {
            delete (*it);
            pics_.erase(it);
            UpdatePicPositions();
            //std::cout << "Delete pics size: " << pics_.size() << std::endl;
            return;
        }
    }
}

void H264DPB::DeleteUnused() {
    for (auto it = pics_.begin(); it != pics_.end();) {
        if ((*it)->outputted && !(*it)->ref) {
            delete (*it);
            it = pics_.erase(it);
        }
        else {
            ++it;
        }
    }
    //std::cout << "Delete pics size: " << pics_.size() << std::endl;
    UpdatePicPositions();
}

void H264DPB::StorePic(H264Picture* pic) {
    if (pics_.size() > max_num_pics_) {
        std::cout << "pics size over size: " << pics_.size() << std::endl;
        return;
    }

    pic->dpb_position = pics_.size();
    pics_.push_back(std::move(pic));
    //std::cout << "StorePic pics size: " << pics_.size() << std::endl;
}

int H264DPB::CountRefPics() {
    int ret = 0;
    for (size_t i = 0; i < pics_.size(); ++i) {
        if (pics_[i]->ref)
            ++ret;
    }
    return ret;
}

void H264DPB::MarkAllUnusedForRef() {
    for (size_t i = 0; i < pics_.size(); ++i)
        pics_[i]->ref = false;
}

H264Picture* H264DPB::GetShortRefPicByPicNum(int pic_num) {
    for (const auto& pic : pics_) {
        if (pic->ref && !pic->long_term && pic->pic_num == pic_num)
            return pic;
    }

    return nullptr;
}

H264Picture* H264DPB::GetLongRefPicByLongTermPicNum(int pic_num) {
    for (const auto& pic : pics_) {
        if (pic->ref && pic->long_term && pic->long_term_pic_num == pic_num)
            return pic;
    }

    return nullptr;
}

H264Picture* H264DPB::GetLowestFrameNumWrapShortRefPic() {
    H264Picture* ret = nullptr;
    for (const auto& pic : pics_) {
        if (pic->ref && !pic->long_term &&
            (!ret || pic->frame_num_wrap < ret->frame_num_wrap))
            ret = pic;
    }
    return ret;
}

void H264DPB::GetNotOutputtedPicsAppending(H264Picture::Vector* out) {
    for (const auto& pic : pics_) {
        if (!pic->outputted)
            out->push_back(pic);
    }
}

void H264DPB::GetShortTermRefPicsAppending(H264Picture::Vector* out) {
    for (const auto& pic : pics_) {
        if (pic->ref && !pic->long_term) {
            out->push_back(pic);
            
        }
    }
}

void H264DPB::GetLongTermRefPicsAppending(H264Picture::Vector* out) {
    for (const auto& pic : pics_) {
        if (pic->ref && pic->long_term)
            out->push_back(pic);
    }
}