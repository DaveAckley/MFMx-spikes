# physics/ -- loadable bytecode physics (BCP files)

A BCP file is one BytePhys physics: a pointer-free blob with a header
(magic 'BCP1', ISA version, size, checksum) and its code and tables
(BytePhys.h 'BCP', BYTEPHYS.md section 3).  It is picked at start, the
way MFMs loads a .so:

    python mfmxctl.py ... --physics bfb        # or, in a .cmd file,
    physics bfb                                # before 'start'

A bare name means physics/NAME.bcp here; anything else is a path.

- bfb.bcp: written by scripts/bytephys/bpasm.py from scripts/bytephys/
  *.bp (aging, bfb1, bfb4); the same bytes as the EWP's compiled-in
  BP_BCP.  Types 7 (BFB1) and 8 (BFB4).
- dreg.bcp: DReg and Res (scripts/ulamlite/dreg), types 9 (DReg) and
  10 (Res), drawn as darker native DReg / Res.

`bpasm.py --check` (run_native_tests.sh) fails if either is stale.  A
loaded physics replaces the compiled-in one: types it lacks are unknown
that run (erased at their first event).

## How it gets there

- The layout config (config/nu1012.dtoml) says which images take a
  physics: `physics = true` under [image.EWP].
- At 'start', before any code is sent, the host verifies the file with
  BytePhys::verifyBCP (the same check the EWP makes: header, checksum,
  tables, every instruction) and checks that it fits between the end of
  the image and the top of the L1 the images may use (0x120000).  A
  missing, refused or too-big file stops the run there.
- The host writes the blob right after the image in L1, at the end of
  the bin file rounded up to 16.  With no physics chosen it writes four
  zero words there instead, so a blob left from an earlier run is never
  taken.  The bin file and the HostBlock are unchanged.
- The EWP looks at __hostblock_end rounded up to 16 (the same place), at
  its first event that needs bytecode.  A nonzero magic means a loaded
  blob: verify it again, once, and run it; zero means the compiled-in
  physics.  `.bv:TRIED:OPEN:BENCH:TYPES:LOADED:SUM.` reports which
  (LOADED 1 = from the host, SUM = the blob's checksum word); a refused
  blob logs BCPBAD and runs no bytecode.

The microbenchmark (ByteBench.h) stays compiled in.
