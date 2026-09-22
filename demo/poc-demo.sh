#!/usr/bin/env bash
# PoC demo walkthrough (Phase 1 close-out): the full declare -> trace ->
# SLA -> compile -> run -> record -> lineage loop, one command per beat.
# Run from the repo root. BIN_DIR overrides the build output directory
# (default build/desktop-release/bin); PACE sets the pause between beats
# for screen recording (seconds, default 1.5; PACE=0 runs unpaced).
set -euo pipefail

BIN_DIR=${BIN_DIR:-build/desktop-release/bin}
PROVIDER=$BIN_DIR/libtraceable_flow_provider.so
PACE=${PACE:-1.5}

if [[ ! -x $BIN_DIR/ti ]]; then
	echo "demo: $BIN_DIR/ti not found; build first:" >&2
	echo "  cmake --preset=desktop-release && cmake --build --preset=desktop-release" >&2
	exit 1
fi

# Put the ti-* family on PATH so the unified entry discovers its verbs.
PATH=$(cd "$BIN_DIR" && pwd):$PATH
export PATH

say() {
	printf '\033[1;36m\n== %s ==\033[0m\n' "$1"
	sleep "$PACE"
}

say "0/6 tool family: 'ti' discovers its verbs from PATH"
ti || true

say "1/6 one binary, whole loop: declare -> dry-run trace -> SLA verdict -> compile -> run"
"$BIN_DIR/traceable_flow_demo"

say "2/6 offline compile: ti-compile resolves the flow by name via --flows dlopen"
ti compile demo_traceable --flows "$PROVIDER" --emit-source

say "3/6 launch by flow name: the compiled artifact runs until SIGINT"
timeout --preserve-status -s INT 5 ti launch demo_traceable --flows "$PROVIDER"

say "4/6 record and replay: offline output is bit-identical to live"
"$BIN_DIR/record_replay_demo"

say "5/6 inspect the record: per-message lineage, no publisher code needed"
ti info /tmp/tianshu_record_demo_v2.trec --messages 2 --lineage

say "6/6 WCET calibration: p99.9 x 1.3 suggested back into with_wcet()"
ti info /tmp/tianshu_record_demo_v2.trec --calibrate

say "done: H1 byte-identical, H2 compiled-at-or-better-than-handwritten, H3 calibrated"
