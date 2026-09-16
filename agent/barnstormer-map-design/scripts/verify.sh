#!/usr/bin/env bash
#
# verify.sh -- is this a valid Barnstormer map, and can it be flown out of?
#
# Two questions, two tools, one exit status, so a consumer does not have to
# know how either tool reports itself:
#
#   barnstormer --check MAP   is the file a map at all
#   flytest MAP               can every aeroplane get off the ground
#
# Both binaries are found in this order: $BARNSTORMER / $FLYTEST, then
# ./build/ under the repository this script lives in, then $PATH.  Nothing is
# built for you: if flytest is missing, `make flytest` makes it.
#
#   verify.sh MAP...          human-readable, one block per map
#   verify.sh --json MAP...   one JSON object per line
#
# Exit: 0 every map is valid and flyable, 1 one of them is not,
#       2 the tools could not be found (nothing was checked).

set -uo pipefail

json=0
case "${1:-}" in
  --json) json=1; shift ;;
  -h|--help)
    sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 0 ;;
esac

if [[ $# -eq 0 ]]; then
  echo "usage: verify.sh [--json] MAP..." >&2
  exit 2
fi

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"          # skill/scripts -> skill -> agent -> repo

find_tool() {                                  # $1 = name, $2 = override
  if [[ -n ${2:-} ]]; then
    command -v "$2" >/dev/null 2>&1 && { command -v "$2"; return 0; }
    [[ -x $2 ]] && { printf '%s\n' "$2"; return 0; }
    return 1
  fi
  [[ -x "$root/build/$1" ]] && { printf '%s\n' "$root/build/$1"; return 0; }
  command -v "$1" >/dev/null 2>&1 && { command -v "$1"; return 0; }
  return 1
}

game=$(find_tool barnstormer "${BARNSTORMER:-}") || {
  echo "verify.sh: no barnstormer binary (set \$BARNSTORMER, or run make)" >&2
  exit 2
}
fly=$(find_tool flytest "${FLYTEST:-}") || {
  echo "verify.sh: no flytest binary (set \$FLYTEST, or run make flytest)" >&2
  exit 2
}

# JSON string escaping for the few characters that can appear in a path or a
# loader message.
esc() { printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\t/ /g'; }

rc=0
for map in "$@"; do
  check_out=$("$game" --check "$map" 2>&1); check_rc=$?
  fly_out=""; fly_rc=0
  if [[ $check_rc -eq 0 ]]; then
    fly_out=$("$fly" "$map" 2>&1); fly_rc=$?
  fi

  ok=0
  [[ $check_rc -eq 0 && $fly_rc -eq 0 ]] && ok=1
  [[ $ok -eq 1 ]] || rc=1

  if [[ $json -eq 1 ]]; then
    printf '{"map":"%s","ok":%s,"valid":%s,"flyable":%s,"check":"%s","fly":"%s"}\n' \
      "$(esc "$map")" \
      "$([[ $ok -eq 1 ]] && echo true || echo false)" \
      "$([[ $check_rc -eq 0 ]] && echo true || echo false)" \
      "$([[ $check_rc -eq 0 && $fly_rc -eq 0 ]] && echo true || echo false)" \
      "$(esc "$(printf '%s' "$check_out" | tr '\n' ';')")" \
      "$(esc "$(printf '%s' "$fly_out" | tr '\n' ';')")"
  else
    if [[ $ok -eq 1 ]]; then
      echo "$map: valid and flyable"
      printf '%s\n' "$check_out" | sed 's/^/  /'
    elif [[ $check_rc -ne 0 ]]; then
      echo "$map: NOT A MAP"
      printf '%s\n' "$check_out" | sed 's/^/  /'
    else
      echo "$map: valid, but NOT FLYABLE"
      printf '%s\n' "$fly_out" | sed 's/^/  /'
    fi
  fi
done

exit $rc
