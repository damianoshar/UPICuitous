# Historical UPIC restoration — implementation plan

Branch: `feature/historical-upic-p1` (created 2026-11-18 from `main` @ `5278224`, v0.29.0)
Inputs (snapshot copies kept in `docs/plans/reference/` so the branch is
self-contained):
- `ACTIONS-LIST.md` — the functional spec and priorities (P1–P4).
- `ACTIONS-LIST-RESEARCH.md` — the historical gap analysis behind it.

This plan turns the action list into shippable work packages that fit the
**current** codebase (v0.29.0 file layout), not the file names assumed by the
research note. It defines, per package: model, engine, UI, persistence, tests,
acceptance criteria, effort and risk. It is deliberately ordered so every wave
ships a complete, backward-compatible feature.

---

## 1. Ground truth: what the code already has

Verified against the v0.29.0 tree, so nothing below re-implements it.

| Area | Where it lives | State |
|---|---|---|
| Arc model | `Source/Arc.h` | waveform/env ids, intensity, mute, mode, sampleId, refFreq, per-arc pitchMode, reversed, `waveUse`/`modRate`/`modDepth`, PM fields, `stepDivisions`, `space` |
| Page model | `Source/Page.h` | duration, tempo, arcs, sieve zones, ambitus (`pitchLo/pitchHi`), `spaces[4]` presets, `warp` + time map |
| Frequency spaces | `Source/FreqSpace.h`, `Page::spaceMap` | 7 presets (span/shift), per-arc assignment via `Arc::space` |
| Tunings | `Source/Tuning.h` | Scala-ish tunings, sieve quantisation, 12-TET default |
| Piece/limits | `Source/Piece.h`, `PieceLimits.h` | piece JSON, 100 pages, 2024 arcs/page, 50 envelopes, 720 s pages |
| Engine | `Source/UpicEngine.cpp` (`render`) | 4 arc modes, per-sample `env->valueAt(life, cursor)`, `intensityAt`, space map + temperament in `arcPitch`, overload guard |
| Lock-free snapshot | `Source/ModelSnapshot.h` | waveforms, envelopes, pages, slots, pageEpoch, revision |
| Undo | `Source/MainComponent.h` | snapshot-based, 50 steps / 64 MB, `beginEdit`/`endEdit` |
| Editors | `ScoreEditor.*`, `WaveformEditor.h`, `EnvelopeEditor.h`, `ArcListPanel.h`, `LibraryManager.h` | draw/edit, arc inventory, wave/env banks (New/Dup/Rename/Delete/Unique) |
| UI shell | `MainComponent*.cpp` (Layout/Model/Piece/Audio/Controls/Export/Input/Jobs/Midi/Recovery) | tabs, transport, overlays, jobs, export |
| Tests | `SelfTest*.cpp`, `AllocCounter.h` | engine/model/files/snapshot/ui/midi checks + "audio path allocates nothing" instrument |

Already achieved and **out of scope**: drawn arcs, wavetables, envelopes,
microtonal glissandi, pages, per-arc spaces (preset level), transposition /
inversion / retrograde / time scaling, PM tab, samples, live input, MIDI/OSC,
undo, stems, `.upic.json`.

Structural difference vs. the research note: there is **no**
`MainComponentAudio`-only monolith, no `FreqSpace` table type yet, no
`ArcGroup`, no `PerformanceSequence`, no amplitude bank, no channels.

---

## 2. Design principles (apply to every package)

1. **Backward compatibility is non-negotiable.** Every new key is omitted when
   at its default; an old `.upic.json` must load and render bit-identically.
   New fixture: `selftest/fixtures/v0.29-piece.upic.json`.
2. **Stable ids.** New objects (groups, tables, sequences, channels, curves) get
   ids from a per-bank `next*Id`, `uniquifyIds()`-style repair on load, and the
   `limits::kMaxItemId` ceiling.
3. **No audio-thread allocation.** Anything resolved per render (group
   overrides, LUTs, channel mixes, modulator cursors) is precomputed on the
   message thread into `ModelSnapshot`; the render path only reads. Enforced by
   `AllocCounter` in the selftest.
4. **One undo transaction per command.** Group create/edit/delete, table edit,
   sequence record/trim, channel edit, transform — each wrapped in
   `beginEdit`/`endEdit` with a label (`undoLabel()`).
5. **Refuse and explain, never truncate silently** (existing H13 pattern):
   cycles, duplicate members, missing assets, oversized sequences.
6. **Message-thread model, audio-thread view.** Model edits stay on the message
   thread under `dataLock`; the audio thread only ever sees a published
   snapshot (revision-gated).
7. **Per-wave gate.** `.\scripts\build.ps1` green (selftest + smoke +
   pluginval 5), old-piece fixture unchanged, new selftest checks added.

---

## 3. Work packages and wave order

| Wave | Version | Package | Action-list item | Effort |
|---|---|---|---|---|
| A | 0.30.0 | WP1 Frequency conversion tables | P1.3 | 6–8 d |
| A | 0.30.0 | WP2 Amplitude conversion tables | P1.4 | 4–5 d |
| B | 0.31.0 | WP3 Persistent arc groups | P1.2 | 7–9 d |
| C | 0.32.0 | WP4 Editable performance sequences | P1.1 | 10–14 d |
| D | 0.33.0 | WP5 Independent modulating arcs | P1.5 | 8–10 d |
| E | 0.34.0 | WP6 Multichannel routing + channel envelopes | P2.6 | 10–13 d |
| F | 0.35.0 | WP7 Asset libraries + sample extraction | P2.7, P2.10 | 8–11 d |
| G | 0.36.0 | WP8 Geometric transforms + performance view | P2.8, P3.12 | 7–9 d |
| H | 0.37.0 | WP9 Generalized curves + multi-page workspace | P3.11, P2.9 | 12–16 d |
| I | 0.38.0 | WP10 Capacity lifts (benchmark-gated) | P4 | 5–8 d |

Each wave = one or two commits series on this branch; WP9/WP10 may move to
sibling branches (`feature/wp9-curves`, `feature/wp10-capacity`) if review load
suggests it.

---

## 4. WP1 — Frequency conversion tables (P1.3)

**Goal.** A page's four spaces stop being presets-only: each can hold a drawn,
nonlinear, possibly discrete table mapping vertical position → frequency,
while the seven presets remain one-click starting points.

**Model** (`Source/FreqSpace.h`, `Page.h`)
```cpp
struct TablePoint { float x = 0.0f; float log2Hz = 0.0f; };  // x normalized 0..1
struct FreqSpaceDef {
    int preset = 0;                 // 0..6 (existing presets)
    bool editable = false;          // true -> `points` is authoritative
    std::vector<TablePoint> points; // sorted by x, 2..16384
    bool stepped = false;           // discrete table
    int  stepDivisions = 0;         // 1..99 ET applied after the curve
    bool inverted = false;          // mirrored mapping helper
};
```
- `Page` gains `std::array<FreqSpaceDef,4> spaceDefs`, while `spaces[4]`
  (`int` presets) stays the serialized source of truth until a table exists.
  Serialization writes `spaceTables` only for spaces with `editable == true`,
  so old files are untouched.
- `limits`: `kMaxTablePoints = 16384` (historical figure), `kMaxStepDivisions = 99`.

**Engine**
- Build a 1024-entry `FreqLut` per space **on the message thread**, stored in
  `ModelSnapshot::pageLuts` (`1 KB × 4 × 100 pages ≈ 400 KB` worst case,
  built only for pages that need it).
- `render()` takes the current page's LUT array; `arcPitch()` uses
  `lut[arc.space].apply(hz)` — 2 reads + lerp, no per-sample scan of table
  points. The identity preset keeps its "bit-exact passthrough" fast path
  (protects the phase vocoder, see the `SpaceMap::identity` comment).
- `stepped` reuses `upic::applyTemperament` after the curve; `Arc::stepDivisions`
  still overrides.

**UI**
- New `Source/FreqTableEditor.h`: x = page position, y = log2 Hz (note names
  from the piece's `Tuning`); tools Draw/Line/Erase/Preset; "Invert", "Stepped
  (n div/oct)".
- Entry points: page space selector → "Edit table…" (per space), plus a
  "Tables" section in the existing library area.
- Canvas: a faint heat-tint overlay showing which space an arc is in (optional
  per `Arcs` view toggle).

**Persistence.** `pages[i].spaceTables = [{space: 2, points: [...], stepped,
divisions, inverted}]`; `Arc::space` already persisted.

**Tests (selftest)**
- identity preset → bit-exact pitch (existing check must still pass).
- drawn table: y→Hz at x = 0/0.25/0.5/0.75/1 matches the LUT within 1e-6.
- stepped table quantises to n divisions; `Arc::stepDivisions` still wins.
- LUT built on message thread; render path allocation-free (AllocCounter).
- old fixture loads unchanged; new piece round-trips exactly.
- inverted filter mirrors about the table midpoint.

**Acceptance.** Draw a table that makes the bottom half of the page map into a
quarter-tone cluster and the top half into a wide octave span; two arcs in the
same space hear it; switching the space back to a preset restores the old sound
exactly.

**Risks.** Per-arc vs per-space table semantics (locked: per space, assigned
per arc — matches the historical "table assigned to the arc, table lives on the
page"). LUT build cost for 100 pages (build lazily per snapshot, current page
first).

---

## 5. WP2 — Amplitude conversion tables (P1.4)

**Goal.** Separate *envelope shape* from *amplitude mapping*: the same envelope
through different tables produces linear, compressed or expanded dynamics.

**Model** (`Source/AmplitudeTable.h` new, `Piece.h`, `Arc.h`)
```cpp
struct AmplitudeTable {
    int id = 0; juce::String name; juce::Colour colour;
    std::vector<EnvPoint> points; // x,y in 0..1, <= 1000
    float valueAt (float x, EnvCursor& c) const;
};
```
- New bank `PieceModel::amplitudeTables` (limit `kMaxAmplitudeTables = 16`;
  historical 4). Serialized piece-level as `ampTables` (omitted when empty).
- `Arc::amplitudeTableId = -1` (`ampTable` key omitted when -1).

**Engine**
- `findAmpTable(ampTables, id)` beside `findEnv`; per-arc `EnvCursor atc`.
- Order is fixed and documented: `level = intensity × envelope(life) ×
  ampTable(envelope) × pressure(t) × edge`. Group gain (WP3) and channel gain
  (WP6) multiply later.
- Applies to oscillator, sample, live and PM paths, so it is one helper
  `static inline float mappedEnv(...)` used by all four.

**UI**
- Third `LibraryManager` instance with prefix `AmpTable` (New/Dup/Rename/
  Delete/Unique reuse).
- `EnvelopeEditor` gains a "table mode": both axes 0..1, presets
  Linear / Square-law / Compress / Expand / S-curve, no time labels.
- Per-arc assignment in the arc inspector + ArcList column.

**Tests**
- Table identity (y = x) → output equals the pre-WP2 render bit-exactly.
- Square-law on a Percussive envelope changes peak level by the expected gain
  while the envelope's stored points are untouched.
- All four arc modes consume the table.
- Round-trip, delete-in-use refusal (LibraryManager contract), undo.

**Acceptance.** Duplicating a piece and swapping only the amplitude table
audibly changes dynamics, not timing or pitch; the envelope drawing is
identical in both.

**Risk.** Confusion with envelopes in the UI — mitigated by naming, separate
bank, and a distinct tick/colour.

---

## 6. WP3 — Persistent arc groups (P1.2)

**Goal.** Named, saved groups with non-destructive shared attributes (mute,
solo, gain, transpose, timbre overrides, channel), usable for performance and
geometry.

**Model** (`Page.h`)
```cpp
struct ArcGroup {
    int id = 0; juce::String name; juce::Colour colour;
    std::vector<int> arcIds;          // membership, <= 2024
    bool muted = false, solo = false;
    float gain = 1.0f;                // 0..2
    double transposeSemitones = 0.0;  // -48..48
    int waveformId = -1, envelopeId = -1, amplitudeTableId = -1, channelId = -1;
};
```
- `Page::groups`, `Page::nextGroupId`; limits `kMaxGroupsPerPage = 64`.
- Membership rules: an arc may belong to **at most one** group (prevents
  contradictory overrides); adding to a second group refuses with a flash and
  offers to move it.

**Resolved view (no audio-thread work)**
- At snapshot publish, build
  `std::vector<std::vector<ArcRenderOverride>>` parallel to pages/arcs:
  `{ muted, gain, transposeSemitones, waveformId, envelopeId, amplitudeTableId,
  channelId }` with solo already resolved (any group solo → non-solo groups
  muted; arc-level `muted` still wins).
- `render()` takes an optional override pointer; resolving is O(arcs) on the
  message thread.

**Engine**
- `level *= ov.gain`; `wave`/`env`/`ampTable` lookups use the override ids.
- Transpose is applied **after** space mapping and temperament, in semitones:
  `hz *= 2^(st/12)` then clamp to `[kMinFreq, kMaxFreq]`. Documented order:
  `space → temperament → group transpose`.
- Sample/live ratios derive from the transposed Hz, so groups transpose
  material arcs consistently.

**UI**
- New `Source/GroupPanel.h`: list (colour swatch, name, member count, M/S),
  Gain knob, Transpose knob, Wave/Env/AmpTable "override" combos (default
  "Arc's own"), channel (after WP6), "Add selection", "Remove selection",
  "Select members", rename, delete (members keep their own sound).
- `ArcListPanel` gains a group filter and a group column.
- Canvas: group colour tints the arc's selection halo; `Groups` view toggle.

**Tests**
- Non-destructive: group transpose leaves `Arc::points[].freq` unchanged.
- Override resolution table (mute/solo precedence, gain multiply, wave/env
  override, channel).
- One-group-per-arc invariant + refusal.
- Round-trip, undo of create/rename/delete/membership.
- Render with overrides matches a hand-edited equivalent piece within 1e-6.

**Acceptance.** Two groups on one page: solo one during playback, mute the
other, transpose one by +7 st and set its envelope override — no arc's stored
data changes, save/reload restores it.

**Risk.** Semantics of group solo vs arc mute (documented above); auto-generated
harmonic groups (a "future idea", not in scope) will later reuse this.

---

## 7. WP4 — Editable performance sequences (P1.1)

**Goal.** Record, redraw, trim and replay the performer's movement through the
score (page + position vs. performance time), independently of the page data.

**Model** (`Source/Performance.h` new, `Piece.h`)
```cpp
struct PerformancePoint { double t = 0.0; int page = 0; double pos = 0.0; };
struct PerformanceSequence {
    int id = 0; juce::String name;
    std::vector<PerformancePoint> points; // sorted by t
    bool loop = true;
};
```
- `PieceModel::sequences`, `nextSequenceId`; limits `kMaxSequences = 2`
  (historical), `kMaxSequencePoints = 20000`, `kMaxSequenceSeconds = 720`.
- Serialized piece-level as `sequences` (omitted when empty).

**Recording**
- Recorded on the message thread from the 30 Hz timer **and** every seek/scrub
  event; points deduped to ≥ 8 ms (historical ≈ 6 ms), capped by decimation
  (drop every other point past the limit) with a flash if decimated.
- Clock: monotonic wall time from record start (`getMillisecondCounterHiRes`),
  stored as `t`; replay never consults the wall clock.

**Playback (engine/transport)**
- New transport source enum `{ Internal, Sequence }`; when Sequence:
  audio thread advances its own `seqSampleClock`, looks up `(page, pos)` at
  `t = seqSampleClock / sr`, interpolating position linearly and deriving a
  block rate = Δpos/Δt clamped to ±4× (feeds the existing dirCrossfade and
  scrub-rate paths).
- Page switching happens on the audio thread via a dedicated atomic
  `seqPageRequest` (validated against `snapshot->pages.size()`), so a sequence
  cannot outrun the snapshot; the UI adopts the audio page for display.
- Holding a position (Δt > 0, Δpos = 0) freezes the playhead like scrub-hold.
- Reverse segments reuse the existing `playRate` direction machinery, so
  sample/live reversal stays correct.
- MIDI scrub and Sequence are mutually exclusive (source selector); switching
  refuses while recording is armed.

**UI**
- New `Source/PerformanceEditor.h`: x = performance time, y = page lane
  (rows), line = position; tools Record / Play / Stop / Loop / Trim / Clear /
  Replace segment (drag a region, re-record over it).
- Placed in the bottom editor area as a fourth view toggle (`Perf`) beside
  Wave / Env / Hints; Focus button like the others.
- Transport strip shows `SRC: Internal | Sequence` and the sequence length.

**Tests**
- Record a synthetic 5 s gesture (forward, hold, reverse), replay: playhead at
  t = 1/3/5 s equals recorded points within one block.
- Page jump at the recorded time (audio thread adopts; UI follows).
- Loop wrap is click-free (existing click guard check extended).
- Save/load reproduces the same replay; decimation respects the cap.
- Zero allocations on the audio path; old piece (no key) unchanged.

**Acceptance.** Record a performance that slows down, reverses, holds, then
jumps to page 2; replay it from the transport, from the editor and after
save/reload — identical motion each time.

**Risk.** Highest-risk package (transport authority). Mitigations: explicit
source enum, audio-thread-owned clock, snapshot-bounded page index, reuse of
existing fade/direction code, smoke test with a fake playhead.

---

## 8. WP5 — Independent modulating arcs (P1.5)

**Goal.** One drawn arc modulates another's pitch or amplitude, with its own
trajectory (distinct from the existing `waveUse` self-LFO and PM tab).

**Model** (`Arc.h`)
```cpp
int    modulatorArcId = -1;  // same page
int    modTarget = 0;        // 0 = pitch, 1 = amplitude
double modDepth  = 0.5;      // 0..1
int    modAlign  = 0;        // 0 = absolute page time, 1 = carrier start
```
- Validation on the message thread: no cycles (DFS over ≤ arcs), one modulator
  per carrier, modulator must exist on the page; refusal flashes.

**Engine**
- Preallocate a flat `ModVoice { carrierId, modId, cursor, value }` vector in
  `prepare()`; no map, no allocation.
- Pitch target: modulator contour deviation from its own mean, in semitones
  `± depth × 12`, summed into the carrier Hz before space/temperament (so the
  mod is musical in the drawn space).
- Amplitude target: `level *= 1 - depth × (1 - modNorm)` (tremolo), clamped ≥ 0.
- A muted modulator still modulates (it is a control source); document it and
  add a "audible" checkbox separate from mute if needed (defer).

**UI**
- Arc inspector: "Modulator" combo (other arcs), target (Pitch/Amp), depth.
- Canvas: dashed connector + arrow from modulator to carrier (view toggle).

**Tests**
- Pitch deviation equals `depth × 12` semitones at the modulator's peak.
- Amplitude tremolo depth/level bounds (never < 0, never > 1).
- Cycle rejected; self-reference rejected; missing id refused.
- Round-trip; mod cursor state survives page switch (reset semantics tested).

**Acceptance.** A slow drawn arc bends a fast carrier ±5 st audibly, with the
carrier's own stored points unchanged; save/reload keeps the relationship.

---

## 9. WP6 — Multichannel routing + channel envelopes (P2.6)

**Goal.** Assign arcs to channels; per-channel gain, mute, solo and a page-long
dynamic envelope; mix to stereo now, physical multi-out for the plugin next.

**Model** (`Source/Channel.h` new, `Piece.h`, `Arc.h`)
```cpp
struct Channel {
    int id = 0; juce::String name; juce::Colour colour;
    float gain = 1.0f; bool muted = false, solo = false;
    std::vector<EnvPoint> envelope; // over the page (x = 0..1)
};
```
- Piece-level `channels` (max `kMaxChannels = 16`); `Arc::channelId = 0`.
- Legacy: an empty `channels` bank means the current stereo path; channel 0 is
  implicit "Main".

**Engine**
- Preallocated `chanScratch[16]` mono buffers sized in `prepare()`.
- Arc render target becomes `chanScratch[channelId]`; after the arc loop, sum
  channels into L/R with `gain × envelope(playhead/duration) × mute/solo`,
  block-rate envelope evaluation (documented as block-quantised, ≤ 1 block).
- `render()` signature gains a `const ChannelMix*` (resolved on message thread
  in the snapshot: solo flags, gains, envelope LUTs at 64 points).
- Master gain remains last.

**Export.** Per-channel stems (mono WAV per channel) alongside the existing
per-voice stems; master render unchanged.

**Plugin multi-out (H4b, inside this wave if schedule allows).**
`BusesProperties` with up to 16 outputs, aux bus mapping in
`isBusesLayoutSupported`, latency/bus re-validation with pluginval 5, smoke
test bus-arrangement checks. If it slips, ship stereo-sum + per-channel stems
and open a follow-up card.

**Tests**
- Channel isolation: an arc in ch 1 produces silence in ch 0's pre-mix scratch.
- Gain/mute/solo precedence; envelope at x = 0/0.5/1.
- Sum identity: stereo mix equals hand-computed weighted sum within 1e-6.
- Per-channel stem RMS matches the channel's contribution.
- Round-trip; old piece (no channels) bit-exact.

---

## 10. WP7 — Cross-project libraries + sample extraction (P2.7, P2.10)

**Goal (libraries).** A user-level asset library outside the piece: waveforms,
envelopes, amplitude/frequency tables and sequences can be published to it and
imported into any piece; the piece still embeds what it uses.

- New `Source/UserLibrary.h`: root at
  `userApplicationDataDirectory/UPICuitous/Library/{waves,envelopes,tables,sequences}/`,
  each asset one JSON file (`kind`, `id`, `name`, `colour`, payload, `version`).
  Reads validate against `PieceLimits`; writes atomic (temp + rename).
- `LibraryManager` gains "Import…", "Publish", "Reveal" and a source switch
  (Piece / User). Ids are re-minted on import; references stay local.
- Tests: publish→import round-trip, corrupt/oversized asset refused, no piece
  dependency on the library (delete library dir → piece still plays).

**Goal (extraction).** `Slot → Wave` gains cycle-aware extraction; new
`Slot → Env`.

- New `Source/SampleAnalysis.h`: `findBestCycle(buf, sr, targetLen)` using
  normalised autocorrelation + zero-crossing alignment; `extractEnvelope(buf,
  sr, mode RMS|Peak, points ≤ 1000)`.
- UI in the Sample tab: cycle preview (start/length, "align zero crossing"),
  A/B preview (original vs reconstructed) through the scrub engine, then
  "Save to wave bank" / "Save to envelope bank".
- Tests: 441 Hz synthetic sine → extracted cycle period matches within 1 %;
  ramp → RMS envelope monotonic; determinism; caps respected.

---

## 11. WP8 — Geometric transforms + performance view (P2.8, P3.12)

**Transforms** (in `ScoreEditor` + model helpers in `Arc.h`/`Page.h`):
`rotate(anchor, angle)`, `reflect(axis)`, `align(edge)` (start/end/pitch),
`scale(anchor, sx, sy)`, `repeat(dt, dPitch, n)`. Rotation/scale work in
**normalised page space** (`x/duration`, `log2(f)/log2(range)`) so results are
independent of zoom; pitch is clamped and results are finite-checked
(`limits::clampFinite`). Apply to selection, group, or whole page; one undo
transaction; menu under the existing right-click context and a Transform
sub-menu.

**Performance view** (`Source/PerformanceView.h`): a dedicated mode/screen
with page selector, group M/S/gain/transpose, scrub/reverse/tempo/loop,
sequence record/replay and a live playhead trajectory. It reuses WP3/WP4
models; it is a view, not a second transport.

**Tests**: rotation preserves arc length in normalized space; reflection is
involutive; align endpoints share t; repeat produces n copies with stable ids;
transforms respect group membership; undo restores exact point arrays.

---

## 12. WP9 — Generalized curves + multi-page workspace (P3.11, P2.9)

**Generalized curves** (`Source/GraphicalCurve.h`):
```cpp
struct GraphicalCurve {
    enum class Domain { Normalised, Seconds, LogHz };
    Domain domain = Domain::Normalised;
    std::vector<CurvePoint> points;
};
```
Converters: `arcToCurve`, `curveToWaveform` (resample to wave size),
`curveToEnvelope`, `curveToTable`, `waveformToCurve`. Commands
"Convert As →" produce **new** assets (never reinterpret in place), so old
objects remain authoritative. Wire `WaveformEditor`/`EnvelopeEditor` to edit a
curve and save as either type.

**Multi-page workspace** (`Source/PageWorkspace.h`): 2–4 `ScoreEditor`
instances side by side, each bound to a different page with independent
zoom/scroll; one transport; active playback page indicated; workspace layout
saved in the piece (`workspace: {pages:[...], split}`).

**Tests**: conversions are lossless within the target resolution; arc→curve→
arc round-trip matches; two canvases edit independent pages without cross-talk;
workspace persists; audio page remains single.

---

## 13. WP10 — Capacity (P4, benchmark-gated)

| Item | Change | Gate |
|---|---|---|
| Envelopes 50 → 128 | `limits::kMaxEnvelopes`; lists/UI scroll | memory + layout sweep |
| Arcs/page 2024 → 4000 | limit + overload guard tuning | benchmark: worst-case block ≤ 70 % budget at 4000 arcs/48 kHz/128 samples |
| Wavetables 1024 → optional 4096 | `Waveform::samples.size()`-aware `valueAt`, serialized length, UI | engine + JSON round-trip + CPU |
| Page 12 min → 60 min | `kMaxPageSeconds = 3600`; confirm export streams to disk | export memory ceiling test (peak RSS < 300 MB) |
| Conversion table 16384 | cap already planned in WP1 | parse/UI |

Rule: no capacity lift before its benchmark passes in `SelfTestEngine.cpp`
(new timing checks with generous headroom, not flaky wall-clock asserts — use
work-per-block curves, not absolute ms in CI).

---

## 14. Cross-cutting: persistence, limits, undo, tests

**Serialization (piece v2, additive).**
`app`, `version: 1` stays; new optional keys: `ampTables`, `sequences`,
`pages[i].groups`, `pages[i].spaceTables`, `channels`, `workspace`. Unknown
keys are ignored on read (already the case). A `version: 2` bump only when a
key's *meaning* changes; this plan avoids that.

**Limits to add** (`PieceLimits.h`): `kMaxAmplitudeTables`, `kMaxGroupsPerPage`,
`kMaxSequences`, `kMaxSequencePoints`, `kMaxTablePoints`, `kMaxChannels`,
`kMaxModulators` (1 per arc). All parsers clamp; load never fails on a bad
count, it truncates and flashes (existing policy).

**Undo/redo integration**: every message-thread mutation in WP1–WP9 is inside
`beginEdit`/`endEdit`; snapshot handover makes undo audible immediately via
`markModelChanged()` where the sound changes.

**Test additions per wave** (target ≥ 30 new checks, totals tracked in
`PROGRESS.md`):
model round-trip, engine math, refusal paths, undo depth, old-fixture
bit-exactness, allocation-free render, and one historical reference fixture per
audio-affecting package (`selftest/fixtures/historical/*.upic.json`).

**Performance fixtures**: `historical-reference` pages (dense arcs, tables,
groups, channels) rendered to reference WAVs once; future waves compare RMS and
spectral centroid tolerances, not hashes (float codegen may differ).

---

## 15. Branch, commit and review strategy

- This branch (`feature/historical-upic-p1`) carries **WP1–WP5** (all P1) in
  waves A–D, one commit series per wave (`wp1-freq-tables`, `wp2-amp-tables`,
  `wp3-groups`, `wp4-sequences`, `wp5-modulating-arcs`).
- WP6–WP10 open sibling branches off `main` once WP3/WP4 merge (they depend on
  groups/sequences).
- Each wave PR: model + engine + UI + selftest + docs (`docs/waves/NN-*.md`),
  version bump via `.\scripts\build.ps1 -Version 0.XX.0`, pluginval 5 green.
- No force-push; merge commits squashed per wave into `main` by the owner.

**Do-not-break list**: `Scripts/build.ps1` flow, pluginval 5, `AllocCounter`
render checks, `.upic.json` old-fixture render identity, Android build
(`fix_android.ps1`), undo limits.

---

## 16. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Sequence transport fights MIDI scrub / host transport | High | Single explicit source enum; sequence clock owned by audio thread; refuse conflicting arms |
| Per-arc LUT cost at 2024 arcs × 4 spaces | Medium | Shared per-page LUTs, rebuilt only on model revision |
| Group/override semantics become opaque | Medium | One resolved override table in snapshot + selftest truth table + UI badges |
| Multi-out plugin breaks host/pluginval | Medium | Ship stereo-sum first; aux buses behind a separate change |
| Wavetable size change ripples through serialization | Medium | Optional size, 1024 default; size-aware `valueAt`; round-trip checks |
| 60-min export memory | Medium | Confirm chunked writer; add peak-RSS test; gate the capacity |
| Scope creep into "modern extensions" (harmonic generator, additive editor) | Low | Explicitly out of scope here; keep to ACTIONS-LIST |

---

## 17. Open questions (decide before the owning wave)

1. **WP1**: Should a table be per-space (page) or per-arc? Plan: per-space, arc
   assigns — confirm against Robindoré's wording if the user's manual becomes
   available.
2. **WP3**: Can an arc belong to two groups? Plan: no (override ambiguity).
3. **WP4**: Should sequence recording be allowed while the transport is
   stopped (scrub-only gestures)? Plan: yes, clock starts at record arm.
4. **WP6**: 16 internal channels but how many physical outs for the plugin in
   the first release? Plan: stereo default, aux 4 for the plugin first.
5. **WP10**: Is 60-minute page duration actually wanted before export streaming
   is proven? Plan: gate behind the memory test.

---

## 18. Immediate next actions

- [x] Create branch `feature/historical-upic-p1`.
- [x] Land `docs/plans/historical-upic-plan.md` (this file).
- [x] Old-piece guard: an inline v1 page JSON renders bit for bit
      (`runFreqTableChecks`, check 9) - no binary fixture needed.
- [x] WP1.1: `FreqSpaceDef` + `limits` + JSON (omitted-when-default).
- [x] WP1.2: `FreqLut` in `ModelSnapshot` + `arcPitch` LUT path (identity
      fast path preserved; offline export builds its own set).
- [x] WP1.3: `FreqTableEditor.h` (overlay) + page-card "Table…" button.
- [x] WP1.4: selftest checks (14) + `docs/waves/26-freq-tables.md`.
- [x] WP2: amplitude tables - `AmplitudeTable.h` bank (`ampTables`, omitted
      when empty), `Arc::amplitudeTableId`, applied in all four arc modes and
      the guard, `AmpTableEditor.h` overlay, 22 checks,
      `docs/waves/27-amp-tables.md`.
- [x] Wave A gate at **0.30.0**: `.\scripts\build.ps1` green - selftest
      1029/1029, plugin 0.30.0, smoke 22/22, pluginval strictness 5 SUCCESS.
- [x] WP3 (Wave B): arc groups - `ArcGroup.h`, per-page `groups` (64 cap,
      omitted when empty), one group per arc, solo/mute/gain/transpose and
      timbre overrides resolved into `ArcRenderOverride` at snapshot
      publication, applied in all four arc modes and the guard,
      `GroupPanel.h` overlay, 38 checks, `docs/waves/28-groups.md`.
- [x] Wave B gate at **0.31.0**: selftest 1067/1067, plugin 0.31.0,
      smoke 22/22, pluginval 5 SUCCESS.
- [x] WP4 (Wave C): performance sequences - `Performance.h` (pure
      position/rate/loop functions), piece-level `sequences` (2 cap, 20k
      points, omitted when empty), audio-thread clock reading snapshot
      points, 30 Hz recorder, `SequencePanel.h` overlay and toolbar
      Rec/Play/Loop/View, 29 checks, `docs/waves/29-sequences.md`.
- [x] Wave C gate at **0.32.0**: selftest 1096/1096, plugin 0.32.0,
      smoke 22/22, pluginval 5 SUCCESS.
- [x] Audit checkpoint (19 Nov): Solo silences ungrouped arcs; transpose
      after temperament; sequence graph plots page position; point drag,
      Trim and Replace-tail; sequence Stop/end/seam fixes; overwrite
      confirmation, point-cap thinning and one take = one undo; golden
      render hash (0.32 reference - the v0.29 binary is not in this
      archive). See `docs/waves/30-seq-stabilisation.md`.
- [x] WP5 (Wave D): independent modulating arcs - `modulatorArcId`,
      `modTarget`, `modArcDepth`, `modAlign`; cycle-safe resolution in
      `ArcRenderOverride`; contour factor applied before the space map in
      all four modes; panel Mod/Tgt/Depth; 19 checks;
      `docs/waves/31-modulating-arcs.md`.
- [x] Wave D gate at **0.33.0**: selftest 1133/1133, plugin 0.33.0,
      smoke 22/22, pluginval 5 SUCCESS.
- [x] WP6.1–WP6.3 + export (Wave E, 0.34.0): `Channel.h` model (16 cap,
      omitted when empty), `Arc::channelId`, snapshot carry, load repair,
      per-channel mixer (Main bucket + gain/mute/solo/curve, solo silences
      Main), channel filter and per-channel stems, legacy path bit-exact,
      allocation-free mixer, 21 checks, `docs/waves/32-channels.md`.
- [x] WP6.4 UI: the mixer panel (lazy, message-thread only) with list,
      gain, mute/solo, rename, new/delete, undo; the Ch combo + Mix button.
      Green with the WP7 tree; the gain-curve editor + UI tests live on
      `feature/wp6-mixer-and-ui` (`2bb3173`) and still need merging.
- [x] Wave E gate at **0.34.0**: selftest 1163/1163, plugin 0.34.0,
      smoke 22/22, pluginval 5 SUCCESS.
- [x] WP7 (Wave F, 0.35.0): user asset libraries (five kinds, exact schema,
      atomic writes, re-minted import ids), cycle-aware RMS/Peak extraction,
      channel-selected isolated A/B preview, generation-safe preview routes,
      legacy fixture gate. WP7 suite green.
- [x] Wave F gate at **0.35.0**: selftest 1470/1470, legacy SHA-256
      8BF28ABA7975…, plugin 0.35.0, smoke 22/22, pluginval 5 SUCCESS.
- [x] Release hardening for 0.35.0: ASan selftest 1470/1470 with no
      sanitizer report; full PageHeap (`!gflag +hpa`, NtGlobalFlag
      0x02000070) selftest 1470/1470 with no heap error; Release
      benchmark 2.52x / 2.56x of the realtime budget (allowed 5x); the
      golden render hash 0x6d7571bdd80e3cd9 and the exact identity
      frequency output are unchanged.
- [x] Release packaging: `upic-clone-juce-release-v1.27.0.zip` regenerated
      from the tracked tree plus the vendored JUCE source - no generated
      `JuceLibraryCode` amalgamation, no `CMakeSettings.json`, no VCD
      trace, no pluginval binary, no build output - and the legacy
      selftest fixture is included. The tree is restaged and inspected,
      and the release and documentation repositories are committed and
      pushed.
- [x] `origin/guerrero-dialog` replayed on top of this release and pushed
      (was `70bd7fb`, now `baa95cc`). The replay settled three compile
      conflicts (a duplicate `setCurrentPageForTest()` hook,
      `guerreroTargetUnchanged()` defined outside its guarded block, and a
      modal message pump that JUCE 8 compiles out), repaired the CP1253
      mojibake, and exposed one sanitizer-only defect: a stale deferred
      level-slider notification cancelled a live sample-extraction
      preview. The rebased tree passes the 0.35.0 gate (1,508/1,508
      checks, legacy SHA-256 8BF28ABA7975..., plugin 0.35.0, smoke 22/22,
      pluginval strictness 5 SUCCESS). See `docs/plans/wp1-wp6-audit.md`
      for the WP6 defect and UI-merge note.