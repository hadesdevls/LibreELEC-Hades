# CCM 001 composer oracle

An independent oracle of the composer of ETSI GS CCM 001 V1.1.1 (2017-02), clause 5.4, and a
harness that runs it on real Dolby Vision profile 7 FEL pictures and compares it, sample by sample,
with the planes that libplacebo's composer hands Kodi's Dolby Vision bridge (`fidbench`, on Mesa
zink/lavapipe), and with the tables that libplacebo builds on the CPU (`pl_dovi_ccm_prepare()`).

The oracle (`ccm001_oracle.py`) was written from the text of the specification only, before any
other implementation of the clause was read. Each step is a literal transcription of the clause's
pseudo-code in 64-bit integer arithmetic, in two forms that the self test compares: a scalar form
with Python integers that checks that every intermediate value fits in a signed 64-bit integer, and
a numpy `int64` form used on whole pictures, guarded by an upper bound of its products computed from
the coefficients before it runs.

Host test only: nothing here goes into an image. It reads the trees it tests and never writes in
them.

## Files

| File | What it is |
| --- | --- |
| `ccm001_oracle.py` | the oracle: `selftest`, `lut`, `compose`, `list` (readings and mutations) |
| `rpu_sets.c` | prints the composer parameters of every picture's RPU (FFmpeg `AV_FRAME_DATA_DOVI_METADATA`) as JSON lines |
| `rpu_groups.py` | groups them into distinct composer parameter sets; summary, picture index |
| `oracle_select.py` | the pictures to draw: one per distinct set, spread ones, one per mapping shape, NLQ extremes |
| `oracle_compare.py` | the oracle against `fidbench`'s composed planes of a picture (`picture`), the negative control (`formula`), RPU of a dumped picture against the scan (`scanmatch`) |
| `lutcheck.c`, `oracle_luts.py` | libplacebo's tables against the oracle for every distinct RPU |
| `meta_edit.c`, `oracle_synth.py` | pictures made on purpose from a real one (changed composing metadata) for the readings the real RPUs do not exercise |
| `pack_sources.py` | the bridge's packing shaders as C strings, as Kodi's build embeds them |
| `oracle_verdict.py` | the verdict, counted line by line from the log (with its own self test) |
| `run-oracle.sh` | the whole run |

## Running it

```
run-oracle.sh all        # build scan select selftest luts pictures mutate lpmut, then the verdict
run-oracle.sh <stage>    # one stage
run-oracle.sh verdict [log]
```

It ends with `ORACLE-DONE n items, p passed, f failed, r not run` and `ORACLE-VERDICT PASSED|FAILED`
(`logs/verdict.txt`); every check prints one `ORACLE-ITEM <name>: PASSED|FAILED|NOT-RUN | <detail>` line
into `logs/run.log`. The verdict also fails a stage that started and did not end, a malformed item line
and an item name given twice.

Settings (environment; those without a default are required, the data and tools are not in this repository): `ORACLE_WORK` (work folder),
`ORACLE_KODI_GIT`/`ORACLE_KODI_REF` (Kodi tree and ref, its `tools/dvbridge` by `git archive`),
`ORACLE_LE_GIT`/`ORACLE_LE_REF` (LibreELEC tree and ref: libplacebo's version, `PKG_SHA256` and patch
series), `ORACLE_LP_TARBALL_DIRS` (where the libplacebo tarball is; its SHA256 must be `PKG_SHA256`),
`ORACLE_FFMPEG` (FFmpeg 9.0.2 with the image's DV patches, for the RPU scan), `ORACLE_FFMPEG_BENCH`
(an FFmpeg with the `dovi_split` filter, for `ref_dump` and `fidbench`), `ORACLE_FIDTOOLS` and
`ORACLE_FIDBENCH` (the fidelity bench's `ref_dump.c`, `layers.h`, `upload.h` and `fidbench.c`),
`ORACLE_GLW` (GLES headers and shim generator), `ORACLE_VASTUB` (a libva stub), `ORACLE_SAMPLES`,
`ORACLE_CLIPS`, `ORACLE_SPREAD` (12), `ORACLE_JOBS` (3 oracle processes, about 0.9 GB each),
`ORACLE_MEM` (3G: the memory scope of each software-renderer run), `ORACLE_KEEP` (keep the layers).

The bench is run as Kodi runs the bridge by default: the composer of CCM 001, the source transport,
step-down level 0 (annex B.3 filters), `DV_MEM_LIMIT_MB=2500`, `glFinish()` after each picture. A sub-list
of pictures is drawn again without the enhancement layer (`FB_NOEL=1`) and with libplacebo's MMR table
(`FB_MMRT=1`, `dvbridge_render_use_mmr_table()`, off by default in Kodi).

## What is compared, and what is not

- **Compared exactly:** h of 5.4.3.3 (12 bits here) for the three planes of every drawn picture, as
  the bridge's composer hands them (`fidbench`'s `ccm_{y,cb,cr}.u16`), against the oracle on the same
  decoded layers and parsed RPU. Without the EL, h = (v + 8) >> 4: the mapped planes v at 12 bits.
  The 16-bit v of every polynomial piece and the r of NLQ for every EL code are compared exactly through
  libplacebo's tables, for every distinct RPU of the clips.
- **Rows the bridge does not compose:** it composes only the rows its later filters read; rows left at
  0 by the bench where the oracle has something are skipped only when they lie outside the active area
  of the RPU's level 5 block (a row or column inside it is compared as it is, and fails).
- **Not compared:** clause 5.5 (BT.1886 base layers; the samples are PQ), 8-bit layers, partitions,
  `coef_data_type` 1 (floating point coefficients), the step-down levels 1-3 (they replace annex B.3
  filters on purpose), what happens after the composer (chroma up-sampling, colour conversion, the
  transport). Annex B.3 is informative: an EL resampled otherwise is not a fault of clause 5.4.
- **The RPU:** `ref_dump`'s FFmpeg parses the RPU of each dumped picture; the harness checks that it
  is the composer set that FFmpeg with the image's DV patches parsed for that picture number.

## Readings of the text

Line numbers refer to the plain-text extraction of the PDF (`ccm001.txt`, 1895 lines; PDF SHA256
`c711442055e73c88be8380317c388d5d9f1992a0fd1fc4bc1f549566a4685b2c`); the PDF page is given too. Where a
reading has a real alternative, `--reading` selects it; `ccm001_oracle.py list` prints them.

- **R1 — a sample at or above the last pivot (5.4.2.2, p. 17, lines 622-629).** The loop leaves
  `pivot_idx = num_pivots_minus2 + 1` when `s >= pivot_value[num_pivots_minus2 + 1]`. Only
  `num_pivots_minus2 + 1` pieces have a `mapping_idc` and coefficients (5.3.3), so that index names no
  piece. The mapping clamps such a sample to the last pivot (5.4.2.3.2 lines 670-671; 5.4.2.3.3 lines
  758-767), the upper end of the last piece, so the last piece is used. No other reading gives
  coefficients; mutation `top-first` shows that the harness would see another choice.
- **R1b — equal pivots (5.4.2.2).** `s < pivot_value[idx + 1]` is strict: a piece whose two pivots are
  equal (`0 0 1023`) is never chosen, and a sample equal to an inner pivot belongs to the upper piece.
  Mutation `pivot-le` uses `<=`.
- **R2 — axes of the luma down-sampling (5.4.2.3.3, p. 18, lines 696-721).** The formula is set in math
  glyphs that the text extraction loses; read from the PDF's glyph runs it is
  `s*[i][j] = (((s[2i-1][2j] + 2*s[2i][2j] + s[2i+1][2j] + 2) >> 2) + ((s[2i-1][2j+1] + 2*s[2i][2j+1] + s[2i+1][2j+1] + 2) >> 2) + 1) >> 1`,
  with pixel replication at the picture boundary (line 721). The arrays are "(PicWidth)x(PicHeight)"
  blocks (5.4.2.1 lines 584-590) and a block is "MxN (M-column by N-row)" (3.1), so the first index
  runs along the width: `i` is the column (default `ds-axes=i-is-column`): a 1-2-1 filter across
  columns 2i-1..2i+1 and the average of rows 2j and 2j+1 (the chroma position of 4:2:0 type 0).
  Alternative `i-is-row`.
- **R3 — grouping of that formula.** The brackets put `>> 2` after each three-tap sum and `+ 1`, `>> 1`
  outside both; read with the left-to-right rule of 3.2.6 for operators of equal precedence, each row
  sum is rounded (`+2 >> 2`) and the two are averaged (`+1 >> 1`). Alternative `ds-round=joint`:
  `(A + B + 4) >> 3`.
- **R4 — MMR coefficient indices (5.3.3.3 lines 524-532; 5.4.2.3.3 lines 803-810).** `mmr_coef[i][j]` is
  the `j`-th coefficient of order `i` (1..`MMR_order`); the sum runs order-major (`cnt` 1..`7*MMR_order`).
  FFmpeg stores it as `mmr_coef[piece][i - 1][j]`. Mutation `mmr-transpose` sums term-major.
- **R5 — fixed-point coefficients (5.3.3.2 line 509, 5.3.3.3 lines 522 and 531, 5.3.4 lines 545, 555 and
  564).** `fp = (int << coefficient_log2_denom) + frac`. FFmpeg's parser (`libavcodec/dovi_rpudec.c`,
  `get_se_coef()`, `get_ue_coef()`) returns `(ipart << denom) | frac`: the same value, the low `denom`
  bits of the shifted integer part being zero. Only `coef_data_type` 0 is covered by the clause.
- **R6 — garbled definitions (3.2.5, p. 9-10, lines 288-311).** `Clip3(x, y, z)` is `x` when `z < x`,
  `y` when `z > y`, else `z`. `Short()` is not used by clause 5.4.
- **R7 — enhancement layer size (clause 4 NOTE 1, lines 355-359; annex B.3, p. 37, lines 1626-1641).**
  Resampling is not normative and happens before clause 5.4. For an EL of a quarter of the BL size the
  oracle applies annex B.3: vertical first, then horizontal, pixel replication at the edges,
  `Clip3(0, 0xFFFF, …)`, on the EL code words before 5.4.3.2 (B.3: "the same data type as the input").
  `el-up=none` takes an EL already at BL size.
- **R8 — output bit depth (5.4.3.3 lines 906-910).** The BL is SMPTE ST 2084: `out_bit_depth =
  hdr_bit_depth` (FFmpeg `vdr_bit_depth`, 12 here); clause 5.5 does not apply.
- **R9 — MMR inputs (5.4.2.3.3 lines 688-767).** The luma is down-sampled from the BL luma before any
  clamp; then `s0` is clamped with the luma pivots and `s1`, `s2` with their own (lines 756-767). The
  pivot index of an MMR sample comes from the chroma sample with its component's pivots (5.4.2.1 steps
  1-2, 5.4.2.2). Mutation `mmr-ownclamp` clamps `s0` with the chroma pivots.
- **R10 — polynomial chroma (5.4.2.3.2).** A polynomial chroma piece maps the chroma sample at chroma
  resolution, without luma.
- **R11 — shifts (3.2.2).** `>>` of a negative value is arithmetic (rounds toward minus infinity).
- **R12 — no residual (5.3.2 lines 445-449; 5.4.3.1 lines 822-824).** With `disable_residual_flag` 1, or
  without an EL picture, nothing is added, whatever the EL holds. FFmpeg reports `nlq_method_idc = -1`
  when the RPU has no NLQ syntax; no residual then either.
- **R13 — pivots as FFmpeg gives them.** `pivot_value` is the running sum of `pred_pivot_value` (5.3.2
  lines 462-471); FFmpeg clips the running sum to 16 bits, which differs only above 65535 (impossible
  with valid 8- or 10-bit pivots).

## Proving the harness

- `ccm001_oracle.py selftest`: hand-computed values of every step, the scalar and vectorised forms
  against each other on random parameter sets and on real ones, the identity.
- Mutations (`ccm001_oracle.py list`): each changes one aspect of the reading; `run-oracle.sh mutate`
  requires every one of them to make the comparison with the bridge report a difference on at least
  one picture, real or made on purpose (`oracle_synth.py`: inner pivots hit by samples, samples below
  the first and above the last pivot, an NLQ threshold and clamp, MMR of order 1), and each made
  picture to detect the mutations it was made for.
- A negative control: the identity polynomial with `disable_residual_flag` 1 and the EL picture
  present, whose result is known (`h = s << 2`), compared with the bridge and with the formula.
- `run-oracle.sh lpmut`: a fault put on purpose into a copy of libplacebo (`<=` in `pivot_index()`)
  must be seen by the picture comparison and by the table comparison.
- `oracle_verdict.py --selftest`: the verdict on logs whose answer is known (all passed, a failed, a
  not run, malformed lines, a name twice, a stage that did not end or did not run, an empty log).
