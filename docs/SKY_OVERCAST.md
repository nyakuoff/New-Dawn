# Winter Mountain Village overcast sky (D2)

Active with `gNewDawn.Sky.Physical` (+ HDR); no new cvar. Visual only.

Winter Mountain Village (`SCENE_10YUKIYAMANOMURA`, `SKYBOX_2`, days 1-2, layer 0) has no skybox: the game paints an
opaque fog-colour fill in `Environment_DrawSkyboxFilters`. Previously the physical sky left it untouched.

- `Environment_DrawSkyboxFilters` keeps the fill and then calls `NewDawnSky_DrawOvercast`, which emits the usual
  physical-sky marker with `skyOvercast = 1`. The pass blends over the fill (alpha = Sky Strength), so when the pass
  cannot run (Deku-bubble LDR fallback, HDR off, shader failure) the original fill remains.
- Shader (`uOvercast`): aerosol 10x and 80% mix to the fog hue (low contrast), the sky blends into the fog over
  ray.y 0-0.45 instead of 0-0.05, and the cloud march uses coverage 0.8 with a bright deck lit by the fog colour
  (`uFog * (0.68 + 0.30h)`, softly shaded by self-shadow and thickness). Because the fog colour carries the time
  of day, dawn/dusk/night stay in the scene's own mood. No sun disc, moon or stars appear (the fill hid them before too).
- Ambient/reflection cube uses snow ground albedo (0.70, 0.74, 0.80) for this scene. The spring village and all
  other scenes are unchanged. Ground brightness vs the previous frame: +0.3% noon, +0.5% dusk, +0.1% night.
- GPU in this scene: sky 0.16 ms, clouds 0.14-0.20 ms, ambient/reflection cube 0.05 ms (previously not drawn here).

Evidence (every image inspected): `~/dev/ghm-mods/showcase/sky-reflections/d2/mountain_village_{noon,dusk,night}.png`
(left: before, right: after), `/tmp/nd/d2/`. Zero GL errors; frozen Stock Pot Inn is byte-identical to main.

Limits: the visible sky in the village is small (steep mountains); the deck is deliberately soft. Snowflakes draw in
the world pass and stay visible over it. The final day, cutscenes and the Goht fight (a different scene) keep the original sky.
