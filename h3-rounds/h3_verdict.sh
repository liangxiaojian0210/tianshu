#!/usr/bin/env bash
# =============================================================================
# TIANSHU H3 verdict (ADR-0029 H3)
# =============================================================================
#
# Parses the per-stage p99.9(us) values from the six round archives
# (run-a{1,2,3}.txt = calibration round A, run-b{1,2,3}.txt = verification
# round B, each containing a ti-info --calibrate table) and applies the
# locked two-round predictive-power protocol EXACTLY:
#
#   per stage:
#     A_med  = median(p99.9 of A1, A2, A3)
#     B_med  = median(p99.9 of B1, B2, B3)
#     hard gate: every B_r <= 1.3 * A_med, else FAIL-UNSAFE
#     eps = (max - min) / median over the six values {A1..A3, B1..B3}
#     if hard gate holds:
#       B_med >= A_med                     -> PASS
#       else (A_med - B_med) <= eps*A_med  -> PASS (tie)
#       else                               -> FAIL-CONSERVATIVE
#
#   overall:
#     any stage eps > 0.10                 -> ENV-INVALID (rc 2, no verdict)
#     all stages PASS or PASS (tie)        -> PASS (rc 0)
#     otherwise                            -> FAIL (rc 1)
#
# Usage: bash h3-rounds/h3_verdict.sh   (round files must sit next to it)
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILES=("$DIR/run-a1.txt" "$DIR/run-a2.txt" "$DIR/run-a3.txt"
	"$DIR/run-b1.txt" "$DIR/run-b2.txt" "$DIR/run-b3.txt")
for f in "${FILES[@]}"; do
	if [[ ! -f "$f" ]]; then
		echo "verdict: missing round archive $f" >&2
		exit 4
	fi
done

awk '
FNR == 1 { fileno++ }
/^--- H3 calibration/ { sec = 1; next }
/^--- / { sec = 0 }
sec && /^h3\// { p999[$1, fileno] = $5 + 0; seen[$1]++ }
function med3(a, b, c, m) {
	m = a
	if ((b >= a && b <= c) || (b >= c && b <= a)) m = b
	if ((c >= a && c <= b) || (c >= b && c <= a)) m = c
	return m
}
END {
	n = split("h3/micro/m1 h3/micro/m2 h3/milli/m3 h3/milli/m4 h3/fanin/a h3/fanin/b h3/fanin/j", order, " ")
	# every stage must appear exactly once per round file
	for (i = 1; i <= n; i++) {
		s = order[i]
		for (f = 1; f <= 6; f++) {
			if (!((s SUBSEP f) in p999)) {
				printf "verdict: stage %s missing from round %d\n", s, f | "cat >&2"
				exit 4
			}
		}
	}
	printf "== H3 verdict (two-round predictive power, ADR-0029 H3) ==\n"
	printf "%-14s %8s %8s %8s %9s %8s %8s %8s %9s %7s  %s\n", \
		"stage", "A1", "A2", "A3", "A_med", "B1", "B2", "B3", "B_med", "eps", "verdict"
	env_invalid = 0
	fail = 0
	fail_list = ""
	for (i = 1; i <= n; i++) {
		s = order[i]
		a1 = p999[s,1]; a2 = p999[s,2]; a3 = p999[s,3]
		b1 = p999[s,4]; b2 = p999[s,5]; b3 = p999[s,6]
		amed = med3(a1, a2, a3)
		bmed = med3(b1, b2, b3)
		# six-value spread: eps = (max - min) / median
		for (j = 1; j <= 6; j++) v[j] = (j <= 3) ? p999[s,j] : p999[s,j]
		# insertion sort of the six values
		for (j = 2; j <= 6; j++) {
			k = v[j]
			for (l = j - 1; l >= 1 && v[l] > k; l--) v[l+1] = v[l]
			v[l+1] = k
		}
		med6 = (v[3] + v[4]) / 2.0
		eps = (v[6] - v[1]) / med6
		# hard gate: every B_r must stay within 1.3 x A_med
		hard_ok = (b1 <= 1.3 * amed && b2 <= 1.3 * amed && b3 <= 1.3 * amed)
		if (!hard_ok) {
			verdict = "FAIL-UNSAFE"
		} else if (bmed >= amed) {
			verdict = "PASS"
		} else if ((amed - bmed) <= eps * amed) {
			verdict = "PASS (tie)"
		} else {
			verdict = "FAIL-CONSERVATIVE"
		}
		printf "%-14s %8.1f %8.1f %8.1f %9.1f %8.1f %8.1f %8.1f %9.1f %6.2f%%  %s\n", \
			s, a1, a2, a3, amed, b1, b2, b3, bmed, eps * 100, verdict
		if (eps > 0.10) env_invalid = 1
		if (verdict == "FAIL-UNSAFE" || verdict == "FAIL-CONSERVATIVE") {
			fail = 1
			fail_list = fail_list (fail_list == "" ? "" : ", ") s "=" verdict
		}
	}
	if (env_invalid) {
		printf "\nOVERALL: ENV-INVALID (some stage eps > 10%%; environment not stable enough to judge — no PASS/FAIL verdict issued)\n"
		exit 2
	}
	if (fail) {
		printf "\nOVERALL: FAIL (%s)\n", fail_list
		exit 1
	}
	printf "\nOVERALL: PASS (all 7 stages PASS or PASS (tie), all eps <= 10%%)\n"
	exit 0
}
' "${FILES[@]}"
