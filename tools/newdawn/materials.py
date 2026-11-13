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
import os
from concurrent.futures import ProcessPoolExecutor
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

VERSION = "1.1.0"
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


def validate_recipe(recipe):
    if set(recipe) != set(DEFAULTS):
        raise ValueError("unknown or missing recipe keys")
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


def read_manifest(path):
    with path.open("rb") as f:
        if path.suffix == ".toml":
            import tomllib
            manifest = tomllib.load(f)
        else:
            manifest = json.load(f)
    return manifest


def load_manifest(path):
    manifest = read_manifest(path)
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
        recipe = defaults | manifest.get("classes", {}).get(item.get("class"), {}) | overrides
        validate_recipe(recipe)
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
    # Incremental provenance: unchanged content reuses, recipe/source edits miss.
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        pack = root/"synthetic.o2r"
        cache = root/"cache"
        cache.mkdir()
        item = {"resource": "scenes/test/Tex", "recipe": dict(DEFAULTS)}
        def write_source(value):
            tex = Texture(8, 8, 1, 1, 1, 1, bytes([value, 100, 120, 255])*64)
            with zipfile.ZipFile(pack, "w") as z:
                z.writestr("alt/"+item["resource"], encode_texture(tex))
        write_source(100)
        task = (str(pack), item, 2, str(cache), np is None)
        first = generate_one(task)
        assert not first[2] and generate_one(task)[2]
        item["recipe"] = DEFAULTS | {"height_scale": .001}
        assert not generate_one(task)[2]
        write_source(101)
        changed = generate_one(task)
        assert not changed[2] and first[1]["source_sha256"] != changed[1]["source_sha256"]
    if np is not None:
        with tempfile.TemporaryDirectory() as tmp:
            pack = Path(tmp)/"classification.o2r"
            name = "alt/scenes/nonmq/Z2_TEST/GrassTex"
            pixels = bytes(v for y in range(256) for x in range(256)
                           for v in (30, 100+(x+y)%40, 20, 255))
            with zipfile.ZipFile(pack, "w") as z:
                z.writestr(name, encode_texture(Texture(256, 256, 1, 1, 1, 1, pixels)))
                z.writestr(name+"Alpha", encode_texture(Texture(256, 256, 1, 1, 1, 1,
                              bytes([30, 120, 20, 0])*65536)))
            entry = classify_one((str(pack), name))
            assert entry["excluded"] is None and entry["class"] == "grass"
            assert classify_one((str(pack), name+"Alpha"))["excluded"] == "alpha/cutout or translucent"
    print("self-check: TextureV1 LE/BE round-trip, bump signs, wrap, flat map, roughness, incremental source/recipe invalidation, backend agreement passed")


def classify_one(task):
    pack, name = task
    with zipfile.ZipFile(pack) as source:
        tex = decode_texture(source.read(name))
    rgba = albedo_rgba(tex)
    # Fixed-size sampling bounds statistics cost independently of source resolution.
    a = np.frombuffer(rgba, dtype=np.uint8).reshape(tex.height, tex.width, 4)
    sample = a[::max(1, tex.height//128), ::max(1, tex.width//128)] / 255.0
    rgb = sample[:, :, :3]
    mx, mn = rgb.max(2), rgb.min(2)
    sat = (mx-mn) / np.maximum(mx, .001)
    mean = rgb.mean((0, 1))
    lum = rgb @ np.array([.2126, .7152, .0722])
    energy = [float(np.abs(np.diff(lum, axis=i)).mean()) if lum.shape[i] > 1 else 0.0
              for i in (0, 1)]
    hf = sum(energy)/2
    seam = float((np.abs(lum[0]-lum[-1]).mean()+np.abs(lum[:, 0]-lum[:, -1]).mean())/2)
    r, g, b = mean
    # Mean hue in degrees; saturation/energy prevent colourful murals becoming metal.
    import colorsys
    hue = colorsys.rgb_to_hsv(float(r), float(g), float(b))[0]*360
    stats = {"hue": round(hue, 2), "saturation": round(float(sat.mean()), 4),
             "luminance": round(float(lum.mean()), 4), "high_frequency": round(hf, 4),
             "border_difference": round(seam, 4), "tileability": round(max(0, 1-seam/max(hf, .01)/8), 4)}
    reason = None
    if min(tex.width, tex.height) < 128 or tex.width*tex.height < 65536:
        reason = "small/detail texture"
    elif np.any(a[:, :, 3] < 250):
        reason = "alpha/cutout or translucent"
    elif float(lum.std()) < .008:
        reason = "near-flat colour"
    path = name.lower()
    cls, confidence = "other", .3
    if g > r*1.08 and g > b*1.10 and sat.mean() > .18:
        cls, confidence = "grass", .75
    elif b > r*1.1 and b > g*1.03 and any(x in path for x in ("gyoson", "water", "sea", "pinnacle")):
        cls, confidence = "wet", .45
    elif sat.mean() < .13 and hf > .13 and lum.mean() < .4:
        cls, confidence = "metal", .35
    elif sat.mean() < .22 and lum.mean() > .48 and hf < .075:
        cls, confidence = "plaster", .65
    elif r > g*1.35 and r > b*1.6 and any(x in path for x in ("clock", "town", "yado", "market")):
        cls, confidence = "roof_tile", .45
    elif r > b*1.3 and .15 < sat.mean() < .65:
        directional = abs(energy[0]-energy[1])
        if directional > .025 and any(x in path for x in ("house", "yado", "shop", "bar", "musichouse", "wood")):
            cls, confidence = "wood", .55
        elif any(x in path for x in ("keikoku", "sogen", "gyoson", "sabaku", "sand")):
            cls, confidence = "dirt", .5
        else:
            cls, confidence = "stone", .45
    elif hf > .025:
        cls, confidence = "stone", .4
    return {"resource": name.removeprefix("alt/"), "class": cls, "confidence": round(confidence * (.7 + .3*stats["tileability"]), 2),
            "size": [tex.width, tex.height], "statistics": stats,
            "excluded": reason}


def classify(args):
    if np is None:
        raise ValueError("--classify requires installed NumPy")
    with zipfile.ZipFile(args.pack) as source:
        names = sorted(n for n in source.namelist() if n.startswith("alt/scenes/") and "Tex" in n.rsplit("/", 1)[-1])
        outside = len(source.namelist())-len(names)
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        entries = list(pool.map(classify_one, ((str(args.pack), n) for n in names), chunksize=8))
    result = {"version": 1, "source_archive": args.pack.name,
              "method": "scene/room paths only; RGB hue/saturation, luminance energy, directional grain and periodic border difference; hypotheses, not semantic recognition",
              "excluded_non_scene_resources": outside, "textures": entries}
    args.classify.write_text(json.dumps(result, indent=2)+"\n")
    print(f"classified {len(entries)} scene textures; selected {sum(e['excluded'] is None for e in entries)}; excluded {outside} non-scene resources")


def generate_one(task):
    pack, item, divisor, cache, stdlib = task
    global np
    if stdlib:
        np = None
    resource = item["resource"]
    with zipfile.ZipFile(pack) as source:
        entry = "alt/"+resource
        if source.namelist().count(entry) != 1:
            raise ValueError(f"expected exactly one source entry: {entry}")
        raw = source.read(entry)
    source_hash = hashlib.sha256(raw).hexdigest()
    key = hashlib.sha256(json.dumps([VERSION, source_hash, item["recipe"], divisor], sort_keys=True).encode()).hexdigest()
    target = Path(cache) / (key+".zip")
    if target.exists():
        with zipfile.ZipFile(target) as z:
            meta = json.loads(z.read("metadata.json"))
        # Labels are review metadata and may change without regenerating pixels.
        return str(target), meta | {"resource": resource, "label": item.get("label", resource)}, True
    tex = decode_texture(raw)
    pixels = albedo_rgba(tex)
    fingerprint = pixel_fingerprint(pixels)
    w, h = tex.width, tex.height
    if divisor == 2:
        pixels = half_rgba(pixels, w, h)
        w, h = w//2, h//2
    n, rh = generate(pixels, w, h, item["recipe"])
    meta = {"resource": resource, "label": item.get("label", resource),
            "source_sha256": source_hash, "source_pixels_fnv1a64": fingerprint,
            "source_hash_scope": "entire uncompressed TextureV1 entry, including header",
            "source_size": [tex.width, tex.height], "output_size": [w, h],
            "source_type": tex.kind, "source_flags": tex.flags,
            "source_h_byte_scale": tex.h_byte_scale, "source_v_pixel_scale": tex.v_pixel_scale,
            "crop_pixels": [0, 0, tex.width, tex.height], "uv_scale": [1, 1], "uv_offset": [0, 0],
            "recipe": item["recipe"]}
    temporary = target.with_suffix(f".{os.getpid()}.tmp")
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=3) as z:
        for suffix, data in (("_n", n), ("_r", rh)):
            z.writestr(suffix, encode_texture(Texture(w, h, 9, 0, 1, 1, data)))
        z.writestr("metadata.json", json.dumps(meta))
    temporary.replace(target)
    return str(target), meta, False


def run(args):
    if args.self_check:
        self_check()
        return
    if args.jobs < 1:
        raise ValueError("--jobs must be positive")
    if args.pack is None:
        raise ValueError("--pack is required")
    if args.classify:
        classify(args)
        return
    if args.out is None:
        raise ValueError("--out DIR is required")
    root = args.out.resolve()
    repo = Path(__file__).resolve().parents[2]
    if root == repo or repo in root.parents:
        raise ValueError("--out must be outside this repository; derived textures must stay private")
    root.mkdir(parents=True, exist_ok=True)
    cache = root / "cache"
    cache.mkdir(exist_ok=True)
    materials = load_manifest(args.manifest)
    if args.classification:
        manifest = read_manifest(args.manifest)
        overrides = {i["resource"]: i for i in materials}
        classification = json.loads(args.classification.read_text())
        if classification.get("version") != 1:
            raise ValueError("classification version must be 1")
        entries = classification["textures"]
        if len({e["resource"] for e in entries}) != len(entries):
            raise ValueError("duplicate resource in classification")
        selected = []
        for entry in entries:
            if entry["excluded"] is not None:
                continue
            item = overrides.pop(entry["resource"], dict(resource=entry["resource"]))
            item["recipe"] = (DEFAULTS | manifest.get("defaults", {}) |
                              manifest["classes"][entry["class"]] | item.get("overrides", {}))
            validate_recipe(item["recipe"])
            selected.append(item)
        materials = selected + list(overrides.values())
    started = time.monotonic()
    metadata = {"generator": "newdawn-materials", "version": VERSION,
                "source_archive": args.pack.name, "resolution_divisor": args.resolution,
                "materials": []}
    hits = 0
    results = []
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        tasks = ((str(args.pack), item, args.resolution, str(cache), args.stdlib) for item in materials)
        for index, result in enumerate(pool.map(generate_one, tasks, chunksize=1)):
            results.append(result)
            hits += result[2]
            metadata["materials"].append(result[1])
            if (index+1) % 50 == 0:
                print(f"{index+1}/{len(materials)} ({hits} reused), {time.monotonic()-started:.1f}s", flush=True)
    output = root/"newdawn-materials.o2r"
    previous = None
    if output.exists():
        with zipfile.ZipFile(output) as z:
            previous = json.loads(z.read("newdawn-materials.json"))
    if previous != metadata:
        with tempfile.TemporaryDirectory(prefix=".generate-", dir=root) as staging:
            staged = Path(staging)/"companion.o2r"
            with zipfile.ZipFile(staged, "w") as dest:
                for cached, meta, _ in results:
                    with zipfile.ZipFile(cached) as z:
                        for suffix in ("_n", "_r"):
                            dest.writestr("alt/"+meta["resource"]+suffix, z.read(suffix),
                                          compress_type=zipfile.ZIP_DEFLATED, compresslevel=3)
                dest.writestr("newdawn-materials.json", json.dumps(metadata, indent=2)+"\n")
            staged.replace(output)
    report = {"count": len(materials), "reused": hits, "generated": len(materials)-hits,
              "seconds": round(time.monotonic()-started, 2),
              "archive_bytes": (root/"newdawn-materials.o2r").stat().st_size,
              "resolution_divisor": args.resolution}
    (root/"generation-report.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, help="local ZIP-backed texture pack .o2r")
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_name("materials.json"))
    parser.add_argument("--out", type=Path, help="private output directory outside the repository (required)")
    parser.add_argument("--resolution", type=int, choices=(1, 2), default=2, help="axis divisor; default half axes")
    parser.add_argument("--classify", type=Path, help="write reviewable scene classification JSON and exit")
    parser.add_argument("--classification", type=Path, help="generate selected scene entries using manifest classes")
    parser.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1), help="bounded worker count (default <=4)")
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
