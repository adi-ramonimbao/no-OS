#!/usr/bin/env bash
#
# Build every no-OS Maxim board/variant locally, mirroring the CI Maxim matrix
# (build-maxim-platform.yaml) but without cloudsmith_helper.py. Combos are
# discovered through no_os_build.py's own API and built one by one.
#
# SDK selection: exports CFS_PATH so the Maxim toolchain
# (drivers/platform/maxim/toolchain.cmake) picks the CodeFusion Studio SDK
# (GCC 14) ahead of MAXIM_LIBRARIES. Override CFS_PATH in the environment to
# point at a different CFS install, or unset it before running to fall back to
# MAXIM_LIBRARIES / MSDK.
#
# Usage:
#   ./build_all_maxim.sh                 # build all maxim combos
#   CFS_PATH=~/other/cfs ./build_all_maxim.sh
#   BUILD_DIR=/tmp/mx ./build_all_maxim.sh
#   ./build_all_maxim.sh ad4630 iio_demo # optional project-name filters
#
set -u

trap 'echo; echo ">> interrupted; stopping."; exit 130' INT

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO/build-maxim-all}"
LOG_DIR="$BUILD_DIR/logs"
JOBS="$(nproc 2>/dev/null || echo 4)"
JOBS="$(( JOBS / 2 > 0 ? JOBS / 2 : 1 ))"

# Optional positional args restrict the run to those project names.
PROJECT_FILTER=("$@")

# --- SDK selection --------------------------------------------------------
: "${CFS_PATH:=$HOME/analog/cfs/2.3.0}"
if [[ -f "$CFS_PATH/cfs.json" ]]; then
	export CFS_PATH
	echo ">> Using CFS SDK: $CFS_PATH"
elif [[ -n "${MAXIM_LIBRARIES:-}" ]]; then
	unset CFS_PATH
	echo ">> CFS not found; falling back to MAXIM_LIBRARIES: $MAXIM_LIBRARIES"
else
	echo "ERROR: no CFS install at '$CFS_PATH' (missing cfs.json) and" >&2
	echo "       MAXIM_LIBRARIES is not set. Point CFS_PATH at a CFS root" >&2
	echo "       or export MAXIM_LIBRARIES at the MaximSDK/Libraries dir." >&2
	exit 1
fi

mkdir -p "$LOG_DIR"

# --- Discover all maxim combos via no_os_build.py's API -------------------
mapfile -t COMBOS < <(python3 - "$REPO" "${PROJECT_FILTER[@]}" <<'PY'
import sys
from pathlib import Path

repo = Path(sys.argv[1])
name_filter = set(sys.argv[2:])
sys.path.insert(0, str(repo / "tools" / "scripts"))
from no_os_build import load_presets, discover_all_combinations

presets = load_presets(repo)
for c in discover_all_combinations(repo, presets):
	if c["platform"] != "maxim":
		continue
	if name_filter and c["project"] not in name_filter:
		continue
	print(f"{c['project']}\t{c['variant']}\t{c['board']}")
PY
)

if [[ ${#COMBOS[@]} -eq 0 ]]; then
	echo "No maxim combinations found (filter: ${PROJECT_FILTER[*]:-none})." >&2
	exit 1
fi

echo ">> ${#COMBOS[@]} maxim combination(s) to build; logs in $LOG_DIR"
echo

# --- Build loop -----------------------------------------------------------
PASS=(); FAIL=()
i=0
for line in "${COMBOS[@]}"; do
	IFS=$'\t' read -r proj var board <<<"$line"
	i=$(( i + 1 ))
	label="$proj/$var/$board"
	logf="$LOG_DIR/${proj}_${var}_${board}.log"
	# no_os_build writes the real cmake/compiler output here (combo_build_dir).
	bdir="$BUILD_DIR/${proj}-${var}-${board}"

	printf '[%d/%d] %-45s ' "$i" "${#COMBOS[@]}" "$label"
	if python3 "$REPO/tools/scripts/no_os_build.py" build \
		--project "$proj" --variant "$var" --board "$board" \
		--build-dir "$BUILD_DIR" --jobs "$JOBS" --fresh \
		>"$logf" 2>&1; then
		echo "PASS"
		PASS+=("$label")
	else
		# The summary in $logf isn't descriptive; replace it with the build
		# dir's build.log, which carries the actual errors.
		[[ -f "$bdir/build.log" ]] && cp "$bdir/build.log" "$logf"
		echo "FAIL  (see $logf)"
		FAIL+=("$label")
	fi
done

# --- Summary --------------------------------------------------------------
echo
echo "================ SUMMARY ================"
echo "passed: ${#PASS[@]}   failed: ${#FAIL[@]}   total: ${#COMBOS[@]}"
if [[ ${#FAIL[@]} -gt 0 ]]; then
	echo
	echo "failed combinations:"
	for f in "${FAIL[@]}"; do
		echo "  - $f"
	done
	exit 1
fi
echo "all maxim combinations built successfully."
