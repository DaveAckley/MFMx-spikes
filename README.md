# MFMx-spikes

This is a repo to store livcomp's early experiments with a Tenstorrent
Blackhole QuietBox. It uses the Tenstorrent `tt-kmd` kernel module,
but _does not_ use the `tt-umd` userspace code. 

Which was probably a big mistake but here we are.

## Warning

livcomp makes NO REPRESENTATION that this code is usable for
_anything_ including instruction. In fact, livcomp explicitly DECLARES
and REPRESENTS that This Is Bad Code.

## Notes

LICENSE is the BSD license of the pybind11 example project this repo
was started from (pybind/scikit_build_example); the example's README
text that used to follow here was removed on 2026-11-28 (`git show
pre-cleanup:README.md`).
