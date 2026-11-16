// 4x texture upscaler for the generated 3DS character textures: a CPU port of the PBRify SPAN V4 network (PBRify_Remix,
// CC0-1.0; weights in assets/newdawn/mm3d/span_v4_4x.bin, see tools/newdawn/mm3d/export_span.py). No third-party runtime:
// 3x3 and 1x1 convolutions, SiLU, sigmoid and a pixel shuffle, verified against ONNX Runtime (docs/MM3D_CHARACTERS.md).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Mm3dFormats.h"

namespace NewDawnMm3d {

class SpanUpscaler {
  public:
    // Reads the weight file; false when it is missing or malformed.
    bool Load(const std::string& path);
    // FNV-1a of the weight file, for the cache signature.
    uint64_t Fingerprint() const {
        return mFingerprint;
    }
    // RGBA8 in, RGBA8 out at 4x. Edge pixels are replicated (character textures are atlases, not tiles); alpha is
    // resized bilinearly. Thread safe after Load.
    bool Upscale4x(const Mm3d::Image& in, Mm3d::Image& out) const;

  private:
    struct Conv {
        int cin = 0, cout = 0, k = 0;
        std::vector<float> w; // [ky*k+kx][cin][cout]
        std::vector<float> b;
    };
    std::vector<Conv> mConvs; // conv_1, 18 block convs, conv_2, conv_cat, upsampler
    uint64_t mFingerprint = 0;
};

} // namespace NewDawnMm3d
