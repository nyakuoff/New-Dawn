# MM3D world conversion — Stage A

Offline tools for an optional visual room replacement in New Dawn. Python 3.10+
and numpy are the only dependencies. No renderer, gameplay, collision, animation,
asset installation, or libultraship changes are made by these tools.

Use only an authorized local extraction. Never commit inputs or generated meshes,
textures, previews, manifests, collision samples, or reports. The output directory
must be outside this repository and outside the input RomFS. No game data is
included here. The scene table is generated from this fork's source table.

## Usage

From the superproject root:

```sh
python3 tools/newdawn/mm3d/convert.py --self-check
python3 tools/newdawn/mm3d/convert.py --generate-table
python3 tools/newdawn/mm3d/convert.py --scene z2_clocktower
python3 tools/newdawn/mm3d/convert.py --all --audit --collision
```

Defaults are developer-local paths; override them for other installations:

```sh
python3 tools/newdawn/mm3d/convert.py --all --audit --collision \
  --romfs /path/to/mm3d/romfs \
  --o2r /path/to/mm.o2r \
  --out /path/outside/repository/mm3d-out
```

`--all` discovers scene files rather than deriving room counts from names or
assuming only the N64 scene set exists. It visits every alternate scene and room
setup, retains setup slot references including nulls/aliases, and deduplicates
identical CMB payloads within each scene. Empty room meshes are valid. A setup
may supply only overrides; its absent commands still require normal game setup
inheritance when a runtime consumer is written.

`--generate-table` is the only option writing a tracked file: `scene-table.json`,
from `mm/include/tables/scene_table.h`. IDs come from the explicit hexadecimal
indices, preserving holes. MM3D names are lower-case segment names. Run this
option again if the fork's scene table changes; conversion and study also read
the current source table directly.

`--self-check` exercises synthetic LzS literal round-trip and overlapping matches,
known ETC1 blocks, ETC1A4 alpha ordering, RGB565 bit replication, analytic floor
and wall intersections/area conservation, and an actual Clock Tower interior
room CMB. The real-room check needs the local RomFS. Errors fail the command.

`--audit` inflates every input file, opens every GAR, decodes every mip/face in
every distinct CTXB/CMB, including CMBs embedded in ZSI room setups, and checks
scene/room and archive collision-only ZSI structures. Identical texture source
containers are SHA-256 deduplicated for decode work; counts distinguish source
CMB entries from embedded room CMB occurrences. Unknown unrelated formats
(e.g. animation and COLB) are counted, not interpreted. These are not failures.

`--collision` is independently runnable and measures all mapped N64 scenes.
`--grid 25` controls the world-space floor sampling step. A smaller step costs
more CPU/memory. Nine MM3D-only scenes have no N64 comparator and are listed
separately by the report; this number should be treated as inventory-dependent.

Outputs:

- `<scene>/manifest.json`: scene and room setups, slot-to-header references,
  room references, raw command inventory, environment records, mesh paths/counts.
- `<scene>/<sha256-of-cmb>.ndmesh`: static mesh and its draw-state metadata.
- `textures/<sha256-of-dimensions-and-rgba>.rgba`: raw row-major RGBA8 mips.
- `conversion-report.json`: per-scene/total conversion counts and failures.
- `audit.json`: file/container, texture format/mip counts and failures.
- `collision-report.md` and `.json`: sorted comparisons, limitations and triage.
- `<scene>/collision-samples.npz`: XYZ, polygon-area weights, nearest absolute
  and signed surface distances, and separate above/below distances. Missing
  distances are infinity; missing signed distances are NaN, not JSON values.
- `preview/{z2_clocktower,z2_00keikoku,z2_insidetower}-{top,oblique}.png`:
  South Clock Town, Termina Field, and Clock Tower interior software previews.

Existing output files may be replaced. Hash files from previous conversions are
not garbage-collected; manifests define the active asset set. A single-scene
run replaces the aggregate conversion report with that run's result.

## NDMM3D01 binary format

All numeric binary data is little-endian. No implicit compiler struct padding.

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 8 bytes | ASCII `NDMM3D01` |
| 8 | u32 | JSON metadata byte length, including trailing spaces for 4-byte alignment |
| 12 | u32 | vertex count |
| 16 | u32 | index count |
| 20 | u32 | vertex stride, currently 48 |
| 24 | JSON bytes | UTF-8 metadata, padded with spaces |
| 24 + JSON length | float32[vertex count][12] | position XYZ, normal XYZ, UV0 ST, colour RGBA |
| next | u32[index count] | triangle-list indices |

Mesh metadata contains bounds, sections/attribute descriptors, original bones,
and a draw list. Every draw has `first_index`, `index_count`, `material`, section,
vertex span, skinning mode and bone palette. Active UV1/UV2 arrays, when used,
are retained in `extra_uvs` on the draw, indexed relative to its vertex span.
Only referenced vertices are exported; indices are remapped to the exported VBO.

Materials preserve three texture bindings (local texture-table indices, `-1`
means absent), GL filter/wrap enums, coordinate sources/mapping and transforms,
opaque/cutout/blend classification, exact alpha compare/reference, separate
RGB/alpha blend factors/equations, depth test/write, cull mode, polygon offset
bytes, fog/lighting flags, material colours and numeric PICA combiners. Numeric
GL/PICA enums are source state, not noclip's graphics-backend enums. The binary
is an interchange format; it does not claim full PICA material emulation.

Texture metadata stores dimensions, original format, cubemap flag and face/mip
paths. Each RGBA file is exactly `width*height*4` bytes, first row first, with no
header or added vertical flip. Colour-space conversion belongs to the runtime
material path. Repeated texture content is shared by hash. Material bindings
refer to the owning mesh's texture table, not to global integer texture IDs.

## Format findings and checks

- Compression is detected by `LzS\x01`, never by extension. The 16-byte LE header
  supplies decompressed and compressed sizes. The payload is LSB-first Okumura
  LZSS: a zeroed 4096-byte ring, initial cursor 4078, absolute 12-bit ring offsets,
  match lengths 3–18. Both sizes and exact stream consumption are checked.
- `GAR\x02` has 12-byte file metadata entries: size, short-name pointer,
  full-name pointer, plus a separate data-offset table. Names and data slices
  are checked independently.
- `ZSI\x09` has a 16-byte preamble; all command pointers are relative to the body
  after it. MM room-list strings have stride `0x34`; lighting records `0x20`.
  Mesh type 2 can point to separate opaque and transparent CMBs. Zero entries and
  alternate-setup-only geometry are valid. Archive collision-only ZSI files
  contain a single collision command without an End command.
- Native ZSI collision command 03 points to a 44-byte header: padding u16;
  min/max XYZ int16; five u16 counts for vertices/polygons/surfaces/cameras/water;
  five u32 pointers in that order. Vertices are int16 XYZ. Polygons are 20 bytes:
  four u16 (type and three flagged indices), three signed int16 normals, padding
  u16, float32 plane distance. Indices mask with `0x1fff`. Normals divide by
  32767; `dot(normal, position) + distance = 0`. Camera/water pointers/counts and
  surface bytes are retained; their detailed behavior is outside Stage A.
- CMB raw version 10 inserts the `qtrs` pointer at `0x28`; later pointers shift
  by four. Bone stride is `0x2c`, mesh stride `0x0c`, shape attribute table starts
  at `0x24` and includes a tangent descriptor after normal. Primitive index
  offsets are in shorts even with byte indices. Reads are checked against the
  declared index span and attribute arrays, then against the exported VBO.
- Array integer attributes use their source scale; float arrays and constants
  do not. Unused UV streams can contain NaNs or short placeholder buffers;
  they are replaced with zero only when no active UV texture coordinator uses
  that stream. Exported position/normal/UV0/colour must all be finite.
- Hierarchical bind transforms are baked for single-bone/rigid sections so output
  is world space. Smooth sections at rest use bind times inverse-bind, identity.
  Normals are transformed and normalized. Original rigs are read, never changed.
- CTXB uses a `tex ` descriptor chunk and separate data pointer. ETC1/ETC1A4
  decode four 4x4 blocks per 8x8 tile; ETC1A4 alpha precedes each colour block.
  Uncompressed PICA formats use 8x8 Morton tiles and packed low/high channels.
  The reader supports RGB8/RGBA8, RGB565, RGBA5551/4444, LA8/LA4, L8/L4, A8/A4,
  HILO8, ETC1 and ETC1A4; unsupported GL format values fail explicitly.
- Structural checks include bounded reads, finite exported attributes, absolute
  position limit of one million game units, collision indices, mesh/material/
  texture references, triangle-list counts and exported-file readback checks.
  Per-mesh extents are recorded for inspection, not inferred from a compile.

## Collision study interpretation

The study reads the actual ZIP-backed `mm.o2r` resources and their 64-byte
legacy header using this fork's `CollisionHeaderFactory.cpp` and exporter, not
an assumed upstream schema. Walkable N64 floor polygons use normal Y > 0.5,
as in this fork's `z_bgcheck.c`. Grid samples share each polygon's actual 3D area;
small polygons with no grid hit use a centroid. Vertically overlapping layers
are counted separately. Threshold percentages use the entire sampled floor
area, including missing MM3D intersections.

At each sample, the algorithm intersects MM3D native collision triangles at the
same XZ and records the nearest surface in either vertical direction. It also
saves independent above/below distances. Ceilings and inaccessible surfaces can
make this metric optimistic; it is not a traversal simulation. Percentiles in
the table exclude missing intersections; the missing-area column remains vital.
Wall coverage uses one centroid ray per N64 wall in both normal directions,
area weighted and limited to 250 units. This is not an exhaustive nearest-wall
measurement or a clearance test.

Coordinates are tested by exact shared collision vertices under all 48 signed
XYZ permutations and scales 0.01, 0.5, 1, 2, 10 and 100. Both bounds and best
transform candidates are recorded. The evidence supports shared XYZ, +Y up,
scale 1, the same handedness and origin. Symmetric or remodeled arenas can
produce ambiguous matches; never infer a scene-specific reflection from bounds.

Triage labels use explicit thresholds in `collision.py`. A high floor score is
only a candidate for visual inspection. Static collision agreement does not
prove alignment of the rendered mesh, doors, ladders, triggers, dynamic actors,
cameras, animations or cutscenes. In particular, scenes with empty static room
meshes may rely on actor geometry. Keep N64 collision and all mechanics intact;
use a reviewed per-scene allowlist and adapt visuals around original collision.

## Preview and runtime limitations

The software previews use an orthographic camera, texture0 times vertex colour,
nearest texture sampling and approximate alpha blending. They deliberately omit
actors, lights, fog, full PICA combiners and animated materials. The interior's
ceiling obscures much of its floor. Backdrop planes and model seams can be
visible from these diagnostic cameras. These images validate recognizable
geometry/texture layout only. No in-game visual improvement or renderer
performance has been verified in Stage A.

Scene archive animations, COLB, LUT lighting/stencil details and animated room
bone/material evaluation are not converted. Static bind geometry and original
bone/section metadata do not reproduce moving scenery. New Dawn must preserve
its existing N64 animation behavior; Stage B needs to retain original dynamic
scenery or explicitly map its existing visual state to replacement geometry.
External CMB texture references with no payload are counted by the audit; they
are not silently populated with unrelated textures. Actor export is out of scope.

## Recommended Stage B integration

Hook `Room_Draw` in `mm/src/code/z_room.c` through an opt-in, per-scene allowlist,
retaining current/previous room, room number and setup selection. Emit a deferred
mesh command into the original OPA/XLU stream and execute it in the interpreter
after flushing pending triangles. Do not issue GL calls directly from game-thread
`Room_Draw`. Use the interpreter's current projection, viewport, framebuffer,
model/view convention and depth attachment; restore/invalidate its GL state
caches after custom draws.

Keep `Scene_Draw` (`z_scene_proc.c`) and its material/segment/colour setup. Normal
and cullable room draw code also sets lights, identity transforms, segments 3/6
and positional lighting. Initially interpret the original room display lists
with only room triangle/room-caster emission suppressed, retaining their state
side effects; audit those lists before replacing this with a compact equivalent.
Never suppress actor geometry or discard original animated effects blindly.

`z_play.c` brackets rooms/opaque actors with HDR scene begin and opaque-end,
then translucent geometry and scene-end. Replacement draws must target the same
HDR or fallback LDR framebuffer; decode colour textures to linear only when the
actual HDR world state is active. Preserve N64 time/weather fog from live game
state rather than switching gameplay to the exported MM3D environment records.

Direct GL meshes bypass interpreter shadow recording. Submit replacement opaque
world-space triangles to the existing caster path before opaque-end, with a
matching cutout policy, and suppress the old room's ghost casters. Sharing the
world depth attachment and projection also keeps SSAO, shafts, water/SSR and
MSAA-resolved pause/pictograph depth readbacks coherent. Validate all of these
in-game before calling a runtime replacement complete. No Stage B code is here.

## References and clean-room scope

Primary references were read from **noclip.website**, MIT licensed, revision
`cc5abc92b5b087e50e219cfc498a3912e036cf5f`:

- [LICENSE](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/LICENSE)
- [cmb.ts](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/src/OcarinaOfTime3D/cmb.ts): chunks, geometry, material fields, texture descriptors.
- [zsi.ts](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/src/OcarinaOfTime3D/zsi.ts): MM command/room/lighting layouts.
- [zar.ts](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/src/OcarinaOfTime3D/zar.ts): GAR2 layout.
- [ctxb.ts](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/src/OcarinaOfTime3D/ctxb.ts): CTXB container.
- [pica_texture.ts](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/src/Common/CTR/pica_texture.ts): ETC1 word/tile ordering, format enums, packed channels.
- [csab.ts](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/src/OcarinaOfTime3D/csab.ts) and [MathHelpers.ts](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/src/MathHelpers.ts): bind transform order.
- [render.ts](https://github.com/magcius/noclip.website/blob/cc5abc92b5b087e50e219cfc498a3912e036cf5f/src/OcarinaOfTime3D/render.ts): attribute scaling and bind-pose skinning conventions.

The license was checked before reading source. It includes a notice that some
implementations were reverse engineered; the reference remains MIT licensed.
Python implementation here was written independently. The permitted markdown
`/home/loganp/dev/ext-research/zelda3d-coop/docs/mm3d_asset_parity.md` supplied
LzS/GAR/version-delta format facts. No code from that unlicensed project was read,
copied or paraphrased. Native ZSI collision layout was inferred from authorized
local bytes and checked against index bounds and plane equations.

Noclip MIT notice, retained for the format reference and decoding tables:

```text
MIT License

Copyright (c) 2018 Jasper St. Pierre

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
