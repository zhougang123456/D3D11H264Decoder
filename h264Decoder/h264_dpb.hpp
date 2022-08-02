#ifndef H264_DPB_HPP_
#define H264_DPB_HPP_
#include <stddef.h>
#include <vector>

#include "codec_picture.hpp"
#include "h264_parser.hpp"

class D3D11H264Picture;
class H264Picture : public CodecPicture
{
public:
	using Vector = std::vector<H264Picture*>;
	enum Field
	{
		FIELD_NONE,
		FIELD_TOP,
		FIELD_BOTTOM,
	};

	H264Picture();

	virtual D3D11H264Picture* AsD3D11H264Picture();

	int pic_order_cnt_type;
	int top_field_order_cnt;
	int bottom_field_order_cnt;
	int pic_order_cnt;
	int pic_order_cnt_msb;
	int pic_order_cnt_lsb;
	int delta_pic_order_cnt_bottom;
	int delta_pic_order_cnt0;
	int delta_pic_order_cnt1;

	int pic_num;
	int long_term_pic_num;
	int frame_num;
	int frame_num_offset;
	int frame_num_wrap;
	int long_term_frame_idx;

	H264SliceHeader::Type type;
	int nal_ref_idc;
	bool idr;
	int idr_pic_id;
	bool ref;
	int ref_pic_list_modification_flag_l0;
	int abs_diff_pic_num_minus1;
	bool long_term;
	bool outputted;
	bool mem_mgmt_5;
	bool nonexisting;

	Field field;

	bool long_term_reference_flag;
	bool adaptive_ref_pic_marking_mode_flag;
	H264DecRefPicMarking ref_pic_marking[H264SliceHeader::kRefListSize];

	int dpb_position;

	~H264Picture();

private:
	
};

class H264DPB {
public:
	H264DPB();
	~H264DPB();

	void set_max_num_pics(size_t max_num_pics);
	size_t max_num_pics() const { return max_num_pics_;  }
	void DeleteUnused();
	void DeleteByPOC(int poc);
	void Clear();
	void StorePic(H264Picture* pic);
	int CountRefPics();
	void MarkAllUnusedForRef();
	H264Picture* GetShortRefPicByPicNum(int pic_num);
	H264Picture* GetLongRefPicByLongTermPicNum(int pic_num);
	H264Picture* GetLowestFrameNumWrapShortRefPic();
	void GetNotOutputtedPicsAppending(H264Picture::Vector* out);
	void GetShortTermRefPicsAppending(H264Picture::Vector* out);
	void GetLongTermRefPicsAppending(H264Picture::Vector* out);
	H264Picture::Vector::iterator begin() { return pics_.begin(); }
	H264Picture::Vector::iterator end() { return pics_.end(); }
	H264Picture::Vector::const_iterator begin() const { return pics_.begin(); }
	H264Picture::Vector::const_iterator end() const { return pics_.end(); }
	H264Picture::Vector::const_reverse_iterator rbegin() const {
		return pics_.rbegin();
	}
	H264Picture::Vector::const_reverse_iterator rend() const {
		return pics_.rend();
	}
	size_t size() const { return pics_.size(); }
	bool IsFull() const { return pics_.size() == max_num_pics_; }
	enum {
		kDPBMaxSize = 16,
	};

private:
	void UpdatePicPositions();
	H264Picture::Vector pics_;
	size_t max_num_pics_;
};

#endif // H264_DPB_HPP_
