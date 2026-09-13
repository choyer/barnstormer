#!/bin/bash
#
# uiflow.sh -- walk the real binary through every screen transition.
#
# main.c's state machine cannot be tested headlessly: it only exists in
# response to real key events, so this types at the game the way a player
# would and checks it is still running, or has quit, at each step.
#
# The sequence is deliberately longer than it looks like it needs to be. A
# bug that put the score screen straight back up after Esc survived testing
# that pressed Esc only once, because the game was still alive either way.
# Only the final "Esc at the title quits" step can tell the two apart, and it
# only gets there by walking the whole path.
#
# Not part of `make test`: it needs a compositor and takes the keyboard for
# about fifteen seconds. Run it with `make test-ui`.
#
# Scores are written to a throwaway XDG_DATA_HOME, never your real board.

set -uo pipefail

BIN="${BIN:-build/barnstormer}"
[[ -x $BIN ]] || BIN="$(dirname "$0")/../build/barnstormer"

skip() { echo "SKIP: $*"; exit 0; }

[[ -n ${WAYLAND_DISPLAY:-} ]] || skip "no WAYLAND_DISPLAY; needs a running session"
command -v wtype >/dev/null || skip "wtype is not installed (pacman -S wtype)"
[[ -x $BIN ]] || { echo "FAIL: $BIN is not built"; exit 1; }

if pgrep -x barnstormer >/dev/null; then
    echo "note: another barnstormer is already running; it may take the keys"
fi

SANDBOX=$(mktemp -d)
export XDG_DATA_HOME="$SANDBOX"
GAME=

cleanup() {
    [[ -n $GAME ]] && kill "$GAME" 2>/dev/null
    rm -rf "$SANDBOX"
}
trap cleanup EXIT

failures=0
step=0

# expect <alive|exited> <what the previous key should have done>
expect() {
    local want=$1 what=$2 got
    kill -0 "$GAME" 2>/dev/null && got=alive || got=exited
    step=$((step + 1))
    if [[ $got == "$want" ]]; then
        printf '  %-38s %s\n' "$what" "ok"
    else
        printf '  %-38s FAILED (%s, wanted %s)\n' "$what" "$got" "$want"
        failures=$((failures + 1))
    fi
}

key() { wtype -k "$1"; sleep "${2:-1.0}"; }

echo "barnstormer UI flow test"

"$BIN" -s -q >/dev/null 2>&1 &
GAME=$!
sleep 2
command -v hyprctl >/dev/null &&
    hyprctl dispatch focuswindow class:barnstormer >/dev/null 2>&1
sleep 0.7

expect alive  "the game starts"
key Escape;   expect alive  "Esc parked at home ends the run"
key Return 1.2; expect alive "Enter plays again"
key Escape;   expect alive  "Esc ends the second run"
key Escape;   expect alive  "Esc leaves the score screen"
key Return 1.2; expect alive "Enter flies from the title"
key Escape;   expect alive  "Esc ends the third run"
key Escape;   expect alive  "Esc returns to the title again"
key Escape 1.2; expect exited "Esc at the title quits"

# Nothing ranked in any of those runs, so nothing should have been written.
if [[ -e $SANDBOX/barnstormer/scores ]]; then
    echo "  a score file was written for an unranked run  FAILED"
    failures=$((failures + 1))
else
    printf '  %-38s %s\n' "no score file for unranked runs" "ok"
fi

# ---- choosing a level ------------------------------------------------------

LVLDIR="$SANDBOX/barnstormer/levels"
mkdir -p "$LVLDIR"
printf 'barnstormer-level 1\nname Test Field\nauthor flow\nsize 3000 200\n\nground 3000:100\n\nrunway 400 0\nrunway 2400 1\n' > "$LVLDIR/test-field.lvl"

"$BIN" -q >/dev/null 2>&1 &
GAME=$!
sleep 2
command -v hyprctl >/dev/null &&
    hyprctl dispatch focuswindow class:barnstormer >/dev/null 2>&1
sleep 0.7

expect alive  "the title comes up"
key Down;     expect alive  "Down reaches the level row"
key Return;   expect alive  "Enter opens the picker"
key Down;     expect alive  "Down moves off the classic map"
key Return;   expect alive  "Enter chooses the level"
key Up;       expect alive  "Up goes back to the modes"
key Return 1.5; expect alive "Enter flies the chosen level"
key Escape;   expect alive  "Esc ends the run"
key Escape;   expect alive  "Esc leaves the summary"
key Escape 1.2; expect exited "Esc at the title quits"

# ---- the level editor ------------------------------------------------------
#
# Same idea, on the other mode the binary has: drive it with the keys a person
# would use and check that what falls out is a level file.

LVL="$SANDBOX/flow.lvl"
"$BIN" --edit "$LVL" -q >/dev/null 2>&1 &
GAME=$!
sleep 2
command -v hyprctl >/dev/null &&
    hyprctl dispatch focuswindow class:barnstormer >/dev/null 2>&1
sleep 0.7

expect alive  "the editor opens on a new file"
key t;        expect alive  "t picks the building tool"
key space;    expect alive  "space places one"
key n;        expect alive  "n opens the name field"
# The field opens with what is already there -- "Flow", from flow.lvl -- so
# clear it the way a person would before typing over it.
for _ in 1 2 3 4 5 6; do wtype -k BackSpace; sleep 0.08; done
wtype "Flow Field"; sleep 0.6
key Return;   expect alive  "Enter keeps the name"
key w;        expect alive  "w writes the file"
key g;        expect alive  "g picks the building up"
# Held, not tapped: the cursor runs while the key is down.
wtype -P Right; sleep 0.6; wtype -p Right; sleep 0.3
key g;        expect alive  "g puts it down further along"
key w;        expect alive  "w writes where it ended up"
key Tab 1.5;  expect alive  "Tab flies the level"
key Tab 1.2;  expect alive  "Tab comes back to editing"
key f;        expect alive  "f flattens, leaving work unsaved"
key Escape;   expect alive  "Esc with work unsaved does not leave"
key Escape 1.2; expect exited "Esc again leaves"

if grep -q '^barnstormer-level 1' "$LVL" 2>/dev/null &&
   grep -q '^target ' "$LVL" 2>/dev/null; then
    printf '  %-38s %s\n' "the editor wrote a level with a building" "ok"
else
    printf '  %-38s FAILED\n' "the editor wrote a level with a building"
    failures=$((failures + 1))
fi
step=$((step + 1))

# It was placed with the cursor at 1500, so a building 16 wide landed at 1492.
# Anything further right means the move tool carried it.
placed=$(awk '/^target /{print $2; exit}' "$LVL" 2>/dev/null)
if [[ -n $placed ]] && (( placed > 1492 )); then
    printf '  %-38s %s\n' "the building moved where it was carried" "ok"
else
    printf '  %-38s FAILED (at %s)\n' "the building moved where it was carried" \
           "${placed:-nothing}"
    failures=$((failures + 1))
fi
step=$((step + 1))

# Typed as "Flow Field", so the case has to have survived the keyboard.
if grep -q '^name Flow Field$' "$LVL" 2>/dev/null; then
    printf '  %-38s %s\n' "with the name that was typed into it" "ok"
else
    printf '  %-38s FAILED\n' "with the name that was typed into it"
    failures=$((failures + 1))
fi
step=$((step + 1))

echo
if (( failures )); then
    echo "FAILURES ($failures of $((step + 1)) checks)"
    exit 1
fi
echo "all ok ($((step + 1)) checks)"
