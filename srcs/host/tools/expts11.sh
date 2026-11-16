#!/bin/bash
# expts11.sh -- the NoC-leg / e1fwd15 experiment batch for the QuietBox (cards 0 and 1, link 0:1:6, ETH tile X=6).
#
# NEW vs expts10: card B's E1 NoC-writes cars into the L1 of Tensix tile (1,2) of card B at 0x100000..0x120800 (the tool puts that
# tile's RISC-V cores in reset and restores them; nothing else may be using that tile/region).  Edit the T6= line below to change.
#
# Run from anywhere:   bash expts11.sh   (needs bash, not sh).   Takes a few minutes.  Every step WRITES to the cards (--go) but only
# through ethfwd16's allowlisted writes; the tool puts E1 back in reset when it exits (tt-kmd NOC_CLEANUP does so if it dies).
# Steps are chained with && and stop at the FIRST failure with a big banner.  All output goes to ./out11/ (created if needed);
# afterwards:   scp -r ./out11 <you>@<host>:/data/claude/CLAUDES_PLACE/     and tell Claude.
#
# Testing the script itself without hardware:   EXTRA=--mock OUT11=/some/dir sh expts11.sh      (all E1 numbers are then FAKE)
cd "$(dirname "$0")" || { echo "cannot cd to the script directory"; exit 1; }
set -o pipefail
OUT=${OUT11:-out11}
X=${EXTRA:-}
LINK="--link 0:1:6"
T6="--t6 1:2 --t6-addr 0x100000"
T=./ethfwd16
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
         "ethfwd16 resets E1 on exit; to double-check, a dry run prints the E1 state:" \
         "    $T $LINK --image e1fwd/e1fwd14.bin" \
         "Look for 'soft-reset=0x00047000 (E1 in reset)' on both cards.  Then send ./$OUT to Claude."
  tail -n 15 "$OUT/$1.txt" 2>/dev/null
  exit 1
}
# step NAME CMD...   : run CMD, tee all output to out11/NAME.txt, call fail on a nonzero exit (also for tool FAIL results)
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

step 01_build_tool       g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd16.cpp -o ethfwd16 \
&& step 02_build_e1fwd14 sh e1fwd/build.sh e1fwd14 \
&& step 03_build_e1fwd15 sh e1fwd/build.sh e1fwd15 \
&& step 04_dryrun_noc    $T $LINK --image e1fwd/e1fwd15.bin --noc --nch 8 $T6 $X \
\
&& step 10_noc_e14_nch1_csr   $T $LINK --image e1fwd/e1fwd14.bin --go --noc --nch 1 --csr --rounds 200 $T6 $X \
&& step 11_noc_e14_nch2_csr   $T $LINK --image e1fwd/e1fwd14.bin --go --noc --nch 2 --csr --rounds 500 $T6 $X \
&& step 12_noc_e14_nch8_csr   $T $LINK --image e1fwd/e1fwd14.bin --go --noc --nch 8 --csr --rounds 200 $T6 $X \
&& step 13_noc_e14_nch2_fence $T $LINK --image e1fwd/e1fwd14.bin --go --noc --nch 2 --rounds 500 $T6 $X \
\
&& step 20_noc_e15_nch2_plain    $T $LINK --image e1fwd/e1fwd15.bin --go --noc --nch 2 --csr --rounds 500 $T6 $X \
&& step 21_noc_e15_nch2_vcstatic $T $LINK --image e1fwd/e1fwd15.bin --go --noc --nch 2 --csr --vcstatic --rounds 500 $T6 $X \
\
&& step 30_noc_stress4160_e15_plain    $T $LINK --image e1fwd/e1fwd15.bin --go --noc --nch 2 --csr --sizes 4160 --rounds 3000 $T6 $X \
&& step 31_noc_stress4160_e15_vcstatic $T $LINK --image e1fwd/e1fwd15.bin --go --noc --nch 2 --csr --vcstatic --sizes 4160 --rounds 3000 $T6 $X \
\
&& step 40_idle_e15_csr   $T $LINK --image e1fwd/e1fwd15.bin --go --idle --nch 8 --csr $X \
&& step 41_route_e15_csr  $T $LINK --image e1fwd/e1fwd15.bin --go --route --nch 2 --csr $X \
&& step 42_reflect_regen_e15_csr $T $LINK --image e1fwd/e1fwd15.bin --go --reflect --nch 2 --ks 1,2,4 --regen --csr $X \
&& step 43_reflect_latency_e15_csr $T $LINK --image e1fwd/e1fwd15.bin --go --reflect --nch 1 --ks 1,2,4 --sizes 16,64,576,4160 --csr $X \
|| exit 1

{ echo "== RESULT lines =="; grep -H "^RESULT" "$OUT"/[0-9]*.txt;
  echo; echo "== noc test lines =="; grep -H "size .* ch:\|-> \|MISMATCH\|first problem" "$OUT"/[1-3][0-9]_noc*.txt;
  echo; echo "== idle =="; grep -H "nch=" "$OUT"/4[0-9]_idle*.txt;
  echo; echo "== reflect =="; grep -H "ch: .* M hops/s" "$OUT"/4[0-9]_reflect*.txt; } > "$OUT/SUMMARY.txt" 2>&1

banner "ALL STEPS PASSED" "Results are in ./$OUT  (start with $OUT/SUMMARY.txt)" \
       "Next:  scp -r ./$OUT <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude."
cat "$OUT/SUMMARY.txt"
