#pragma once
#include <JuceHeader.h>
#include <atomic>
#include "UpicModel.h"
#include "UpicEngine.h"
#include "ModelSnapshot.h"
#include "ScoreEditor.h"
#include "ArcListPanel.h"
#include "MidiHub.h"
#include "WaveformEditor.h"
#include "EnvelopeEditor.h"
#include "OverlayPanels.h"
#include "OscScrubReceiver.h"

//==============================================================================
// Main UPIC window: transport + score page + wavetable/envelope editors.
// X=time, Y=pitch(log). Each arc = wavetable osc * envelope * intensity.
//==============================================================================
class MainComponent : public juce::AudioAppComponent,
                      public juce::Timer,
                      private OscScrubReceiver::Listener
{
public:
    // pluginMode (v1.11): VST3 wrapper - the host drives prepareToPlay /
    // getNextAudioBlock instead of an audio device, and the Live-tab input tap
    // reads the host's input bus (see setPluginInputChannels).
    explicit MainComponent(bool pluginMode = false);
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // v1.15: the score render for one block (into its own buffer, so the
    // caller can fade it when the block cannot be rendered).
    // v1.18 (wave 15): the render comes from the lock-free snapshot, so the
    // audio thread never takes dataLock any more.
    void renderScoreBlock(const ModelSnapshot& snap, float* left, float* right, int numSamples);

    // v1.30 (5.3 refactor): the parts of the audio block that were inlined
    // in getNextAudioBlock() are their own methods. They all run on the
    // audio thread, so they only touch the members.
    struct AudioBlock
    {
        float* outL = nullptr;      // the device / host buffer
        float* outR = nullptr;
        float* scoreL = nullptr;    // the rendered score
        float* scoreR = nullptr;
        float* freezeL = nullptr;   // the frozen (held) score
        float* freezeR = nullptr;
        int numSamples = 0;
        bool playingAtStart = false;
    };

    void mixMonitorInput (float* left, float* right, int numSamples);
    void applyScoreFade (float* scoreLeft, float* scoreRight, int numSamples,
                         int fadeSamples, bool rendered, bool modelChanged);
    void mixMasterGain (float* left, float* right, const float* scoreLeft,
                        const float* scoreRight, int numSamples);
    void applyStopFade (float* left, float* right, int numSamples, bool stopping);
    void renderScrubPreview (const ModelSnapshot& snap, AudioBlock& block, double previewGain);
    void renderScrubFreeze (const ModelSnapshot& snap, AudioBlock& block, double freezeGain);

    // v1.15: call after a change that replaces the sound that is playing
    // (page change, loaded material, pitch-mode switch) - the audio thread
    // fades across it instead of cutting
    void markModelChanged() { modelEpoch.fetch_add(1, std::memory_order_relaxed); }

    // v1.16: the host can only compensate ONE latency for the whole plugin,
    // and the score paths have none while the live path has a lot - so the
    // latency is only reported when the page is made of delayed paths alone.
    // The plugin (and only the plugin) publishes it to the host.
    std::function<void(int)> onLatencyChanged;
    int liveLatencySamples() const;
    void publishLatency();      // hand the current latency to the host
    int publishedLatency = -1;   // the value last handed to the host
    double getAudioSampleRate() const { return audioSampleRate; }
    std::atomic<int> timerTicks { 0 };  // diagnostics: how often the status timer ran

    // v1.20 (wave 18): the live reverse - the frozen segment the shifter
    // reads (mirrored in time) and the state of the effect.
    const juce::AudioBuffer<float>& reversePlaybackBuffer() const { return revBuf[revPlayIdx]; }
    int reverseBufferFrames() const { return revBufFrames.load(std::memory_order_relaxed); }
    bool reverseIsOn() const { return revLiveOn.load(std::memory_order_relaxed); }

    // v1.23 (wave 20): the MIDI foundation, for the selftest.
    MidiHub& midiHub() { return midi; }

    // v1.29: the OSC scrub receiver (phone/MobMuPlat).
    bool oscIsListening() const { return osc.isListening(); }
    int oscPortForTest() const { return osc.getPort(); }
    int oscMulticastPortForTest() const { return osc.getMulticastPort(); }
    juce::String oscMulticastGroupForTest() const { return osc.getMulticastGroup(); }
    bool settingsVisibleForTest() const { return settingsOverlay.isVisible(); }
    bool oscEnabledForTest() const { return osc.isEnabled(); }
    double pageDurationForTest() const { return curPage().durationSec; }
    double playheadForTest() const { return playheadSec; }
    bool externalScrubActiveForTest() const { return externalScrubActive; }
    bool isPlayingForTest() const { return isPlaying.load (std::memory_order_relaxed); }
    void injectOscMessageForTest (const juce::OSCMessage& m) { osc.handleMessageForTest (m); }
    void updateExternalScrubForTest (double nowMs) { updateExternalScrub (nowMs); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    // --- construction ---
    // v1.30 (5.3 refactor): the constructor used to be 1400 lines long. It
    // is now one call per group of controls, and each group lives in its
    // own method, in the order the groups appear in the window.
    void buildTransportControls();
    void buildViewToggles();
    void buildAudioOverlay();
    void buildSettingsOverlay();
    void buildClearOverlay();
    void buildLiveControls();
    void buildCombos();
    void buildToolbarControls();
    void buildMaterialControls();
    void buildSieveControls();
    void buildArcListPanel();
    void buildMidiControls();
    void buildMaterialTabs();
    void buildPmControls();
    void buildSampleControls();
    void buildLiveInputControls();
    void buildScoreArea();
    void buildEditors();

    // --- the layout ---
    // v1.30 (5.3 refactor): resized() was 460 lines. The flow layout (the
    // rows, the scale fitting, the placing) stays in resized(); the side
    // column, the tab strip and the overlay cards have their own methods.
    void layoutSideColumn (const juce::Rectangle<int>& bounds);
    void layoutOverlays (float scale);

    // --- data ---
    std::vector<upic::Waveform> waveforms;
    std::vector<upic::Envelope> envelopes;
    std::vector<upic::SampleSlot> sampleSlots; // fixed 8 slots, ids 1..8
    std::vector<upic::Page> pages;
    int currentPage = 0;
    int selWave = 0, selEnv = 0, selSlot = 0;
    int drawMatMode = 0;                 // 0 = oscillator, 1 = sample pitch-shift
    double drawRef = upic::kDefaultRefFreq;
    float drawIntensity = 0.8f;

    UpicEngine engine;
    UpicEngine scrubEngine;   // v1.28: dedicated engine for scrub previews
    juce::CriticalSection dataLock;   // message-thread model lock; the audio
                                      // thread never takes it (v1.18)
    double playheadSec = 0.0;         // message thread's view (ScoreEditor)
    double audioSampleRate = 44100.0; // from prepareToPlay (device may differ)
    double audioPlayRate = 1.0;     // audio-thread view of the direction
    int    seenDirChangeSerial = 0;
    int    dirFadeOutRemaining = 0;
    int    dirFadeInRemaining  = 0;
    std::atomic<bool> isPlaying { false };
    std::atomic<double> playRate { 1.0 };   // +1 forward, -1 backward
    std::atomic<double> prevPlayRate { 1.0 };
    std::atomic<int>    dirChangeSerial { 0 };
    double scrubVelocity = 0.0, scrubTarget = 0.0;
    double lastStopClickMs = -1.0e9;  // v1.27: double-click Stop = rewind
    std::vector<juce::Rectangle<int>> toolbarGroups; // one panel per toolbar row
    bool   scrubActive = false;
    bool   scrubAudition = true;            // v1.28: short audio preview while scrubbing
    std::atomic<bool> arcEditing { false }; // an arc is being dragged/drawn
    std::atomic<bool> muteWhileDragging { true };        // skip model publishes while editing
    std::atomic<int> dragSmoothMs { 20 };              // fade time on release
    std::atomic<bool> scrubPreviewActive { false };
    std::atomic<bool> scrubFreezeActive { false };
    std::atomic<double> scrubPreviewPlayhead { 0.0 };
    std::atomic<double> scrubRate { 1.0 }; // signed scrub speed (drag direction)
    std::atomic<int> scrubPreviewSerial { 0 }; // resets the scrub voice on each drag event
    int seenScrubPreviewSerial = 0;
    std::atomic<double> scrubFreezePlayhead { 0.0 };
    std::atomic<double> scrubFreezeStopAtMs { 0.0 };
    std::atomic<double> scrubFreezeTransitionStartMs { 0.0 };
    std::atomic<double> scrubFreezeTransitionEndMs { 0.0 };
    std::atomic<int> scrubPreviewRemaining { 0 };
    std::atomic<int> scrubFreezeStartSerial { 0 };
    int seenFreezeStartSerial = 0;
    int scrubPreviewLen = 0;               // samples; set in constructor
    double lastScrubT = 0.0;
    double lastScrubMs = 0.0;
    int scrubCrossMs = 20;
    int scrubTailMs = 30;
    int scrubGrainMs = 40;                 // freeze-hold grain size
    int sampleGrainMs = 40;                // granular playback grain size
    int liveGrainMs = 40;                  // v1.30: granular grain size for LIVE arcs
    bool loopPage = true;
    std::atomic<float> masterGain { 0.8f };

    // --- v1.18 (wave 15): the lock-free model handover ---
    // The message thread builds a ModelSnapshot (30 Hz timer, plus right
    // after a load / capture / sample change) and publishes it; the audio
    // thread renders from the snapshot it last took and never locks.
    SnapshotHandover<ModelSnapshot> snapshots;
    ModelSnapshot* liveSnapshot = nullptr;          // audio thread only
    std::atomic<int> pageEpoch { 0 };               // message -> audio
    int seenPageEpoch = -1;                         // audio
    int audioPage = 0;                              // audio thread's page
    double audioPlayhead = 0.0;                     // audio thread's playhead
    std::atomic<double> seekTo { 0.0 };             // message -> audio
    std::atomic<int> seekSerial { 0 };
    int seenSeekSerial = 0;                         // audio
    std::atomic<double> playheadPublished { 0.0 };  // audio -> UI display
    std::atomic<bool> playheadAtEnd { false };      // the piece ran to its end
    std::atomic<int> engineResetSerial { 0 };       // message -> audio (Play)
    int seenEngineResetSerial = 0;                  // audio

    // --- v1.20 (wave 18): the live reverse ---
    // Two buffers of the maximum duration: one is captured (the write head
    // moves BACKWARDS, so its content is mirrored in time - reading it
    // forward plays it backwards), the other is the frozen segment the
    // shifter reads. When the capture is full the two swap roles, so the
    // capture never stops and nothing is ever copied (no audio-thread
    // spike).
    //
    // Ownership: the message thread owns the settings (the duration, the on
    // / off switch) and only ever *asks*; the audio thread owns the buffers,
    // the heads and the indices. The buffers are allocated once (at the
    // maximum duration) - the message thread never reallocates them while
    // the audio thread may be reading.
    juce::AudioBuffer<float> revBuf[2];
    std::atomic<bool> revLiveOn { false };
    std::atomic<int> revBufFrames { 0 };   // the chosen duration in frames
    std::atomic<bool> revRestart { false }; // the audio thread resets its state
    double revBufSec = 1.0;                // the message thread's duration
    int revCapIdx = 0;                     // the buffer being captured (audio)
    int revPlayIdx = 1;                    // the buffer being played (audio)
    int revCapHead = 0;                    // the capture write position (audio)
    long long revCapCount = 0;             // the frames captured so far (audio)
    int revPlayHead = 0;                   // the playback read position (audio)

    // --- v1.15: click-free output ---
    // The score renders into its own buffer, so a block that cannot be
    // rendered (the UI thread holds the model lock, or the piece ended) can
    // be faded out instead of cut - a hard cut at a block boundary is a
    // click, and it does not care how big the host's buffer is.
    // The gains are smoothed per sample for the same reason: a step in a
    // gain (a slider move, host automation) is a click.
    juce::AudioBuffer<float> scoreBuf, freezeBuf;
    float scoreGain = 1.0f;   // the score's fade gain (audio thread)
    float scoreTail = 0.0f;   // the score's last sample (for the fade-out)
    float masterSmoothed = 0.8f;
    float inGainSmoothed = 0.8f;
    static constexpr int kScoreFadeSamples = 96; // ~2 ms at 48 kHz
    static constexpr int kDirCrossfadeSamples = 960; // ~20 ms at 48 kHz
    // scrub smoothing (first-guess; needs real-controller validation)
    static constexpr double kScrubSmoothOld = 0.7;
    static constexpr double kScrubSmoothNew = 0.3;
    static constexpr double kScrubTargetDecay = 0.6;
    static constexpr double kScrubHysteresis  = 0.05;
    static constexpr float kGainSmooth = 0.001f;  // ~17 ms time constant

    // The model can change under the audio thread (a page change, a loaded
    // sample, a pitch-mode switch). Those changes replace the sound that is
    // playing, which is a step - a click - so the message thread bumps this
    // counter and the audio thread fades across the change.
    std::atomic<int> modelEpoch { 0 };
    int seenModelEpoch = 0;   // audio thread

    // --- v1.1 piece state ---
    bool seqPages = false;              // page sequencer: cycle all pages while looping
    bool recordPressure = false;        // pen pressure -> per-point intensity
    bool sieveOn = false;
    int sieveM = 12;
    std::vector<int> sieveR { 0 };
    juce::String flashMsg;              // transient status message (timer shows it)
    unsigned int flashUntilMs = 0;

    // --- v1.2 piece state (magnetic sieves, UPISketch-style) ---
    double stickiness = 1.0;            // 0..1 snap strength ("Stickiness" slider)
    double timeGrid = 0.0;              // time grid in divisions/sec (0 = off)

    // --- v1.2 click-free stop: output ramps to silence over kStopFadeSec ---
    static constexpr double kStopFadeSec = 0.03;
    std::atomic<bool> wantsStop { false };
    // The fade position - the audio thread owns it (it counts the samples
    // down); the message thread only asks for the stop.
    int stopFadePos = 0;
    int stopFadeTotal = 0;

    int drawSampleId = 1;               // sample slot of the current draw voice (set by pushVoice)

    // --- v1.4 live input: monitor + capture takes into sample slots ---
    // No live-gate voice: takes become ordinary sample material (varispeed,
    // stems, save all work unchanged). Audio thread touches only atomics +
    // the preallocated buffers; slot install happens on the message thread.
    juce::ToggleButton monBtn{ "Mon" };
    juce::TextButton capBtn{ "Capture" };
    // v1.31 (U4): the UPIC's wave page could be digitised from a
    // recording. This turns the selected slot into a wavetable.
    juce::TextButton waveFromSlotBtn{ "Slot -> Wave" };
    juce::Slider inGainSlider;
    juce::Label inGainLab{ {}, "In" };
    bool inputEnabled = false;
    std::atomic<float> inputGain{ 0.8f };
    std::atomic<bool> monitoring{ false };
    std::atomic<bool> capturing{ false };
    std::atomic<bool> capStopRequested{ false };
    std::atomic<bool> capReady{ false };
    std::atomic<bool> capDiscard{ false };
    std::atomic<bool> capAbortPending{ false };
    std::atomic<int> capStopReason{ 0 }; // 1 user, 2 full, 3 input lost, 4 device changed
    std::atomic<int> capPos{ 0 };
    std::atomic<int> numLiveInputs{ 0 };
    int capMax = 0, capFinalCount = 0, capFinalReason = 0;
    double capRate = 44100.0;
    juce::AudioBuffer<float> capBuf;    // audio thread writes, message thread reads after handoff
    juce::AudioBuffer<float> inScratch; // per-block input copy (read pre-clear)
    static constexpr double kMaxCaptureSec = 30.0;

    // --- v1.5 live pitch-follow: input ring read by mode-2 (live input) arcs ---
    // tapLiveInput writes one block per callback; the engine reads behind the
    // write head (granular shifter lag) in the same callback. Audio-thread
    // state only; the ring vectors are (re)allocated in prepareToPlay.
    std::vector<float> liveRingA, liveRingB;
    int liveRingSize = 0;
    long long liveWritePos = 0;  // absolute frames written
    bool liveTapActive = false;  // input channels present this callback
    static constexpr double kLiveRingSec = 2.0;

    // --- UI: transport ---
    juce::TextButton playBtn{ "Play" }, stopBtn{ "Stop" };
    juce::TextButton saveBtn{ "Save" }, loadBtn{ "Load" }, exportBtn{ "Render WAV" };
    juce::TextButton stemsBtn{ "Stems" };
    juce::TextButton clearBtn{ "Clear" };
    juce::TextButton menuBtn{ "File" };
    juce::TextButton audioBtn{ "Audio" };
    juce::TextButton settingsBtn{ "Settings" };
    juce::ToggleButton loopBtn{ "Loop" }, seqBtn{ "Seq" }, revPlayBtn{ "Rev" }, pressBtn{ "Pressure" }, scrubBtn{ "Scrub" };
    // scrub audition controls
    juce::Label scrubLenLab{ {}, "Scrub len" }, scrubCrossLab{ {}, "Crossfade" }, scrubTailLab{ {}, "Release tail" };
    juce::Label scrubGrainLab{ {}, "Hold grain" };
    juce::Slider scrubLenSlider, scrubCrossSlider, scrubTailSlider, scrubGrainSlider;
    juce::Label grainLab{ {}, "Grain" };
    juce::Slider grainSlider;

    // drag-edit smoothing controls
    juce::ToggleButton muteDragBtn{ "Mute drag" };
    juce::Label dragSmoothLab{ {}, "Smooth" };
    juce::Slider dragSmoothSlider;

    juce::ComboBox pageBox, toolBox, waveBox, envBox, waveEditBox, envEditBox, pmIndexEnvBox, pmPresetBox, pmOpsBox;
    juce::TextButton pmImportBtn{ "Import" }, pmExportBtn{ "Export" };
    juce::TextButton pageAddBtn{ "+" }, pageDupBtn{ "Dup" }, pageDelBtn{ "-" };
    juce::Slider durSlider, masterSlider, intenSlider, pmRatioSlider, pmIndexSlider, pmFeedbackSlider, tempoSlider;
    juce::Label pageLab{ {}, "Page" }, durLab{ {}, "Dur" }, masterLab{ {}, "Master" }, intenLab{ {}, "Inten" };
    juce::Label timeLab, statusLab;

    juce::ToggleButton snapBtn{ "Snap" };
    // v1.8 one strip: each material is a tab AND its own canvas layer
    // (Osc = mode 0, Sample = mode 1, Live = mode 2, Hz grid)
    // v1.7 view toggles: bottom editors + instruction hints (right end of
    // the same strip)
    juce::TextButton waveTog{ "Wave" }, envTog{ "Env" }, hintsTog{ "Hints" };
    bool showWaveEd = true, showEnvEd = true, showHints = true;

    // v1.7 audio settings: an in-app overlay, since Android has no floating
    // dialog windows (a DialogWindow there has no title bar and no close
    // button, so the audio settings could never be dismissed).
    // v1.30 (5.3 refactor): the overlay type lives in OverlayPanels.h.
    AudioOverlay audioOverlay;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;
    juce::TextButton audioOkBtn{ "OK" };

    // v1.30: MIDI + OSC live in their own settings overlay. The audio overlay
    // keeps only the device selector (UPIC-56), and the phone controller's
    // endpoints are configurable here instead of being compile-time constants.
    AudioOverlay settingsOverlay;
    juce::TextButton settingsOkBtn{ "OK" }, oscApplyBtn{ "Apply" };
    juce::Label midiSectionLab{ {}, "MIDI" }, oscSectionLab{ {}, "OSC" };
    juce::Label oscStatusLab{ {}, "OSC starting..." };
    juce::Label oscPortLab{ {}, "UDP port" }, oscGroupLab{ {}, "Group" }, oscMcPortLab{ {}, "M-port" };
    juce::Slider oscPortSlider, oscMcPortSlider;
    juce::TextEditor oscGroupEdit;
    juce::ToggleButton oscMcBtn{ "Multicast" };
    juce::ToggleButton oscOnBtn{ "OSC on" };

    // v1.31 (U1): the polyagogy - the arc's own wave can also be a control
    // signal (an amplitude LFO or a frequency LFO). These live on the Osc
    // tab's material row and apply to the selected arcs.
    juce::ComboBox modUseBox;
    juce::Slider modRateSlider, modDepthSlider;
    juce::Label modUseLab{ {}, "Wave as" }, modRateLab{ {}, "Rate" },
              modDepthLab{ {}, "Depth" };

    // v1.27: Clear is destructive, so it asks first. An in-app overlay keeps
    // this working on Android, where floating dialogs cannot be dismissed.
    // v1.30 (5.3 refactor): the overlay type lives in OverlayPanels.h.
    ClearOverlay clearOverlay;
    juce::TextButton clearConfirmBtn{ "Clear page" }, clearCancelBtn{ "Cancel" };
    // v1.9: live pitch algorithm for LIVE-tab arcs (session-only setting)
    juce::ComboBox livePitchBox;
    juce::Label livePitchLab{ {}, "Live pitch" };

    // v1.20 (wave 18): the live reverse - the microphone's last N seconds
    // played backwards. N (the buffer duration) is the user's choice.
    juce::Label liveRevLab{ {}, "Rev len" };
    juce::ComboBox liveRevLenBox;
    juce::TextButton liveRevBtn{ "Rev" };

    // v1.30: the LIVE tab's granular grain size (the same 5-500 ms range as
    // the SAMPLE tab's Granular control). It only affects the granular live
    // pitch mode; Tape and Phase vocoder ignore it.
    juce::Label liveGrainLab{ {}, "Grain" };
    juce::Slider liveGrainSlider;

    // --- UI: material (sample pitch-shift) row ---
    juce::ComboBox sampleBox;
    juce::TextButton loadSampleBtn{ "Load sample..." };
    // v1.19 (wave 17): the selected slot plays backwards. Session-only (the
    // piece format is untouched), like the pitch modes and the grid.
    juce::TextButton revBtn{ "Rev" };
    juce::Slider refSlider;
    juce::Label slotLab{ {}, "Slot" }, refLab{ {}, "Ref" }, matHintLab;
    // v1.10: how SAMPLE-tab arcs transpose the slot (session-only, like the
    // live pitch mode): varispeed (the original behaviour) stays the default
    juce::ComboBox samplePitchBox;
    juce::Label samplePitchLab{ {}, "Pitch" };

    // v1.8 the one tab strip (replaces the Material dropdown AND the old
    // Score / Live pitch tabs): each material is its own tab + canvas layer
    // - Osc (wavetable), Sample (slots + capture), Live (mic)
    juce::TextButton matOscTog{ "Osc" }, matSampleTog{ "Sample" }, matLiveTog{ "Live" }, matPmTog{ "PM" };

    // v1.21 (wave 19): the arc list - the score's inventory on the right,
    // with the per-arc controls (the Rev box and the mode dropdown). It is a
    // view: the visibility is session state.
    juce::TextButton arcListBtn{ "Arcs" };
    ArcListPanel arcListPanel;
    bool showArcList = false;
    bool arcListDirty = true;   // the rows have to be rebuilt
    int selectedArcId = -1;    // the arc the panel (and the canvas) point at
    std::vector<int> lastArcSelection; // canvas selection the panel last showed

    // v1.23 (wave 20): the MIDI input foundation - the device list, the
    // activity monitor and the learn (in the audio settings overlay).
    MidiHub midi;
    juce::ComboBox midiDevBox;
    juce::Label midiDevLab{ {}, "MIDI in" };
    juce::Label midiActLab{ {}, "no MIDI yet" };
    juce::TextButton midiLearnGain{ "Learn" }, midiLearnInt{ "Learn" }, midiLearnScrub{ "Learn" }, midiLearnTransport{ "Learn" };
    juce::Label midiBindGainLab{ {}, "-" }, midiBindIntLab{ {}, "-" }, midiBindScrubLab{ {}, "-" }, midiBindTransportLab{ {}, "-" };
    juce::Label midiGainLab{ {}, "Master gain" }, midiIntLab{ {}, "Intensity" }, midiScrubLab{ {}, "Scrub" }, midiTransportLab{ {}, "Transport" };

    // v1.29: the OSC scrub receiver. MobMuPlat (or any OSC sender) moves the
    // playhead with /upic/scrub <0..1> on UDP 9000.
    // v1.30 (5.3 refactor): the receiver and its endpoints live in
    // OscScrubReceiver; this class keeps the scrub gesture itself.
    // The receiver is constructed with *this as its listener (see the
    // constructor's initialiser list).
    OscScrubReceiver osc;
    bool externalScrubActive = false;
    bool externalScrubFrozen = false;
    double externalScrubLastT = 0.0;
    double lastExternalScrubMs = 0.0;
    bool oscAnnounced = false;

    // --- UI: sieve quantizer (same row as material) ---
    juce::ToggleButton sieveBtn{ "Sieve" };
    juce::TextEditor sieveMEdit, sieveREdit;

    // --- UI: v1.2 magnetic-sieve row (preset library, stickiness, time grid,
    //     procedural arc generators) + shortcut/wheel hint line ---
    juce::ComboBox sievePresetBox, gridBox, genBox;
    juce::Slider stickSlider;
    juce::Label pmRatioLab{ {}, "Ratio" }, pmIndexLab{ {}, "Index" }, pmIndexEnvLab{ {}, "IdxEnv" }, pmFeedbackLab{ {}, "Fdbk" }, pmPresetLab{ {}, "Preset" }, pmOpsLab{ {}, "Ops" }, tempoLab{ {}, "Tempo" }, stickLab{ {}, "Stick" }, gridLab{ {}, "T-grid" }, genLab{ {}, "Gen" };
    juce::Label kbdHintLab;

    // --- UI: editors ---
    ScoreEditor score;
    WaveformEditor waveEd;
    EnvelopeEditor envEd;
    juce::TextButton wSine{ "Sine" }, wSaw{ "Saw" }, wSqr{ "Sqr" }, wTri{ "Tri" }, wNoise{ "Noise" }, wSmooth{ "Smooth" }, wNorm{ "Norm" };
    juce::TextButton eFlat{ "Flat" }, ePerc{ "Perc" }, eSwell{ "Swell" }, eGate{ "Gate" }, eStr{ "Strings" };
    juce::TextButton assignBtn{ "Assign to sel." }, delArcBtn{ "Del sel." }, muteArcBtn{ "Mute sel." };
    juce::Label voiceLab, waveLab{ {}, "Wave" }, envLab{ {}, "Env" }, toolLab{ {}, "Tool" };

    upic::Page& curPage();
    const upic::Page& curPage() const;
    void refreshBoxes();
    void refreshScoreBinding();
    void updateStatus();
    void publishModelSnapshot();      // v1.18: message -> audio handover
    void seekPlayhead(double t);      // message: move the audio playhead
    void bumpPageEpoch();             // message: the audio adopts the page
    void requestEngineReset();        // message: drop voices on the next block
    double displayPlayhead() const;   // the playhead the UI shows
    void pushVoice();                 // UI -> ScoreEditor current voice
    bool prepareWaveEdit();           // v1.27: fork the wavetable for a selection
    void syncControlsToArc(int arcId); // load an arc into the toolbar
    void updateSampleView();
    void updateRevButton();       // v1.19: the Reverse button follows the slot
    void updateRevBuffers();      // v1.20: (re)allocate the live reverse buffers
    void rebuildArcList();        // v1.21: rebuild the panel's rows from the model
    void handleMidi();            // v1.23: the MIDI messages (message thread)

    // v1.29: the phone/MobMuPlat OSC scrub. The receiver parses the
    // messages and hands the scrub value over; the gesture itself (the
    // preview, the freeze, the timeout) belongs to the app.
    void oscScrubReceived (double normalised) override;
    void handleOscScrub (double normalised);
    void updateExternalScrub (double nowMs);

    void updateMidiLabels();      // v1.23: the learn buttons and the bindings
    void refreshMidiDevices();    // v1.23: rebuild the device list
    void setMaterial(int m);          // v1.8: tab + canvas layer (0 osc, 1 sample, 2 live)
    void syncMaterialTabs();          // tab toggles follow drawMatMode
    void loadSampleIntoSlot(int slotIndex);
    void pushSieve();                 // sieve UI -> state + ScoreEditor
    void runGenerator(int kind);      // procedural arc generation (nUPIC-style)
    void exportStems();               // one WAV per voice into a folder
    void flash(const juce::String& m); // transient status message (4s)
    void startPlayback(), stopPlayback(bool fromAudio = false);
    void renderWav();
    void saveFile(), loadFile();
    void ensureLiveInputFor(std::function<void(bool)> done);
    void requestLiveInput(std::function<void(bool)> done);
    bool enableDeviceInputs();
    bool tapLiveInput(const juce::AudioSourceChannelInfo& info); // true -> apply monitor mix
    void startCapture();
    void requestCaptureStop(int reason);
    void handoffCapture();  // audio thread -> message thread handoff
    void finalizeCapture(); // message thread: install take into selected slot
    juce::String takeName();

public:
    void injectMidiMessage (const juce::MidiMessage&); // v1.25: plugin host MIDI bus
    // Piece (de)serialization — public so the selftest can round-trip them.
    juce::var pieceToVar() const;
    void loadFromVar(const juce::var& v);

    // --- v1.11 VST3 plugin mode ---------------------------------------------
    bool isPluginMode() const noexcept { return pluginMode_; }
    // Host state: the same JSON text a saved .upic.json piece contains.
    juce::String pieceStateText();
    // Host input-bus channel count (0/1/2); the wrapper calls this from
    // prepareToPlay and from each processBlock.
    void setPluginInputChannelCount(int numChannels);

private:
    void showAudioSettings();
    void closeAudioSettings();
    void showSettings();
    void closeSettings();
    void startOscReceiver();
    void stopOscReceiver();
    void applyOscSettings();
    void updateOscStatus();
    void rebindOscReceiver();
    void showClearWarning();
    void closeClearWarning();
    void clearCurrentPage();
    // v1.31 (U3): the UPIC's pitch transformations (transpose, invert).
    void transformPage (int op);
    // v1.31 (U4): the digitised wave page (the slot becomes a wavetable).
    void waveFromSelectedSlot();

    // v1.11 VST3 plugin mode
    bool pluginMode_ = false;
    bool pluginFollowHost = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
