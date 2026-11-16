#!/usr/bin/env python3
"""Export the PBRify SPAN V4 4x weights (CC0) from an ONNX file to mm/assets/newdawn/mm3d/span_v4_4x.bin.

The in-game C++ upscaler (mm/2s2h/NewDawn/SpanUpscale.cpp) reads this file. Dev time only (needs the `onnx` package).
Source model: PBRify_Remix, 4x-PBRify_UpscalerSPANV4.pth, CC0-1.0 (https://github.com/Kim2091/PBRify_Remix), exported to
ONNX with torch/pnnx. Layout: "SPAN", u32 version 1, then float32 little endian tensors in the order of ORDER, each
weight as [out][in][ky][kx] followed by its bias.

  export_span.py span.onnx [out.bin]
"""
import struct
import sys
from pathlib import Path

import numpy as np
import onnx
from onnx import numpy_helper

ORDER = (['conv_1.eval_conv'] + [f'block_{b}.c{c}_r.eval_conv' for b in range(1, 7) for c in (1, 2, 3)] +
         ['conv_2.eval_conv', 'conv_cat', 'upsampler.0'])
OUT = Path(__file__).resolve().parents[3] / 'mm/assets/newdawn/mm3d/span_v4_4x.bin'


def main():
    model = onnx.load(sys.argv[1])
    w = {i.name: numpy_helper.to_array(i).astype('<f4') for i in model.graph.initializer}
    out = bytearray(b'SPAN' + struct.pack('<I', 1))
    for name in ORDER:
        out += w[name + '.weight'].tobytes() + w[name + '.bias'].tobytes()
    Path(sys.argv[2] if len(sys.argv) > 2 else OUT).write_bytes(out)
    print(len(out), 'bytes,', len(ORDER), 'layers')


if __name__ == '__main__':
    main()
