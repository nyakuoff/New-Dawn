#!/usr/bin/env bash
# New Dawn look audit (docs/LOOK_BIBLE.md): the same places at dawn, noon, dusk and night, plus rain and the final day,
# with the Ultra look. Frozen frames (capture.py --freeze), so a later run of the same build gives the same pixels and a
# run of another build is a fair before/after.
#   tools/newdawn/lookdev_audit.sh OUT_DIR [extra capture.py options, e.g. --cvar NAME=VALUE]
# Writes OUT_DIR/<place>_<state>.png, OUT_DIR/sheets/<place>.png and the capture logs in OUT_DIR/raw. Needs gamescope,
# ImageMagick and nd-heavy.
set -u
out=${1:?usage: lookdev_audit.sh OUT_DIR [capture.py options]}; shift
cd "$(dirname "$0")/../.."
mods=${ND_MODS:-$HOME/dev/ghm-mods/sets/community}
raw=$out/raw
mkdir -p "$out/sheets" "$raw"

cap() { # cap STATE POINTS [capture.py options]
    local state=$1 points=$2; shift 2
    # MESA_DEBUG and the stats switch make the logs report GL errors; neither changes the picture.
    MESA_DEBUG=context nd-heavy game -- python3 tools/newdawn/capture.py --bin-dir build/mm --preset --mods "$mods" \
        --out "$raw/$state" --points "$points" --cvar gNewDawn.PostFx.Debug.Stats=1 "$@" > "$raw/$state.log" 2>&1 ||
        echo "WARNING: $state, see $raw/$state.log"
    for f in "$raw/$state"/*.png; do [ -e "$f" ] && mv "$f" "$out/$(basename "${f%.png}")_$state.png"; done
}
hour() { echo "--cvar gNewDawn.Debug.BootHour=$1"; }
frozen="--freeze 60 --wait 24"

# Places lit by the clock, at four times of day.
outdoor=south_clock_town_plaza,termina_field,southern_swamp,southern_swamp_cleared,mountain_village_winter,snowhead,great_bay_coast,great_bay_coast_sea,ikana_canyon,stone_tower
town=north_clock_town,east_clock_town,west_clock_town
cap dawn  $outdoor       $frozen $(hour 6.5)  "$@"
cap noon  $outdoor,$town $frozen $(hour 12.0) "$@"
cap dusk  $outdoor       $frozen $(hour 17.5) "$@"
cap night $outdoor,$town $frozen $(hour 23.0) "$@"
# Places with their own fixed light.
cap day   stock_pot_inn,woodfall_temple,snowhead_temple,stone_tower_temple,great_bay_temple,zora_hall,grotto $frozen $(hour 12.0) "$@"
cap late  stock_pot_inn $frozen $(hour 23.0) "$@"
# Day 2 rain (live: the rain needs the game running; the long wait is to be past the day's opening fade, which an
# earlier 18 s capture sometimes caught) and the final day (noon, and the last hours before the Moon falls).
cap rain       south_clock_town_plaza,termina_field --wait 30 $(hour 12.0) --cvar gNewDawn.Debug.BootDay=2 "$@"
cap finalnoon  south_clock_town_plaza,termina_field $frozen $(hour 12.0) --cvar gNewDawn.Debug.BootDay=3 "$@"
cap finalhours south_clock_town_plaza,termina_field,north_clock_town $frozen $(hour 3.0) --cvar gNewDawn.Debug.BootDay=3 "$@"

# One sheet per place: every state of it in clock order, half size, labelled.
states="dawn noon dusk night day late rain finalnoon finalhours"
for place in $(ls "$out"/*.png | sed -E "s|.*/||; s/_(${states// /|})\.png\$//" | sort -u); do
    files=()
    for s in $states; do [ -e "$out/${place}_$s.png" ] && files+=("$out/${place}_$s.png"); done
    montage -label '%t' "${files[@]}" -resize 960x540 -tile 2x -geometry +4+4 -pointsize 22 \
        -background '#181818' -fill white "$out/sheets/$place.png"
done
echo "GL error lines: $(cat "$raw"/*/logs/*.log 2>/dev/null | grep -c "GL error\|Mesa: error\|GL_INVALID")"
grep -h "WARNING" "$raw"/*.log 2>/dev/null | sort | uniq -c | head -20
echo "done: $out"
