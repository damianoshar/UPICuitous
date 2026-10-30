# Wave 24 - the v1.26 toolbar / selection ticket sweep

**Status:** shipped in v1.26.0 (commit `df9b073`).
**Kanso:** cards 328-334 (Next up, board 3).
**Goal:** make the toolbar and the two selection surfaces agree: what the
toolbar shows is what the selected arcs use, and what is selected can be
edited as a group.

## Why

The v1.25 FM/PM wave left a set of follow-up reports in one stack:

- drawing used the toolbar values, but changing a control did not update
  the arc that was already selected (328);
- the audio-settings overlay's OK button did not respond (329);
- the Sample tab's Granular and Varispeed modes sounded the same (330);
- Ref was missing from Live and did not retune a Sample arc (331);
- envelope points could only be selected one at a time (332);
- clicking an arc in the side list changed the tab flags but not the canvas
  layer (333);
- the side list and canvas could select only one arc, so a settings change
  could not be applied to a group (334).

## What was decided

- **One selection model on both surfaces.** The canvas and the arc list
  share the same `selArcs` vector. Ctrl/Cmd/Shift-click toggles an arc;
  a plain click selects one. The toolbar shows the primary (first) arc and
  edits every selected arc.
- **The toolbar is a live editor, not only a template.** Wave, envelope,
  intensity, Ref and the sample slot apply to the current selection. PM
  ratio / index / index envelope / feedback / operators also update the
  current PM voice, so the next PM arc is born with the toolbar values.
- **Ref belongs to both pitched materials.** It is visible on Sample and
  Live and retunes the selected arcs immediately; the grid repaints through
  the existing `setCurrentVoice` ref-changed path.
- **PM arcs have no Follow-the-tab menu.** The sample/live pitch-mode menu
  only applies to modes 1 and 2; a PM arc is always PM.
- **Envelope points are a group selection.** Ctrl/Cmd/Shift-click builds
  the group, dragging one selected point moves all of them, and the header
  minus removes the group. The curve always keeps at least two points.
- **Granular is a real time-preserving shifter.** The sample path passes a
  0.04-second hop, but `GrainCore` clamped every hop to at least 2.0, so the
  sample granular path degenerated to varispeed. The clamp is now 1e-6.
  The pitch-accuracy test uses a 100 Hz tone because 40 ms is exactly four
  cycles at 100 Hz; a 220 Hz tone has 8.8 cycles per hop, so the grain-rate
  comb moves the measured zero-crossing/FFT peak even though the algorithm
  is behaving as designed.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| Multi-selection across layers | The canvas is layer-filtered by design. A list click switches to the arc's tab first; additive selection then applies within that layer. Cross-layer groups are not offered. |
| Group envelope drag with moving indices | The drag snapshots the points and a selected flag, applies one delta to the selected items, stable-sorts, then rebuilds both the points and the selection. No cumulative index drift. |
| Toolbar feedback loops | `syncControlsToArc` and `refreshBoxes` use `dontSendNotification`; only user changes run the apply-to-selection callbacks. |
| Granular test masking a real bug | The 220 Hz failure was the grain comb, not the shifter: at 100 Hz all ratios land exactly, and the ratio-2 ramp test still proves granular and varispeed produce different output. |
| Stack pressure in the selftest | `MainComponent` is about 90 KB; the new UI checks heap-allocate it with `std::make_unique` instead of adding stack locals. |

## What was verified

- App selftest **312/312**, including:
  - arc-list selection switches the canvas layer and the tab;
  - arc-list selection uses the row's page when ids repeat between pages;
  - Ctrl-click multi-selects on the canvas and in the list;
  - the toolbar wave / intensity / Ref / sample-slot edits reach every
    selected arc;
  - Ref is visible on Live and retunes a Sample arc;
  - PM rows have no mode menu, and generated PM arcs inherit the toolbar;
  - the audio-overlay OK button wins the hit test;
  - envelope multi-select, group drag and group delete;
  - sample varispeed and granular differ at a 2x arc ratio, and granular
    pitch is correct at 1x / 2x / 0.5x with the grain-friendly tone.
- Plugin smoke **15/15**, pluginval strictness 5 **SUCCESS**, VST3
  **1.26.0**.
- Android project matches the `.jucer` at 1.26.0 (`fix_android.ps1
  -Verify`).

## What came out of it

- The T-grid / zone-grid editor and true T-grid lines remain deferred until
  the user approves them.
- The algorithm editor (FM/PM ticket 9) remains deferred.
- The installed VST3 still needs a manual UAC-elevated install; the built
  bundle is 1.26.0.
- Follow-up: the granular mode still has the accepted grain tick from
  `known-issues.yaml`; it is inherent to the shifter, not the hop clamp.
