# UPICuitous — Missing Historical UPIC Features

Condensed functional specifications and development action points

## P1 — Core historical functionality

### 1. Editable performance sequences

Functionality: Record and reproduce how a performer navigates through a score. Playback can move forward, backward, accelerate, decelerate, pause at a position, or jump between pages. The recorded movement becomes an editable graphical curve.

Action points:

- Create a `PerformanceSequence` model storing time, page and playback position.
- Add a graphical performance-sequence editor.
- Implement Record, Play, Stop, Loop and Edit Sequence.
- Allow recorded trajectories to be redrawn, trimmed and overwritten.
- Integrate sequence playback with the existing transport and audio engine.
- Persist sequences in project files.

### 2. Persistent arc groups

Functionality: Organize multiple arcs into named groups that can be manipulated together without modifying their individual properties.

Action points:

- Add an `ArcGroup` model with persistent arc membership.
- Support grouping through selection, arc lists and filtering criteria.
- Add group-level Mute, Solo, Gain, Transpose and Timbre controls.
- Apply geometric transformations to entire groups.
- Add group colors and a dedicated group-management panel.
- Save and restore groups with compositions.

### 3. Drawable frequency conversion tables

Functionality: Replace the restriction to predefined frequency mappings with fully editable graphical tables. A conversion table determines how a drawn vertical position maps to a synthesized frequency.

Action points:

- Introduce a `FrequencyConversionTable` model.
- Create a graphical table editor.
- Allow continuous, discrete, nonlinear and inverted mappings.
- Support custom tuning, frequency ranges and divisions per octave.
- Assign different conversion tables to individual arcs.
- Preserve existing frequency-space presets as defaults.

### 4. Amplitude conversion tables

Functionality: Separate the shape of an amplitude envelope from the mathematical mapping of its values to synthesis amplitude. Different tables can produce different dynamic responses from the same envelope.

Action points:

- Create an `AmplitudeConversionTable` model and asset bank.
- Add a graphical table editor with presets and custom curves.
- Assign tables to individual arcs.
- Apply the mapping after envelope evaluation in the synthesis engine.
- Support multiple reusable tables per composition.
- Include table references in serialization and undo/redo.

### 5. Independent modulating arcs

Functionality: Let one drawn arc modulate another arc's pitch or amplitude. The modulator follows its own graphical trajectory independently of the carrier.

Action points:

- Add explicit relationships between carrier and modulator arcs.
- Implement frequency and amplitude modulation targets.
- Provide modulation depth and time-alignment parameters.
- Add visual indicators for modulation connections.
- Prevent invalid or circular modulation relationships.
- Integrate modulation with existing LFO and PM facilities.

## P2 — Composition and sound-management functionality

### 6. Multichannel synthesis and routing

Functionality: Assign arcs to independent audio channels, each with its own volume, mute, solo and dynamic envelope. Route these channels to available physical outputs.

Action points:

- Add channel assignment to the arc model.
- Create a multichannel mixer.
- Implement per-channel gain, mute, solo and automation envelopes.
- Support configurable output-device routing.
- Update the engine for multichannel synthesis.
- Extend offline export to support channel-separated audio.

### 7. Cross-project waveform and envelope libraries

Functionality: Maintain reusable banks of graphical sound objects independently of individual compositions. Composers can transfer material between projects.

Action points:

- Introduce global waveform and envelope libraries.
- Support Import, Export, Copy, Rename and Delete.
- Add searchable library-management panels.
- Allow assets to be copied between projects.
- Preserve independent object IDs and references.
- Embed required assets in saved compositions for portability.

### 8. Complete graphical transformation tools

Functionality: Manipulate arcs and groups as graphical geometry, beyond the existing transposition, inversion, retrograde and time-scaling operations.

Action points:

- Implement rotation around a selectable anchor.
- Add reflection across arbitrary axes.
- Add horizontal and vertical alignment.
- Support scaling around movable reference points.
- Implement repeat/duplicate with time and pitch offsets.
- Ensure transformations work on individual arcs and persistent groups.

### 9. Multiple simultaneous page windows

Functionality: Display and edit several composition pages simultaneously, rather than switching between pages in a single score editor.

Action points:

- Introduce a multi-page workspace supporting up to four visible pages.
- Give each page independent zoom, scrolling and viewport state.
- Enable copying and dragging material between pages.
- Support adjustable and detachable page windows.
- Indicate the currently active playback page.
- Preserve workspace configuration when saving and reopening projects.

### 10. Sample-to-waveform and sample-to-envelope extraction

Functionality: Derive reusable oscillator waveforms and amplitude envelopes from recorded audio rather than relying exclusively on manually drawn shapes.

Action points:

- Improve existing sample-to-wavetable conversion with cycle selection.
- Implement zero-crossing alignment and periodicity detection.
- Add Sample → Envelope extraction.
- Support RMS and peak-based envelope extraction.
- Provide an audio preview comparing original and reconstructed sounds.
- Save extracted objects in the waveform/envelope banks.

## P3 — Advanced UPIC composition workflows

### 11. Generalized graphical curves

Functionality: Allow a drawn curve to be reused as different types of musical information: pitch trajectory, waveform, envelope, modulation curve or conversion table.

Action points:

- Introduce a reusable `GraphicalCurve` representation.
- Separate graphical coordinates from their musical interpretation.
- Implement Arc → Waveform, Arc → Envelope and Curve → Conversion Table.
- Support transformations between normalized coordinate systems.
- Add explicit Convert and Duplicate As commands.
- Preserve existing object types and backward compatibility.

### 12. Dedicated live-performance workspace

Functionality: Provide a unified interface for manipulating a composition during playback, rather than distributing performance functions across editing panels.

Action points:

- Create a dedicated Performance View.
- Expose page switching, Scrub, Reverse, Tempo and Loop controls.
- Integrate persistent arc-group controls.
- Display current playback position and performance trajectory.
- Allow live manipulation of group gain, pitch and timbre.
- Integrate performance-sequence recording and replay.

## P4 — Historical capacity and fidelity improvements

| Feature               | Current limitation   | Action                                                                   |
| --------------------- | -------------------- | ------------------------------------------------------------------------ |
| Envelope bank         | 50 envelopes         | Increase toward the New UPIC's 128                                       |
| Arcs per page         | 2,024 arcs           | Benchmark support for 4,000                                              |
| Wavetable resolution  | 1,024 samples        | Add optional 4,096-sample wavetables                                     |
| Page duration         | 12 minutes           | Support up to 60 minutes                                                 |
| Conversion tables     | Preset mappings only | Support editable tables, with up to 16,384 entries where appropriate     |
| Performance sequences | Not implemented      | Add two independent recordable sequences as an initial historical target |
| Audio output          | Stereo               | Support multichannel output and channel envelopes                        |

Capacity changes should follow audio-performance testing and must not break existing project files.

## Development order

1. Foundation: Frequency tables, amplitude tables, and persistent arc groups.
2. Performance: Editable performance sequences and dedicated performance workspace.
3. Synthesis: Modulating arcs, channel routing and channel envelopes.
4. Composition workflow: Asset libraries, graphical transformations and multi-page editing.
5. Advanced graphical control: Sample extraction and generalized curve conversions.
6. Historical limits: Increase capacities after benchmarking and regression tests.

Implementation requirements across all features: Use stable object IDs, preserve existing `.upic.json` compatibility, integrate with undo/redo, avoid audio-thread allocations, and add save/load plus audio-engine regression tests.

The three highest-value historical restorations are editable performance sequences, persistent arc groups, and drawable frequency/amplitude conversion tables.