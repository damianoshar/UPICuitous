#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"
#include "LivePitch.h"
#include <map>
#include <algorithm>
#include <atomic>

//==============================================================================
// Polyphonic renderer.
//  mode 0 - wavetable oscillator: phase-accumulating lookup, freq log-interp.
//  mode 1 - sample material: pitch shifts the slot following the arc's
//           frequency curve; ratio = arcFreq/refFreq. Three selectable modes
//           (setSamplePitchMode): varispeed (default) reads the material at
//           the ratio, so the pitch is EXACT for any curve and the slot's
//           playback length scales with it; granular and phase vocoder keep
//           the material advancing at 1x and shift pitch only, so the slot's
//           playback length stays intact. A mode change drops the voices so
//           each re-anchors at its next contact.
//  mode 2 - live input: the host publishes a ring of recent input frames
//           (setLiveInput); the selected live shifter - granular (default),
//           tape or phase vocoder, see setLivePitchMode - reads it to follow
//           the arc's frequency curve in realtime (ratio = arcFreq/refFreq,
//           clamped). Realtime-only: offline renders (WAV/stems) leave it
//           silent.
// Amplitude = intensity * pressure-curve(t) * envelope(life) * 12ms edge fade.
// Per-arc oscillator phase / material voice state is kept across blocks.
//==============================================================================
class UpicEngine
{
public:
    UpicEngine() = default;

    // The largest block a host may send (the same limit the score buffers
    // use). The accumulation buffer is sized for it in prepare(), so the
    // audio thread never grows it.
    static constexpr int kMaxBlockSize = 8192;

    // WP6.3: the number of mix buckets. Bucket 0 is "Main" (the implicit
    // channel every arc without a channel assignment lands in); the piece's
    // channel bank occupies buckets 1..kMaxChannels. So the bank needs
    // kMaxChannels buckets PLUS the Main one - sizing the buffer for
    // kMaxChannels alone silently dropped the last channel of the bank.
    static constexpr int kChannelBuckets = upic::limits::kMaxChannels + 1;

    void prepare(double sampleRate_, int blockSize = 0);


    // v1.33.4 (M21): the vocoder's working memory (~50 KB per voice plus
    // the FFT engine) is prepared here, on the message thread, before the
    // page that needs it starts. The audio thread only reads it: an
    // allocation there can block, and a blocked audio thread is a dropout.
    //
    // `voices` is how many slots to prepare - the caller knows how many arcs
    // the page has. The audio thread hands the slots out in order, so the
    // first N arcs to sound are the ones that find a prepared voice.
    void prepareSampleVocoderMemory (int voices);

    void prepareLiveVocoderMemory (int voices);


    // v1.15: the oscillator phase is free-running state - clearing it makes
    // every arc jump back to phase 0 mid-flight, which is a click. Only the
    // material/ring readers (sample + live voices) have to re-anchor, because
    // the material or the input window changed under them.
    void reset();

    // v1.20: drop the live voices only (they re-anchor on their next
    // contact) - the oscillator phases keep running (v1.15).
    void resetLiveVoices();

    // v1.28: carry oscillator phases from another engine (used to smooth the
    // scrub-preview -> freeze transition). Sample/PM voices are NOT copied;
    // they re-anchor at the freeze position.
    void copyOscillatorPhasesFrom(const UpicEngine& other);

    void noteOffArc(int arcId);

    void setScrubPreview(bool on);

    // Shared with the material view so its time bar uses the same tape
    // position as the audio engine (pitch-scaled, not ratio * elapsed).
    static double scrubMaterialPos(const upic::Arc& arc, double t, double ref, double matDur,
                                   const upic::Page::SpaceMap& map);


    // v1.32: the arc's SOUNDING pitch. The drawn pitch goes through the
    // arc's frequency space: with a discrete temperament (the UPIC's
    // "discrete/non-discrete" function) the continuum becomes a staircase
    // of steps, so a glissando is heard as discrete pitches. With no
    // temperament this is the drawn pitch, unchanged.
    //
    // v1.33: the drawn pitch is first placed in the page's frequency space
    // the arc names (the UPIC's four frequency tables) - the position on
    // the page's vertical axis is mapped into that space - and the
    // temperament then divides the space.
    //
    // The space's mapping is precomputed per arc (see upic::Page::spaceMap),
    // so the per-sample cost is one log2 and one exp2. The map is passed BY
    // VALUE: through a reference the compiler has to reload its five fields
    // on every sample (it cannot prove they do not alias the arc), which
    // cost a third of the render path.
    // WP5: `modFactor` scales the DRAWN frequency before the space map, so a
    // modulating arc bends the pitch inside the same space as the carrier.
    static double arcPitch (const upic::Arc& arc, double t,
                                   upic::Page::SpaceMap map, double modFactor = 1.0);


    // v1.33.4 (U6-C): the cursor-based pitch. The render walks the samples
    // of a block in order, so the arc's segment can be followed instead of
    // searched for every sample.
    static double arcPitch (const upic::Arc& arc, double t,
                                   upic::Page::SpaceMap map,
                                   upic::Arc::PitchCursor& cursor,
                                   double modFactor = 1.0);

    // WP5: the resolved modulator, carried into the render paths. The map is
    // the modulator's own space, so its contour bends the carrier in the
    // same drawn space it was drawn in.
    struct ModContext
    {
        const upic::Arc* arc = nullptr;
        upic::Page::SpaceMap map;
        int target = 0;
        double depth = 0.0, lo = 0.0, hi = 0.0;
        int align = 0;
    };


    // Time scale for a sample voice. Mode 1 (granular stretch) uses the same
    // arc-length mapping in normal playback and scrub; mode 2 (phase vocoder)
    // only maps onto the arc in the scrub preview. Mode 3 (granular no
    // stretch) keeps the slot's own timeline.
    static double sampleArcTimeScale(int mode, bool scrub, double rate,
                                     double matDur, double arcDur);

    void setSampleGrainMs(int ms);

    void setScrubGrainMs(int ms);

    // v1.30: the LIVE tab's granular grain/hop length (5-500 ms).
    void setLiveGrainMs(int ms);


    // Host publishes the live input ring each block (audio thread): frames up
    // to writeEnd are valid (absolute count), stereo ringA/ringB (B may be
    // null for mono). active=false silences mode-2 arcs entirely.
    void setLiveInput(const float* ringA, const float* ringB, int size,
                      long long writeEnd, bool active);


    // v1.21 (wave 19): the reversed live ring - the same content, mirrored in
    // time (the capture's write head moves backwards). The arcs with
    // `reversed` read this one instead of the forward ring.
    void setLiveReverse(const float* ringA, const float* ringB, int size,
                        long long writeEnd, bool active);


    // Live pitch algorithm for mode-2 voices (safe from the message thread):
    // 0 = granular (v1.5 default), 1 = tape, 2 = phase vocoder. A change
    // drops the live voices so each re-anchors on its next contact.
    void setLivePitchMode(int mode);

    int getLivePitchMode() const;


    // Sample pitch algorithm for mode-1 voices (safe from the message
    // thread): 0 = varispeed (default - the slot plays back faster/slower
    // with the arc pitch, the original behaviour), 1 = granular (stretch -
    // maps the slot onto the arc), 2 = phase vocoder (keeps the slot's
    // playback length and shifts pitch only), 3 = granular (no stretch -
    // keeps the slot's length and shifts pitch only). A change drops the
    // sample voices so each re-anchors.
    void setSamplePitchMode(int mode);

    int getSamplePitchMode() const;


    // v1.33.4 (M24): the effective pitch algorithm of an arc - the arc's own
    // override (v1.33.4 / H7), or the tab's setting. The DSP and the latency
    // reporting share this rule: the old latency code consulted only the
    // global setting, so a page whose arc overrides it (a vocoder arc on a
    // granular page) reported the wrong latency to the host.
    int effectiveMode (const upic::Arc& arc, int globalMode) const;


    // Realtime render: playhead in seconds (page-local), outputs stereo (same mono sum).
    // Cost model: each arc only touches its active sample range, with the
    // waveform/envelope lookups hoisted out of the per-sample loop.
    //
    // WP1: `luts` carries the page's compiled frequency tables (from the
    // snapshot). It is optional: without it the drawn tables are evaluated
    // directly (correct, slower), which is what the UI and the tests use.
    //
    // WP6.4: `mix` is the RESOLVED mixer (the plan's `const ChannelMix*`):
    // the channel bank with the solo flags, the gains and the gain curve
    // already sampled into a 64-point LUT. It is built on the message
    // thread (in the snapshot) or on the export's worker thread, never on
    // the audio thread. An empty mix means the legacy stereo path.
    void render(const upic::Page& page, double playhead,
                const std::vector<upic::Waveform>& waves,
                const std::vector<upic::Envelope>& envs,
                const std::vector<upic::SampleSlot>& slots,
                float* left, float* right, int numSamples, float masterGain,
                double rate = 1.0, bool freeze = false,
                const upic::PageLutSet* luts = nullptr,
                const std::vector<upic::Envelope>* ampTables = nullptr,
                const std::vector<upic::ArcRenderOverride>* overrides = nullptr,
                const std::vector<upic::ChannelMix>* mix = nullptr);


    // v1.33.4 (H8): the offline render, in blocks.
    //
    // The export used to render the whole page into one buffer that was
    // given durationSec * sampleRate frames - but the page's own time runs
    // at a varying rate, so a warped page needs more audio than it has
    // score and the end of the page was cut off (an arc at 25-29 s of a 30 s
    // page exported as silence). The render now walks the page's own time
    // with the page's exact time map (the same one the playback uses), so
    // the page is covered whole, and it hands the caller one block at a
    // time: the export streams the blocks into the file, so the memory
    // stays flat whatever the page's length.
    //
    // `totalFrames` is how much audio the caller wants (the page's audio
    // duration, in frames). The callback gets each block and returns false
    // to stop the render (a write error).
    template <typename BlockFn>
    bool renderPageBlocks (const upic::Page& page,
                           const std::vector<upic::Waveform>& waves,
                           const std::vector<upic::Envelope>& envs,
                           const std::vector<upic::SampleSlot>& slots,
                           const std::vector<upic::Envelope>* ampTables,
                           const std::vector<upic::ChannelMix>* mix,
                           double sr, float masterGain,
                           int totalFrames, BlockFn&& fn)
    {
        if (sr <= 0.0 || totalFrames <= 0) return false;
        prepare (sr, 8192);

        // v1.33.4 (U6-B): an export is exact - the overload guard must not
        // shed an arc however long the render takes.
        const bool savedGuardOffline = guardOffline;
        guardOffline = true;

        // the block size: small, so the export's timing matches a typical
        // host block and the memory stays flat
        const int blockSize = 512;
        juce::AudioBuffer<float> block (2, blockSize);
        int done = 0;
        double scorePos = 0.0;

        // WP1: the export walks the page once, so the drawn frequency tables
        // are compiled here, on the caller's (worker) thread, and passed to
        // each block. A preset-only page gets no LUTs and renders exactly as
        // it always did.
        std::array<upic::FreqLut, 4> lutsOwned;
        upic::PageLutSet lutSet;
        for (int s = 0; s < 4; ++s)
        {
            const auto& def = page.spaceDefs[(size_t) s];
            if (def.editable || def.inverted)
            {
                lutsOwned[(size_t) s] = upic::buildFreqLut (def, page.pitchLo, page.pitchHi);
                lutSet.lut[s] = &lutsOwned[(size_t) s];
            }
        }
        // WP3: the groups are resolved once for the whole page, on this
        // (worker) thread - the block loop only indexes them.
        std::vector<upic::ArcRenderOverride> overrides;
        if (! page.groups.empty())
            overrides = page.resolveArcOverrides();

        while (done < totalFrames)
        {
            const int n = juce::jmin (blockSize, totalFrames - done);
            // the page's time map: the score position at the end of this
            // block, and the block's average rate (so the block's content
            // and the playhead agree). Without a warp the rate is exactly 1,
            // so an unwarped page renders exactly as it always did.
            const double a0 = page.audioTimeAtScore (scorePos);
            const double a1 = a0 + (double) n / sr;
            const double s1 = page.scoreAtAudioTime (a1);
            const double avgRate = page.averageRate (scorePos, (double) n / sr);

            block.clear();
            render (page, scorePos, waves, envs, slots,
                    block.getWritePointer (0), block.getWritePointer (1),
                    n, masterGain, avgRate, false, &lutSet, ampTables,
                    overrides.empty() ? nullptr : &overrides, mix);
            if (! fn (block, n)) { guardOffline = savedGuardOffline; return false; }

            scorePos = s1;
            done += n;
        }
        guardOffline = savedGuardOffline;
        return true;
    }

    // Offline render of a whole page to a buffer (stereo interleaved in two buffers)
    void renderPageOffline(const upic::Page& page,
                           const std::vector<upic::Waveform>& waves,
                           const std::vector<upic::Envelope>& envs,
                           const std::vector<upic::SampleSlot>& slots,
                           juce::AudioBuffer<float>& dest, double destSampleRate, float masterGain);


    //==========================================================================
    // v1.33.4 (U6-B): the overload guard.
    //
    // A page can ask the audio thread for more work than one block has time
    // for: the 2024-arc cap (U6 option A) allows a page whose arcs all
    // sound at once, and that costs ~2.5x a 48 kHz/512-sample block even
    // with the cursor path. The guard measures every
    // block; when the smoothed load stays over the budget it fades out the
    // quietest sounding arcs - over 5 ms, inside the block, so a dropped arc
    // cannot click - and skips them once silent. When the load drops, the
    // limit grows again and the arcs fade back in the same way.
    //
    // The per-arc gain lives in a fixed table keyed by arc id (ids are
    // unique per piece, L13), so the audio thread never allocates and the
    // table only holds the arcs that are actually fading or silent. An
    // export never runs the guard (renderPageBlocks sets guardOffline), so
    // the file is exactly what the score says however long it takes.
    //
    // The tests drive the limit directly, because the adaptive policy is a
    // function of the machine's speed.
    struct GuardState
    {
        float load = 0.0f;   // smoothed render time / block time
        int limit = 0;       // the arc limit in force (0 = the guard is idle)
        int silenced = 0;    // arcs faded out in the last block
    };

    GuardState guardState() const noexcept;


    // The app and the plugin turn the guard on in their prepare; the raw
    // engine leaves it off, so a test renders exactly what the score says.
    void setOverloadGuardEnabled (bool on) noexcept;
    // WP6.3: -1 renders every channel; 0 = Main; i+1 = bank channel i.
    void setChannelFilter (int ch) noexcept { channelFilter = ch; }


    // 0 = off, a positive number = that many arcs (the policy does not
    // adapt). The default is the adaptive policy.
    void setGuardLimitForTest (int limit);


    // the gain the guard is applying to an arc (1.0 = untouched)
    float guardGainForTest (int id) const noexcept;

    // F1: how many SOUNDING voice slots are live (an id >= 0). The
    // oscillator phase slots are free-running state and survive a reset on
    // purpose, so they are not counted. The transport's stop fade must leave
    // the sample/PM/live pools empty once the state is Stopped; the selftest
    // checks that no voice is stuck after a rapid Play/Stop sequence.
    int activeVoiceCountForTest() const noexcept;


private:
    double sampleRate = 44100.0;

    // v1.33.4 (M21): the voices live in fixed pools, not in std::map. A map
    // allocates a node for every voice - in the audio thread - and frees it
    // again on a reset. The pool's storage is reserved once, in prepare()
    // (the message thread), and a reset only marks the slots free, so the
    // audio thread neither allocates nor frees.
    //
    // The cap is the piece format's arc cap (H13, U6 option A): a page
    // cannot hold more arcs than that, so the pool cannot run out.
    static constexpr int kPoolCapacity = 2048; // >= kMaxArcsPerPage (2024)

    // How many slots of the voice pools are built in prepare() (the message
    // thread). A page with more arcs than this grows the pool while it
    // renders - which allocates nothing in a release build, because the
    // pool's storage is reserved.
    static constexpr int kPrebuiltVoices = 64;

    struct PhaseSlot { int id = -1; double phase = 0.0; };
    struct PmVoice { double carPhase = 0.0, modPhase = 0.0, prevModOut = 0.0; double ph[4] = {}; double prev[4] = {}; int ops = 2; bool prepared = false; void prepare(double){} };
    struct SampleSlot { int id = -1; upic::SampleVoice voice; };
    struct PmSlot { int id = -1; PmVoice voice; };
    struct LiveSlot { int id = -1; upic::LiveShifter shifter; };

    std::vector<PhaseSlot> phases;
    std::vector<SampleSlot> sampleVoices; // mode-1 voice state per arc
    std::vector<PmSlot> pmVoices; // mode-3 PM voice state per arc
    std::vector<float> scratch; // per-block accumulation, reserved in prepare()

    // WP6.3: the per-channel buckets. Routing is on only when the piece has
    // channels, so the legacy single-scratch path stays bit-exact.
    std::vector<float> chanBuf;
    bool chanRoutingActive = false;
    int  chanStride = 0;
    int  mixChannel = 0;
    int  channelFilter = -1;
    void addToMix (int n, float v);


    // live input view (published each block by the host; audio thread only)
    const float* liveRingA = nullptr;
    const float* liveRingB = nullptr;
    int liveRingSize = 0;
    long long liveWriteEnd = 0; // absolute frames written into the ring
    bool liveActive = false;
    // v1.21: the reversed live ring (the same content, mirrored in time)
    const float* revRingA = nullptr;
    const float* revRingB = nullptr;
    int revRingSize = 0;
    long long revWriteEnd = 0;
    bool revActive = false;
    std::vector<LiveSlot> liveShift; // live-mode voice state per arc
    std::atomic<int> livePitchMode { 0 }; // written by the message thread
    int livePitchModeApplied = 0;         // audio thread: mode of the live voices
    std::atomic<int> samplePitchMode { 0 }; // written by the message thread
    int samplePitchModeApplied = 0;         // audio thread: mode of the sample voices
    bool scrubPreview = false;              // scrub engine: vocoder maps to the arc, tape speed follows the drag
    std::atomic<int> sampleGrainMs { 40 };  // granular grain size (normal playback)
    std::atomic<int> scrubGrainMs { 40 };   // granular grain size while freeze-holding
    std::atomic<int> liveGrainMs { 40 };    // granular grain size for LIVE arcs

    // ---- v1.33.4 (U6-B): the overload guard ----
    //
    // The table only holds the arcs whose gain is not 1.0 (fading or
    // silent); a fully restored arc is removed, so the table stays small
    // and the hash keeps the per-arc lookup O(1). The table is sized in
    // prepare() and the audio thread only reads and writes it.
    static constexpr int kGuardTableSize = 4096; // power of two; > 2x the arc cap
    static constexpr int kGuardMaxArcs = 2048;   // the level scratch / the "off" point
    struct GuardSlot { int id = 0; float gain = 1.0f; bool used = false; };

    std::vector<GuardSlot> guardTable;
    std::vector<float> guardLevels;  // one level per page arc, per block
    int guardUsed = 0;               // slots in use
    int guardFading = 0;             // arcs whose gain is below 1.0
    bool guardEnabled = false;       // the app/plugin turn it on in prepare
    float guardLoad = 0.0f;          // smoothed render time / block time
    int guardMaxArcs = 0;            // 0 = no limit
    bool guardAdaptive = true;       // false after setGuardLimitForTest()
    int guardSilenced = 0;           // arcs skipped in the last block
    float guardThreshold = -1.0f;    // the level of the last arc kept
    bool guardOffline = false;       // the offline render must not shed arcs

    static unsigned guardHash (int id) noexcept;


    float guardGainOf (int id) const noexcept;


    // The standard backward-shift deletion: a hole must not break the probe
    // chain of the slots that follow it.
    void guardRemoveAt (unsigned i) noexcept;


    void guardSetGain (int id, float gain) noexcept;


    static float guardGainAt (float g0, float gStep, int n) noexcept;


    // ---- v1.33.4 (M21): the voice pools ----
    //
    // Every helper below only marks slots free or hands out a slot that is
    // already there. Nothing in them allocates: the pool's storage was
    // reserved in prepare() (the message thread), and a slot keeps its
    // buffers when it is released.

    static PhaseSlot* phaseSlotFor (std::vector<PhaseSlot>& pool, int id);

    static SampleSlot* sampleSlotFor (std::vector<SampleSlot>& pool, int id);

    static PmSlot* pmSlotFor (std::vector<PmSlot>& pool, int id);

    static LiveSlot* liveSlotFor (std::vector<LiveSlot>& pool, int id);


    // The phase of an arc, creating the slot if the arc is new. The slot's
    // phase is free-running state (v1.15), so it survives a reset.
    double& phaseFor (int id);


    void releasePhaseSlots();

    void releasePhaseSlot (int id);

    void releaseSampleSlots();

    void releaseSampleSlot (int id);

    void releaseLiveSlots();

    void releaseLiveSlot (int id);

    void releasePmSlots();


    // Take a free slot for an arc, or make a new one. The pool's storage is
    // reserved, so this does not allocate (unless the pool is full, which
    // the piece format's arc cap rules out).
    SampleSlot* acquireSampleSlot (int id);

    LiveSlot* acquireLiveSlot (int id);

    PmSlot* acquirePmSlot (int id);


    double overflowPhase = 0.0; // the fallback when the phase pool is full

    static float edgeFade(double t, double arcStart, double arcEnd);


    // Varispeed material position for a scrub reset: integrate the arc's
    // pitch ratio from the arc start to `t`, so a falling pitch curve still
    // advances the sample forward instead of flipping direction.
    static double pitchScaledPos(const upic::Arc& arc, double t, double ref, double matDur,
                                 const upic::Page::SpaceMap& map);


    // Material read: mono mix of the slot at a position in seconds, wrapped
    // over the seamless loop, linear interpolation (shared by every mode).
    // Reads the material at `pos` seconds. The interpolation and the wrap
    // rules are the same in both directions; a reversed slot mirrors the
    // read (the material's last frame comes first), so every pitch mode
    // works on the mirrored material without touching the shifter.
    static float readMaterial(const upic::SampleSlot& smp, double pos, bool mirrored);


    // Mode-1 voice: pitch follows the arc's frequency curve (ratio = arcFreq
    // / refFreq). Varispeed (the original behaviour) advances the material
    // position at the pitch ratio, so pitch and playback length change
    // together. Granular (no stretch) keeps the slot's own playback length
    // and only shifts its pitch; Granular (stretch) maps the whole slot onto
    // the arc's x-axis in both playback and scrub. The phase vocoder keeps
    // its slot length during playback and maps onto the arc only in the
    // scrub preview.
    // Voices anchor on first contact (fresh after seek/reset) and wrap over
    // the slot's seamless loop.
    void renderSampleArc(const upic::Arc& arc, const upic::SampleSlot& smp,
                         double playhead, int i0, int i1, double invSr,
                         const upic::Envelope* e, const upic::Envelope* ampTable,
                         const ModContext& mod, const upic::Page::SpaceMap& map,
                         double rate = 1.0, bool freeze = false,
                         float g0 = 1.0f, float gStep = 0.0f);


    // Live input follows the arc's pitch: the selected live shifter (granular
    // / tape / phase vocoder) reads the host ring; ratio = arcFreq/refFreq
    // like mode 1. Voice state anchors on first contact (fresh after
    // seek/reset) and then rides the write head, so the latency stays
    // constant.
    //
    // v1.21 (wave 19): the arc can override the algorithm (its own pitch
    // mode) and can read the REVERSED ring (the same content, mirrored in
    // time) instead of the forward one.
    void renderLiveArc(const upic::Arc& arc, double playhead, int i0, int i1,
                       double invSr, const upic::Envelope* e, const upic::Envelope* ampTable,
                       const ModContext& mod, const upic::Page::SpaceMap& map,
                       double rate = 1.0, float g0 = 1.0f, float gStep = 0.0f);


    // WP6.3: -1 renders every channel; 0 = Main; i+1 = bank channel i.
    static const upic::Waveform* findWave(const std::vector<upic::Waveform>& v, int id);

    static const upic::Envelope* findEnv(const std::vector<upic::Envelope>& v, int id);

    // Exact slot if it has audio, else the first loaded slot (so material
    // still sounds if the arc points at an empty slot by mistake).
    static const upic::SampleSlot* findSlot(const std::vector<upic::SampleSlot>& v, int id);

};

//==============================================================================
// v1.33.4 (H11): write a WAV without ever destroying the previous one.
//
// The export used to delete the destination first (createOutputStream() appends
// on Windows), so a failed export - a locked file, a full disk, a missing
// folder - left the user with nothing at all. The render now goes to a
// temporary sibling and replaces the target only once the whole file is on
// disk. The result is reported: false means "nothing was written", and the
// file that was there before is untouched.
bool writeWavFile (const juce::File& target,
                          const juce::AudioBuffer<float>& buf,
                          double sr);


//==============================================================================
// v1.33.4 (H8): the page rendered straight into a WAV file, block by block.
//
// The export used to render the whole page into one buffer and then wrote
// it: the buffer was given durationSec * sampleRate frames, so the end of a
// warped page was cut off (an arc at 25-29 s of a 30 s page exported as
// silence), and a long page needed the whole render in memory (a 12 minute
// page at 96 kHz is over half a gigabyte).
//
// Now the page is walked with the page's exact time map - the same map the
// playback uses - and each block is written as it is rendered, so the file
// is as long as the page really needs and the memory stays flat. The write
// still goes to a temporary sibling and replaces the target only when the
// whole file is on disk (as writeWavFile does).
//
// The session's settings come with it: the pitch algorithms and the grain
// sizes, so the file sounds like the session (the export used the engine's
// defaults).
//
// v1.33.4 (M28): the export runs on a worker thread, so it can report its
// progress and be cancelled: `progress` is called with the percentage done
// and returns false to stop the render (the file that was there before is
// then left alone - the write goes to a temporary sibling).
//
// WP6.4: `channelBucket` is the mix bucket the render is limited to (1 =
// the first mixer channel of the bank, ...); -1 renders the whole mix. It
// is the ENGINE's bucket, not the channel's id - the piece names a channel
// by its id, the engine counts its buckets.
//
// `mono` writes ONE channel instead of two. A mixer channel is a mono bus
// (the plan's "mono WAV per channel"); the master render and the per-voice
// stems stay stereo.
bool writePageToWavFile (const juce::File& target,
                                const upic::Page& page,
                                const std::vector<upic::Waveform>& waves,
                                const std::vector<upic::Envelope>& envs,
                                const std::vector<upic::SampleSlot>& slots,
                                double sr, float masterGain,
                                int samplePitchMode = 0,
                                int sampleGrainMs = 0,
                                int liveGrainMs = 0,
                                const std::vector<upic::Envelope>* ampTables = nullptr,
                                const std::vector<upic::Channel>* channels = nullptr,
                                int channelBucket = -1,
                                const std::function<bool (int percent)>& progress = {},
                                bool mono = false);


//==============================================================================
// The result of a stem export: the files that were really written, and how
// many renders were attempted and failed (v1.33.4 / H11 - a failed render
// must not be reported as a written file).
struct StemExport
{
    std::vector<juce::File> written;
    int attempted = 0;
    int failed = 0;
    // v1.33.4 (M23): what was left out, in words - the live arcs cannot be
    // rendered offline, and a page that holds nothing else says so.
    juce::String note;
};

// Stems: render one WAV per voice (wavetable for synth arcs / sample slot for
// sample-mode arcs) into outDir. Muted arcs and voices with no audible
// material are skipped. Returns the files that were really written and the
// failures (see StemExport). `samplePitchMode` mirrors
// UpicEngine::setSamplePitchMode so stems match what the session hears, and
// the grain sizes do the same (v1.33.4 / H8).
//
// WP6.4: the mixer channels get a stem each too (one MONO WAV per channel -
// the channel is a mono bus). `onlyChannel` narrows the export to ONE
// mixer channel: it names the channel by its id (0 = Main, > 0 = the
// channel's id); -1 (the default) exports everything - the per-voice stems
// AND one stem per mixer channel. The parameter used to be ignored, so a
// caller could never ask for a single channel.
//
// v1.33.4 (M28): the export runs on a worker thread, so it can report its
// progress and be cancelled: `progress` is called with the percentage done
// and returns false to stop (the files that were already written stay).
StemExport renderStems(const upic::Page& page,
                              const std::vector<upic::Waveform>& waves,
                              const std::vector<upic::Envelope>& envs,
                              const std::vector<upic::SampleSlot>& slots,
                              const juce::File& outDir,
                              const juce::String& prefix,
                              double sr, float masterGain,
                              int samplePitchMode = 0,
                              int sampleGrainMs = 0,
                              int liveGrainMs = 0,
                              const std::vector<upic::Envelope>* ampTables = nullptr,
                              const std::vector<upic::Channel>* channels = nullptr,
                              int onlyChannel = -1,
                              const std::function<bool (int percent)>& progress = {});
