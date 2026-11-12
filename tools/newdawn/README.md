# tools/newdawn — New Dawn headless capture and comparison

`capture.py` (Python 3 stdlib only; needs `gamescope`, `gamescopectl`, ImageMagick `magick`/`montage`) boots the
game with no input, screenshots fixed spots, kills its own process tree and exits. Run from the repo root.

```sh
# 1. every named point (12, about 2.5 min), 1920x1080, from the frozen baseline
tools/newdawn/capture.py --bin-dir build-baseline --out /tmp/newdawn/before

# 2. the same against a new build, optionally with a setting changed
tools/newdawn/capture.py --bin-dir build-cmake/mm --out /tmp/newdawn/after --cvar gEnhancements.Graphics.DisableBlackBars=1

# 3. title sequence at given seconds after launch (no warp), alone or in addition to the points
tools/newdawn/capture.py --no-points --attract 20,48,76 --out /tmp/newdawn/attract

# 4. labelled side-by-side image per capture point (A left, B right)
tools/newdawn/capture.py --compare /tmp/newdawn/before /tmp/newdawn/after --out /tmp/newdawn/side
```

`--rot DEG` overrides a point's yaw (0 = +Z, 90 = +X; the camera starts behind the player), e.g. to look towards the sun. `--burst N,INTERVAL` takes N screenshots per point INTERVAL seconds apart (`<point>_00.png` ...), for motion checks. `--list` shows the points; `--points a,b` selects some; `--wait N` is the seconds from launch to the screenshot
(default 10; the scene is fully faded in after about 3). `--cvar name=value` is repeatable and wins over everything.

How it works / things to know:

- Each run gets a private `SHIP_HOME` under `<bin-dir>/captures/.run-*` (removed afterwards) holding hard links to
  the binary and archives plus a generated `2ship2harkinian.json`. Nothing in `<bin-dir>` is modified. The
  run dir also contains an empty `assets/` folder: without it the game shows an "Extractor assets not found"
  popup instead of starting (the dark grey frame).
- A point is a `WarpPoints` entry with `bootToPoint` (see `mm/2s2h/DeveloperTools/WarpPoint.cpp`); save is 8:00,
  day 1. Coordinates come from the scenes inside `mm.o2r`; each point's origin is in `--list`.
- `south_clock_town` needs the intro-skip cvars (see the comment above `POINTS` in `capture.py`); its save differs
  from the other points (human form, magic bar).
- The game and its gamescope are killed by PID with SIGKILL in a `finally` (also on Ctrl-C / SIGTERM) and checked
  gone; another agent's gamescope is never touched. Per-run game logs are in `<out>/logs/`.
- Warnings are printed at the end if a screenshot has the wrong size or is black/uniform; exit status is 1 then.
- Mods: every run gets its own empty `mods/`, so mods installed in `<bin-dir>/mods` are *not* loaded by default.
  `--mods DIR` links DIR's `.o2r`/`.otr` files into it and enables them in sorted filename order
  (`gSettings.EnabledMods`). Each run prints the archives the game actually loaded (`2ship.o2r`, `mm.o2r`, mods).
