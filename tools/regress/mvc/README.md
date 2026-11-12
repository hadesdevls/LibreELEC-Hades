# h264_mvc regression

The tests of `h264_mvc`, the separate H.264 MVC decoder of `packages/multimedia/ffmpeg/h264-mvc` (Dom Cobley's
series of FFmpeg pull request 24303 on a copy of the stock decoder, plus the fixes of the project): each finding
of the automatic review of that pull request (2026-10-10, on the series head `f5a6ecf95da1`), and what was found
while testing them, against the decoder that the tree builds. Every test is proven in both directions: it fails
on a mutant of the decoder that puts the defect back (`mut.py`), and the ones that the series of August had also
fail on that series (`rev`, the tree at `12728a2`, without the project's fixes).

Host test only: nothing here goes into an image. It reads the trees it tests and never writes in them; all it
makes goes into `$MVC_WORK`.

## The findings and their tests

| # | Finding of the review | In the decoder of the tree | Test (fails on its mutant and on `rev`) |
| --- | --- | --- | --- |
| 1 | flush leaves the field-pair state of a view | series (`ff_h264_view_reset()` clears the whole view context) | `f1reset.c`: no state after `avcodec_flush_buffers()`, and the next switch to the view does not abort; `flushf1.sh`: flushes in the field-coded MVCICT-1 |
| 2 | B lists: inter-view entries appended before the swap of 8.2.4.2.4 | series | `refs-test.c` (`ff_h264_build_ref_list()` itself): `[T, V]` in both lists |
| 3 | a temporal modification matches an inter-view entry by `pic_id` | series | `refs-test.c`: `[T0, T0, T1, V]` |
| 4 | the PPS of a dependent view derived from the SPS that shares its id | fix 9006 | `views.sh` on `f4-*` (scaling matrices in the subset SPS) against JM |
| 5 | a replaced subset SPS leaves the PPS bound to the old one | fix 9006 | `views.sh` on `f5-replace`, `f5-replace-shared` against JM |
| 6 | the first subset SPS of the list taken for the views | fix 9007 | `views.sh` on `f6-views` against JM; `f6.sh`: the view id each picture carries |
| 7 | a second view that starts late is lost with frame threads | fix 9005 | `late.sh`: 1/2/8 frame and 8 slice threads against the decode of the whole stream |
| 8 | level count read with `get_ue_golomb_31()`, counts unbounded | series | `f8parse.c`: the table read to its end with 40 and 64 levels; a count of 2^32-1 refused at once (the series of August spends 36 s on it) |
| 9 | `abs_diff_view_idx_minus1` narrowed and used before its check | series | `f9.sh`: three values out of range refused, with their value; no UBSan report |
| 10 | a prefix NAL unit of four bytes taken for a truncated one | series | `prefix.sh`: no "Failed to parse header of NALU (type 14)" |
| 11 | `mvcC` count of five bits in the mov demuxer | not shipped (`twin.sh` leaves libavformat out); Kodi reads the box | `kodi/mvcc_count_test.cpp`: Kodi reads seven bits, keeps five in `avcC`, refuses a merge that five bits cannot count |
| 12 | FATE registration | not shipped | — |
| 13 | corrective commits later in the series | not a defect of the code that is built | — |
| N1 | an IDR access unit in a packet that begins with the dependent view of the unit before: idr() runs before that view's picture is finished, which puts it back among the references of its view | fix 9009 | `jmlayouts.sh` on `x-sd540x2`, `x-f4x2`, `f5-replace-shared`: both packet layouts against JM (without the fix: 11 of 50 pictures of the second view differ, from the IDR on) |
| N2 | field-coded MVC (MVCICT-1) in that same layout, decoder opened without the reorder depth that probing finds (as Kodi opens it): 225 pictures of the second view for 150 | **open**, known since fix 9002 (its description says so); not a format of Blu-ray 3D (progressive only) | `flush.sh` (reported as known by the verdict); with the probed depth (`jmlayouts.sh`) both layouts are JM's |
| C1 | a concern the review left unverified: film grain not synthesized where the view switch ends the base view picture | series (`ff_h264_field_end()` synthesizes it whatever `in_setup` is) | `grain.sh`: with both views decoded, the base view of a stream with a film grain SEI is the stock decoder's (on the series of August it is not, and differs between runs) |

`views.sh` and `jmlayouts.sh` also run every ITU-T MVC conformance stream and the two streams of the reference
encoder. The one known exception: `MVCDS-6_old`, a retired stream, whose second view always differs from JM
(cause not known); its first view is JM's. One known open defect is reported as such by the verdict, not
passed: N2 above.

## Running it

```
MVC_WORK=<work folder> run-mvc.sh all                     # the tree's decoder
MVC_WORK=<work folder> EXTRAFIX=<patch> run-mvc.sh all        # with a fix that is not in the tree yet
run-mvc.sh <stage>...                                       # base twins streams unit views jmlayouts late prefix
                                                            # f6 f8 f9 grain flushf1 flush mutants kodi verdict
```

The verdict (`results/verdict.txt`) is read line by line from the results of each stage and ends with the count
of failures (exit 1 when there is one). What the inputs are, and the variables that name them (required), is in `env.sh`: the
FFmpeg source of the recipe (checked against `PKG_SHA256`), JM 19.1 (`ldecod`, the reference decoder, built
for Hades with `gcc` only; SHA256 `166f3572…`), the ITU-T H.264.1 MVC bitstreams (zip, SHA256 checked), the two streams
of the reference encoder (SHA256 checked) and, for `kodi/`, a Kodi tree.

The decoders are built as twin.sh builds them, on a small host FFmpeg (`host.sh`): `ship` (the tree), `rev` (the
series that the review read), `ship-san` (AddressSanitizer and UndefinedBehaviorSanitizer), and `mut` for each
mutation. 8 jobs, lowest priority.

## Files

| File | What it is |
| --- | --- |
| `env.sh` | the paths, from the environment or their defaults |
| `run-mvc.sh` | the stages and the verdict |
| `host.sh`, `twin-make.sh` | a host FFmpeg as the image patches it, and a twin tree with h264_mvc |
| `streams.sh`, `craft.py`, `craft22.py`, `h264bits.py`, `bits-selftest.py` | the streams made on purpose (parameter sets, NAL unit headers and slice header lists rewritten; `bits-selftest.py` checks the writer byte for byte on every stream) |
| `views.sh`, `picmd5.py`, `cmp-pics.py` | each view against JM, picture by picture |
| `jmlayouts.sh`, `mvcpack.py`, `mvcmd5.c` | packet by packet as a player feeds it, in two layouts, against JM |
| `unit.sh`, `refs-test.c`, `f1reset.c` | the reference lists and the view reset, at the level of the decoder's functions |
| `late.sh`, `late.py` | a second view that starts late |
| `prefix.sh`, `prefix-sizes.py`, `f6.sh`, `f8.sh`, `f8parse.c`, `f9.sh`, `grain.sh` | findings 10, 6, 8, 9 and the film grain concern |
| `flushf1.sh`, `flush.sh`, `mvcflush.c` | flushes, as at a seek |
| `mut.py`, `mutrun.sh` | the mutations, and a test run on a mutant |
| `mk-fx.sh` | a git repository of the files twin.sh stages, to write a fix on |
| `base2d.py` | a stream of two views made one of one view |
| `kodi/run-kodi.sh`, `kodi/stereo_rate_test.cpp`, `kodi/mvcc_count_test.cpp`, `kodi/mutate_range.py` | Kodi's side: the 3D output at twice the refresh rate and the count of `mvcC` |
