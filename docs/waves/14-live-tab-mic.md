# Wave 14 - the Live tab no longer opens the mic (v1.17)

**Status:** shipped in v1.17 (`6070cb3`).
**Kanso:** card 268 (Done).
**Goal:** clicking the Live tab must not stall the UI.

## Why

The user reported: "the UI lags when I click Live". The obvious suspects
were the DSP and the paint - both were measured, and both were innocent.

## What was decided

- **The tab click is now pure UI.** Clicking the tab called
  `ensureLiveInputFor`, which opens the microphone with
  `setAudioChannels(2, 2)` - a **synchronous device restart on the
  message thread**. Measured with a temporary probe in the app: the
  click took **4735 ms**; the paint of the whole window was 40 ms
  (Debug). After the fix the same click takes **0.14 ms**.
- **The mic opens lazily where it is actually needed**: **Mon**,
  **Capture**, or **Play** when the page has live arcs. All three
  already did this, so the tab call was redundant - it was removed.
- **Nothing about the live audio path changed**: the shifter's lag, the
  latency reporting and the sound are untouched.

## The risks that mattered

| Risk | What actually happened |
|---|---|
| The live arcs would be silent until the mic is opened | The mic is opened on Play when the page has live arcs - that path already existed |
| The tab would have to open the mic to be useful | It never did: the tab is a view, the mic is a material |
| The fix would be invisible in the selftest | The selftest now clicks the real Live tab (it used to set the canvas layer directly to avoid the mic) and asserts the switch stays under 500 ms and lands on the live layer; it also clicks Osc back |

## What was verified

- The click time: 4735 ms -> 0.14 ms, measured in the app.
- The selftest clicks the real tab and checks the timing and the layer.
- Selftest 194 -> 197 checks.
- The tablet pass (tabs, grids, both pitch selectors, rotation, the view
  toggles) and the DAW pass.

## What came out of it

- The version was installed to `C:\Program Files\VstPlugIns` and verified
  (SHA-256, FileVersion, moduleinfo) - the previous install said 1.15.0
  but was actually the v1.16 build, because the version number lagged
  the wave numbers. The install is a script now (wave 16).
- The lesson: a UI action that touches the audio device is a device
  restart, and a device restart is seconds. The same class of mistake
  was waiting in the plugin's latency report (wave 13) and in the
  version resource.
