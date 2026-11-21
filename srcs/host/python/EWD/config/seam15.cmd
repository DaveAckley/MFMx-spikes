# Report stream RESTART test: seam11 shape (four corners, FB1, 12 min) with
# deliberately lost compressed bytes, to see detection, the restart request,
# the hub's restart and the host's resync work end to end.  Needs the hub image
# and host module with the restart code (ZHostDecompressor, EP_ACacheBlock,
# LiveT1, lzmfmx reset).  Run WITHOUT --rtmp (the video is not needed):
#   python mfmxctl.py --layout seam4 --boot -s config/seam15.cmd --script-exit --abort-on-error
# Expect, in order, for each broken hub (host output and its tile log):
#   ZHD TEST dropped one compressed byte ...      (the injected loss)
#   ZHD FIRST-BAD ... then ZHD DESYNC ...          (about a second later)
#   ZHD RESTART requested (epoch 1)
#   ZHD RESTARTED #1 at ... good reports ...       (the hub answered)
# and in the hub's tile log the marks  .zq:EPOCH:CBYTES:SPIN:HOSTSPIN:HOSTSEQ:CONSEC
# (asked) and  .zd:...:1 (done).  `zhd` should end with desynced 0 and the
# restarts counted; the tiles keep moving on the video.  A second break of the
# same hub later tests the epoch toggle (restart #2).  If a hub never answers,
# `zhd` shows "restart requested, waiting for the hub": that hub's restart
# hung (look at its .zq/.zd marks); `zhd restart off` stops further requests.
# Afterwards
#   grep -h ZHD MFMX-<date>-<time>/misc/*ZHD.txt
#   grep -h "\.z[qd]:" MFMX-<date>-<time>/tiles/at0a7.mfmk
tick slowscan off
policy fourcorners
events honest
decay 4
dwell 4 8
start
seams
status
zhd
cycle start
wait 120
zhd
zhd break at0a7,at323,at2e9
wait 15
zhd
wait 30
zhd
zhd break at0a7
wait 15
zhd
wait 30
zhd all
wait 120
zhd
cycle stop
wait 15
stop
seams
quit close
