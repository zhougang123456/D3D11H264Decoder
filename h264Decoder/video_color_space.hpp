#ifndef VIDEO_COLOR_SPACE_HPP_
#define VIDEO_COLOR_SPACE_HPP_
#include <stdint.h>
class ColorSpace {
public:
    enum class PrimaryID : uint8_t {
        INVALID,
        BT709,
        BT470M,
        BT470BG,
        SMPTE170M,
        SMPTE240M,
        FILM,
        BT2020,
        SMPTEST428_1,
        SMPTEST431_2,
        SMPTEST432_1,
        XYZ_D50,
        ADOBE_RGB,
        // Corresponds the the primaries of the "Generic RGB" profile used in the
        // Apple ColorSync application, used by layout tests on Mac.
        APPLE_GENERIC_RGB,
        // A very wide gamut space with rotated primaries. Used by layout tests.
        WIDE_GAMUT_COLOR_SPIN,
        // Primaries defined by the primary matrix |custom_primary_matrix_|.
        CUSTOM,
        kMaxValue = CUSTOM,
    };

    enum class TransferID : uint8_t {
        INVALID,
        BT709,
        // On macOS, BT709 hardware decoded video frames, when displayed as
        // overlays, will have a transfer function of gamma=1.961.
        BT709_APPLE,
        GAMMA18,
        GAMMA22,
        GAMMA24,
        GAMMA28,
        SMPTE170M,
        SMPTE240M,
        LINEAR,
        LOG,
        LOG_SQRT,
        IEC61966_2_4,
        BT1361_ECG,
        IEC61966_2_1,
        BT2020_10,
        BT2020_12,
        SMPTEST2084,
        SMPTEST428_1,
        ARIB_STD_B67,  // AKA hybrid-log gamma, HLG.
        // The same as IEC61966_2_1 on the interval [0, 1], with the nonlinear
        // segment continuing beyond 1 and point symmetry defining values below 0.
        IEC61966_2_1_HDR,
        // The same as LINEAR but is defined for all real values.
        LINEAR_HDR,
        // A parametric transfer function defined by |transfer_params_|.
        CUSTOM,
        // An HDR parametric transfer function defined by |transfer_params_|.
        CUSTOM_HDR,
        // An HDR transfer function that is piecewise sRGB, and piecewise linear.
        PIECEWISE_HDR,
        kMaxValue = PIECEWISE_HDR,
    };

    enum class MatrixID : uint8_t {
        INVALID,
        RGB,
        BT709,
        FCC,
        BT470BG,
        SMPTE170M,
        SMPTE240M,
        YCOCG,
        BT2020_NCL,
        BT2020_CL,
        YDZDX,
        GBR,
        kMaxValue = GBR,
    };

    enum class RangeID : uint8_t {
        INVALID,
        // Limited Rec. 709 color range with RGB values ranging from 16 to 235.
        LIMITED,
        // Full RGB color range with RGB valees from 0 to 255.
        FULL,
        // Range is defined by TransferID/MatrixID.
        DERIVED,
        kMaxValue = DERIVED,
    };

    ColorSpace() {}
    ColorSpace(PrimaryID primaries, TransferID transfer)
        : ColorSpace(primaries, transfer, MatrixID::RGB, RangeID::FULL) {}
    ColorSpace(PrimaryID primaries,
        TransferID transfer,
        MatrixID matrix,
        RangeID range)
        : primaries_(primaries),
        transfer_(transfer),
        matrix_(matrix),
        range_(range) {}
private:
    PrimaryID primaries_ = PrimaryID::INVALID;
    TransferID transfer_ = TransferID::INVALID;
    MatrixID matrix_ = MatrixID::INVALID;
    RangeID range_ = RangeID::INVALID;
};

class VideoColorSpace {
public:
    // These values are persisted to logs. Entries should not be renumbered or
    // removed and numeric values should never be reused.
    // Please keep in sync with "VideoColorSpace.PrimaryID"
    // in src/tools/metrics/histograms/enums.xml.
    // Table 2
    enum class PrimaryID : uint8_t {
        INVALID = 0,
        BT709 = 1,
        UNSPECIFIED = 2,
        BT470M = 4,
        BT470BG = 5,
        SMPTE170M = 6,
        SMPTE240M = 7,
        FILM = 8,
        BT2020 = 9,
        SMPTEST428_1 = 10,
        SMPTEST431_2 = 11,
        SMPTEST432_1 = 12,
        EBU_3213_E = 22,
        kMaxValue = EBU_3213_E,
    };

    // These values are persisted to logs. Entries should not be renumbered or
    // removed and numeric values should never be reused.
    // Please keep in sync with "VideoColorSpace.TransferID"
    // in src/tools/metrics/histograms/enums.xml.
    // Table 3
    enum class TransferID : uint8_t {
        INVALID = 0,
        BT709 = 1,
        UNSPECIFIED = 2,
        GAMMA22 = 4,
        GAMMA28 = 5,
        SMPTE170M = 6,
        SMPTE240M = 7,
        LINEAR = 8,
        LOG = 9,
        LOG_SQRT = 10,
        IEC61966_2_4 = 11,
        BT1361_ECG = 12,
        IEC61966_2_1 = 13,
        BT2020_10 = 14,
        BT2020_12 = 15,
        SMPTEST2084 = 16,
        SMPTEST428_1 = 17,

        // Not yet standardized
        ARIB_STD_B67 = 18,  // AKA hybrid-log gamma, HLG.

        kMaxValue = ARIB_STD_B67,
    };

    // Table 4
    enum class MatrixID : uint8_t {
        RGB = 0,
        BT709 = 1,
        UNSPECIFIED = 2,
        FCC = 4,
        BT470BG = 5,
        SMPTE170M = 6,
        SMPTE240M = 7,
        YCOCG = 8,
        BT2020_NCL = 9,
        BT2020_CL = 10,
        YDZDX = 11,
        INVALID = 255,
        kMaxValue = INVALID,
    };

    VideoColorSpace();
    VideoColorSpace(int primaries,
        int transfer,
        int matrix,
        ColorSpace::RangeID range);
    VideoColorSpace(PrimaryID primaries,
        TransferID transfer,
        MatrixID matrix,
        ColorSpace::RangeID range);

    // Returns true if any of the fields have a value other
    // than INVALID or UNSPECIFIED.
    bool IsSpecified() const;

    // These will return INVALID if the number you give it
    // is not a valid enum value.
    static PrimaryID GetPrimaryID(int primary);
    static TransferID GetTransferID(int transfer);
    static MatrixID GetMatrixID(int matrix);

    static VideoColorSpace REC709();
    static VideoColorSpace REC601();
    static VideoColorSpace JPEG();
    static VideoColorSpace REC709_235();

    ColorSpace ToGfxColorSpace() const;

    // Note, these are public variables.
    PrimaryID primaries = PrimaryID::INVALID;
    TransferID transfer = TransferID::INVALID;
    MatrixID matrix = MatrixID::INVALID;
    ColorSpace::RangeID range = ColorSpace::RangeID::INVALID;
};

#endif // VIDEO_COLOR_SPACE_HPP_
