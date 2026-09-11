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

echo
if (( failures )); then
    echo "FAILURES ($failures of $((step + 1)) checks)"
    exit 1
fi
echo "all ok ($((step + 1)) checks)"
