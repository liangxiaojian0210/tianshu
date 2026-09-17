#!/usr/bin/env bash
# One-off H2 R1 perf attribution driver (NOT a repo tool).
# Usage: h2_perf.sh <filter_regex> <stat|record> <out_prefix> [duration_s]
# Puts the bench in the §6.1 shield, then attaches perf as an outside
# observer: stat -> per-process counters; record -> system-wide cycles
# sampling on the shielded core only.
set -euo pipefail

FILTER="$1"
MODE="$2"
OUT="$3"
DUR="${4:-14}"
MINT="${MIN_TIME:-10s}"
BIN=/home/foliage/ring/github.com/lykling/tianshu/build/desktop-release/bin/codegen_vs_handwritten
BENCH_CORES=3,15
PIN_CORE=3

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
echo "shield up (bench=${BENCH_CORES} pin=${PIN_CORE})"

BENCHMARK_FILTER="$FILTER" "$BIN" --benchmark_min_time="$MINT" >"${OUT}.bench.txt" 2>&1 &
pid=$!
echo "$pid" | sudo tee /sys/fs/cgroup/tianshu-bench/cgroup.procs >/dev/null
taskset -pc "$BENCH_CORES" "$pid" >/dev/null 2>&1 || true
taskset -pc "$PIN_CORE" "$pid" >/dev/null

case "$MODE" in
stat)
	# shellcheck disable=SC2024  # redirect runs as the invoking user into a
	# user-owned path; sudo only wraps perf itself
	sudo perf stat -d -x',' -p "$pid" -- sleep "$DUR" >"${OUT}.stat.txt" 2>&1 || true
	;;
record)
	sudo perf record -F 3000 -e cycles -C "$PIN_CORE" -o "${OUT}.perf.data" -- sleep "$DUR" 2>"${OUT}.record.log" || true
	sudo chown "$(id -u)" "${OUT}.perf.data" 2>/dev/null || true
	;;
esac

wait "$pid" || true
echo "done: ${OUT}"
