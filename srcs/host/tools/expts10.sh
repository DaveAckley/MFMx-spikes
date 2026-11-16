#!/bin/bash
# expts10.sh -- the e1fwd13/e1fwd14 experiment batch for the QuietBox (cards 0 and 1, link 0:1:6, ETH tile X=6).
#
# Run from anywhere:   bash expts10.sh   (needs bash, not sh).   Takes a few minutes.  Every step WRITES to the cards (--go) but only
# through ethfwd15's allowlisted writes; the tool puts E1 back in reset when it exits (tt-kmd NOC_CLEANUP does so if it dies).
# Steps are chained with && and stop at the FIRST failure with a big banner.  All output goes to ./out10/ (created if needed);
# afterwards:   scp -r ./out10 <you>@<host>:/data/claude/CLAUDES_PLACE/     and tell Claude.
#
# Testing the script itself without hardware:   EXTRA=--mock OUT10=/some/dir sh expts10.sh      (all E1 numbers are then FAKE)
cd "$(dirname "$0")" || { echo "cannot cd to the script directory"; exit 1; }
set -o pipefail
OUT=${OUT10:-out10}
X=${EXTRA:-}
LINK="--link 0:1:6"
T=./ethfwd15
mkdir -p "$OUT" || { echo "cannot create $OUT"; exit 1; }
rm -f "$OUT"/*.txt

banner() {
  echo
  echo "################################################################################"
  for l in "$@"; do echo "####  $l"; done
  echo "################################################################################"
}
fail() {   # $1 = step name
  banner "EXPERIMENT FAILED: $1" \
         "Output of the failing step: $OUT/$1.txt   (last lines below)" \
         "STOPPING: later experiments were NOT run.  Do not rerun blindly." \
         "ethfwd15 resets E1 on exit; to double-check, a dry run prints the E1 state:" \
         "    $T $LINK --image e1fwd/e1fwd14.bin" \
         "Look for 'soft-reset=0x00047000 (E1 in reset)' on both cards.  Then send ./$OUT to Claude."
  tail -n 15 "$OUT/$1.txt" 2>/dev/null
  exit 1
}
# step NAME CMD...   : run CMD, tee all output to out10/NAME.txt, call fail on a nonzero exit (also for tool FAIL results)
step() {
  local name=$1; shift
  banner "STEP $name" "$*"
  echo "# $*" > "$OUT/$name.txt"
  if ! { "$@" 2>&1 | tee -a "$OUT/$name.txt"; }; then fail "$name"; fi
  sleep 1
}

# ------------------------------------------------------------------------------------------------------------------------------
{ echo "date: $(date -Is)"; echo "host: $(uname -a)"; echo "git: $(git rev-parse HEAD 2>&1) $(git status --short 2>&1 | grep -v '^??' | head -5)";
  echo "gcc: $(g++ --version | head -1)"; echo "extra args: '$X'"; } > "$OUT/00_env.txt"

step 01_build_tool       g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd15.cpp -o ethfwd15 \
&& step 02_build_e1fwd13 sh e1fwd/build.sh e1fwd13 \
&& step 03_build_e1fwd14 sh e1fwd/build.sh e1fwd14 \
&& step 04_dryrun        $T $LINK --image e1fwd/e1fwd14.bin $X \
\
&& step 10_idle_e13_fence $T $LINK --image e1fwd/e1fwd13.bin --go --idle --nch 8 $X \
&& step 11_idle_e14_fence $T $LINK --image e1fwd/e1fwd14.bin --go --idle --nch 8 $X \
&& step 12_idle_e13_csr   $T $LINK --image e1fwd/e1fwd13.bin --go --idle --nch 8 --csr $X \
&& step 13_idle_e14_csr   $T $LINK --image e1fwd/e1fwd14.bin --go --idle --nch 8 --csr $X \
\
&& step 20_route_e14_fence_nch2 $T $LINK --image e1fwd/e1fwd14.bin --go --route --nch 2 $X \
&& step 21_route_e14_csr_nch2   $T $LINK --image e1fwd/e1fwd14.bin --go --route --nch 2 --csr $X \
&& step 22_route_e14_csr_nch8   $T $LINK --image e1fwd/e1fwd14.bin --go --route --nch 8 --csr --rounds 200 $X \
\
&& step 30_reflect_regen_e13_fence $T $LINK --image e1fwd/e1fwd13.bin --go --reflect --nch 2 --ks 1,2,4 --regen $X \
&& step 31_reflect_regen_e14_fence $T $LINK --image e1fwd/e1fwd14.bin --go --reflect --nch 2 --ks 1,2,4 --regen $X \
&& step 32_reflect_regen_e14_csr   $T $LINK --image e1fwd/e1fwd14.bin --go --reflect --nch 2 --ks 1,2,4 --regen --csr $X \
&& step 33_reflect_regen_e14_csr_nch8 $T $LINK --image e1fwd/e1fwd14.bin --go --reflect --nch 8 --ks 4 --regen --csr $X \
\
&& step 40_reflect_speed_e13_fence $T $LINK --image e1fwd/e1fwd13.bin --go --reflect --nch 2 --ks 4 $X \
&& step 41_reflect_speed_e14_fence $T $LINK --image e1fwd/e1fwd14.bin --go --reflect --nch 2 --ks 4 $X \
&& step 42_reflect_speed_e13_csr   $T $LINK --image e1fwd/e1fwd13.bin --go --reflect --nch 2 --ks 4 --csr $X \
&& step 43_reflect_speed_e14_csr   $T $LINK --image e1fwd/e1fwd14.bin --go --reflect --nch 2 --ks 4 --csr $X \
|| exit 1

{ echo "== RESULT lines =="; grep -H "^RESULT" "$OUT"/[0-9]*.txt;
  echo; echo "== idle sweep rates =="; grep -H "nch=" "$OUT"/1[0-9]_*.txt;
  echo; echo "== reflect =="; grep -H "ch: .* M hops/s" "$OUT"/[34][0-9]_*.txt; } > "$OUT/SUMMARY.txt" 2>&1

banner "ALL STEPS PASSED" "Results are in ./$OUT  (start with $OUT/SUMMARY.txt)" \
       "Next:  scp -r ./$OUT <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude."
cat "$OUT/SUMMARY.txt"
