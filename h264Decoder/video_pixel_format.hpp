#ifndef VIDEO_PIXEL_FORMAT_HPP_
#define VIDEO_PIXEL_FORMAT_HPP_

// Pixel formats roughly based on FOURCC labels, see:
// http://www.fourcc.org/rgb.php and http://www.fourcc.org/yuv.php
// Logged to UMA, so never reuse values. Leave gaps if necessary.
// Ordered as planar, semi-planar, YUV-packed, and RGB formats.
// When a VideoFrame is backed by native textures, VideoPixelFormat describes
// how those textures should be sampled and combined to produce the final
// pixels.
enum VideoPixelFormat {
    PIXEL_FORMAT_UNKNOWN = 0,  // Unknown or unspecified format value.
    PIXEL_FORMAT_I420 =
    1,  // 12bpp YUV planar 1x1 Y, 2x2 UV samples, a.k.a. YU12.

    // Note: Chrome does not actually support YVU compositing, so you probably
    // don't actually want to use this. See http://crbug.com/784627.
    PIXEL_FORMAT_YV12 = 2,  // 12bpp YVU planar 1x1 Y, 2x2 VU samples.
    
    PIXEL_FORMAT_I422 = 3,   // 16bpp YUV planar 1x1 Y, 2x1 UV samples.
    PIXEL_FORMAT_I420A = 4,  // 20bpp YUVA planar 1x1 Y, 2x2 UV, 1x1 A samples.
    PIXEL_FORMAT_I444 = 5,   // 24bpp YUV planar, no subsampling.
    PIXEL_FORMAT_NV12 =
    6,  // 12bpp with Y plane followed by a 2x2 interleaved UV plane.
    PIXEL_FORMAT_NV21 =
    7,  // 12bpp with Y plane followed by a 2x2 interleaved VU plane.
    PIXEL_FORMAT_UYVY =
    8,  // 16bpp interleaved 2x1 U, 1x1 Y, 2x1 V, 1x1 Y samples.
    PIXEL_FORMAT_YUY2 =
    9,  // 16bpp interleaved 1x1 Y, 2x1 U, 1x1 Y, 2x1 V samples.
    PIXEL_FORMAT_ARGB = 10,   // 32bpp BGRA (byte-order), 1 plane.
    PIXEL_FORMAT_XRGB = 11,   // 24bpp BGRX (byte-order), 1 plane.
    PIXEL_FORMAT_RGB24 = 12,  // 24bpp BGR (byte-order), 1 plane.
    
    /* PIXEL_FORMAT_RGB32 = 13,  Deprecated */
    PIXEL_FORMAT_MJPEG = 14,  // MJPEG compressed.
    /* PIXEL_FORMAT_MT21 = 15,  Deprecated */
    
    // The P* in the formats below designates the number of bits per pixel
    // component. I.e. P9 is 9-bits per pixel component, P10 is 10-bits per pixel
    // component, etc.
    PIXEL_FORMAT_YUV420P9 = 16,
    PIXEL_FORMAT_YUV420P10 = 17,
    PIXEL_FORMAT_YUV422P9 = 18,
    PIXEL_FORMAT_YUV422P10 = 19,
    PIXEL_FORMAT_YUV444P9 = 20,
    PIXEL_FORMAT_YUV444P10 = 21,
    PIXEL_FORMAT_YUV420P12 = 22,
    PIXEL_FORMAT_YUV422P12 = 23,
    PIXEL_FORMAT_YUV444P12 = 24,
    
    /* PIXEL_FORMAT_Y8 = 25, Deprecated */
    PIXEL_FORMAT_Y16 = 26,  // single 16bpp plane.
    
    PIXEL_FORMAT_ABGR = 27,  // 32bpp RGBA (byte-order), 1 plane.
    PIXEL_FORMAT_XBGR = 28,  // 24bpp RGBX (byte-order), 1 plane.
    
    PIXEL_FORMAT_P016LE = 29,  // 24bpp NV12, 16 bits per channel
    
    PIXEL_FORMAT_XR30 =
    30,  // 32bpp BGRX, 10 bits per channel, 2 bits ignored, 1 plane
    PIXEL_FORMAT_XB30 =
    31,  // 32bpp RGBX, 10 bits per channel, 2 bits ignored, 1 plane
    
    PIXEL_FORMAT_BGRA = 32,  // 32bpp ARGB (byte-order), 1 plane.
    
    PIXEL_FORMAT_RGBAF16 = 33,  // Half float RGBA, 1 plane.
    
    // Please update UMA histogram enumeration when adding new formats here.
    PIXEL_FORMAT_MAX =
    PIXEL_FORMAT_RGBAF16,  // Must always be equal to largest entry logged.
};

#endif