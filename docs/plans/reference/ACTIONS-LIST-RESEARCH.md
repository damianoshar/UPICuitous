# UPICuitous vs. the original Xenakis UPIC

Historical and technical gap analysis · 18 November 2026

The most important finding UPICuitous already implements much of the original UPIC concept, but it does not yet reproduce the full compositional performance system developed by CEMAMu.

The largest remaining differences are not the appearance of the drawing tablet or the number of available oscillators. They concern the way the historical UPIC allowed composers to manipulate musical structures, reinterpret drawings, control multiple sound channels and record performances as new editable graphical objects.

This analysis is based on your two latest uploads

- `UPICuitous(8).zip` — the current JUCE implementation, including its source, tests and interface assets.
- `UPICuitous-docs(1).zip` — the development documentation, backlog and feature plans.

I am comparing these with historical material from the Centre Iannis Xenakis, Xenakis's published description of the New UPIC, and technical and historical research.

## 1. Which historical UPIC should be reproduced

This distinction matters because UPIC evolved substantially.

[‘UPIC system’ (Unité Polyagogique Informatique du CEMAMu) Patrick Saint-Jean & Iannis Xenakis, France, 1977. – 120 Years of Electronic Music](httpsimages.openai.comstatic-rsc-4CkU3PuHstY93bp7cEsuoUDTCoxxT0mzLZDOY6NqQxOFiQqvNdGkv9WaWURsdajWRKRMnqDMtZXaCIlaWRJpvKJXOOg5t9d4JfOc0Amde3BbSBnw_bWmyP53IFA0I5o_KGP1e8NILWt4xvmeSGhbHLHiTjYd6BqN98FpYDyhiHYApurpose=inline)

1977–early 1980s the original UPIC

Large digitizing tablet, electromagnetic pen, handwritten pitch-time curves, drawn waveforms and envelopes, and computer-generated sound. Early synthesis was computed offline rather than offering all the real-time facilities of later models.

The foundational system of Mycènes Alpha.

[UPIC Unite Polyagogique Informatique du CEMAMu - Collections du Musée de la musique - Philharmonie de Paris - Pôle ressources](httpsimages.openai.comstatic-rsc-42njFYnAVBm8o71dotsYm74JI1tILYRjYHktycUAK_2BEjAwe9n_uihmYiSiybmp-OnKVTasEyLEe383som347dWhLp7kZ-e8pbBC2iYEbIx7HMaxb6qruhf08HoCjYG93jt9DDGyO4TvQW_xjONtOhKCUKh3PkVdyUW3l1z4miQpurpose=inline)

Mid-to-late 1980s real-time UPIC

The system evolved toward immediate sound feedback, expanded interaction and new sound-capture capabilities. The historical drawing interface remained central.

[UPIC – Graphic Interfaces for Notation Conference  ZKM](httpsimages.openai.comstatic-rsc-4sR7jH5R-ZasL9_oOK6YteW2DEzWAaELzf330S7_iNTQuNTtZGqUH-ykxIdaKiE3Jzx5Fll9BBag6070SUjvQKNWsJwI9gFQn-OEzr-6AGKpgLMjP1myJ2zLSOjjPBwK6bgimgFCgHo7fO5PpyKKqMv2gX63Fk6eq8fPiLxDKeEApurpose=inline)

1990–1991 the New UPIC

More sophisticated graphical editing, persistent arc groups, frequency and amplitude conversion tables, multichannel synthesis, live page manipulation and recordable performance sequences.

The historical descriptions confirm that the 1977 system and the later commercial-era UPIC had meaningfully different capabilities.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.centre-iannis-xenakis.org&sz=32)

presentation · Constellation Iannis Xenakis.+2



The later UPIX software, developed around 2001, is another distinct generation, not the original hardware implementation.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpsecontact.ca&sz=32)

econtact.ca+1



For development, I recommend using the 1990–1991 New UPIC as the principal functional reference, while preserving a separate 1977 historical mode or theme. It is the most complete well-documented expression of the original UPIC design philosophy.

## 2. What the latest UPICuitous already reproduces

The November 18 source is substantially more complete than the version we discussed previously. In particular, several ideas you raised on November 17 have already been implemented.

 Historical UPIC capability                     Latest UPICuitous status                                  Code evidence                             
 ---------------------------------------------  --------------------------------------------------------  ----------------------------------------- 
 Draw pitch-versus-time arcs                    Implemented                                               `ScoreEditor.cpp`, `Arc.h`                
 Draw and edit oscillator waveforms             Implemented                                               `WaveformEditor.h`, `Waveform.h`          
 Draw amplitude envelopes                       Implemented                                               `EnvelopeEditor.h`, `Envelope.h`          
 Associate a waveform and envelope with an arc  Implemented                                               `Arc.h`                                   
 Continuous microtonal glissandi                Implemented                                               `Arc.h`, `UpicEngine.cpp`                 
 Independent page durations and multiple pages  Implemented                                               `Page.h`, `Piece.h`                       
 Work at very short time scales                 Implemented, from 5 ms                                    `UpicConstants.h`                         
 Non-discrete and discrete pitch spaces         Implemented                                               `FreqSpace.h`, `Arc.h`                    
 Four frequency spaces per page                 Partially equivalent                                      `Page.h`, `FreqSpace.h`                   
 Transpose, invert, reverse and stretch arcs    Implemented                                               `Arc.h`, page transformations             
 Edit and play in real time                     Substantially implemented                                 `MainComponentAudio.cpp`, model snapshots 
 Digitize recorded audio into a wavetable       Implemented                                               `Waveform.h`                              
 Manage waveforms and envelopes                 Implemented                                               `LibraryManager.h`                        
 Focusmaximize the three editors               Implemented                                               `MainComponentLayout.cpp`                 
 Collapsible toolbar groups                     Implemented                                               `MainComponentLayout.cpp`                 
 More precise drawing and zoom                  Implemented                                               `ScoreEditor.cpp`, `ScoreViewBar.h`       
 Safe transport startstop transitions          Implemented in code, not independently audio-tested here  `MainComponentAudio.cpp`                  

The project also goes beyond historical UPIC in several areas sample playback, microphone-driven live arcs, granular and phase-vocoder pitch shifting, custom Scala tunings, MIDI controls, OSC scrub input, VST3 integration, Android support, and offline stem export.

An important correction to our previous discussion New, Duplicate, Rename, Delete and Make Unique for both waveforms and envelopes are present in this archive. So are the editor Focus buttons and the transport state machine. These should no longer be treated as missing features.

### The difference between implemented and historically equivalent

Several items above deserve a partial rating rather than a simple yes.

For example, UPICuitous offers four frequency spaces, but they are selected from seven predefined mappings. The historical New UPIC had editable conversion tables whose contents could themselves be drawn.

Likewise, UPICuitous has a page sequence toggle, but that is fundamentally different from the historical UPIC's ability to record, redraw and replay the movement of the performance through the score.

These distinctions lead to the main gaps.

## 3. The most important missing historical capabilities

Priorities reflect historical significance and likely value to UPICuitous, not implementation difficulty.

### Gap 1 — The editable performance sequence

Critical historical gap

This is possibly the single most important missing capability.

The 1990 New UPIC separated a static musical page from a musician's performance of that page.

The performer could move through the score at variable speed, jump to different positions, reverse direction, accelerate and slow down. The system could record those movements as a sequence, represented graphically as page position versus performance time.

That recording was editable sections could be overwritten or redrawn, then played back with independent position and tempo controls. Two sequences of up to 12 minutes were documented, with approximately 6 ms recording resolution.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



Historical performance sequence, conceptually

Rising = moving forward through the page; falling = reversing; horizontal = holding a score position. The slope controls speed. This is an explanatory diagram, not an original UPIC screenshot.

What exists now `seqPages` cycles pages, Scrub lets you audition positions, and `Pagewarp` provides accelerationdeceleration. None of these stores an independently editable performance trajectory.

What to add A fourth editor called `Performance` or `Sequence`, holding timestamped page references and playhead positions. Include Record Performance, Edit Trajectory, Replace Segment, Loop and Replay.

This would bring back a genuinely important aspect of UPIC the same static drawing could produce multiple different musical performances without changing its arcs.

### Gap 2 — Persistent groups of arcs

High

The New UPIC offered up to four groups on each page. These were not merely temporary multi-selections.

Groups could be created through block, list or criteria-based selection, then given shared sound attributes. A performer could instantly mute, solo, transpose or change the timbre and other characteristics of an entire group. Geometrical transformations could also operate on groups.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



What exists now UPICuitous has multi-selection, an Arc List panel, individual arc mute controls and batch operations.

What's missing persistent, named groups with their own editable performance state, saved in the piece.

I would introduce `ArcGroup` with member IDs and attributes such as group gain, transposition, mute, solo, waveform override, envelope override and output channel. These should be non-destructive overrides, leaving individual arcs unchanged.

A new `Groups` panel should display group names, color swatches and the number of member arcs.

This would also be the logical foundation for your automatic harmonics idea generate harmonic arcs, then place them in one persistent group for manipulation.

### Gap 3 — Actual drawn frequency conversion tables

High

This is where the current implementation is close to the original in purpose, but not in flexibility.

In the New UPIC, a frequency table converted the score's graphical pitch values into oscillator frequencies. Tables could be edited graphically or defined through parameters such as frequency range, tuning and number of divisions per octave. They could also be inverted, and their behavior could be continuous or discrete.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



UPICuitous currently provides four frequency spaces per page, but each is an affine mapping chosen from seven presets

- Page range, half range or double range.
- One octave up or down.
- Half range up an octave, or double range down an octave.

This is useful, but it is a restricted subset of an arbitrary drawable mapping.

Current linear mapping in log-frequency space

Proposed arbitrary editable conversion curve

An actual table editor would allow nonlinear tuning, custom frequency distributions, inversions, plateaus and discontinuities.

I would retain the seven existing presets as convenient starting points, and add an `Edit Table` command allowing the composer to draw the conversion curve.

### Gap 4 — Amplitude conversion tables

High

The New UPIC had four amplitude conversion tables, separate from the bank of amplitude envelopes.

This distinction is essential. The envelope describes the shape of the dynamic evolution, while the conversion table determines how its graphical values are mapped to actual synthesis amplitudes. The historical instrument allowed these tables to be redrawn and assigned to arcs.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



The current `EnvelopevalueAt()` directly interpolates the stored envelope values. There is no additional, assignable amplitude conversion table.

For example, one table might be linear, another heavily compress low amplitudes, and another exaggerate soft envelope movements.

A straightforward implementation would add an `AmplitudeTable` bank, an `amplitudeTableId` to every arc, and a mapping stage after envelope evaluation.

This would create additional timbral and dynamic possibilities while staying faithful to the New UPIC's architecture.

### Gap 5 — Independent modulating arcs

High

The historical New UPIC let a composer assign a modulating arc to another arc, rather than restricting modulation to the carrier's own waveform.

An arc's attributes included its modulating arc, and the system supported frequency modulation during performance.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



UPICuitous now offers waveform-as-amplitude-LFO, waveform-as-frequency-LFO and a separate PM synthesis tab.

Those are useful features, but they are not a complete substitute for independently drawn modulation relationships.

I recommend a graph of arc connections

Concept an independent drawn curve controls another arc's frequency or amplitude.

Start with one assignable modulator per arc, two target types (pitch and amplitude), and an optional modulation depth. It would be important to define how modulator time is aligned with the carrier and to prevent unwanted reference cycles.

This is also the right place to distinguish historically motivated FM behavior from the project's modern PM algorithms.

### Gap 6 — Audio channels and per-channel envelopes

High

The historical New UPIC provided channel assignment as an arc attribute and independent dynamicenvelope controls over the duration of a page.

Its published description mentions 16 controllable output-channel parameters, alongside a four-channel physical converter board. These figures describe different hardwarecontrol layers and should not simply be interpreted as 16 separate physical outputs.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



UPICuitous currently renders through two output channels, with no equivalent per-arc assignable channel bus in `Arc.h`.

The missing model is a multichannel mixer with

 Capability                    Proposed implementation                  
 ----------------------------  ---------------------------------------- 
 Output channel assignment     `ArcchannelId`                         
 Per-channel gain              Mixer channel gain                       
 Per-channel dynamic envelope  Curve across the entire page             
 Per-channel mute and solo     Mixer controls                           
 Routing to physical outputs   JUCE output-bus mapping                  
 Offline rendering             Channel-separated or grouped stem export 

The existing stem renderer gives this feature a useful starting point, but stems are not the same as real-time channel routing.

### Gap 7 — Independent banks for reusable composition objects

Medium–High

The original UPIC stored pages, waveforms, envelopes and other objects in separate banks. The New UPIC extended this to conversion tables and performance sequences, with facilities for copying, renaming, deleting and using objects from different banks.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



UPICuitous now has excellent within-piece wavetable and envelope management. However, the banks live inside the piece model rather than constituting a reusable asset library across compositions.

That means an important historical workflow is still incomplete creating a personal sound library independently of a particular piece.

I would add a persistent library alongside the current embedded assets, with commands to import assets into the current composition or publish them to the library. The piece should continue embedding the assets it needs, so it never depends on an external file to play correctly.

For an authentic 1977-style mode, a separate public bank and personal bank would also be appropriate.

### Gap 8 — Complete graphical transformation tools

Medium

The New UPIC supported geometric operations on selections, including symmetry, rotation and vertical alignment. More generally, the ability to manipulate graphical material as geometry—not merely as musical notes—was central to its approach.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



UPICuitous already has transposition, inversion, retrograde, augmentation and diminution, plus procedural generators. The missing portion is a more general geometric transformation system.

 Proposed tool                 Operation                                                
 ----------------------------  -------------------------------------------------------- 
 Rotate                        Rotate a selection in normalized time–pitch coordinates  
 Reflect across a chosen axis  Symmetry around a user-defined line                      
 Align                         Align starts, ends or frequencies of selected arcs       
 Scale selection               Stretch the entire selection around a movable anchor     
 Repeat pattern                Copy a graphical arrangement with time and pitch offsets 
 Transform group               Apply any operation to a stored arc group                

Rotation needs a carefully defined coordinate system the same rotation in screen pixels and in normalized time–log-frequency space produces different musical results.

### Gap 9 — Simultaneous page windows

Medium

The New UPIC could display up to four pages simultaneously in movable, resizable windows. Only one was played at a time, but performers could switch between them.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



UPICuitous supports more stored pages than the historical system, but its main score workspace displays one selected page at a time.

The recent Focus View changes enlarge an individual editor; they do not provide side-by-side page workspaces.

A faithful extension would include a `Pages Workspace` with two or four score canvases. Each view would have independent zoom and scroll state, while sharing a single transport and set of project assets.

This would be useful for copying material between pages and preparing changes during live performance.

### Gap 10 — Complete sampled-sound extraction

Medium

The historical UPIC evolved beyond manually drawn waveforms. Later versions could derive waveforms—and in the New UPIC, envelopes—from sampled sound.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.iannis-xenakis.org&sz=32)

Iannis Xenakis+1



UPICuitous has microphone recording, loaded sample materials, and `Slot - Wave`, which resamples a recording into one oscillator waveform.

However, I did not find an equivalent Sample → Envelope function. Nor does the current `waveFromTake()` provide detailed cycle selection or waveform extraction based on periodicity; it resamples the chosen take into 1,024 waveform samples.

Suggested additions include a waveform-cycle selector, zero-crossing alignment, envelope extraction using RMS or peak analysis, and a preview that compares the original sound with the reconstructed oscillator sound.

### Gap 11 — A more complete polyagogic curve system

High conceptually

One of UPIC's deepest ideas was that drawing could operate across sound's different time scales. A curve could define waveform structure, amplitude, pitch or a control function, depending on its interpretation.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.iannis-xenakis.org&sz=32)

Iannis Xenakis



UPICuitous's `Wave as` functionality is an important improvement, but it is not a fully general system of curve reinterpretation.

The underlying data types remain separate

 Drawn object     Current representation            
 ---------------  --------------------------------- 
 Arc              Time, frequency, intensity points 
 Wavetable        Fixed-size amplitude samples      
 Envelope         Normalized amplitude points       
 Frequency space  Preset range mapping              

I recommend one reusable `GraphicalCurve` representation with explicitly configurable coordinate mappings and transformation operations. It should support commands such as `Arc → Waveform`, `Arc → Envelope`, `Envelope → Frequency Table`, and `Waveform → Modulation Curve`.

These conversions should produce new objects initially rather than silently reinterpreting or changing existing objects.

This is a larger architectural change, but it would bring UPICuitous closer to the original philosophy than simply adding more synthesizer presets.

### Gap 12 — Richer live performance controls

Medium–High

Even apart from recording a complete sequence, the New UPIC was designed for performance. A performer could rapidly switch pages, alter group attributes, move backward and forward through the score, and manipulate sound parameters while the page was playing.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpswww.schoenberg.ee&sz=32)

schoenberg.ee



UPICuitous has many underlying mechanisms Scrub, reverse playback, page tempo, acceleration, and editable materials during playback.

What is still missing is a coherent performance-oriented control surface combining these functions.

I would introduce a dedicated performance view with page selection, group controls, playhead trajectory, tempo and recording controls, separate from the more detailed composition workspace.

## 4. Quantitative comparison with the historical hardware

Historical capacities are useful, but need interpretation. The original UPIC A and the 1990 New UPIC had different architectural limits, and some numbers describe loaded objects rather than total stored project assets.

 Parameter                       Early UPIC A             New UPIC, circa 1990     UPICuitous, latest                 
 ------------------------------  -----------------------  -----------------------  ---------------------------------- 
 Waveforms                       32                       64                       200                                
 Envelopes                       55                       128                      50                                 
 Arcs per page                   2,000                    4,000                    2,024                              
 Pages                           24 per project           4 simultaneously loaded  100 per piece                      
 Samples per wavetable           Version-dependent        4,096                    1,024                              
 Entries in a conversion table   —                        16,384                   Preset mapping, no arbitrary table 
 Recorded performance sequences  —                        2                        None                               
 Maximum page duration           Early micromacro range  1 hour                   12 minutes                         
 Synthesis output sample rate    25 or 38.46 kHz          44.1 kHz                 Configurable with audio device     
 Physical audio outputs          Version-dependent        4 per converter board    2 in current app path              

Historical figures derive from Guy Médigue's first-hand account of UPIC A and the technical appendix for the New UPIC. They are not all directly comparable because the systems used different storage and synthesis architectures.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpsdokumen.pub&sz=32)

DOKUMEN.PUB+1

Three quantitative gaps stand out.

First, envelope capacity `PieceLimits.h` currently limits UPICuitous to 50 envelopes. That is fewer even than the 55 available on the early UPIC A, and substantially fewer than the New UPIC's 128.

Second, page density the current 2,024-arc limit closely matches one documented historical capacity, but does not reach the New UPIC's 4,000. The source includes a CPU overload guard, so increasing this limit should depend on benchmark results, not on the historical number alone.

Third, page duration the current maximum of 720 seconds reproduces a historical 12-minute time scale, but not the New UPIC's documented one-hour maximum page. Importantly, that one-hour limit is separate from the 12-minute performance-sequence capacity.

I would not prioritize increasing resolution and limits before implementing the missing composition features. An authentic conversion table or performance sequence would add far more musical capability than increasing the wavetable from 1,024 to 4,096 samples.

## 5. What is not actually missing from historical UPIC

Several attractive ideas should be classified as modern extensions, not restoration work.

 Idea                                              Historical assessment                                                                                       
 ------------------------------------------------  ----------------------------------------------------------------------------------------------------------- 
 Automatically generating harmonic arcs            Compatible with UPIC's approach, but not established here as a standard original UPIC command               
 Additive editor with individual harmonic sliders  A useful modern timbre editor; the original instrument primarily specified waveforms graphically            
 Random colors per sampled arc                     Not a documented original function                                                                          
 Network transport synchronization                 External synchronization was under investigation in the 1990 description, not a confirmed completed feature 
 OSC communication                                 Modern interoperability extension                                                                           
 Granular and phase-vocoder processing             Modern synthesis additions to this implementation                                                           
 Customizable collapse buttons                     Modern UI convenience; the latest code already has them                                                     
 Focus views                                       Modern equivalent of enlarging workspaces; already implemented                                              
 PlayStop click fix                               Software correctness requirement rather than historical functionality                                       

A particularly interesting historical detail concerns your color idea. In Dimitris Kamarotos's account of the UPIC in Greece, Xenakis discussed using color to control additional sound parameters—rather than merely making graphical objects easier to distinguish.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpsdokumen.pub&sz=32)

DOKUMEN.PUB



That suggests a more ambitious direction for UPICuitous make color optionally meaningful.

For example, a selected color could represent a channel, modulation type or frequency space. Random colors would remain available as a display option, but semantic color mapping would be closer to Xenakis's exploratory intentions.

The distinction also applies to stochastic composition. Later researchers have proposed graphically defined probability distributions, but Rodolphe Bourotte explicitly presents that approach as an extension not present in the original UPIC.&#x20;

[image](httpswww.google.coms2faviconsdomain=httpsdokumen.pub&sz=32)

DOKUMEN.PUB



Your existing stochastic generators should therefore be treated as valuable additional functionality, not as proof of historically exact feature coverage.

## 6. Recommended development roadmap

I would organize the missing features into six work packages, each independently testable and suitable for a separate Git branch.

 Phase  Work package                                                   Main source impact                                       Priority 
 -----  -------------------------------------------------------------  -------------------------------------------------------  -------- 
 H1     Authentic graphical conversion tables                          New table model, `FreqSpace.h`, `UpicEngine.cpp`         P1       
 H2     Persistent arc groups and live overrides                       `Arc.h`, `Page.h`, `ScoreEditor.cpp`, group UI           P1       
 H3     Editable performance sequences                                 New sequence model and editor, `MainComponentAudio.cpp`  P1       
 H4     Multichannel routing and channel envelopes                     `Arc.h`, `Page.h`, `UpicEngine.cpp`, audio device setup  P2       
 H5     Cross-project banks and sound extraction                       `LibraryManager.h`, `Waveform.h`, sample processing      P2       
 H6     Generalized curve reuse, geometric tools and multi-page views  Model layer, `ScoreEditor.cpp`, layout                   P2–P3    

### Suggested technical implementation

The most consequential additions to the data model would be these conceptual structures

```

struct ArcGroup {
    int id;
    stdstring name;
    stdvectorint arcIds;
    bool muted = false;
    bool solo = false;
    double gain = 1.0;
    double transposeSemitones = 0.0;
};

struct ConversionTable {
    enum class Kind { Frequency, Amplitude };
    int id;
    Kind kind;
    stdvectorfloat values;
};

struct PerformancePoint {
    double performanceTime;
    int pageId;
    double pagePosition;
};

struct PerformanceSequence {
    int id;
    stdstring name;
    stdvectorPerformancePoint trajectory;
};

```

These are architectural sketches, not drop-in code. They would need stable ID handling, serialization, undo transactions, model validation and lock-free publication consistent with UPICuitous's existing engine.

The key design requirement is to keep historical features optional and backward-compatible. An older `.upic.json` piece should continue loading and rendering identically when no conversion tables, groups, channel buses or performance sequences have been assigned.

### Tests that matter

For historical fidelity, tests should cover not only whether the GUI command works, but whether the new concepts behave musically.

For example, an amplitude table must change the sound without changing an envelope's underlying geometry. A performance sequence must reproduce the same recorded playhead motion after saveload. Group transposition must not destroy the member arcs' original pitches. A frequency table must be independently editable without corrupting unrelated pages.

I would also create a small historical reference suite reconstruct sample UPIC-style pages and waveforms, capture expected output characteristics, then compare engine behavior across future revisions.

This would provide a better basis for claims of fidelity than matching historical screenshots.

## 7. Research sources and further verification

These are the most valuable sources I identified, ranked by relevance to the missing-feature analysis.

[Formalized Music by Iannis Xenakis, Sharon Kanach](httpsimages.openai.comstatic-rsc-4UDnQ6yBchH-8dBXG44stZ6GVj6ogdHhK0C2QGU18vojzptc5_1Pw4KflWjAIw30G3VvztUbgA9gsVx1V1ejqglgy1A1tdvV6BYSSJn0Yqud9bhhUS4UKdg11avAao-mrMmjngWRuszAN2ydM2kCfvVHOlCL6NCPuFl1cah1Fq0spurpose=inline)

Formalized Music Thought and Mathematics in Composition — Appendix III The New UPIC System

The most important technical reference for the 1990 architecture arc groups, conversion tables, modulation, channels, performance sequences, live editing and hardware capacities.

[Read the technical appendix](httpswww.schoenberg.eeXENAKIS_WWWXENAKIS_UPIC_FORMALIZED_MUSIC_APP3_EN_EE.htmutm_source=chatgpt.com)

[Book Release From Xenakis’s UPIC to Graphic Notation Today  ZKM](httpsimages.openai.comstatic-rsc-4C4ZdwNk1WtR_dEw6B2389_RyAwAevUbdLj8pwuMkSoCyyA3Rx6wNknPgF2qMuOd23ePguFKhmXYGw1NW9DKIUdIXAisjVZ2MpYWbMQ5uLhcJl2g8u2GAlNWtSg7O1sSVrBMbjd5LAuelt7JbHlPayU4HKA6tSWqdLdVdDh06YRopurpose=inline)

From Xenakis’s UPIC to Graphic Notation Today — ZKM  Hatje Cantz

An extensive historical collection with first-hand contributions by Guy Médigue, Brigitte Robindoré, Dimitris Kamarotos, Julio Estrada and others. Particularly valuable for understanding the differences between generations of UPIC.

[Official open-access publication](httpszkm.deenfrom-xenakiss-upic-to-graphic-notation-todayutm_source=chatgpt.com)

Centre Iannis Xenakis — UPIC historical presentation

Authoritative historical overview of the original system and its development through UPIX.

[View UPIC history](httpswww.centre-iannis-xenakis.orgcix_upic_presentationlang=en&utm_source=chatgpt.com)

[UPIC - Graphic Interfaces for Notation Conference  ZKM](httpsimages.openai.comstatic-rsc-43N3Nleomx6ttVOI4JZ68LjnEZ-JhkL-DKVy0lqYN2VrNE50x3hELQ0sDvT1S5TOjkCHPuPnIRMr89tmkIcKGWWzbwjphKSWirSiJQrMtMNyB6E3uU76HRhgo0o2_8dp_94ijeAidm68bH1Ux8YEMFb9zkBsXd4THUnvnuCVkBUcpurpose=inline)

Julian Scordato — From UPIC to IanniX

A useful independent historical summary covering UPIC's graphical architecture, its 1990 changes and its legacy in software notation systems.

[Read research article](httpsecontact.ca19_3scordato_iannix.htmlutm_source=chatgpt.com)

One especially useful next historical source would be Brigitte Robindoré's The UPIC User's Guide and Tutorial (1992), identified in the ZKM collection as a Centre Iannis Xenakis archival document (4224). I found references to it, but did not obtain and verify the complete manual. It could reveal operational commands and edge cases that the published 1990 overview omits.

This research compares documented capabilities with the uploaded source and documentation. It is a source-level feature audit, not a completed runtime acceptance test of every interface command or an audio comparison against surviving UPIC hardware.

## Final assessment

UPICuitous has successfully recreated the basic graphical synthesis paradigm of Xenakis's UPIC, and it already exceeds the historical machine in some modern sound-processing capabilities.

The biggest remaining work is not more oscillators, more presets or a closer reproduction of the old drawing board. It is the missing middle layer between drawing sound objects and performing an entire composition.

I would prioritize these three features together

1. Editable performance sequences, 2. persistent groups of arcs, and 3. fully drawable frequencyamplitude conversion tables.

Those features would transform UPICuitous from a capable graphical synthesizer into a substantially more faithful recreation of the New UPIC compositional instrument.

The wider goal should be to reproduce not just how the original UPIC looked or how its oscillators sounded, but the unusual freedom it gave composers to move between graphical form, sound synthesis, and live musical interpretation.