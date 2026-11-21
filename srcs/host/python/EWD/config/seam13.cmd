# seam11.cmd (12 minutes, four corners, FB1, 4 cards) with the video feed
# started LATE: the command line has NO --rtmp, and 'feed on' comes right
# after 'start', so the RTMP stream (and ffmpeg) does not exist during
# bring-up.  Purpose: the Firefox/hls.js freezes have all hit the NO DATA gap
# at bring-up (OPEN_ISSUES.md, video lockup); with the feed born after 'start'
# the player should only ever see a flowing stream and NO DATA should all but
# vanish.  Open the viewer page AFTER the 'feed on' line appears (before that
# the mediamtx page has no stream; refresh once when it starts).  If it still
# freezes mid-run, the cause is the player or mediamtx, not the bring-up gap.
# Same policy, seed, dwell and telemetry as seam11.cmd, so numbers compare
# directly with it (and with honest11 / seam12 if the hub image is the same
# build: check the '.ee' mark).
#   python mfmxctl.py --layout seam4 --boot -s config/seam13.cmd --script-exit --abort-on-error
# Afterwards
#   python3 scripts/aer/aer.py MFMX-<date>-<time> --series 120
#   python3 scripts/xchg/xchg.py MFMX-<date>-<time> --bolts seam13-NN.out
tick slowscan off
policy fourcorners
events honest
decay 4
dwell 4 8
start
feed on
seams
status
cycle start
wait 240
seams
status
wait 240
seams
status
wait 240
seams
status
cycle stop
wait 15
stop
seams
quit close
