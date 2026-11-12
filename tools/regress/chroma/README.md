# The chroma of the Dolby Vision transport

`chroma_bench.py` measures how far the 12-bit 4:2:2 signal that LibreELEC Hades sends for Dolby Vision is from a full
4:4:4 picture, in delta E ITP (ITU-R BT.2124), as a display rebuilds the right pixel of each pair (`hold`: the left
pixel's colour repeated; `linear`; `cubic`: (-1, 9, 9, -1) / 16, the up-sampling of ETSI GS CCM 001 annex C).

It does so for both signals of `videoscreen.hadesdvtransport`:
- `ipt`: IPT-PQ-c2 (`0`), an expert option. The packer computes I for every pixel, and P and T from the left pixel
  of each pair (pass 9 of Kodi's `tools/dvbridge/dvbridge_source.c`). It uses the matrices of Kodi's
  `tools/dvbridge/dvbridge_ipt.h`, which go out in the DM header, and the display decodes with them.
- `ycc`: the composer's YCbCr as it is ("As composed", `2`, the default), with the matrices of the RPU.

The chroma of a pair is made in four ways:
- `transport`: the bridge's own bytes;
- `even`: the left pixel's, emulated from the reference;
- `annexc`: (1, 6, 1) / 8 centred on the left pixel, the down-sampling of annex C, which is informative and written for
  clause 5.5;
- `mean`: the mean of the pair, what the bridge sent before 2026-10-15.

Usage:
- `python3 chroma_bench.py selftest` checks the measure in its two directions:
  - a flat picture gives 0;
  - a chroma shifted by one pixel gives more than ten times the rounding of 12 bits;
  - IPT-PQ-c2 codes of 12 bits come back within the rounding, and a decoder that leaves out the crosstalk does not.
- `python3 chroma_bench.py synth` measures the synthetic pictures, in both signals:
  - chroma of a stream at its own Nyquist frequency;
  - one-pixel columns of colour and coloured strokes (detail that only menus and subtitles drawn over the picture have);
  - gradients.
- `python3 chroma_bench.py real3 <picture.f32> <composed.rgba> <ipt.rgba> <ccm_y.u16> <ccm_cb.u16> <ccm_cr.u16> [l,r,t,b]`
  measures a real picture, from `fidbench` of the oracle harness:
  - the reference is libplacebo's own 4:4:4 conversion (`FB_SOURCE=0 FB_FLOATS=1`);
  - the transports are the bridge's (`FB_SOURCE=1`, and with `FB_IPT=1`, the switch `dvbridge_render_use_ipt()`), with the
    composer's 4:2:0 planes;
  - `l,r,t,b` are the margins of the active picture (fidbench's log), outside which the transport is black.

  First it checks the bytes:
  - the luma of the transport as composed against the composer's, and its chroma on the even rows against the
    composer's 4:2:0 samples;
  - the IPT-PQ-c2 transport against IPT-PQ-c2 computed here from the codes of the one as composed, for the I of the
    left pixels and P and T of every pair.

  Then it gives the delta E of both transports and of the emulated ways, over the active picture. It prints one JSON
  line a way and display.
