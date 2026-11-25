"""machine_seams.py -- Machine methods for the eth-seams and the PKTS.txt
packet-slot geometry."""
import os

from machine_util import ETH_DIRS


class SeamMixin:
    """Machine methods: eth-seams and packet geometry."""

    # ---- PKTS.txt: packet-slot geometry for offline log analysis ----------------
    @staticmethod
    def tlbi_to_noc(t):
        """tlbi -> (x,y) NoC0 coords (UxC.h layout: 14 cols x 10 rows)."""
        row, col = divmod(t, 14)
        x = col + 1 + (2 if col + 1 > 7 else 0)
        return x, row + 2

    def write_pkts(self):
        """Write misc/PKTS.txt (plain text, once per start): per-blockcode car
        geometry and per-tile block addresses of packet-carrying blocks, from
        the deployed images.  Read by scripts/ptrace/ptrace.py."""
        try:
            lines = ["PKTS:V1"]
            bcs, tiles = {}, []
            for bh in self.bhs:
                chip = bh.getChipNumber()
                for t in range(self.t6s):
                    iname = bh.getTLBIImageName(t)
                    if not iname:
                        continue
                    blks = self.im.getT6Image(iname).getPacketBlocks()
                    x, y = self.tlbi_to_noc(t)
                    tiles.append(f"TILE chip={chip} tlbi={t} noc={x},{y} "
                                 f"tag={bh.getTLBITag(t)} img={iname}")
                    for (bn, addr, scnt, ssz, csz, ccnt) in blks:
                        n = bn[3:] if bn.startswith("BC_") else bn
                        bcs[n] = (scnt, ssz, csz, ccnt)
                        tiles.append(f"BLK chip={chip} tlbi={t} bc={n} "
                                     f"addr=0x{addr:x}")
            for n, (scnt, ssz, csz, ccnt) in sorted(bcs.items()):
                lines.append(f"BC name={n} storcnt={scnt} storsz={ssz} "
                             f"carsz={csz} carcnt={ccnt}")
            lines += tiles
            path = f"{self.simDir}misc/PKTS.txt"
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write("\n".join(lines) + "\n")
            return f"PKTS.txt: {len(bcs)} blockcodes, {len(tiles)} tile/block lines"
        except Exception as e:           # diagnostics must never break start()
            return f"PKTS.txt not written: {e!r}"

    # ---- eth-seams (see host/tools/ERISC_SEAM_DESIGN.md section 15) -------------
    def _seam_text(self):
        names = {v: k for k, v in ETH_DIRS.items()}
        return ", ".join(f"bh{a}-{names[d]}->bh{b} eth{','.join(map(str, xs))}"
                         for a, d, b, xs in self.seams)

    def _declare_seams(self):
        """Before ANY chip deploys its T6 images: CodeManager copies each
        chip's seam mask into the HostBlocks at deploy."""
        if not self.has_fire_phaser:
            raise RuntimeError(
                "ethseams need the firePHASER start path (the E1 links must come up "
                "between T6 code deploy and T6 release); this MFMx build lacks it")
        h = self.M.EthSeamHost.get()
        h.clearSeams()
        for a, d4, b, xs in self.seams:
            err = h.declareSeam(a, d4, b, xs)
            if err:
                raise RuntimeError(f"ethseam bh{a}->bh{b}: {err}")
        return f"eth-seams declared: {self._seam_text()}"

    def _bring_up_seams(self):
        """After every chip is deployed, before any T6 is released."""
        err = self.M.EthSeamHost.get().bringUpSeams(self.e1image)
        if err:
            raise RuntimeError(
                f"eth-seam bring-up failed ({err}); T6s were NOT released and no "
                f"link is left up; use 'stop'/'close' to leave the chips cleanly")
        return "eth-seams up:\n" + self.M.EthSeamHost.get().status().rstrip()

    def _down_seams(self):
        """After the T6s are stopped.  Harmless when nothing is up."""
        h = self.M.EthSeamHost.get()
        h.tearDownAll()
        return "eth-seams down"

    def seams_report(self):
        """Declared seams, link status, and per-link-end health counters."""
        if not self.seams:
            return "no ethseams in the active layout"
        h = self.M.EthSeamHost.get()
        lines = [f"declared: {self._seam_text()}",
                 f"e1image: {self.e1image}",
                 h.status().rstrip()]
        for a, _d, b, xs in self.seams:
            for x in xs:
                for chip in (a, b):
                    r = h.health(chip, x)
                    if not r.get("ok"):
                        lines.append(f"  bh{chip} eth{x}: no link")
                    else:
                        lines.append(
                            f"  bh{chip} eth{x}: port={r['port']} hb={r['heartbeat']} "
                            f"retrain={r['retrain']} fec corrected={r['fecCorrected']} "
                            f"uncorrected={r['fecUncorrected']}")
        lines.append(self.seam_counters())
        return "\n".join(lines)

    # E1 channel-block stat words (shared/include/EthSeam.h ChannelWord)
    SEAM_STATS = (("out", 8), ("in", 9), ("noc", 20),
                  ("txfail", 11), ("nocfail", 21), ("partial", 22))

    def seam_counters(self):
        """Per-channel E1 counters of every live link end.
        out = cars sent over the link, in = new cars seen from the link,
        noc = cars NoC-written into the local T6, plus the failure counts."""
        h = self.M.EthSeamHost.get()
        lines = ["channel counters (cars): out=sent to link  in=seen from link  "
                 "noc=written to local T6"]
        total = dict.fromkeys((n for n, _w in self.SEAM_STATS), 0)
        for a, _d, b, xs in self.seams:
            for x in xs:
                for chip in (a, b):
                    for ch in h.channelsOf(chip, x):
                        vals = {n: h.channelStat(chip, x, ch, w)
                                for n, w in self.SEAM_STATS}
                        if any(v > 0 for v in vals.values()):
                            lines.append(f"  bh{chip} eth{x} ch{ch}: " + " ".join(
                                f"{n}={v}" for n, v in vals.items()))
                        for n, v in vals.items():
                            total[n] += max(v, 0)
        lines.append("  total: " + " ".join(f"{n}={v}" for n, v in total.items()))
        return "\n".join(lines)
