#!/bin/bash
# expts12.sh -- stage 3a: bare-metal T6 ping-pong across the seam (t6pp image + ethfwd17 --pp) for the QuietBox (cards 0 and 1, link 0:1:6, ETH tile X=6).
#
# NEW vs expts11: the sending/receiving side is now a bare-metal BRISC image on a Tensix tile of EACH card instead of the host.  The tool
# resets those tiles' cores, loads t6pp at L1 0, and restores everything at exit.  Tiles must be FREE (you said all cards are).  The tile pairs
# below are NoC0 coordinates (Tensix columns x 1..7, 10..16; rows y 2..11) and are deliberately spread to flex NoC routes; a harvested tile
# makes the tool exit with code 4 and this script prints a WARNING and CONTINUES (any other failure stops the batch).
#
# Run from anywhere:   bash expts12.sh   (needs bash, not sh).   Takes a few minutes.  Every step WRITES to the cards (--go) but only
# through ethfwd17's allowlisted writes; the tool puts E1 back in reset when it exits (tt-kmd NOC_CLEANUP does so if it dies).
# Steps are chained with && and stop at the FIRST failure with a big banner.  All output goes to ./out12/ (created if needed);
# afterwards:   scp -r ./out12 <you>@<host>:/data/claude/CLAUDES_PLACE/     and tell Claude.
#
# Testing the script itself without hardware:   EXTRA=--mock OUT12=/some/dir sh expts12.sh      (all E1 numbers are then FAKE)
cd "$(dirname "$0")" || { echo "cannot cd to the script directory"; exit 1; }
set -o pipefail
OUT=${OUT12:-out12}
X=${EXTRA:-}
LINK="--link 0:1:6"
IMG="--image e1fwd/e1fwd15.bin --t6-image t6pp/t6pp.bin"
T=./ethfwd17
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
         "ethfwd17 resets E1 on exit; to double-check, a dry run prints the E1 state:" \
         "    $T $LINK --image e1fwd/e1fwd14.bin" \
         "Look for 'soft-reset=0x00047000 (E1 in reset)' on both cards.  Then send ./$OUT to Claude."
  tail -n 15 "$OUT/$1.txt" 2>/dev/null
  exit 1
}
# step NAME CMD...   : run CMD, tee all output to out12/NAME.txt, call fail on a nonzero exit (also for tool FAIL results)
step() {
  local name=$1; shift
  banner "STEP $name" "$*"
  echo "# $*" > "$OUT/$name.txt"
  if ! { "$@" 2>&1 | tee -a "$OUT/$name.txt"; }; then fail "$name"; fi
  sleep 1
}

# soft_step NAME CMD... : like step, but exit code 4 (requested T6 tile harvested/unavailable) is only a loud WARNING
soft_step() {
  local name=$1; shift
  banner "STEP $name (tile-tolerant)" "$*"
  echo "# $*" > "$OUT/$name.txt"
  "$@" 2>&1 | tee -a "$OUT/$name.txt"
  local rc=${PIPESTATUS[0]}
  if [ "$rc" = 4 ]; then
    banner "WARNING: step $name SKIPPED: a requested T6 tile is harvested/unavailable (tool exit code 4)" "Nothing was started; continuing with the next step."
    echo "SKIPPED (tile unavailable)" >> "$OUT/$name.txt"
  elif [ "$rc" != 0 ]; then fail "$name"; fi
  sleep 1
}
# ------------------------------------------------------------------------------------------------------------------------------
{ echo "date: $(date -Is)"; echo "host: $(uname -a)"; echo "git: $(git rev-parse HEAD 2>&1) $(git status --short 2>&1 | grep -v '^??' | head -5)";
  echo "gcc: $(g++ --version | head -1)"; echo "extra args: '$X'"; } > "$OUT/00_env.txt"

step 01_build_tool        g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd17.cpp -o ethfwd17 \
&& step 02_build_e1fwd15  sh e1fwd/build.sh e1fwd15 \
&& step 03_build_t6pp     sh t6pp/build.sh \
&& step 04_dryrun_pp      $T $LINK $IMG --pp --nch 2 $X \
\
&& step 10_pp_first_1ch_1car  $T $LINK $IMG --go --pp --nch 1 --ks 1 --sizes 64 --dur-ms 500 --csr $X \
&& step 11_pp_1ch_sizes_k     $T $LINK $IMG --go --pp --nch 1 --ks 1,2,4 --sizes 16,64,576,4160 --csr $X \
&& step 12_pp_2ch             $T $LINK $IMG --go --pp --nch 2 --ks 1,4 --sizes 64,576,4160 --csr $X \
&& step 13_pp_8ch             $T $LINK $IMG --go --pp --nch 8 --ks 4 --sizes 64,4160 --csr $X \
&& step 14_pp_verify_every_car $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 576,4160 --ppve 1 --csr $X \
&& step 15_pp_fence_e1        $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 64,4160 $X \
\
&& soft_step 20_flex_near_1x2_1x2     $T $LINK $IMG --go --pp --t6a 1:2  --t6b 1:2  --nch 1 --ks 1,4 --sizes 64,4160 --csr $X \
&& soft_step 21_flex_near_7x2_7x2     $T $LINK $IMG --go --pp --t6a 7:2  --t6b 7:2  --nch 1 --ks 1,4 --sizes 64,4160 --csr $X \
&& soft_step 22_flex_far_16x11_16x11  $T $LINK $IMG --go --pp --t6a 16:11 --t6b 16:11 --nch 1 --ks 1,4 --sizes 64,4160 --csr $X \
&& soft_step 23_flex_cross_1x11_16x2  $T $LINK $IMG --go --pp --t6a 1:11 --t6b 16:2  --nch 1 --ks 1,4 --sizes 64,4160 --csr $X \
&& soft_step 24_flex_mid_10x6_4x6     $T $LINK $IMG --go --pp --t6a 10:6 --t6b 4:6   --nch 1 --ks 1,4 --sizes 64,4160 --csr $X \
&& soft_step 25_flex_far_16x11_1x2_8ch $T $LINK $IMG --go --pp --t6a 16:11 --t6b 1:2 --nch 8 --ks 4 --sizes 4160 --csr $X \
|| exit 1

{ echo "== RESULT lines =="; grep -H "^RESULT\|SKIPPED" "$OUT"/[0-9]*.txt;
  echo; echo "== pp lines =="; grep -H "TEST pp\|round trips/s\|payload checks\|-> " "$OUT"/[1-2][0-9]_*.txt; } > "$OUT/SUMMARY.txt" 2>&1

banner "ALL STEPS PASSED (or were skipped for harvested tiles)" "Results are in ./$OUT  (start with $OUT/SUMMARY.txt)" \
       "Next:  scp -r ./$OUT <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude."
cat "$OUT/SUMMARY.txt"
