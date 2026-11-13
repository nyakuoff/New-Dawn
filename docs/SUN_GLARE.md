# HDR sun glare (E1)

`gNewDawn.Sky.GlareFix`: default 0, New Dawn preset 0 pending the exact-main and Termina sun-facing gates; requires an active HDR world framebuffer.
The native sun full-screen wash alone is tagged in its display list and omitted by OpenGL in HDR.
The sun disc, physical atmosphere/Mie glow, HDR bloom and native flare sprites remain. Custom flares,
LDR frames (including HDR's runtime fallback), screen fades and all environment state updates are unchanged.
No shader assets or samplers change. The tags flush the interpreter at their boundaries.

Validation: main `51c57581e`, renderer `6221f142`, Linux Mesa OpenGL, RX 7800 XT,
1920×1080, MSAA 4, canonical capture tool with `--preset --mods ~/dev/ghm-mods/sets/full`,
`MESA_DEBUG=context`. Build with `cmake --build build -j6`; both default shader copies match (the renderer checkout had CRLF; normalised to LF).
Coast 17:30 sun-facing before/after inspected: orange veil removed, native local glow and sprites retained,
water and terrain regain colour. Zero GL/shader/framebuffer errors. No new GPU pass or texture cost.
Coast stage means before/after (ms, 300 runs): AO .392/.307, HDR .178/.155, shadows .277/.230,
shafts .316/.250, water .180/.141, opaque copy .134/.122, sky .218/.169, clouds .349/.273,
cube .075/.059. GPU clock/load variation affects every stage; these numbers do not establish a speedup.

Evidence: `~/dev/ghm-mods/work/glare-fog/session/e1-{before,after}-coast/` and
`~/dev/ghm-mods/showcase/glare-fog/e1/coast/` (split.py, left off/right on).
Termina pond before/after: `e1-field-{0,1}`, split in `e1/field`. This view is occluded from the sun;
**Termina sun-facing glare remains unverified**. Three framing failures were stopped per instruction.
The frozen Stock Pot Inn toggle pair (`e1-inn-{0,1}`, frame 200) is byte-identical.
**Exact-main byte identity is not established**: main/modified captures differ in sparse render noise
(1,213 pixels at matched 14-second timing; the 18-second pair changes 22,642 pixels). No glare
markers are emitted with the toggle off. Three comparison attempts were stopped; no pixel gate is claimed.
