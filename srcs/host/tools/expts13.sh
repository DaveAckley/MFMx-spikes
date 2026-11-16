#!/bin/bash
# expts13.sh -- stage 3a with hart / NIU-buffer probing (t6pp2 image + ethfwd18 --pp): which hart and cmd buffer can drive the seam? for the QuietBox (cards 0 and 1, link 0:1:6, ETH tile X=6).
#
# expts13 FIRST PROBES small variants (hart B or NC x NIU command buffer) with a single car; a failing probe is reported but does NOT stop
# the batch.  The first working variant, in the order NC/3 (what MFMx does), B/3, NC/0, B/0, B/1, B/2, is then used for the full suite.
# If NO variant works the batch stops with a banner.
# NEW vs expts11: the sending/receiving side is now a bare-metal BRISC image on a Tensix tile of EACH card instead of the host.  The tool
# resets those tiles' cores, loads t6pp at L1 0, and restores everything at exit.  Tiles must be FREE (you said all cards are).  The tile pairs
# below are NoC0 coordinates (Tensix columns x 1..7, 10..16; rows y 2..11) and are deliberately spread to flex NoC routes; a harvested tile
# makes the tool exit with code 4 and this script prints a WARNING and CONTINUES (any other failure stops the batch).
#
# Run from anywhere:   bash expts13.sh   (needs bash, not sh).   Takes a few minutes.  Every step WRITES to the cards (--go) but only
# through ethfwd18's allowlisted writes; the tool puts E1 back in reset when it exits (tt-kmd NOC_CLEANUP does so if it dies).
# Steps are chained with && and stop at the FIRST failure with a big banner.  All output goes to ./out13/ (created if needed);
# afterwards:   scp -r ./out13 <you>@<host>:/data/claude/CLAUDES_PLACE/     and tell Claude.
#
# Testing the script itself without hardware:   EXTRA=--mock OUT13=/some/dir sh expts13.sh      (all E1 numbers are then FAKE)
cd "$(dirname "$0")" || { echo "cannot cd to the script directory"; exit 1; }
set -o pipefail
OUT=${OUT13:-out13}
X=${EXTRA:-}
LINK="--link 0:1:6"
IMG="--image e1fwd/e1fwd15.bin --t6-image t6pp/t6pp2.bin"
T=./ethfwd18
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
         "ethfwd18 resets E1 on exit; to double-check, a dry run prints the E1 state:" \
         "    $T $LINK --image e1fwd/e1fwd14.bin" \
         "Look for 'soft-reset=0x00047000 (E1 in reset)' on both cards.  Then send ./$OUT to Claude."
  tail -n 15 "$OUT/$1.txt" 2>/dev/null
  exit 1
}
# step NAME CMD...   : run CMD, tee all output to out13/NAME.txt, call fail on a nonzero exit (also for tool FAIL results)
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
# try_step NAME CMD... : a probe.  Records success/failure in PROBE_OK[NAME] and never stops the batch.
declare -A PROBE_OK
try_step() {
  local name=$1; shift
  banner "PROBE $name" "$*"
  echo "# $*" > "$OUT/$name.txt"
  "$@" 2>&1 | tee -a "$OUT/$name.txt"
  local rc=${PIPESTATUS[0]}
  if [ "$rc" = 0 ]; then PROBE_OK[$name]=1; echo "PROBE $name: WORKED" | tee -a "$OUT/$name.txt"
  else PROBE_OK[$name]=0; banner "PROBE $name FAILED (rc=$rc)  -- continuing with the other probes" "$OUT/$name.txt"; echo "PROBE FAILED rc=$rc" >> "$OUT/$name.txt"; fi
  sleep 1
}
# ------------------------------------------------------------------------------------------------------------------------------
{ echo "date: $(date -Is)"; echo "host: $(uname -a)"; echo "git: $(git rev-parse HEAD 2>&1) $(git status --short 2>&1 | grep -v '^??' | head -5)";
  echo "gcc: $(g++ --version | head -1)"; echo "extra args: '$X'"; } > "$OUT/00_env.txt"

step 01_build_tool        g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd18.cpp -o ethfwd18 \
&& step 02_build_e1fwd15  sh e1fwd/build.sh e1fwd15 \
&& step 03_build_t6pp2    sh t6pp/build.sh t6pp2 \
&& step 04_dryrun_pp      $T $LINK $IMG --pp --nch 1 --t6-hart nc $X \
|| exit 1

PROBE="$T $LINK $IMG --go --pp --nch 1 --ks 1 --sizes 64 --dur-ms 500 --csr"
try_step 10_probe_nc_buf3 $PROBE --t6-hart nc --nri-buf 3 $X
try_step 11_probe_b_buf3  $PROBE --t6-hart b  --nri-buf 3 $X
try_step 12_probe_nc_buf0 $PROBE --t6-hart nc --nri-buf 0 $X
try_step 13_probe_b_buf0  $PROBE --t6-hart b  --nri-buf 0 $X
try_step 14_probe_b_buf1  $PROBE --t6-hart b  --nri-buf 1 $X
try_step 15_probe_b_buf2  $PROBE --t6-hart b  --nri-buf 2 $X

BEST=""
for v in "10_probe_nc_buf3|--t6-hart nc --nri-buf 3" "11_probe_b_buf3|--t6-hart b --nri-buf 3" "12_probe_nc_buf0|--t6-hart nc --nri-buf 0" \
         "13_probe_b_buf0|--t6-hart b --nri-buf 0" "14_probe_b_buf1|--t6-hart b --nri-buf 1" "15_probe_b_buf2|--t6-hart b --nri-buf 2"; do
  n=${v%%|*}; if [ "${PROBE_OK[$n]}" = 1 ]; then BEST=${v#*|}; BESTNAME=$n; break; fi
done
{ echo "== probes =="; for n in 10_probe_nc_buf3 11_probe_b_buf3 12_probe_nc_buf0 13_probe_b_buf0 14_probe_b_buf1 15_probe_b_buf2; do echo "$n: ${PROBE_OK[$n]:-not run} (1 = worked)"; done; } > "$OUT/PROBES.txt"
cat "$OUT/PROBES.txt"
if [ -z "$BEST" ]; then
  banner "NO HART / CMD-BUFFER VARIANT WORKED" "All six probes failed.  See $OUT/1[0-5]_*.txt and send ./$OUT to Claude.  The full suite was NOT run."
  exit 1
fi
banner "USING VARIANT: $BEST   (from probe $BESTNAME)" "The full suite below runs with these options."

step 20_pp_1ch_sizes_k     $T $LINK $IMG --go --pp --nch 1 --ks 1,2,4 --sizes 16,64,576,4160 --csr $BEST $X \
&& step 21_pp_2ch          $T $LINK $IMG --go --pp --nch 2 --ks 1,4 --sizes 64,576,4160 --csr $BEST $X \
&& step 22_pp_8ch          $T $LINK $IMG --go --pp --nch 8 --ks 4 --sizes 64,4160 --csr $BEST $X \
&& step 23_pp_verify_every_car $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 576,4160 --ppve 1 --csr $BEST $X \
&& step 24_pp_fence_e1     $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 64,4160 $BEST $X \
\
&& soft_step 30_flex_near_1x2_1x2     $T $LINK $IMG --go --pp --t6a 1:2  --t6b 1:2  --nch 1 --ks 1,4 --sizes 64,4160 --csr $BEST $X \
&& soft_step 31_flex_near_7x2_7x2     $T $LINK $IMG --go --pp --t6a 7:2  --t6b 7:2  --nch 1 --ks 1,4 --sizes 64,4160 --csr $BEST $X \
&& soft_step 32_flex_far_16x11_16x11  $T $LINK $IMG --go --pp --t6a 16:11 --t6b 16:11 --nch 1 --ks 1,4 --sizes 64,4160 --csr $BEST $X \
&& soft_step 33_flex_cross_1x11_16x2  $T $LINK $IMG --go --pp --t6a 1:11 --t6b 16:2  --nch 1 --ks 1,4 --sizes 64,4160 --csr $BEST $X \
&& soft_step 34_flex_mid_10x6_4x6     $T $LINK $IMG --go --pp --t6a 10:6 --t6b 4:6   --nch 1 --ks 1,4 --sizes 64,4160 --csr $BEST $X \
&& soft_step 35_flex_far_16x11_1x2_8ch $T $LINK $IMG --go --pp --t6a 16:11 --t6b 1:2 --nch 8 --ks 4 --sizes 4160 --csr $BEST $X \
|| exit 1

{ echo "== variant used: $BEST (probe $BESTNAME) =="; cat "$OUT/PROBES.txt";
  echo; echo "== RESULT lines =="; grep -H "^RESULT\|SKIPPED\|PROBE" "$OUT"/[0-9]*.txt;
  echo; echo "== pp lines =="; grep -H "TEST pp\|round trips/s\|payload checks\|-> " "$OUT"/[1-3][0-9]_*.txt; } > "$OUT/SUMMARY.txt" 2>&1

banner "ALL STEPS PASSED (or were skipped for harvested tiles)" "Results are in ./$OUT  (start with $OUT/SUMMARY.txt)" \
       "Next:  scp -r ./$OUT <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude."
cat "$OUT/SUMMARY.txt"
