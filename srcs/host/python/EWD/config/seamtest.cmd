# Eth-seam wiring check: two cards joined east/west, T6s idle across the seam.
#   python mfmxctl.py --layout seamtest --boot -s config/seamtest.cmd --script-exit
# (boot runs tt-smi -r unless --no-chip-reset)
start
seams
status
wait 30
seams
wait 30
seams
stop
seams
quit close
