#!/bin/bash
# expts16.sh -- T6 -> ERISC over NoC1 (t6pp3 + ethfwd22 --t6-noc) for the QuietBox (cards 0 and 1, link 0:1:6, ETH tile X=6).
#
# QUESTION: does a Tensix NoC write INTO an ETH tile work over NIU1 (NoC1) when the ETH node is addressed correctly?  expts15 (out15) delivered
# nothing: it passed NoC0 (6,1), which on NoC1 is the tile E13.  Rows Y=0/1 (PCIe/ARC/ETH) are NOT translated, so X there is the raw coordinate
# of the NoC in use; NoC1 mirrors raw X (x1 = 16 - x0), Y is unchanged (tt-isa-documentation BlackholeA0/NoC/Coordinates.md).  ethfwd22 sends
# (16-6,1) = (10,1) with --t6-noc 1.  Tensix tiles use the shared translated coordinates (NIU cfg 0x6005 bit 14) and need no flip.
# Same stage-3a rig as expts12..15: a bare-metal image on a Tensix tile of EACH card sends cars to its local E1 (the leg under test), the E1 ships
# them over the link, the peer E1 NoC-writes them into the peer tile where the image reflects them.  Every car carries a checkable payload.
#
# SAFETY: the tiles must be FREE and mfmxctl must not be running.  The probe is ONE channel, ONE 64 B car, one second.  If the node is still wrong,
# the worst case is that single 64 B write into L1 0x10000 of another tile; the tool REFUSES the unflipped (expts15) node over NIU1, and a failed
# probe STOPS the batch (no variants are tried on their own).  The control step (NoC0) runs first and must pass.  E1 is returned to reset on exit.
# Steps: build; read-only NIU dump; dry run (prints the node it will use); CONTROL (T6 writes over NoC0); the PROBE (T6 writes over NoC1); if it
# worked: sizes 64/576/4160, two channels, E1 replies over NoC0 and NoC1, three tile pairs over the Tensix rows, and a 60 s soak.
# Harvested T6 tiles exit with code 4 (WARNING, continues).
# Run from anywhere:   bash expts16.sh   (bash, not sh).   A few minutes.   Output in ./out16/ of the directory you ran it from (SUMMARY.txt first); afterwards
#   scp -r ./out16 <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude.
# Testing the script without hardware:   EXTRA=--mock OUT16=/some/dir bash expts16.sh      (all numbers are then FAKE)
HERE=$PWD   # where the user ran us: output goes here, not next to the script
cd "$(dirname "$0")" || { echo "cannot cd to the script directory"; exit 1; }
set -o pipefail
OUT=${OUT16:-$HERE/out16}
X=${EXTRA:-}
LINK="--link 0:1:6"
IMG="--image e1fwd/e1fwd16.bin --t6-image t6pp/t6pp3.bin"
T=./ethfwd22
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
         "ethfwd22 resets E1 on exit; to double-check, a dry run prints the E1 state:" \
         "    $T $LINK --image e1fwd/e1fwd14.bin" \
         "Look for 'soft-reset=0x00047000 (E1 in reset)' on both cards.  Then send ./$OUT to Claude."
  tail -n 15 "$OUT/$1.txt" 2>/dev/null
  exit 1
}
# step NAME CMD...   : run CMD, tee all output to out16/NAME.txt, call fail on a nonzero exit (also for tool FAIL results)
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

PPC="--nch 2 --ks 4 --sizes 64,576,4160 --csr --t6-hart nc --nri-buf 3"   # common ping-pong options (as expts14 steps 16-17)

step 01_build_tool       g++ -std=c++17 -O2 -Wall -Wextra -Werror ethfwd22.cpp -o ethfwd22 \
&& step 02_build_e1fwd16 sh e1fwd/build.sh e1fwd16 \
&& step 03_build_t6pp3   sh t6pp/build.sh t6pp3 \
&& step 04_dump_niu      $T $LINK --dump-niu $X \
&& step 05_dryrun        $T $LINK $IMG --pp --nch 1 --t6-noc 1 $X \
&& step 10_control_noc0  $T $LINK $IMG --go --pp $PPC $X \
|| exit 1

try_step 20_probe_t6_noc1 $T $LINK $IMG --go --pp --nch 1 --ks 1 --sizes 64 --csr --t6-hart nc --nri-buf 3 --t6-noc 1 $X
if [ "${PROBE_OK[20_probe_t6_noc1]}" = 1 ]; then
  banner "T6 -> ETH over NoC1 WORKED in the probe: running the matrix"
  step 21_t6noc1_e1noc0       $T $LINK $IMG --go --pp $PPC --t6-noc 1 $X \
  && step 22_t6noc1_e1noc1    $T $LINK $IMG --go --pp $PPC --t6-noc 1 --e1-noc 1 $X \
  && soft_step 23_upper_row   $T $LINK $IMG --go --pp $PPC --t6-noc 1 --t6a 3:2 --t6b 3:2 $X \
  && soft_step 24_mid_row     $T $LINK $IMG --go --pp $PPC --t6-noc 1 --t6a 5:6 --t6b 5:6 $X \
  && soft_step 25_lower_row   $T $LINK $IMG --go --pp $PPC --t6-noc 1 --t6a 12:10 --t6b 12:10 $X \
  && soft_step 26_lower_ctrl  $T $LINK $IMG --go --pp $PPC --t6a 12:10 --t6b 12:10 $X \
  && step 27_soak_noc1_60s    $T $LINK $IMG --go --pp --nch 2 --ks 4 --sizes 576 --dur-ms 60000 --csr --t6-hart nc --nri-buf 3 --t6-noc 1 $X \
  || exit 1
else
  banner "T6 -> ETH over NoC1 FAILED or timed out in the probe (check that the dry run printed NIU1 raw (10,1) for ETH tile NoC0 (6,1); if so the ETH tile does not accept NoC1 writes from the Tensix tiles)" \
         "Skipping the matrix; see $OUT/20_probe_t6_noc1.txt and the NIU dump in $OUT/04_dump_niu.txt"
fi

{ echo "== RESULT / PROBE lines =="; grep -H "^RESULT\|PROBE" "$OUT"/[0-9]*.txt;
  echo; echo "== NIU dump =="; grep -h "NIU[01] buf" "$OUT/04_dump_niu.txt"; echo; echo "== ETH node the T6 image used =="; grep -H "T6 -> ETH node\|REFUSED" "$OUT"/[0-9]*.txt;
  echo; echo "== link health deltas =="; grep -H "delta over the run" "$OUT"/[1-2][0-9]_*.txt;
  echo; echo "== pp (control step 10 and 26 are NoC0; 20-25, 27 are T6 over NoC1 with the ETH node mirrored) =="; grep -H "round trips/s" "$OUT"/1[0-9]_*.txt "$OUT"/2[0-9]_*.txt 2>/dev/null; } > "$OUT/SUMMARY.txt" 2>&1

banner "ALL STEPS DONE" "Results are in ./$OUT  (start with $OUT/SUMMARY.txt)" \
       "Next:  scp -r ./$OUT <you>@<host>:/data/claude/CLAUDES_PLACE/    and tell Claude."
cat "$OUT/SUMMARY.txt"
