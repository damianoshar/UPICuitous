# Future ideas - the roadmap after v1.14

Reference write-up of the ideas discussed on 2026-10-21. **Nothing here is
approved work.** Every item lives on the Kanso board "UPIC Clone" (Nextcloud),
in the columns *Future Ideas (Do not implement yet)* and *S / M / L - ... (do not
implement yet)*; all cards are `not_started` and labelled `future-idea`.

**The open questions are deliberately not in this file** - they are checklists on
the Kanso cards, so they can be ticked off as they get answered. This file is
the reference; the board is the source of truth.

| Idea | Kanso | Size | Where it lands |
|---|---|---|---|
| MIDI timeline control + reverse playback | UPIC-22 | M (~2-3 weeks) | the immediate next feature |
| FM/PM synthesis of the produced pitches | UPIC-16 | L (M for a 2-op proof) | in-app voice, or the FM8 wire |
| Bridge to the FM8 clone | UPIC-23 | M prototype / L product | cross-repo |
| DAW time sync | UPIC-17 | L (M for stage 1) | plugin + standalone |

---

## 1. MIDI timeline control + reverse playback (UPIC-22)

*The idea:* a MIDI action moves the time bar. Forward produces the pitches as
now (oscillator, sample or live). Backward: the sample is produced in reverse,
and the live input becomes a pitched reversed delay with short feedback.

**Why this is the right next step.** The score is already a function of time
(`freqAt(t)`, envelopes, edge fades), so random access is free. What is missing
is only (a) a signed playhead, (b) a reverse read for the sample/live sources,
(c) MIDI input. None of that touches the score model, the file format or the UI
model - unlike DAW sync.

**How it decomposes**

| Card | What | Size |
|---|---|---|
| UPIC-24 | MIDI input foundation: device list, learn, activity monitor - **shipped v1.23** | S (~2-4 days) |
| UPIC-25 | Sample reverse read: slots play backwards at `-ratio` - **shipped v1.19** | S (~2-3 days) |
| UPIC-27 | Live pitched reverse delay: read the ring backwards, short feedback - **shipped v1.20** (as the live reverse) | M (~3-5 days) |
| UPIC-28 | MIDI scrub transport: a CC/notes move a signed playhead | M (~1-2 weeks) |

Total ~2-3 weeks for a solid version. The reverse slice (UPIC-25 + UPIC-27) was
proved with on-screen Reverse buttons before any MIDI plumbing existed - MIDI is
only the controller, and UPIC-28 is the remaining slice.

**Design notes**

- *Sample reverse*: the slot reader advances at `-ratio` with the same
  interpolation and wrap rules. The score time runs backwards too, so a mirrored
  stroke sounds mirrored.
- *Live reverse*: the input ring already holds ~2 s. Read it backwards at
  `ratio` and feed 30-50% back into the write head for 1-2 repeats; fade the
  ring ends. A live input cannot be rewound - the ring is the tape.
- *Osc*: stationary - it simply follows the time bar (or mutes in reverse).
- *Mapping*: a relative CC (jog) scrubs at a speed proportional to the delta;
  notes or a CC for play/stop. Absolute CC -> position is simpler but less
  expressive. Clamp the scrub rate (+/- 4x) and add hysteresis at the zero
  crossing so the direction does not flap.
- *Plugin*: MIDI arrives in `processBlock`; scrub mode must not fight the host
  transport, so it needs an explicit mode switch.

**Risks**: clicks at the direction change (crossfade + reset the shifter taps);
MIDI jitter (timestamp and smooth the scrub rate); host-transport conflict in
the plugin; the live tail is capped by the ring length (~2 s).

---

## 2. FM/PM synthesis of the produced pitches (UPIC-16)

*The idea:* the pitches the score produces could be *modulated* - FM/PM on top
of the drawn pitch curve. The parallel FM8 clone (live input instead of
oscillators) could be wired in; it could receive input from this project.

**Why it fits UPIC.** The arc's y-axis is already the carrier frequency, and the
arc's life envelope is already a contour. A 2-op voice needs almost no new UI:
modulator at `ratio x f`, index driven by the existing envelope, and one drawn
stroke gets a bright attack with a mellow tail.

**Three routes**

1. **2-op PM proof in-app** (UPIC-26, M, ~1-2 weeks). Carrier + modulator per
   arc; ratio 0.5..8; index 0..10; index envelope. Use *phase* modulation, not
   true FM (no DC drift - it is what the DX7/FM8 actually do). Hide it behind a
   mode flag, then measure the sidebands `f +/- k*ratio*f` to prove it.
2. **Full FM voice in-app** (UPIC-30, L, ~4-6 weeks). 4-6 operators, 6-8
   algorithms, feedback, per-operator envelopes, a compact matrix UI, presets.
   This is a synth engine - the FM8 clone's job. Only worth it if the wire
   proves too limiting.
3. **Wire to the FM8 clone** (UPIC-23 / UPIC-31).

**The real question is not "can we implement FM"** but *"does FM on a
continuously gliding pitch sound good?"* A constant glide can smear the
sidebands. The 2-op proof answers that in days; only then decide between a full
engine and the wire.

**Technical notes**: use PM; the score's 1/16x..16x range multiplied by FM\n\n**Rigorous plan**: docs/waves/23-fm-pm-plan.md (10 tickets, S/M/L sizing,\ndependencies, open questions). The 2-op proof (ticket 1) shipped on the\nm-pm branch; the remaining tickets are not started.
ratios needs clamping; aliasing at high ratios needs oversampling or a
band-limited modulator; if the glide does smear the sidebands, the carrier may
have to be held or quantised per grain window.

---

## 3. Bridge to the FM8 clone (UPIC-23)

*The idea:* the parallel FM8 clone has live input instead of oscillators. Maybe
a wire between the two apps - it could receive input from this project. "This
thought gets complicated."

**Two wires, very different**

- **Audio route (zero code, an afternoon).** UPIC's output -> the FM8 clone's
  live input (loopback device / virtual cable). Cheap, but it chains two
  pitch-shifters and is not true FM - the clone only hears the already-shaped
  result.
- **Control route** (UPIC-31, M prototype / L product). Stream the score's
  control signal at 100-200 Hz over OSC/UDP (or `juce::InterprocessConnection` -
  both apps are JUCE): `{arcId, pitchHz, gate, env, transport, tempo}`. The
  clone uses pitch/gate as its carrier/modulator targets and its own live input
  as the excitation. Roles stay clean: UPIC = gesture/score, clone = timbre.

**The hard parts**

- *Who owns the pitch?* If both apps shift, you get double transposition. The
  wire has to say either "UPIC sends the target Hz and the clone must not track
  or shift again" or "the clone tracks its live input and UPIC only sends
  gesture/envelope".
- *Latency/clock*: timestamp the messages and buffer a few ms in the clone.
  Audio-rate over the wire is unnecessary and fragile.
- *Schema/versioning*: a small versioned JSON/OSC schema in a shared header, so
  both apps can evolve.
- *Cross-repo*: the clone is a separate project; this depends on its control
  surface and may belong on the FM8 board with a link back.

**Alternative**: merge the FM engine into UPIC as a module - no wire, no clock,
one state model. That is better if the two apps stay personal tools; the wire
wins if the clone keeps growing on its own.

---

## 4. DAW time sync (UPIC-17)

**Where we are.** The plugin is host-driven for audio (`processBlock`) but runs
its own transport: the playhead advances by samples, the score is stored in
**seconds** (`durationSec`, arc point times), and Play/Stop is internal. The
host's play state, position and tempo are ignored.

- **Stage 1 - transport follow** (UPIC-29, M, ~3-5 days). Read
  `AudioPlayHead::PositionInfo` in `processBlock` (`isPlaying`, `ppqPosition`,
  `bpm`, `timeSig`, `isLooping`); map `ppq -> seconds` at the current BPM;
  start/stop the playhead with the host; locate on host seek; a "follow host"
  toggle (off = internal transport); a fake `AudioPlayHead` in the smoke test
  (start at bar 3, stop, locate back).
- **Stage 2 - musical time** (UPIC-32, L, ~3-4 weeks). Store the score in beats
  (or both beats and seconds); draw the ruler in bars/beats; follow host tempo
  changes and loop/cycle; `.upic.json` version bump + migration; the standalone
  needs its own BPM + transport for parity.
- **Stage 3 - polish** (M). Host loop/cycle, latency compensation (report the
  engine latency to the host), offline/freeze render, tempo automation.

**Why L overall**: the playhead is plumbing, the data model is the cost. Every
editor, the audio overlay, the selftest timings and the file format assume
seconds today. Stage 1 works for a constant-tempo project; if the tempo changes
later the score drifts - that is exactly what stage 2 fixes.

---

## Sized work items

| Card | Column | Item | Estimate | Depends on |
|---|---|---|---|---|
| UPIC-24 | S | MIDI input foundation | 2-4 days | - |
| UPIC-25 | S | Sample reverse read | 2-3 days | - |
| UPIC-26 | M | 2-op PM proof | 1-2 weeks | - |
| UPIC-27 | M | Live pitched reverse delay | 3-5 days | - |
| UPIC-28 | M | MIDI scrub transport | 1-2 weeks | UPIC-24, UPIC-25, UPIC-27 |
| UPIC-29 | M | Host transport follow | 3-5 days | - |
| UPIC-30 | L | Full FM engine | 4-6 weeks | UPIC-26 |
| UPIC-31 | L | OSC control wire to the FM8 clone | 2-4 weeks | the clone's control surface |
| UPIC-32 | L | Musical time (beats, ruler, migration) | 3-4 weeks | UPIC-29 |

## What these four ideas have in common

- **Random access is free.** The score is a function of time, so anything that
  scrubs, reverses or re-times is an engine change, not a data-model change.
- **The envelope is a free contour.** The arc's life curve can drive the FM
  index, the reverse fades and the scrub ramps - no new drawing needed.
- **Prove the sound first.** The reverse slice (UPIC-25 + UPIC-27) and the 2-op
  PM proof (UPIC-26) are small enough to be experiments; build the controller
  (MIDI) or the engine (full FM) only after the sound is worth it.
- **The data model is the expensive part.** Anything that turns seconds into
  beats (UPIC-32), or adds a new per-arc lane such as a drawn FM modulator,
  touches the file format, every editor and the selftest timings - that is
  where an "L" comes from.
