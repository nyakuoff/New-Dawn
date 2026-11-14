#!/usr/bin/env python3
"""Recolour an explicit shortlist of private TextureV1 albedos; never bundles game art."""

import argparse
from contextlib import ExitStack
from dataclasses import replace
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile
import zipfile

import numpy as np

from materials import TEXTURE_TYPE, Texture, albedo_rgba, decode_texture, encode_texture

VERSION = "1.0.0"
DEFAULTS = {"hue": 0.0, "saturation": 1.0, "gamma": 1.0,
            "gain": [1.0, 1.0, 1.0], "lift": 0.0}


def validate_recipe(recipe):
    if set(recipe) != set(DEFAULTS):
        raise ValueError("unknown or missing recipe keys")
    for key, low, high in (("hue", -180, 180), ("saturation", 0, 2),
                           ("gamma", .5, 2), ("lift", -.2, .2)):
        value = recipe[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError(f"invalid {key}: expected finite {low}..{high}")
    gain = recipe["gain"]
    if not isinstance(gain, list) or len(gain) != 3 or any(
            isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or not .5 <= v <= 1.5 for v in gain):
        raise ValueError("gain must contain three finite values in .5..1.5")


def load_manifest(path):
    manifest = json.loads(path.read_text())
    if manifest.get("version") != 1:
        raise ValueError("manifest version must be 1")
    classes = manifest.get("classes", {})
    if not isinstance(classes, dict):
        raise ValueError("classes must be an object")
    for recipe in classes.values():
        validate_recipe(DEFAULTS | recipe)
    items = manifest.get("textures")
    if not isinstance(items, list) or not items:
        raise ValueError("manifest requires a nonempty textures list")
    seen = set()
    for item in items:
        resource = item["resource"]
        if (not isinstance(resource, str) or not resource.startswith("scenes/nonmq/") or "\\" in resource or
                any(p in ("", ".", "..") for p in resource.split("/"))):
            raise ValueError("resource must be a canonical scenes/nonmq/ path without alt/")
        if resource in seen:
            raise ValueError(f"duplicate resource: {resource}")
        seen.add(resource)
        cls = item["class"]
        if cls not in classes:
            raise ValueError(f"unknown class: {cls}")
        item["recipe"] = DEFAULTS | classes[cls] | item.get("overrides", {})
        validate_recipe(item["recipe"])
    return items


def rgba_pixels(tex):
    """Reuse pack decoder; expand native non-paletted base textures when no HD entry exists."""
    if tex.flags != 0 or tex.kind == 1:
        return albedo_rgba(tex)
    count = tex.width * tex.height
    if tex.kind == 2 and len(tex.pixels) == count * 2:  # native RGBA5551 is big-endian
        v = np.frombuffer(tex.pixels, dtype=">u2")
        rgb = np.stack(((v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31), axis=-1)
        rgb = ((rgb << 3) | (rgb >> 2)).astype(np.uint8)
        return np.column_stack((rgb, (v & 1) * 255)).astype(np.uint8).tobytes()
    bits = {5: 4, 6: 8, 7: 4, 8: 8, 9: 16}.get(tex.kind)
    if bits is None or len(tex.pixels) != (count * bits + 7) // 8:
        raise ValueError("unsupported native texture (paletted textures require their TLUT)")
    v = np.frombuffer(tex.pixels, dtype=np.uint8)
    if bits == 4:
        v = np.stack((v >> 4, v & 15), axis=-1).ravel()[:count]
    alpha = np.full(count, 255, dtype=np.uint8)
    if tex.kind == 5:
        intensity = v * 17
    elif tex.kind == 6:
        intensity = v
    elif tex.kind == 7:
        intensity = np.rint((v >> 1) * (255 / 7)).astype(np.uint8)
        alpha = (v & 1) * 255
    elif tex.kind == 8:
        intensity, alpha = (v >> 4) * 17, (v & 15) * 17
    else:
        intensity, alpha = v[::2], v[1::2]
    return np.column_stack((intensity, intensity, intensity, alpha)).astype(np.uint8).tobytes()


def decode_albedo(raw):
    # The base game's archive uses V0 (no flags/scales), whereas the HD pack uses V1.
    if len(raw) >= 80 and raw[0] in (0, 1):
        order = "<" if raw[0] == 0 else ">"
        if struct.unpack_from(order + "II", raw, 4) == (TEXTURE_TYPE, 0):
            kind, width, height, size = struct.unpack_from(order + "IIII", raw, 64)
            if len(raw) != 80 + size:
                raise ValueError("TextureV0 payload size mismatch")
            return decode_texture(encode_texture(Texture(width, height, kind, 0, 1, 1, raw[80:])))
    return decode_texture(raw)


def recolor(pixels, width, height, recipe):
    """Display-space YIQ hue rotation, saturation and levels; alpha is copied byte-for-byte."""
    source = np.frombuffer(pixels, dtype=np.uint8).reshape(height, width, 4)
    rgb = source[:, :, :3].astype(np.float32) / 255
    # Hue rotates chroma about Y, keeping greys neutral. No geometric filtering or resampling.
    if recipe["hue"]:
        yiq = rgb @ np.array([[.299, .596, .211], [.587, -.274, -.523], [.114, -.322, .312]], dtype=np.float32)
        angle = math.radians(recipe["hue"])
        i, q = yiq[:, :, 1].copy(), yiq[:, :, 2].copy()
        yiq[:, :, 1] = i * math.cos(angle) - q * math.sin(angle)
        yiq[:, :, 2] = i * math.sin(angle) + q * math.cos(angle)
        rgb = yiq @ np.array([[1, 1, 1], [.956, -.272, -1.106], [.621, -.647, 1.703]], dtype=np.float32)
    lum = rgb @ np.array([.2126, .7152, .0722], dtype=np.float32)
    rgb = lum[:, :, None] + (rgb - lum[:, :, None]) * recipe["saturation"]
    rgb = np.maximum(rgb * np.array(recipe["gain"], dtype=np.float32) + recipe["lift"], 0) ** recipe["gamma"]
    out = source.copy()
    out[:, :, :3] = np.rint(np.clip(rgb, 0, 1) * 255).astype(np.uint8)
    return out.tobytes()


def private_path(path):
    path = path.expanduser().resolve()
    repo = Path(__file__).resolve().parents[2]
    if path == repo or repo in path.parents:
        raise ValueError("derived textures and previews must stay outside this repository")
    return path


def write_entry(archive, name, data):
    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, data, compresslevel=3)


def generate(pack, base, items, output, preview=None):
    output = private_path(output)
    preview = private_path(preview) if preview else None
    if output in {p.resolve() for p in (pack, base) if p is not None}:
        raise ValueError("output must not replace a source archive")
    output.parent.mkdir(parents=True, exist_ok=True)
    metadata = {"generator": "newdawn-recolor", "version": VERSION, "textures": []}
    with ExitStack() as stack, tempfile.TemporaryDirectory(prefix=".recolor-", dir=output.parent) as tmp:
        source = stack.enter_context(zipfile.ZipFile(pack))
        fallback = stack.enter_context(zipfile.ZipFile(base)) if base else None
        staged = Path(tmp) / "recolor.o2r"
        with zipfile.ZipFile(staged, "w") as dest:
            for item in items:
                resource = item["resource"]
                entry = "alt/" + resource
                if entry in source.namelist():
                    archive, name = source, entry
                elif fallback and resource in fallback.namelist():
                    archive, name = fallback, resource
                else:
                    raise ValueError(f"missing texture in pack and base: {resource}")
                if archive.namelist().count(name) != 1:
                    raise ValueError(f"ambiguous duplicate source: {name}")
                raw = archive.read(name)
                tex = decode_albedo(raw)
                pixels = rgba_pixels(tex)
                result = recolor(pixels, tex.width, tex.height, item["recipe"])
                # Native fallback becomes raw RGBA8; HD resource type/scales/flags are preserved.
                replacement = (replace(tex, pixels=result) if tex.flags != 0 or tex.kind == 1 else
                               Texture(tex.width, tex.height, 1, 1, 1, 1, result))
                encoded = encode_texture(replacement)
                write_entry(dest, entry, encoded)
                metadata["textures"].append({"resource": resource, "label": item.get("label", resource),
                    "source_archive": Path(archive.filename).name, "source_sha256": hashlib.sha256(raw).hexdigest(),
                    "output_sha256": hashlib.sha256(encoded).hexdigest(), "size": [tex.width, tex.height],
                    "recipe": item["recipe"]})
                if preview:
                    from PIL import Image
                    preview.mkdir(parents=True, exist_ok=True)
                    pair = Image.new("RGBA", (tex.width * 2, tex.height))
                    pair.paste(Image.frombytes("RGBA", (tex.width, tex.height), pixels), (0, 0))
                    pair.paste(Image.frombytes("RGBA", (tex.width, tex.height), result), (tex.width, 0))
                    pair.thumbnail((1024, 512))
                    pair.save(preview / (resource.rsplit("/", 1)[1] + ".png"))
            write_entry(dest, "newdawn-recolor.json", (json.dumps(metadata, indent=2) + "\n").encode())
        staged.replace(output)  # No partial archive is installed if any entry fails.
    print(f"recoloured {len(items)} textures -> {output} ({output.stat().st_size:,} bytes)")


def self_check():
    pixels = bytes(v for i in range(256) for v in (i, 255-i, i//2, i))
    assert recolor(pixels, 16, 16, DEFAULTS) == pixels
    recipe = DEFAULTS | {"hue": 25, "saturation": .7, "gamma": .9, "gain": [1.1, .9, 1]}
    result = recolor(pixels, 16, 16, recipe)
    assert result != pixels and result[3::4] == pixels[3::4]
    grey = bytes([128, 128, 128, 37])
    assert recolor(grey, 1, 1, DEFAULTS | {"hue": 90}) == grey
    assert rgba_pixels(Texture(1, 1, 2, 0, 1, 1, b"\xf8\x01")) == b"\xff\0\0\xff"
    for kind, payload, expected in ((5, b"\xf0", b"\xff\xff\xff\xff"), (6, b"\x80", b"\x80\x80\x80\xff"),
                                    (7, b"\xe0", b"\xff\xff\xff\0"), (8, b"\xf8", b"\xff\xff\xff\x88"),
                                    (9, b"\x80\x37", b"\x80\x80\x80\x37")):
        assert rgba_pixels(Texture(1, 1, kind, 0, 1, 1, payload)) == expected
    for bad in ({"gamma": float("nan")}, {"hue": 181}, {"gain": [1, 1]}, {"oops": 1}):
        try:
            validate_recipe(DEFAULTS | bad)
        except ValueError:
            pass
        else:
            raise AssertionError(f"accepted invalid recipe: {bad}")
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        pack, base, output = (root/n for n in ("pack.o2r", "base.o2r", "result.o2r"))
        resource = "scenes/nonmq/Z2_TEST/TestTex"
        with zipfile.ZipFile(pack, "w") as z:
            z.writestr("alt/"+resource, encode_texture(Texture(16, 16, 1, 1, 1, 1, pixels)))
        with zipfile.ZipFile(base, "w") as z:
            header = bytearray(64)
            struct.pack_into("<II", header, 4, TEXTURE_TYPE, 0)
            z.writestr(resource+"Base", header + struct.pack("<IIII", 2, 1, 1, 2) + b"\xf8\x01")
        manifest = root/"manifest.json"
        manifest.write_text(json.dumps({"version": 1, "classes": {"test": recipe},
            "textures": [{"resource": resource, "class": "test"}, {"resource": resource+"Base", "class": "test"}]}))
        items = load_manifest(manifest)
        generate(pack, base, items, output)
        before = output.read_bytes()
        generate(pack, base, items, output)
        assert before == output.read_bytes()
        with zipfile.ZipFile(output) as z:
            tex = decode_texture(z.read("alt/"+resource))
            assert albedo_rgba(tex) == result
            assert albedo_rgba(decode_texture(z.read("alt/"+resource+"Base")))[3] == 255
        try:
            generate(pack, base, items + [{"resource": resource+"Missing"}], output)
        except ValueError:
            pass
        else:
            raise AssertionError("missing source accepted")
        assert before == output.read_bytes()
        manifest.write_text(json.dumps({"version": 1, "classes": {"test": recipe}, "textures": [items[0], items[0]]}))
        try:
            load_manifest(manifest)
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate resource accepted")
    print("self-check: identity, alpha, hue neutrals, native fallback, validation, archive round-trip, determinism, atomic failure passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, help="local ZIP-backed 4K .o2r")
    parser.add_argument("--base", type=Path, help="local mm.o2r, fallback for resources absent from the pack")
    parser.add_argument("--manifest", type=Path, default=Path(__file__).with_suffix(".json"))
    parser.add_argument("--out", type=Path, default=Path.home()/"dev/ghm-mods/sets/full/zy-newdawn-recolor.o2r")
    parser.add_argument("--preview", type=Path, help="private before/after texture previews (requires Pillow)")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_check:
            self_check()
        elif args.pack is None:
            parser.error("--pack is required")
        else:
            generate(args.pack, args.base, load_manifest(args.manifest), args.out, args.preview)
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile) as exc:
        parser.exit(1, f"recolor: {exc}\n")


if __name__ == "__main__":
    main()
