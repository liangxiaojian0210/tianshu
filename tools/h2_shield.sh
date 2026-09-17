#!/usr/bin/env bash
# =============================================================================
# TIANSHU H2 measurement shield (compiler.md §6.1 protocol)
# =============================================================================
#
# Per ADR-0004, this is an AUXILIARY tool, not a build entry point.
#
# Puts the H2 benchmark rig into a measurement-valid environment and runs
# it for N rounds, then ALWAYS restores the machine state:
#   1. performance governor on every CPU (fixed clock)
#   2. user.slice + system.slice squeezed off the reserved physical core
#      (taskset alone is NOT isolation: affinity says where a task MAY
#      run, not who ELSE may run there; the SMT sibling of the pinned
#      logical CPU shares execution units and L1/L2)
#   3. dedicated cgroup cpuset owning both logical CPUs of the physical
#      core; the benchmark enters the group FIRST, then gets pinned
#      (reverse order -> empty affinity intersection, unschedulable)
#
# Validity criterion: N rounds of p50 must be identical to the digit.
#
# Usage:
#   tools/h2_shield.sh [rounds] [out_prefix]
#   tools/h2_shield.sh 3 /tmp/h2_round       # defaults: 3 rounds
#
# Requires: passwordless sudo, desktop-release build with the
# codegen_vs_handwritten target. Artifacts are expected to be pre-warmed
# (compile once before shielding: a plain unfiltered run fills the cache).

set -euo pipefail

ROUNDS="${1:-3}"
OUT="${2:-/tmp/tianshu-h2-round}"
BENCH_CORES="3,15" # both logical CPUs of one Zen5 physical core
PIN_CORE="3"       # run on the big core, sibling stays exclusive

BIN="$(dirname "$0")/../build/desktop-release/bin/codegen_vs_handwritten"
if [[ ! -x "$BIN" ]]; then
	echo "benchmark binary missing: $BIN (build the desktop-release preset first)" >&2
	exit 1
fi

restore() {
	sudo rmdir /sys/fs/cgroup/tianshu-bench 2>/dev/null || true
	sudo systemctl set-property --runtime user.slice AllowedCPUs=0-23
	sudo systemctl set-property --runtime system.slice AllowedCPUs=0-23
	echo powersave | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null
	echo "restored: slices=0-23 governor=powersave"
}
trap restore EXIT

echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null
sudo systemctl set-property --runtime user.slice AllowedCPUs=0-2,4-14,16-23
sudo systemctl set-property --runtime system.slice AllowedCPUs=0-2,4-14,16-23
sudo mkdir /sys/fs/cgroup/tianshu-bench
echo "$BENCH_CORES" | sudo tee /sys/fs/cgroup/tianshu-bench/cpuset.cpus >/dev/null
echo 0 | sudo tee /sys/fs/cgroup/tianshu-bench/cpuset.mems >/dev/null
echo "shield up: slices=$(cat /sys/fs/cgroup/user.slice/cpuset.cpus) bench=$(cat /sys/fs/cgroup/tianshu-bench/cpuset.cpus)"

for round in $(seq 1 "$ROUNDS"); do
	"$BIN" --benchmark_min_time=1s >"${OUT}${round}.txt" 2>&1 &
	pid=$!
	echo "$pid" | sudo tee /sys/fs/cgroup/tianshu-bench/cgroup.procs >/dev/null
	taskset -pc "$BENCH_CORES" "$pid" >/dev/null 2>&1 || true
	taskset -pc "$PIN_CORE" "$pid" >/dev/null
	wait "$pid"
	echo "round $round done"
done
