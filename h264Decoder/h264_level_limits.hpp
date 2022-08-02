#ifndef H264_LEVEL_LIMITS_HPP_
#define H264_LEVEL_LIMITS_HPP_
#include <stddef.h>
#include <stdint.h>
#include "video_codecs.hpp"
uint32_t H264LevelToMaxMBPS(uint8_t level);
uint32_t H264LevelToMaxFS(uint8_t level);
uint32_t H264LevelToMaxDpbMbs(uint8_t level);
uint32_t H264ProfileLevelToMaxBR(VideoCodecProfile profile, uint8_t level);
bool CheckH264LevelLimits(VideoCodecProfile profile,
                          uint8_t level,
                          uint32_t bitrate,
                          uint32_t framerate,
                          uint32_t framesize_in_mbs);
uint8_t FindValidH264Level(VideoCodecProfile profile,
                           uint32_t bitrate,
                           uint32_t framerate,
                           uint32_t framesize_in_mbs);
#endif // H264_LEVEL_LIMITS_HPP_
