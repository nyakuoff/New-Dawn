#!/usr/bin/env python3
"""Generate private companion materials from TextureV1 resources in a local o2r.

No game assets are bundled. See README.md for the binary/channel contract.
"""

import argparse
from array import array
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import time
import zipfile
import zlib

try:
    import numpy as np
except ImportError:
    np = None

VERSION = "1.0.0"
TEXTURE_TYPE = 0x4F544558
EXPANDED_TYPES = {1, 2, 5, 6, 7, 8, 9}
DEFAULTS = {"height_scale": 0.025, "normal_strength": 1.0,
            "invert": False, "roughness_range": [0.65, 0.95]}


@dataclass
class Texture:
    width: int
    height: int
    kind: int
    flags: int
    h_byte_scale: float
    v_pixel_scale: float
    pixels: bytes


def decode_texture(data):
    """ResourceLoader's 64-byte header + ResourceFactoryBinaryTextureV1 body."""
    if len(data) < 92 or data[0] not in (0, 1):
        raise ValueError("truncated TextureV1 or unsupported byte order")
    order = "<" if data[0] == 0 else ">"
    resource_type, version = struct.unpack_from(order + "II", data, 4)
    if (resource_type, version) != (TEXTURE_TYPE, 1):
        raise ValueError(f"expected TextureV1, got type={resource_type:#x}, version={version}")
    kind, width, height, flags, hs, vs, size = struct.unpack_from(order + "IIIIffI", data, 64)
    # Fast::Texture stores dimensions in uint16_t, despite uint32_t wire fields.
    if not (0 < width <= 65535 and 0 < height <= 65535):
        raise ValueError(f"invalid texture dimensions {width}x{height}")
    if not all(math.isfinite(v) and v > 0 for v in (hs, vs)):
        raise ValueError("invalid TextureV1 byte/pixel scales")
    if len(data) != 92 + size:
        raise ValueError(f"TextureV1 payload size mismatch: declared {size}, actual {len(data) - 92}")
    return Texture(width, height, kind, flags, hs, vs, data[92:])


def encode_texture(tex):
    header = bytearray(64)
    header[1] = 1  # custom resource
    struct.pack_into("<IIQ", header, 4, TEXTURE_TYPE, 1, 0xDEADBEEFDEADBEEF)
    body = struct.pack("<IIIIffI", tex.kind, tex.width, tex.height, tex.flags,
                       tex.h_byte_scale, tex.v_pixel_scale, len(tex.pixels))
    return bytes(header) + body + tex.pixels


def albedo_rgba(tex):
    # ImportTextureRaw consumes expanded RGBA8 regardless of the original N64
    # Type. Do not reinterpret a raw RGBA16bpp-labelled replacement as RGB5551.
    expanded = tex.flags in (1, 2) and tex.kind in EXPANDED_TYPES
    native_rgba = tex.flags == 0 and tex.kind == 1
    if not (expanded or native_rgba) or len(tex.pixels) != tex.width * tex.height * 4:
        raise ValueError(f"unsupported albedo layout: type={tex.kind}, flags={tex.flags}, "
                         f"{len(tex.pixels)} bytes for {tex.width}x{tex.height}; "
                         "expected expanded RGBA8 (LOAD_AS_RAW/IMG) or native RGBA32")
    return tex.pixels


def half_rgba(pixels, width, height):
    if width % 2 or height % 2:
        raise ValueError("half resolution requires even source dimensions")
    if np is not None:
        a = np.frombuffer(pixels, dtype=np.uint8).reshape(height // 2, 2, width // 2, 2, 4)
        return np.rint(a.mean(axis=(1, 3))).astype(np.uint8).tobytes()
    out = bytearray(width * height)
    for y in range(height // 2):
        for x in range(width // 2):
            i = (y * 2 * width + x * 2) * 4
            for c in range(4):
                out[(y * (width // 2) + x) * 4 + c] = round(
                    (pixels[i+c] + pixels[i+4+c] + pixels[i+width*4+c] + pixels[i+width*4+4+c]) / 4)
    return bytes(out)


def luminance(pixels, width, height):
    """Linear Rec.709 luminance; companions themselves are linear data."""
    lut = [(v / 255 / 12.92 if v <= 10 else ((v / 255 + 0.055) / 1.055) ** 2.4)
           for v in range(256)]
    if np is not None:
        rgb = np.frombuffer(pixels, dtype=np.uint8).reshape(height, width, 4)
        linear = np.asarray(lut, dtype=np.float32)
        return sum(linear[rgb[:, :, c]] * weight
                   for c, weight in enumerate((0.2126, 0.7152, 0.0722)))
    return array("f", (sum(lut[pixels[i+c]] * weight
                           for c, weight in enumerate((0.2126, 0.7152, 0.0722)))
                       for i in range(0, len(pixels), 4)))


def box_blur(values, width, height, radius):
    """Separable O(pixel count) periodic box filter, including tiny textures."""
    if np is not None:
        a = values
        for axis, length in ((1, width), (0, height)):
            r = min(radius, (length - 1) // 2)
            if r == 0:
                continue
            pad = [(0, 0), (0, 0)]
            pad[axis] = (r, r + 1)
            sums = np.cumsum(np.pad(a, pad, mode="wrap"), axis=axis, dtype=np.float64)
            pad[axis] = (1, 0)
            sums = np.pad(sums, pad, mode="constant")
            a = ((np.take(sums, range(2*r+1, 2*r+1+length), axis=axis) -
                  np.take(sums, range(length), axis=axis)) / (2*r+1)).astype(np.float32)
        return a
    a = values
    for axis, length, lines in ((1, width, height), (0, height, width)):
        r = min(radius, (length - 1) // 2)
        out = array("f", [0]) * (width * height)
        stride = 1 if axis == 1 else width
        for line in range(lines):
            base = line * width if axis == 1 else line
            total = sum(a[base + (k % length) * stride] for k in range(-r, r+1))
            for p in range(length):
                out[base + p*stride] = total / (2*r+1)
                total += (a[base + ((p+r+1) % length)*stride] -
                          a[base + ((p-r) % length)*stride])
        a = out
    return a


def pixel_fingerprint(pixels):
    """FNV-1a of decoded RGBA8; the renderer checks this off its render thread."""
    value = 14695981039346656037
    for byte in pixels:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def normal_map(heightmap, width, height, scale):
    """+X right, +Y up (image rows down), +Z out; height scale is in UV units."""
    if np is not None:
        dx = np.roll(heightmap, -1, 1) - np.roll(heightmap, 1, 1)
        dy = np.roll(heightmap, -1, 0) - np.roll(heightmap, 1, 0)
        dx = (np.roll(dx, 1, 0) + 2*dx + np.roll(dx, -1, 0)) * (width * scale / 8)
        dy = (np.roll(dy, 1, 1) + 2*dy + np.roll(dy, -1, 1)) * (height * scale / 8)
        length = np.sqrt(1 + dx*dx + dy*dy)
        return np.rint(np.clip(np.stack((-dx/length, dy/length), axis=-1)*0.5+0.5,
                               0, 1)*255).astype(np.uint8).tobytes()
    out = bytearray(width * height * 2)
    for y in range(height):
        for x in range(width):
            dx = dy = 0.0
            for k, weight in ((-1, 1), (0, 2), (1, 1)):
                dx += weight * (heightmap[((y+k) % height)*width+(x+1) % width] -
                                heightmap[((y+k) % height)*width+(x-1) % width])
                dy += weight * (heightmap[((y+1) % height)*width+(x+k) % width] -
                                heightmap[((y-1) % height)*width+(x+k) % width])
            dx *= width * scale / 8
            dy *= height * scale / 8
            length = math.sqrt(1+dx*dx+dy*dy)
            i = (y*width+x)*2
            out[i] = round((-dx/length*0.5+0.5)*255)
            out[i+1] = round((dy/length*0.5+0.5)*255)
    return bytes(out)


def generate(pixels, width, height, recipe):
    lum = luminance(pixels, width, height)
    unit = min(width, height)
    radii = [max(1, round(unit * f)) for f in (1/512, 1/128, 1/32)]
    softened = [box_blur(lum, width, height, r) for r in radii]
    broad = box_blur(lum, width, height, max(1, round(unit/8)))
    sign = -1 if recipe["invert"] else 1
    lo, hi = recipe["roughness_range"]
    mean = softened[1]
    squared = lum*lum if np is not None else array("f", (v*v for v in lum))
    mean_squared = box_blur(squared, width, height, radii[1])
    if np is not None:
        h = np.clip(0.5 + sign*(0.6*softened[0]+0.3*softened[1]+0.1*softened[2]-broad), 0, 1)
        rough = lo + (hi-lo)*np.clip(np.sqrt(np.maximum(0, mean_squared-mean*mean))*4, 0, 1)
        rh = np.rint(np.stack((rough, h), axis=-1)*255).astype(np.uint8).tobytes()
    else:
        h = array("f", (min(1, max(0, 0.5 + sign*(0.6*a+0.3*b+0.1*c-d)))
                        for a, b, c, d in zip(*softened, broad)))
        rh = bytearray(width*height*2)
        for i, (m, m2, hv) in enumerate(zip(mean, mean_squared, h)):
            rough = lo + (hi-lo)*min(1, math.sqrt(max(0, m2-m*m))*4)
            rh[2*i] = round(rough*255)
            rh[2*i+1] = round(hv*255)
        rh = bytes(rh)
    return normal_map(h, width, height, recipe["height_scale"] * recipe["normal_strength"]), rh


def write_png(path, width, height, rgb):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag+data))
    scanlines = b"".join(b"\0" + rgb[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def preview(path, pixels, normal, rh, width, height):
    factor = min(1, 512 / max(width, height))
    pw, ph = max(1, round(width*factor)), max(1, round(height*factor))
    out = bytearray(pw*4*ph*3)
    for y in range(ph):
        sy = min(height-1, int((y+0.5)*height/ph))
        for x in range(pw):
            sx = min(width-1, int((x+0.5)*width/pw))
            i = sy*width+sx
            nx, ny = normal[i*2]/255*2-1, normal[i*2+1]/255*2-1
            nz = math.sqrt(max(0, 1-nx*nx-ny*ny))
            panels = (pixels[i*4:i*4+3],
                      bytes((normal[i*2], normal[i*2+1], round((nz*0.5+0.5)*255))),
                      bytes([rh[i*2]])*3, bytes([rh[i*2+1]])*3)
            for panel, rgb in enumerate(panels):
                j = (y*pw*4+panel*pw+x)*3
                out[j:j+3] = rgb
    write_png(path, pw*4, ph, out)


def load_manifest(path):
    with path.open("rb") as f:
        if path.suffix == ".toml":
            import tomllib
            manifest = tomllib.load(f)
        else:
            manifest = json.load(f)
    if not isinstance(manifest, dict) or manifest.get("version") != 1:
        raise ValueError("manifest version must be 1")
    defaults = dict(DEFAULTS)
    if not isinstance(manifest.get("defaults", {}), dict):
        raise ValueError("manifest defaults must be an object")
    defaults.update(manifest.get("defaults", {}))
    materials = manifest.get("materials")
    if not isinstance(materials, list) or not materials:
        raise ValueError("manifest needs a nonempty materials list")
    seen = set()
    for item in materials:
        if not isinstance(item, dict):
            raise ValueError("each material must be an object")
        resource = item["resource"]
        if (not isinstance(resource, str) or resource.startswith(("alt/", "/")) or
                "\\" in resource or any(p in ("", ".", "..") for p in resource.split("/"))):
            raise ValueError("resource must be a canonical relative path without alt/")
        if resource in seen:
            raise ValueError(f"duplicate resource: {resource}")
        seen.add(resource)
        overrides = item.get("overrides", {})
        if not isinstance(overrides, dict):
            raise ValueError(f"overrides must be an object for {resource}")
        if set(defaults) - set(DEFAULTS) or set(overrides) - set(DEFAULTS):
            raise ValueError(f"unknown recipe key for {resource}")
        recipe = defaults | overrides
        for key in ("height_scale", "normal_strength"):
            v = recipe[key]
            if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0:
                raise ValueError(f"{key} must be finite and nonnegative")
        bounds = recipe["roughness_range"]
        if (not isinstance(bounds, list) or len(bounds) != 2 or
                not all(isinstance(v, (int, float)) and not isinstance(v, bool) and
                        math.isfinite(v) for v in bounds) or not 0 <= bounds[0] <= bounds[1] <= 1):
            raise ValueError("roughness_range must be [minimum, maximum] within [0,1]")
        if not isinstance(recipe["invert"], bool):
            raise ValueError("invert must be boolean")
        item["recipe"] = recipe
    return materials


def self_check():
    global np
    tex = Texture(2, 2, 9, 0, 1.0, 1.0, bytes(range(8)))
    assert decode_texture(encode_texture(tex)) == tex
    # Same record in big-endian, as ResourceLoader supports both byte orders.
    big = bytearray(encode_texture(tex))
    big[0] = 1
    struct.pack_into(">IIQ", big, 4, TEXTURE_TYPE, 1, 0xDEADBEEFDEADBEEF)
    struct.pack_into(">IIIIffI", big, 64, 9, 2, 2, 0, 1, 1, 8)
    assert decode_texture(big) == tex
    try:
        decode_texture(encode_texture(tex)[:-1])
    except ValueError:
        pass
    else:
        raise AssertionError("truncated payload accepted")
    assert pixel_fingerprint(b"") == "cbf29ce484222325"
    assert pixel_fingerprint(b"hello") == "a430d84680aabd0b"
    # Exercise both implementations, checking signs, wrapping, bounds and agreement.
    original_np = np
    results = []
    try:
        for backend in ([None, original_np] if original_np is not None else [None]):
            np = backend
            bump = array("f", (math.exp(-((x-4)**2+(y-4)**2)/4) for y in range(9) for x in range(9)))
            h = np.asarray(bump).reshape(9, 9) if np is not None else bump
            n = normal_map(h, 9, 9, 0.1)
            assert n[(4*9+2)*2] < 128 and n[(4*9+6)*2] > 128
            assert n[(2*9+4)*2+1] > 128 and n[(6*9+4)*2+1] < 128
            edge = array("f", [1]+[0]*80)
            edge = np.asarray(edge).reshape(9, 9) if np is not None else edge
            blur = box_blur(edge, 9, 9, 1)
            flat = blur.ravel() if np is not None else blur
            assert abs(flat[80]-1/9) < 1e-6
            flat_h = np.zeros((9, 9)) if np is not None else array("f", [0])*81
            assert normal_map(flat_h, 9, 9, 1) == bytes([128, 128])*81
            pixels = bytes(v for y in range(10) for x in range(12)
                           for v in ((x*17+y*5) % 256, x*11, y*23, 255))
            reduced = half_rgba(pixels, 12, 10)
            n, rh = generate(reduced, 6, 5, DEFAULTS)
            assert len(n) == len(rh) == 60
            assert min(rh[::2]) >= round(DEFAULTS["roughness_range"][0]*255)
            assert max(rh[::2]) <= round(DEFAULTS["roughness_range"][1]*255)
            flat_n, inverted = generate(reduced, 6, 5, DEFAULTS | {"normal_strength": 0, "invert": True})
            assert flat_n == bytes([128, 128])*30
            assert all(abs(a+b-255) <= 1 for a, b in zip(rh[1::2], inverted[1::2]))
            assert rh[::2] == inverted[::2]
            results.append((n, rh))
        if len(results) == 2:
            assert all(abs(a-b) <= 1 for left, right in zip(*results) for a, b in zip(left, right))
    finally:
        np = original_np
    print("self-check: TextureV1 LE/BE round-trip, bump signs, wrap, flat map, roughness, overrides, backend agreement passed")


def run(args):
    if np is None:
        print("NumPy unavailable; using slow stdlib path (no packages installed).", file=sys.stderr)
    if args.self_check:
        self_check()
        return
    if args.pack is None:
        raise ValueError("--pack is required unless using --self-check")
    materials = load_manifest(args.manifest)
    # Keep derived assets private and out of source control. Output is always new.
    if args.out is None:
        raise ValueError("--out DIR is required unless using --self-check")
    root = args.out.resolve()
    repo = Path(__file__).resolve().parents[2]
    if root == repo or repo in root.parents:
        raise ValueError("--out must be outside this repository; derived textures must stay private")
    output = root / "newdawn-materials.o2r"
    root.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise ValueError(f"output already exists; choose a new --out directory: {output}")
    preview_dir = root / "preview"
    if preview_dir.exists():
        raise ValueError(f"preview directory already exists: {preview_dir}")
    started = time.monotonic()
    metadata = {"generator": "newdawn-materials", "version": VERSION,
                "source_archive": args.pack.name, "resolution_divisor": args.resolution,
                "channels": {"_n": ["normal_x", "normal_y"], "_r": ["roughness", "height"]},
                "normal_convention": "+X right, +Y up, +Z out; image rows down; decode RG/255*2-1",
                "storage": "TextureV1 type=9 (IA16 container), flags=0, tightly packed linear RG8",
                "height_recipe": "linear luminance; periodic boxes 1/512,1/128,1/32 of short axis; "
                                 "weights .6,.3,.1; subtract 1/8-axis box, add .5, clamp; invert about .5",
                "roughness_recipe": "lo+(hi-lo)*clamp(4*sqrt(local variance),0,1); radius short axis/128",
                "materials": []}
    # Stage privately, publishing only after every selected resource succeeds.
    with tempfile.TemporaryDirectory(prefix=".generate-", dir=root) as staging:
        stage = Path(staging)
        (stage / "preview").mkdir()
        with zipfile.ZipFile(args.pack) as source, zipfile.ZipFile(
                stage / "companion.o2r", "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as dest:
            names = source.namelist()
            for index, item in enumerate(materials):
                tick = time.monotonic()
                resource = item["resource"]
                entry = "alt/" + resource
                if names.count(entry) != 1:
                    raise ValueError(f"expected exactly one source entry: {entry}")
                raw = source.read(entry)
                try:
                    tex = decode_texture(raw)
                    pixels = albedo_rgba(tex)
                except ValueError as exc:
                    raise ValueError(f"{entry}: {exc}") from exc
                pixel_hash = pixel_fingerprint(pixels)
                w, h = tex.width, tex.height
                if args.resolution == 2:
                    pixels = half_rgba(pixels, w, h)
                    w, h = w//2, h//2
                n, rh = generate(pixels, w, h, item["recipe"])
                for suffix, data in (("_n", n), ("_r", rh)):
                    dest.writestr(entry+suffix, encode_texture(Texture(w, h, 9, 0, 1, 1, data)))
                name = f"{index+1:02}-{resource.rsplit('/', 1)[-1]}.png"
                preview(stage / "preview" / name, pixels, n, rh, w, h)
                metadata["materials"].append({
                    "resource": resource, "label": item.get("label", resource),
                    "source_sha256": hashlib.sha256(raw).hexdigest(),
                    "source_pixels_fnv1a64": pixel_hash,
                    "source_hash_scope": "entire uncompressed TextureV1 entry, including header",
                    "source_size": [tex.width, tex.height], "output_size": [w, h],
                    "source_type": tex.kind, "source_flags": tex.flags,
                    "source_h_byte_scale": tex.h_byte_scale, "source_v_pixel_scale": tex.v_pixel_scale,
                    "crop_pixels": [0, 0, tex.width, tex.height],
                    "uv_scale": [1, 1], "uv_offset": [0, 0],
                    "recipe": item["recipe"], "preview": name})
                print(f"{index+1}/{len(materials)} {resource}: {tex.width}x{tex.height} -> "
                      f"{w}x{h}, {time.monotonic()-tick:.2f}s", flush=True)
            dest.writestr("newdawn-materials.json", json.dumps(metadata, indent=2)+"\n")
        # Exclusive creation also protects against another concurrent generator.
        with output.open("xb") as out, (stage / "companion.o2r").open("rb") as src:
            import shutil
            shutil.copyfileobj(src, out)
        preview_dir.parent.mkdir(exist_ok=True)
        (stage / "preview").rename(preview_dir)
    print(f"wrote {output} ({output.stat().st_size / 1048576:.2f} MiB) in {time.monotonic()-started:.2f}s")
    print(f"previews: {preview_dir} (albedo | normal | roughness | height)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, help="local ZIP-backed texture pack .o2r")
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("materials.json"))
    parser.add_argument("--out", type=Path, help="private output directory outside the repository (required)")
    parser.add_argument("--resolution", type=int, choices=(1, 2), default=2, help="axis divisor; default half axes")
    parser.add_argument("--stdlib", action="store_true", help="force slow fallback, also for validation")
    parser.add_argument("--self-check", action="store_true", help="synthetic checks; no pack or outputs needed")
    args = parser.parse_args()
    global np
    if args.stdlib:
        np = None
    try:
        run(args)
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile, ImportError, OverflowError) as exc:
        parser.exit(1, f"materials: {exc}\n")


if __name__ == "__main__":
    main()
