# Wave 25 - the wavetable fork, Stop rewind and Clear warning

**Status:** shipped in v1.27.0 (commit `714a1c5`).
**Kanso:** follow-up reports after the v1.26 ticket sweep (board 3).
**Goal:** make the bottom wavetable editor respect the selection, make
Clear ask before it deletes, and let a double-click on Stop return to the
start of the page.

## Why

Four reports came in after v1.26:

- "when changing wave shape from the bottom screen arcs everywhere
  change. dropdown on the top works as intended."
- "also wavetable is weird for sqr and saw wave."
- "add these double stop goes to the beginning of the score."
- "clear button warning screen and button color that deletes everything."

The top Wave dropdown was already the model: it assigns a wavetable to
the selected arc. The bottom editor was still editing the shared
wavetable object, so every arc that named it changed too. The saw and
square displays also drew a diagonal line because the stroke path was
closed from the last sample back to the first.

## What was decided

- **Copy-on-write for the bottom editor.** With a selection, the first
  edit checks whether any *unselected* arc (on any page) uses the current
  wavetable. If one does, the editor clones the wave with a new max+1 id
  and a "copy" name, assigns every selected arc to the clone, and edits
  the clone. If no unselected arc shares it, the wave is edited in place.
  With no selection, the old library/template edit is unchanged.
- **Page-aware selection.** Arc ids repeat between pages, so only the
  current page can contribute selected ids; every arc on another page is
  treated as unselected.
- **The fork happens before the first sample changes.** `WaveformEditor`
  gained an `onEditStart` hook. Mouse drawing calls it before the first
  brush dab, and the shape buttons use the same `beginEdit()` path.
- **Only Osc and PM edit a wavetable.** Sample and Live are read-only or
  no-op for wave shape.
- **Double-click Stop rewinds.** A second Stop click within 400 ms calls
  `stopPlayback()` and then seeks the playhead to 0. A single click still
  stops normally.
- **Clear warns in-app.** A custom `ClearOverlay` explains that the
  current page's arcs will be deleted and offers Cancel plus a red
  "Clear page" button. The File menu item and the toolbar button both
  route through it. An in-app overlay works on Android, where a floating
  dialog cannot be dismissed reliably.
- **The saw/square display is an open stroke.** The fill is closed down
  to the zero line separately, so the stroke no longer draws a diagonal
  from the last sample to the first.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| A fork on every mouse-down would create a new wave per dab | The clone is only made when an unselected arc shares the wave; after the first fork the selected arcs use the clone, so later dabs edit it in place |
| Arc ids collide across pages | The selected-id check is limited to the current page; all other pages count as unselected |
| A single Stop click would rewind | Only a second click inside the 400 ms window rewinds; `startPlayback()` also resets the click timer |
| Clear could delete without a way back | It is still destructive, but the arcs are untouched until the red confirm button is pressed; Cancel, Escape and a backdrop tap all dismiss the warning |
| The overlay would not work on Android | It is a child component with its own buttons, not a `DialogWindow` |

## What was verified

- Selftest 312 -> 323: the shape-button fork gives the selection a
  private copy and leaves the old wave untouched, drawing follows the
  same path, double-click Stop returns the time label to 0.0, Clear
  opens the warning, leaves the arcs intact, Cancel closes it, and only
  the confirm button deletes; the Clear button carries the destructive
  colour.
- Plugin smoke 15/15, pluginval strictness 5 SUCCESS, app and VST3
  version 1.27.0, Android manifest/CMake version 1.27.0
  (`fix_android.ps1 -Verify` passes).
- The saw/square fix is visual: the editor now builds an open stroke and
  a separate fill down to the zero line. The normal app/plugin builds and
  snapshot pass still run; no pixel-level assertion was added.

## What came out of it

- Clear still means "every arc on the current page", not every page.
  The warning text matches that behavior.
- The zone time-grid submenu, true T-grid lines and ticket 9 remain
  deferred pending approval.
- The built VST3 is 1.27.0; the system-installed copy is still 1.20.0
  because the UAC prompt was declined. Run
  `scripts/install_plugin.ps1 -Version 1.27.0` and approve the prompt to
  update it (or use the per-user VST3 folder).
