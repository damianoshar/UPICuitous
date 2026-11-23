# WP1–WP6 audit (2026-11-22, 0.35 tree)

Scope: verify the historical UPIC work packages as they stand in the 0.35
working tree. Verification used the full release gate:

```
app selftest 1470/1470   legacy fixture 8BF28ABA7975…
plugin 0.35.0            smoke 22/22            pluginval 5 SUCCESS
```

Executed WP1–WP6 check groups: frequency tables 19, amplitude tables 22,
groups 41, sequences 42, modulation 28, channels 29.

## Status by work package

| WP | Model | Engine | UI | Tests | Status |
|---|---|---|---|---|---|
| WP1 frequency tables | `FreqSpaceDef`, `spaceTables` omitted at default, 16 384 cap | `pageLuts` → `luts->lut[…]`, identity bit-exact, stepped/inverted | `FreqTableEditor` + Table… | 19 | complete |
| WP2 amplitude tables | `AmplitudeTable` bank, `ampTable` key, 16 cap | four mode sites + guard, after envelope | `AmpTableEditor` + Amp combo | 22 | complete |
| WP3 groups | `ArcGroup`, 64 cap, one group per arc | `resolveArcOverrides`; solo silences ungrouped; transpose last | `GroupPanel` | 41 | complete |
| WP4 sequences | `PerformanceSequence`, 2 / 20 000 / 720 limits | audio-thread clock, loop/seam split, stop/release, overwrite + decimation | `SequencePanel`, drag/Trim/Replace | 42 | complete |
| WP5 modulation | `modulatorArcId` + `mod*` keys, cycle repair | factor before space map in all four modes | Mod/Tgt/Depth | 28 | complete |
| WP6 channels | `Channel`, 16 cap, `ch` / `channels` additive | per-channel gain/mute/solo/curve, Main bucket, filter, stems | minimal panel | 29 | defect + UI gap |

Serialization is additive everywhere (`spaceTables`, `ampTables`, `groups`,
`sequences`, `channels`, `ampTable`, `mod`, `ch` omitted at defaults); the
v0.29 fixture renders bit-exactly and the golden hash `0x6d7571bdd80e3cd9`
is enforced. No TODO/FIXME markers and no UTF-16/NUL corruption in the audited
headers.

## WP6 findings

1. **16th channel is dropped (audio-path defect).**
   - `chanBuf` holds 16 buckets (0 = Main, 1..15 = channels).
   - `addToMix` accepts `mixChannel < 16`; the mix loop stops at `ci < 15`.
   - Bank channel 16 maps to bucket 16 and is never mixed: effective maximum
     is 15 of the 16 modelled channels.
   - Fix: allocate 17 buckets, allow `mixChannel <= 16`, loop `ci < 16`
     (keep a `< 17` guard), then re-run the routing/stems checks.
2. **WP6.4 curve editor is not in this tree.**
   - `Source/ChannelPanel.h` here is the minimal panel (list, gain,
     mute/solo, rename, new/delete); no curve canvas or presets, and
     `runChannelUiChecks` (10 checks) is absent.
   - The complete panel is committed on `feature/wp6-mixer-and-ui`
     (`2bb3173`, encoding fix `0b7b70e`); merge it before calling WP6.4 done.
3. Group channel overrides inherit the 15-channel clamp.

## Recorded out-of-scope issues (not fixed here)

- Live input tap allocates on the audio path; `callAsync()` allocation on the
  audio thread; >64 vocoder voices; vocoder preparation race; live monitoring
  bypasses the channel mixer.

## Release leftovers

The 0.35.0 hardening runs are done: the ASan selftest passes 1470/1470 with
no sanitizer report, the full PageHeap run (`!gflag +hpa`, NtGlobalFlag
0x02000070) passes 1470/1470 and exits cleanly, and the Release benchmark
costs 2.52x / 2.56x of the realtime budget (allowed 5x). The release
archive has been regenerated and the tree restaged and inspected.

Still open: the `origin/guerrero-dialog` rebase/push, and the external
Android/Windows signing (H5, L6).
