# The wave design notes

One note per wave: what the wave was for, what was decided, which risks
were real, what was verified, and what came out of it. The notes are
numbered like the waves in `PROGRESS.md` (wave 15 = the lock-free model
snapshot), and the milestone log there has the commit hashes.

Most of the notes were written **after** the wave, from the log and the
commits - they are the record of the decisions, not a plan that was
followed. Wave 15's note is the exception: it was written as a design
first and has the plan at the top and the outcome at the bottom.

| Note | Wave | Version |
|---|---|---|
| [00-base-app.md](00-base-app.md) | the base app | - |
| [01-composition-tools.md](01-composition-tools.md) | v1.2 | 1.2 |
| [02-mobile-android.md](02-mobile-android.md) | v1.3 | 1.3 |
| [03-live-input-capture.md](03-live-input-capture.md) | live input & capture | 1.4 - 1.5 |
| [04-tabs-ui-polish.md](04-tabs-ui-polish.md) | v1.6 / v1.7 | 1.6 - 1.7 |
| [05-one-tab-strip.md](05-one-tab-strip.md) | v1.8 | 1.8 |
| [06-live-pitch-modes.md](06-live-pitch-modes.md) | v1.9 | 1.9 |
| [07-sample-pitch-modes.md](07-sample-pitch-modes.md) | v1.10 | 1.10 |
| [08-vst3-plugin.md](08-vst3-plugin.md) | the VST3 plugin | 1.11 |
| [09-live-ratio-grid.md](09-live-ratio-grid.md) | v1.12 | 1.12 |
| [10-live-pitch-range.md](10-live-pitch-range.md) | v1.13 | 1.13 |
| [11-live-vocoder-buzz.md](11-live-vocoder-buzz.md) | v1.14 | 1.14 |
| [12-no-clicks.md](12-no-clicks.md) | v1.15 | 1.15 |
| [13-toolkit-pass.md](13-toolkit-pass.md) | the toolkit pass | 1.16 |
| [14-live-tab-mic.md](14-live-tab-mic.md) | v1.17 | 1.17 |
| [15-lock-free-model.md](15-lock-free-model.md) | the lock-free snapshot | 1.18 |
| [16-housekeeping.md](16-housekeeping.md) | the housekeeping | 1.18 |
| [17-sample-reverse.md](17-sample-reverse.md) | the sample reverse read | 1.19 |
| [18-live-reverse.md](18-live-reverse.md) | the live reverse | 1.20 |
| [19-arc-list-panel.md](19-arc-list-panel.md) | the arc list panel | 1.22 |
| [20-midi-input-foundation.md](20-midi-input-foundation.md) | the MIDI input foundation | 1.23 |
| [21-envelope-points.md](21-envelope-points.md) | envelope point editing (+ / -) | 1.24 |

## The template

```
# Wave N - <title>

**Status:** shipped in v1.x.y (<commit>).
**Kanso:** <card> (<column>).
**Goal:** <one sentence>.

## Why
<the problem, in the user's words where there were any>

## What was decided
- <the decisions that shaped the wave>

## The risks that mattered
| Risk | What actually happened |
|---|---|
| <risk> | <outcome> |

## What was verified
- <the checks that were added or run>

## What came out of it
- <the follow-ups, the things that were left alone, the next wave>
```

## The rules these notes follow

* **The user's words first.** When a wave started with a report ("the UI
  lags when I click Live"), the report is the headline, not the fix.
* **Measure, do not guess.** Every claim about timing, level or frequency
  carries the number that was measured.
* **Leave the known ones alone.** A risk that was accepted is written
  down as accepted (see `known-issues.yaml`), not quietly forgotten.
* **One wave, one idea.** If a wave had two unrelated ideas, they are two
  waves in the log - and the notes say so.
| [22-signed-playhead.md](22-signed-playhead.md) | the signed playhead + the MIDI scrub | 1.25 |
| [23-fm-pm-plan.md](23-fm-pm-plan.md) | the FM/PM plan + implementation | 1.25 |
| [24-v126-ticket-sweep.md](24-v126-ticket-sweep.md) | the v1.26 toolbar / selection ticket sweep | 1.26 |
| [25-v127-wavetable-and-safety.md](25-v127-wavetable-and-safety.md) | the v1.27 wavetable fork, Stop rewind and Clear warning | 1.27 |
