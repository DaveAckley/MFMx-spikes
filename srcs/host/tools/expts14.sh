#!/bin/bash
# expts14.sh -- e1fwd16 experiments: BH-55 dummy-read workaround A/B, E1 NoC writes over NoC1 (probes), NIU command-buffer dump, link-health soak (ethfwd19) for the QuietBox (cards 0 and 1, link 0:1:6, ETH tile X=6).
#
# expts14 (this file): 1) read-only NIU command-buffer dump and link-health snapshots; 2) BH-55 A/B on the SAME e1fwd16 image (--txqfix vs not):
# idle sweep, reflect speed, route, reflect --regen, T6 ping-pong; 3) NoC1 PROBES: E1's NoC writes over NIU1 into T6 (1,2) (a probe that fails is
# reported but does not stop the batch; NoC1 from an ETH tile is UNPROVEN, a misrouted write lands on another tile or nowhere); the later NoC1 steps
# run only if the first probe worked; 4) two 60 s soaks with link-health deltas (retrain count, FEC counters, base-FW dyn NoC words).
# Every ethfwd19 run prints link health before/after (read-only).  T6 tile 1:2 on both cards is used throughout (free, mirror-safe for NoC1).
#
# NEW vs expts11: the sending/receiving side is now a bare-metal BRISC image on a Tensix tile of EACH card instead of the host.  The tool
# resets those tiles' cores, loads t6pp at L1 0, and restores everything at exit.  Tiles must be FREE (you said all cards are).  The tile pairs
# below are NoC0 coordinates (Tensix columns x 1..7, 10..16; rows y 2..11) and are deliberately spread to flex NoC routes; a harvested tile
# makes the tool exit with code 4 and this script prints a WARNING and CONTINUES (any other failure stops the batch).
#
# Run from anywhere:   bash expts14.sh   (needs bash, not sh).   Takes a few minutes.  Every step WRITES to the cards (--go) but only
# through ethfwd19's allowlisted writes; the tool puts E1 back in reset when it exits (tt-kmd NOC_CLEANUP does so if it dies).
# Steps are chained with && and stop at the FIRST failure with a big banner.  All output goes to ./out14/ (created if needed);
# afterwards:   scp -r ./out14 <you>@<host>:/data/claude/CLAUDES_PLACE/     and tell Claude.
#
# Testing the script itself without hardware:   EXTRA=--mock OUT14=/some/dir sh expts14.sh      (all E1 numbers are then FAKE)
cd "$(dirname "$0")" || { echo "cannot cd to the script directory"; exit 1; }
set -o pipefail
OUT=${OUT14:-out14}
X=${EXTRA:-}
LINK="--link 0:1:6"
IMG="--image e1fwd/e1fwd16.bin --t6-image t6pp/t6pp2.bin"
T=./ethfwd19
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
         "ethfwd19 resets E1 on exit; to double-check, a dry run prints the E1 state:" \
         "    $T $LINK --image e1fwd/e1fwd14.bin" \
         "Look for 'soft-reset=0x00047000 (E1 in reset)' on both cards.  Then send ./$OUT to Claude."
  tail -n 15 "$OUT/$1.txt" 2>/dev/null
  exit 1
}
# step NAME CMD...   : run CMD, tee all output to out14/NAME.txt, call fail on a nonzero exit (also for tool FAIL results)
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

step 01_build_tool       g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd19.cpp -o ethfwd19 \
&& step 02_build_e1fwd16 sh e1fwd/build.sh e1fwd16 \
&& step 03_build_t6pp2   sh t6pp/build.sh t6pp2 \
&& step 04_dump_niu      $T $LINK --dump-niu $X \
&& step 05_dryrun        $T $LINK $IMG --noc --nch 1 $X \
\
&& step 10_idle_plain     $T $LINK $IMG --go --idle --nch 8 --csr $X \
&& step 11_idle_txqfix    $T $LINK $IMG --go --idle --nch 8 --csr --txqfix $X \
&& step 12_reflect_plain  $T $LINK $IMG --go --reflect --nch 2 --ks 4 --sizes 64,576,4160 --csr $X \
&& step 13_reflect_txqfix $T $LINK $IMG --go --reflect --nch 2 --ks 4 --sizes 64,576,4160 --csr --txqfix $X \
&& step 14_route_txqfix   $T $LINK $IMG --go --route --nch 2 --csr --txqfix $X \
&& step 15_reflect_regen_txqfix $T $LINK $IMG --go --reflect --nch 2 --ks 1,2,4 --regen --csr --txqfix $X \
&& step 16_pp_plain       $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 64,4160 --csr --t6-hart nc --nri-buf 3 $X \
&& step 17_pp_txqfix      $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 64,4160 --csr --txqfix --t6-hart nc --nri-buf 3 $X \
|| exit 1

try_step 30_probe_e1_noc1 $T $LINK $IMG --go --noc --nch 1 --rounds 200 --csr --e1-noc 1 --t6 1:2 $X
if [ "${PROBE_OK[30_probe_e1_noc1]}" = 1 ]; then
  banner "NoC1 probe worked: running the larger NoC1 steps"
  step 31_noc_e1_noc1_2ch   $T $LINK $IMG --go --noc --nch 2 --rounds 500 --csr --e1-noc 1 --t6 1:2 $X \
  && step 32_noc_e1_noc1_8ch $T $LINK $IMG --go --noc --nch 8 --rounds 200 --csr --e1-noc 1 --t6 1:2 $X \
  && step 33_pp_e1_noc1      $T $LINK $IMG --go --pp --nch 2 --ks 1,4 --sizes 64,4160 --csr --e1-noc 1 --t6-hart nc --nri-buf 3 $X \
  || exit 1
else
  banner "NoC1 probe FAILED or timed out: E1 writes over NIU1 do not reach the T6 as configured (expected if NoC1 coordinates differ)" "Skipping the other NoC1 steps; see $OUT/30_probe_e1_noc1.txt"
fi

step 40_soak_reflect_60s  $T $LINK $IMG --go --reflect --nch 2 --ks 4 --sizes 576 --dur-ms 60000 --csr $X \
&& step 41_soak_pp_60s    $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 576 --dur-ms 60000 --csr --t6-hart nc --nri-buf 3 $X \
|| exit 1

{ echo "== RESULT lines =="; grep -H "^RESULT\|PROBE" "$OUT"/[0-9]*.txt;
  echo; echo "== NIU dump =="; grep -h "NIU[01] buf" "$OUT/04_dump_niu.txt";
  echo; echo "== link health deltas =="; grep -H "delta over the run" "$OUT"/[1-4][0-9]_*.txt;
  echo; echo "== idle =="; grep -H "nch=" "$OUT"/1[01]_*.txt;
  echo; echo "== reflect speed (plain vs txqfix) =="; grep -H "ch: .* M hops/s" "$OUT"/1[23]_*.txt "$OUT"/40_*.txt;
  echo; echo "== pp =="; grep -H "round trips/s" "$OUT"/1[67]_*.txt "$OUT"/33_*.txt "$OUT"/41_*.txt 2>/dev/null; } > "$OUT/SUMMARY.txt" 2>&1

banner "ALL STEPS DONE" "Results are in ./$OUT  (start with $OUT/SUMMARY.txt)" \
       "Next:  scp -r ./$OUT <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude."
cat "$OUT/SUMMARY.txt"
