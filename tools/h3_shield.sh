#!/usr/bin/env bash
# =============================================================================
# TIANSHU H3 measurement shield (WCET measurement rounds, ADR-0029 H3)
# =============================================================================
#
# Per ADR-0004, this is an AUXILIARY tool, not a build entry point.
#
# Same isolation recipe as tools/h2_shield.sh (read that first), adapted
# for the H3 WCET rig:
#   1. performance governor on every CPU (fixed clock)
#   2. user.slice + system.slice squeezed off the reserved physical core
#      (taskset alone is NOT isolation: affinity says where a task MAY
#      run, not who ELSE may run there; the SMT sibling of the pinned
#      logical CPU shares execution units and L1/L2)
#   3. dedicated cgroup cpuset owning both logical CPUs of the physical
#      core; the rig enters the group FIRST, then gets pinned
#      (reverse order -> empty affinity intersection, unschedulable)
#
# Differences from h2: runs h3_wcet_rig (not codegen_vs_handwritten) with
# arbitrary extra args passed through verbatim, exactly ONE run per
# invocation, and captures into ONE output file, in order:
#   env preamble (host / nproc / load / governor / shield state),
#   the rig stdout+stderr, and the ti-info --calibrate table of the
#   record this run produced (the shield runs ti-info itself; when the
#   rig was given --declare, the same vector is fed to --wcet so the
#   table carries DECLARED/RATIO columns and the drift verdict).
#
# Usage:
#   tools/h3_shield.sh <out.txt> [rig args...]
#   tools/h3_shield.sh h3-rounds/run-a1.txt --out /tmp/opencode/h3-a1.trec
#
# Requires: passwordless sudo, desktop-release build with h3_wcet_rig and
# ti-info. Warm the machine first (one unshielded default run fills the
# page/path caches). Exit code: rig rc if nonzero, else ti-info rc
# (0 clean / 3 drift / 2 usage / 4 record missing).

set -euo pipefail

if [[ $# -lt 1 ]]; then
	echo "usage: tools/h3_shield.sh <out.txt> [rig args...]" >&2
	echo "  rig args are passed through to h3_wcet_rig verbatim" >&2
	exit 2
fi

OUT_FILE=$1
shift
RIG_ARGS=("$@")

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RIG_BIN="$REPO_ROOT/build/desktop-release/bin/h3_wcet_rig"
TI_INFO="$REPO_ROOT/build/desktop-release/bin/ti-info"
for b in "$RIG_BIN" "$TI_INFO"; do
	if [[ ! -x "$b" ]]; then
		echo "binary missing: $b (build the desktop-release preset first)" >&2
		exit 1
	fi
done

# Locate --out <trec> and --declare <vector> in the passthrough args so
# the shield calibrates exactly what this run recorded (and re-checks
# the declared vector against it, when one was given).
TREC=""
DECLARE_VEC=""
for ((i = 0; i < ${#RIG_ARGS[@]}; i++)); do
	case "${RIG_ARGS[$i]}" in
	--out) TREC="${RIG_ARGS[$((i + 1))]:-}" ;;
	--declare) DECLARE_VEC="${RIG_ARGS[$((i + 1))]:-}" ;;
	esac
done
if [[ -z "$TREC" ]]; then
	echo "rig args must include --out <path.trec> (shield calibrates that record)" >&2
	exit 2
fi

BENCH_CORES="3,15" # both logical CPUs of one physical core
PIN_CORE="3"       # run on one logical CPU, sibling stays exclusive
CGROUP=/sys/fs/cgroup/tianshu-bench-h3
NPROC=$(nproc)
ALL_CPUS="0-$((NPROC - 1))"
SQUEEZE_CPUS=$(
	ids=()
	for ((c = 0; c < NPROC; c++)); do
		if [[ ",$BENCH_CORES," == *",$c,"* ]]; then
			continue
		fi
		ids+=("$c")
	done
	IFS=,
	echo "${ids[*]}"
)

# shellcheck disable=SC2317  # invoked via the EXIT trap below
restore() {
	sudo rmdir "$CGROUP" 2>/dev/null || true
	sudo systemctl set-property --runtime user.slice AllowedCPUs="$ALL_CPUS"
	sudo systemctl set-property --runtime system.slice AllowedCPUs="$ALL_CPUS"
	echo powersave | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null
	echo "restored: slices=$ALL_CPUS governor=powersave"
}
trap restore EXIT

t_start=$(date +%s)

echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null
sudo systemctl set-property --runtime user.slice AllowedCPUs="$SQUEEZE_CPUS"
sudo systemctl set-property --runtime system.slice AllowedCPUs="$SQUEEZE_CPUS"
sudo mkdir "$CGROUP"
echo "$BENCH_CORES" | sudo tee "$CGROUP/cpuset.cpus" >/dev/null
echo 0 | sudo tee "$CGROUP/cpuset.mems" >/dev/null
echo "shield up: slices=$(cat /sys/fs/cgroup/user.slice/cpuset.cpus) bench=$(cat "$CGROUP/cpuset.cpus")" >&2

# --- env preamble: archive of the measurement-valid environment ------
{
	echo "== H3 shield env preamble =="
	echo "date: $(date -Iseconds)"
	echo "host: $(hostname) [$(uname -srmo)]"
	echo "nproc: $NPROC"
	echo "loadavg: $(cat /proc/loadavg)"
	echo "governor: $(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort -u | paste -sd,) on all $NPROC CPUs"
	echo "shield: bench_cores=$BENCH_CORES pin_core=$PIN_CORE (enter cgroup first, then pin)"
	echo "user.slice AllowedCPUs: $(cat /sys/fs/cgroup/user.slice/cpuset.cpus)"
	echo "system.slice AllowedCPUs: $(cat /sys/fs/cgroup/system.slice/cpuset.cpus)"
	echo "cgroup tianshu-bench-h3 cpuset.cpus: $(cat "$CGROUP/cpuset.cpus")"
	echo "rig: $RIG_BIN ${RIG_ARGS[*]}"
	echo "calibrate: $TI_INFO $TREC --calibrate${DECLARE_VEC:+ --wcet $DECLARE_VEC}"
	echo ""
} >"$OUT_FILE"

# --- rig run: enter the cgroup FIRST, then pin (h2 order) ------------
"$RIG_BIN" "${RIG_ARGS[@]}" >>"$OUT_FILE" 2>&1 &
pid=$!
echo "$pid" | sudo tee "$CGROUP/cgroup.procs" >/dev/null
taskset -pc "$BENCH_CORES" "$pid" >/dev/null 2>&1 || true
taskset -pc "$PIN_CORE" "$pid" >/dev/null
rig_rc=0
wait "$pid" || rig_rc=$?

# --- calibrate exactly what this run recorded ------------------------
echo "" >>"$OUT_FILE"
echo "--- ti-info --calibrate (run by shield on this run's record) ---" >>"$OUT_FILE"
ti_rc=0
if [[ -f "$TREC" ]]; then
	ti_args=(--calibrate)
	[[ -n "$DECLARE_VEC" ]] && ti_args+=(--wcet "$DECLARE_VEC")
	"$TI_INFO" "$TREC" "${ti_args[@]}" >>"$OUT_FILE" 2>&1 || ti_rc=$?
else
	echo "ti-info: record $TREC missing (rig failed?)" >>"$OUT_FILE"
	ti_rc=4
fi

t_end=$(date +%s)
{
	echo ""
	echo "rig rc=$rig_rc  ti-info rc=$ti_rc  shield wall=$((t_end - t_start))s"
} >>"$OUT_FILE"

echo "done: $OUT_FILE (rig rc=$rig_rc, ti-info rc=$ti_rc, $((t_end - t_start))s)" >&2
if [[ $rig_rc -ne 0 ]]; then
	ti_rc=$rig_rc
fi
exit "$ti_rc"
