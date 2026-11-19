# References of run-dvbridge-render.sh

Every file here is what the bridge must give again, byte for byte, on the software GPU (zink on lavapipe) with the
libplacebo and the FFmpeg of the recipes. Where a reference was made by an earlier version of the bridge and the
code of today gives the same bytes, the file is that earlier one. Where only the SHA256 of an earlier file was kept,
today's file was checked against that SHA256 before it was put here.

| File | What it is | Where it comes from |
| --- | --- | --- |
| `fidelity-data.sha256` | the 112 files (layers, `meta.txt`, `meta.bin`) of the 14 pictures that `ref_dump` decodes and pairs from `bl_el.mkv` | the pictures of the fidelity bench as first written; `ref_dump` of today writes them again byte for byte |
| `ccm-ref.sha256` | the composed planes of clause 5.4 (`src/ccm_ref.c`) of those 14 pictures | the same first run of the fidelity bench |
| `f-src0.sums`, `f-src2.sums`, `f-cnv0.sums`, `f-gui.sums` | fidbench in those configurations | the final run of the renderer's work of 1.0 RC4 |
| `f-src1.sums` | fidbench, step-down level 1 | equal by SHA256 to the run of 1.0 RC4 made before the rows of the enhancement layer were read once and kept in half floats |
| `f-resh.sums`, `rt-resh.lines` | libplacebo's own reshaping (fidbench, render test) | since libplacebo-9978 (the rounding of the reshaping, 1.0 RC4), equal by SHA256 to the references made then |
| `f-noel.sums`, `f-src3.sums`, `f-half.sums`, `f-gui1.sums` ... `f-guichk.sums` | fidbench with the GUI cache as Kodi uses it, in 13 more configurations | the final pass of the bridge's tests of 1.0 RC5, whose files of the configurations above are these byte for byte |
| `m.draw` | the drawing lines of the measured test | the final run of the renderer's work of 1.0 RC4 |
| `rt-ccm.lines` | the drawing lines of the render test, composer of CCM 001 | the same run |
| `rscale-el-s0.sums`, `rscale-noel-s0.sums` | the step-down of a picture scaled to the raster, level 0 | equal by SHA256 to the run of 1.0 RC4 that made the step-down of a scaled picture |
| `dvrender-test.lines` | the drawing lines of `dvrender_test.c` | the run of 1.0 RC4 before the finer gamut table, which leaves them as they were |
| `gamut-ident-B.sha256` | 16 pictures as they are, mapped to SDR and to a projector | made by today's code: the 10 mapped pictures changed with "dvbridge: the BT.2390 curve with the knee of ITU-R BT.2390" (projects/Generic/patches/kodi/x86/kodi-9917), found by bisecting the bridge's commits on the PQ chart mapped to SDR; the 6 drawn as they are did not change |
| `p5gui-*.frames` | the frame hashes of `p5gui_test.c` in its seven modes | made by today's code (the earlier ones were drawn with GUI shaders that 1.0 no longer has); held by the test's own checks, by the equality of the modes with the GUI cache on, off, checked and at GUI level 1, and by the mutant of the cache |
