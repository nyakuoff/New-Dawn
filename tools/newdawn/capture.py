#!/usr/bin/env python3
"""New Dawn: headless, repeatable screenshots of 2 Ship 2 Harkinian for before/after rendering comparisons.

Each capture point boots the game straight into a scene (the port's WarpPoints / bootToPoint
developer feature, 8:00 on day 1), waits, takes a screenshot through gamescope and kills the whole
process tree by PID.  Nothing in the binary directory is modified: every run gets a private run
directory holding hard links to the binary and archives plus a generated config (SHIP_HOME).

  capture.py --bin-dir build-baseline --out /tmp/before
  capture.py --bin-dir build-cmake/mm --out /tmp/after
  capture.py --compare /tmp/before /tmp/after /tmp/side-by-side
"""
import argparse, json, os, re, shutil, signal, struct, subprocess, sys, tempfile, time
from pathlib import Path

# Capture points. entrance = (scene index << 9) | (spawn << 4) | variant, i.e. the ENTRANCE() macro in
# mm/include/z64scene.h.  room, pos and rot are the SetStartPositionList / SetEntranceList values of
# that entrance's own spawn in mm.o2r (scenes/nonmq/<SCENE>/<SCENE>), so the standing spot is valid by
# construction.  "src" documents which spawn each came from.
#
# INTRO: every South Clock Town spawn plays the first-arrival Tatl cutscene on a fresh save, and the
# cutscene overrides the player's position.  The port's SkipIntroSequence + SkipFirstCycle enhancements set
# the story flags that suppress it, but only when the save's entrance is ENTRANCE(CUTSCENE, 0) (0x1c00),
# which they redirect to South Clock Town.  So that point warps to 0x1c00 with those two cvars; the player
# is still placed at the pos/rot given (the warp respawn data wins).  Side effect: that point's save is
# the post-first-cycle one (human form, ocarina, magic bar), unlike the other points.
POINTS = {}
INTRO_CVARS = {"gEnhancements.Cutscenes.SkipIntroSequence": 1, "gEnhancements.Cutscenes.SkipFirstCycle": 1}


def point(name, entrance, room, pos, rot, src, intro=False):
    warp = dict(entranceId=entrance, roomNum=room, pos=dict(zip("xyz", pos)), rotY=rot, bootToPoint=True)
    POINTS[name] = warp, src, INTRO_CVARS if intro else {}


# fmt: off
# POINTS-TABLE-BEGIN
point('south_clock_town', 0x1c00, 0, (-279, 0, 627), 23040, 'Z2_CLOCKTOWER spawn 1 (entrance 0xd810); entrance id replaced, see INTRO', intro=True)
point('termina_field', 0x54d0, 0, (2044, 48, -477), 3460, 'Z2_00KEIKOKU spawn 13 (entrance 0x54d0)')
point('southern_swamp_cleared', 0x0c00, 0, (-191, 61, -1410), 0, 'Z2_20SICHITAI2 spawn 0 (entrance 0x0c00)')
point('woods_of_mystery', 0xc200, 1, (274, 0, 0), -30976, 'Z2_26SARUNOMORI spawn 0, entrance 0: room 1')
point('woodfall_exterior', 0x8600, 0, (-41, 12, -1353), 0, 'Z2_21MITURINMAE spawn 0 (entrance 0x8600)')
point('southern_swamp', 0x8400, 0, (-191, 61, -1410), 0, 'Z2_20SICHITAI spawn 0 (entrance 0x8400)')
point('mountain_village_winter', 0x9a80, 0, (-649, 8, -196), 14336, 'Z2_10YUKIYAMANOMURA spawn 8 (entrance 0x9a80)')
point('great_bay_coast', 0x6890, 0, (-305, 80, 4756), 0, 'Z2_30GYOSON spawn 9 (entrance 0x6890)')
point('ikana_canyon', 0x20b0, 0, (546, 400, 858), -27776, 'Z2_IKANA spawn 11 (entrance 0x20b0)')
point('deku_palace', 0x5020, 0, (5, 0, 1433), 0, 'Z2_22DEKUCITY spawn 2 (entrance 0x5020)')
point('zora_hall', 0x6070, 0, (0, 0, 132), 0, 'Z2_33ZORACITY spawn 7 (entrance 0x6070)')
point('stock_pot_inn', 0xbc00, 0, (149, 0, 303), 23040, 'Z2_YADOYA spawn 0 (entrance 0xbc00)')
point('woodfall_temple', 0x3000, 2, (0, -1185, 2370), 23078, 'Z2_MITURIN spawn 0 (entrance 0x3000)')
point('great_bay_temple', 0x8c00, 13, (0, -210, 2654), 23081, 'Z2_SEA spawn 0 (entrance 0x8c00)')
point('stone_tower_temple', 0x2600, 0, (0, 90, 810), 23086, 'Z2_INISIE_N spawn 0 (entrance 0x2600)')
# POINTS-TABLE-END
# fmt: on

DEFAULT_ATTRACT = "20,48,76"
# A fresh 8:00-day-1 save plays story/entrance cutscenes in several scenes; these existing enhancements skip
# them so the capture lands in gameplay.  Applied to both builds being compared; override with --cvar.
DEFAULT_CVARS = {"gEnhancements.Cutscenes.SkipEntranceCutscenes": 1, "gEnhancements.Cutscenes.SkipStoryCutscenes": 1}
RUN_FILES_SKIP = {"2ship2harkinian.json", "imgui.ini", "logs", "mods", "captures", "Save", "gamecontrollerdb.txt"}


# ---------------------------------------------------------------- process handling
def _proc_table():
    """pid -> (ppid, pgrp) for every process we can see."""
    t = {}
    for p in Path("/proc").iterdir():
        if p.name.isdigit():
            try:
                f = (p / "stat").read_text().rsplit(")", 1)[1].split()
                t[int(p.name)] = (int(f[1]), int(f[2]))
            except (OSError, IndexError):
                pass
    return t


def tree_pids(root, pgid):
    """root, all its descendants and every member of its process group."""
    t = _proc_table()
    found = {root} | {p for p, (_, g) in t.items() if g == pgid}
    grew = True
    while grew:
        grew = False
        for p, (pp, _) in t.items():
            if pp in found and p not in found:
                found.add(p)
                grew = True
    return {p for p in found if p in t}


def kill_tree(root, pgid):
    """SIGKILL our own tree by PID (never by name: another gamescope may be running) and verify."""
    for _ in range(20):
        pids = tree_pids(root, pgid)
        if not pids:
            return
        for p in pids:
            try:
                os.kill(p, signal.SIGKILL)
            except ProcessLookupError:
                pass
        try:
            os.waitpid(root, os.WNOHANG)  # reap, so it does not linger as a zombie
        except ChildProcessError:
            pass
        time.sleep(0.2)
    raise RuntimeError(f"could not kill process tree {sorted(tree_pids(root, pgid))}")


# ---------------------------------------------------------------- run directory / config
def make_run_dir(bin_dir, parent, width, height, cvars, warp, mods=None):
    """Private SHIP_HOME == app dir: hard links to the binary and archives, generated config."""
    run = Path(tempfile.mkdtemp(prefix=".run-", dir=parent))
    for e in bin_dir.iterdir():
        if e.name in RUN_FILES_SKIP:
            continue
        if e.is_dir():
            (run / e.name).symlink_to(e.resolve())
        else:
            try:
                os.link(e, run / e.name)  # cheap; /proc/self/exe must resolve inside run/, so no symlinks
            except OSError:
                shutil.copy2(e, run / e.name)
    # The extractor refuses to start the game without an assets/ folder next to the binary, and shows a
    # modal "Extractor assets not found" popup instead (that was the dark grey frame).  Empty is enough.
    (run / "assets").mkdir(exist_ok=True)
    # Always give the run its own mods/ (the game would otherwise fall back to the binary dir's mods and
    # silently enable everything in it).  --mods DIR links that directory's archives in, enabled in sorted order.
    (run / "mods").mkdir()
    enabled = []
    if mods:
        for f in sorted(Path(mods).rglob("*"), key=lambda p: p.name):
            if f.is_file() and f.suffix.lower() in (".o2r", ".otr"):  # same filter and name rule as ModMenu.cpp
                (run / "mods" / f.name).symlink_to(f.resolve())
                enabled.append(f.stem)
        enabled.sort()
        cvars = {**cvars, "gSettings.EnabledMods": "|".join(enabled)}
    cfg = {"ConfigVersion": 2, "CVars": {}, "Window": {
        "AudioBackend": "sdl", "Backend": {"Id": 2, "Name": "OpenGL"}, "Fullscreen": {"Enabled": False},
        "Width": width, "Height": height, "PositionX": 0, "PositionY": 0}}
    for k, v in cvars.items():
        d = cfg["CVars"]
        *path, leaf = k.split(".")
        for p in path:
            d = d.setdefault(p, {})
        d[leaf] = v
    if warp:
        cfg["WarpPoints"] = {warp[0]: warp[1]}
    (run / "2ship2harkinian.json").write_text(json.dumps(cfg, indent=1))
    return run


def parse_cvar(s):
    k, _, v = s.partition("=")
    if not k or not _:
        raise argparse.ArgumentTypeError(f"--cvar wants name=value, got {s!r}")
    for conv in (int, float):
        try:
            return k, conv(v)
        except ValueError:
            pass
    return k, v


def preset_cvars():
    """The New Dawn preset, read from the sNewDawnPresetCvars table so captures can't drift from the menu.
    Entries flagged as floats stay floats; on/off and integer settings must be ints or the game ignores them."""
    src = (Path(__file__).resolve().parents[2] / "mm/2s2h/BenGui/BenMenu.cpp").read_text()
    table = src[src.index("sNewDawnPresetCvars[] = {"):]
    table = table[:table.index("};")]
    out = []
    for name, is_float, value in re.findall(r'\{\s*"([^"]+)",\s*(true|false),\s*([-0-9.]+)f', table):
        if not name.endswith("Debug.View"):
            out.append((name, float(value) if is_float == "true" else int(float(value))))
    return out


# ---------------------------------------------------------------- one game launch
def png_size(path):
    with open(path, "rb") as f:
        h = f.read(24)
    return struct.unpack(">II", h[16:24]) if h[:8] == b"\x89PNG\r\n\x1a\n" else None


def image_stats(path):
    r = subprocess.run(["magick", str(path), "-format", "%[fx:mean] %[fx:standard_deviation]", "info:"],
                       capture_output=True, text=True)
    try:
        return tuple(float(x) for x in r.stdout.split())
    except ValueError:
        return None


def run_game(a, tag, shots, warp, log_dir, cvars=()):
    """Launch the game once; shots = [(seconds_after_launch, output_png)]. Returns list of problems."""
    cvars = dict(cvars)
    bin_dir = Path(a.bin_dir).resolve()
    parent = bin_dir / "captures"
    parent.mkdir(exist_ok=True)
    run = make_run_dir(bin_dir, parent, a.width, a.height, {**DEFAULT_CVARS, **cvars, **dict(a.cvar)}, warp, a.mods)
    log = open(log_dir / f"{tag}.log", "wb")
    proc = None
    problems = []
    try:
        env = dict(os.environ, SHIP_HOME=str(run))
        env.pop("GAMESCOPE_WAYLAND_DISPLAY", None)
        proc = subprocess.Popen(
            ["gamescope", "--backend", "headless", "-W", str(a.width), "-H", str(a.height), "--", "./2s2h.elf"],
            cwd=run, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        t0 = time.monotonic()
        # our own socket name comes from our own gamescope's log, never from guessing gamescope-0
        display = None
        while display is None:
            m = re.search(rb"wayland display '(gamescope-\d+)'", (log_dir / f"{tag}.log").read_bytes())
            display = m.group(1).decode() if m else None
            if display is None:
                if proc.poll() is not None or time.monotonic() - t0 > 30:
                    raise RuntimeError(f"gamescope did not come up, see {log_dir / (tag + '.log')}")
                time.sleep(0.2)
        for when, out in shots:
            time.sleep(max(0, t0 + when - time.monotonic()))
            if proc.poll() is not None:
                problems.append(f"{out.name}: gamescope exited early")
                continue
            out.unlink(missing_ok=True)
            subprocess.run(["gamescopectl", "screenshot", str(out)], env=dict(env, GAMESCOPE_WAYLAND_DISPLAY=display),
                           capture_output=True, timeout=30)
            for _ in range(50):  # the file appears asynchronously
                if out.exists() and png_size(out):
                    time.sleep(0.2)
                    break
                time.sleep(0.2)
            else:
                problems.append(f"{out.name}: no screenshot written")
                continue
            if png_size(out) != (a.width, a.height):
                problems.append(f"{out.name}: size {png_size(out)}, wanted {(a.width, a.height)}")
            st = image_stats(out)
            if st and (st[0] < 0.02 or st[1] < 0.02):
                problems.append(f"{out.name}: looks black/uniform (mean {st[0]:.3f}, sd {st[1]:.3f})")
    finally:
        if proc is not None:
            kill_tree(proc.pid, proc.pid)
        log.close()
        shutil.rmtree(run, ignore_errors=True)
    loaded = [Path(m).name for m in re.findall(r"Adding Archive (\S.*?) to Archive Manager", (log_dir / f"{tag}.log").read_text(errors="replace"))]
    print(f"    archives: {', '.join(loaded)}", flush=True)  # 2ship.o2r, mm.o2r, then any mods
    return problems


# ---------------------------------------------------------------- compare
def compare(a_dir, b_dir, out_dir):
    a_dir, b_dir, out_dir = Path(a_dir), Path(b_dir), Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    names = sorted({p.name for p in a_dir.glob("*.png")} | {p.name for p in b_dir.glob("*.png")})
    for n in names:
        a, b = a_dir / n, b_dir / n
        if not (a.exists() and b.exists()):
            print(f"skip {n}: only in {'A' if a.exists() else 'B'}")
            continue
        subprocess.run(["montage", "-label", f"A: {a_dir.name}", str(a), "-label", f"B: {b_dir.name}", str(b),
                        "-tile", "2x1", "-geometry", "+6+6", "-pointsize", "32", "-background", "#202020",
                        "-fill", "white", "-title", n[:-4], str(out_dir / n)], check=True)
        print(out_dir / n)


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin-dir", default="build-baseline", help="directory with 2s2h.elf, 2ship.o2r, mm.o2r (default %(default)s)")
    ap.add_argument("--out", help="output directory (default <bin-dir>/captures/<timestamp>)")
    ap.add_argument("--points", default="all", help="comma separated capture point names, or 'all' (default). See --list")
    ap.add_argument("--attract", metavar="SECONDS", help=f"also/only capture the title sequence at these seconds after launch, e.g. {DEFAULT_ATTRACT}")
    ap.add_argument("--rot", type=float, metavar="DEG", help="override the point's yaw (degrees, 0 = +Z, 90 = +X); the camera starts behind the player")
    ap.add_argument("--pos", metavar="X,Y,Z", help="override the point's standing position (world units), e.g. to stand at a spot of the same scene")
    ap.add_argument("--burst", metavar="N,INTERVAL", help="take N screenshots INTERVAL seconds apart per point, starting at --wait, "
                    "named <point>_00.png ... (each screenshot adds its own latency, so the real spacing can be larger)")
    ap.add_argument("--no-points", action="store_true", help="skip the named points (use with --attract)")
    ap.add_argument("--wait", type=float, default=10.0, help="seconds after launch before the screenshot of a point (default %(default)s)")
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("--cvar", action="append", default=[], type=parse_cvar, metavar="NAME=VALUE",
                    help="console variable merged into the generated config, e.g. gEnhancements.Graphics.DisableBlackBars=1 (repeatable)")
    ap.add_argument("--preset", action="store_true", help="apply the New Dawn graphics preset from BenMenu.cpp (later --cvar options override it)")
    ap.add_argument("--mods", metavar="DIR", help="link the .o2r/.otr archives of DIR into each run's mods/ and enable them (sorted by filename). Default: no mods, even if <bin-dir>/mods has some")
    ap.add_argument("--list", action="store_true", help="list capture points and exit")
    ap.add_argument("--compare", nargs=2, metavar=("DIR_A", "DIR_B"), help="write labelled side-by-side images of two capture directories into --out")
    a = ap.parse_args()
    if a.preset:
        a.cvar = preset_cvars() + a.cvar

    if a.list:
        for n, (w, src, _) in POINTS.items():
            print(f"{n:24} entrance {w['entranceId']:#06x} room {w['roomNum']:3} pos {tuple(w['pos'].values())} rot {w['rotY']:6}  {src}")
        return 0
    if a.compare:
        if not a.out:
            ap.error("--compare needs --out")
        compare(*a.compare, a.out)
        return 0

    names = [] if a.no_points else (list(POINTS) if a.points == "all" else a.points.split(","))
    bad = [n for n in names if n not in POINTS]
    if bad:
        ap.error(f"unknown point(s) {bad}; see --list")
    out = Path(a.out or Path(a.bin_dir) / "captures" / time.strftime("%Y%m%d-%H%M%S")).resolve()
    out.mkdir(parents=True, exist_ok=True)
    log_dir = out / "logs"
    log_dir.mkdir(exist_ok=True)

    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))  # run the finally blocks
    problems = []
    t0 = time.monotonic()
    try:
        for n in names:
            print(f"[{time.monotonic() - t0:5.0f}s] {n}", flush=True)
            shots = [(a.wait, out / f"{n}.png")]
            if a.burst:
                count, gap = a.burst.split(",")
                shots = [(a.wait + i * float(gap), out / f"{n}_{i:02d}.png") for i in range(int(count))]
            warp = dict(POINTS[n][0])
            if a.rot is not None:
                warp["rotY"] = int(round(a.rot / 360.0 * 65536)) % 65536
            if a.pos:
                warp["pos"] = dict(zip("xyz", (float(v) for v in a.pos.split(","))))
            problems += run_game(a, n, shots, (n, warp), log_dir, POINTS[n][2])
        if a.attract:
            secs = sorted(float(s) for s in a.attract.split(","))
            print(f"[{time.monotonic() - t0:5.0f}s] attract {secs}", flush=True)
            problems += run_game(a, "attract", [(s, out / f"attract_{int(s):03d}s.png") for s in secs], None, log_dir)
    except KeyboardInterrupt:
        print("interrupted; game processes killed", file=sys.stderr)
        return 130
    print(f"done in {time.monotonic() - t0:.0f}s -> {out}")
    for p in problems:
        print("WARNING:", p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
