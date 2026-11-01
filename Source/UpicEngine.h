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

    void prepare(double sampleRate_, int blockSize = 0)
    {
        sampleRate = sampleRate_ > 0 ? sampleRate_ : 44100.0;
        phases.clear();
        sampleVoices.clear();
        liveShift.clear();
        if (blockSize > 0) scratch.resize((size_t) blockSize);
    }

    // v1.15: the oscillator phase is free-running state - clearing it makes
    // every arc jump back to phase 0 mid-flight, which is a click. Only the
    // material/ring readers (sample + live voices) have to re-anchor, because
    // the material or the input window changed under them.
    void reset() { sampleVoices.clear(); liveShift.clear(); pmVoices.clear(); }
    // v1.20: drop the live voices only (they re-anchor on their next
    // contact) - the oscillator phases keep running (v1.15).
    void resetLiveVoices() { liveShift.clear(); }
    // v1.28: carry oscillator phases from another engine (used to smooth the
    // scrub-preview -> freeze transition). Sample/PM voices are NOT copied;
    // they re-anchor at the freeze position.
    void copyOscillatorPhasesFrom(const UpicEngine& other) { phases = other.phases; }
    void noteOffArc(int arcId) { phases.erase(arcId); sampleVoices.erase(arcId); liveShift.erase(arcId); }
    void setScrubPreview(bool on) { scrubPreview = on; }
    // Shared with the material view so its time bar uses the same tape
    // position as the audio engine (pitch-scaled, not ratio * elapsed).
    static double scrubMaterialPos(const upic::Arc& arc, double t, double ref, double matDur)
    {
        return pitchScaledPos(arc, t, ref, matDur);
    }

    // Time scale for a sample voice. Mode 1 (granular stretch) uses the same
    // arc-length mapping in normal playback and scrub; mode 2 (phase vocoder)
    // only maps onto the arc in the scrub preview.
    static double sampleArcTimeScale(int mode, bool scrub, double rate,
                                     double matDur, double arcDur)
    {
        const double arcScale = std::abs(rate) * matDur / juce::jmax(1.0e-6, arcDur);
        if (mode == 1) return arcScale;
        if (mode == 2) return scrub ? arcScale : 1.0;
        return 1.0;
    }
    void setSampleGrainMs(int ms) { sampleGrainMs.store(juce::jlimit(5, 500, ms), std::memory_order_relaxed); }
    void setScrubGrainMs(int ms) { scrubGrainMs.store(juce::jlimit(5, 500, ms), std::memory_order_relaxed); }

    // Host publishes the live input ring each block (audio thread): frames up
    // to writeEnd are valid (absolute count), stereo ringA/ringB (B may be
    // null for mono). active=false silences mode-2 arcs entirely.
    void setLiveInput(const float* ringA, const float* ringB, int size,
                      long long writeEnd, bool active)
    {
        liveRingA = ringA;
        liveRingB = ringB;
        liveRingSize = size;
        liveWriteEnd = writeEnd;
        liveActive = active;
    }

    // v1.21 (wave 19): the reversed live ring - the same content, mirrored in
    // time (the capture's write head moves backwards). The arcs with
    // `reversed` read this one instead of the forward ring.
    void setLiveReverse(const float* ringA, const float* ringB, int size,
                        long long writeEnd, bool active)
    {
        revRingA = ringA;
        revRingB = ringB;
        revRingSize = size;
        revWriteEnd = writeEnd;
        revActive = active;
    }

    // Live pitch algorithm for mode-2 voices (safe from the message thread):
    // 0 = granular (v1.5 default), 1 = tape, 2 = phase vocoder. A change
    // drops the live voices so each re-anchors on its next contact.
    void setLivePitchMode(int mode) { livePitchMode.store(juce::jlimit(0, 2, mode)); }
    int getLivePitchMode() const { return livePitchMode.load(std::memory_order_relaxed); }

    // Sample pitch algorithm for mode-1 voices (safe from the message
    // thread): 0 = varispeed (default - the slot plays back faster/slower
    // with the arc pitch, the original behaviour), 1 = granular (stretch),
    // 2 = phase vocoder (both keep the slot's playback length intact,
    // shifting the pitch only), 3 = granular with linked pitch/speed (no
    // time stretch). A change drops the sample voices so each re-anchors.
    void setSamplePitchMode(int mode) { samplePitchMode.store(juce::jlimit(0, 3, mode)); }
    int getSamplePitchMode() const { return samplePitchMode.load(std::memory_order_relaxed); }

    // Realtime render: playhead in seconds (page-local), outputs stereo (same mono sum).
    // Cost model: each arc only touches its active sample range, with the
    // waveform/envelope lookups hoisted out of the per-sample loop.
    void render(const upic::Page& page, double playhead,
                const std::vector<upic::Waveform>& waves,
                const std::vector<upic::Envelope>& envs,
                const std::vector<upic::SampleSlot>& slots,
                float* left, float* right, int numSamples, float masterGain,
                double rate = 1.0, bool freeze = false)
    {
        if (numSamples <= 0) return;
        if (! freeze && std::abs(rate) < 1e-6) return; // stopped transport: silence
        if (scratch.size() < (size_t) numSamples) scratch.resize((size_t) numSamples);
        std::fill(scratch.begin(), scratch.begin() + numSamples, 0.0f);

        // a shifter-mode change drops the live voices so each re-anchors
        const int pitchMode = livePitchMode.load(std::memory_order_relaxed);
        if (pitchMode != livePitchModeApplied)
        {
            livePitchModeApplied = pitchMode;
            liveShift.clear();
        }

        // same for the sample pitch mode (varispeed / granular / vocoder)
        const int smpMode = samplePitchMode.load(std::memory_order_relaxed);
        if (smpMode != samplePitchModeApplied)
        {
            samplePitchModeApplied = smpMode;
            sampleVoices.clear();
        }

        const double invSr = 1.0 / sampleRate;
        const double step = freeze ? 0.0 : rate * invSr;

        for (auto& arc : page.arcs)
        {
            if (arc.muted || arc.points.size() < 2) continue;

            const double arcStart = arc.startTime(), arcEnd = arc.endTime();
            if (arcEnd <= arcStart) continue;

            int i0, i1;
            if (freeze)
            {
                if (playhead < arcStart || playhead > arcEnd) continue;
                i0 = 0;
                i1 = numSamples - 1;
            }
            else
            {
                const double nA = (arcStart - playhead) * sampleRate / rate;
                const double nB = (arcEnd   - playhead) * sampleRate / rate;
                i0 = (int) std::ceil(juce::jmin(nA, nB));
                i1 = (int) std::floor(juce::jmax(nA, nB));
                i0 = juce::jmax(0, i0);
                i1 = juce::jmin(numSamples - 1, i1);
                if (i0 > i1) continue;
            }

            const upic::Envelope* e = findEnv(envs, arc.envelopeId);
            const float intensity = arc.intensity;

            if (freeze && arc.mode == 2) continue; // live input cannot be frozen

            // ---- mode 1: pitch-shifted sample material (see samplePitchMode) ----
            if (arc.mode == 1)
            {
                const upic::SampleSlot* smp = findSlot(slots, arc.sampleId);
                if (smp == nullptr) continue;
                renderSampleArc(arc, *smp, playhead, i0, i1, invSr, e, rate, freeze);
                continue;
            }

            // ---- mode 2: live input, pitch follows the arc in realtime ----
            if (arc.mode == 2)
            {
                renderLiveArc(arc, playhead, i0, i1, invSr, e, rate);
                continue;
            }
            // ---- mode 3: phase modulation (PM). pmOps operators (2 or 4),
            //     serial chain: mod_top -> ... -> mod_1 -> carrier. ----
            if (arc.mode == 3)
            {
                const int ops = (arc.pmOps == 4) ? 4 : 2;
                const upic::Envelope* idxEnv = (arc.pmIndexEnv > 0)
                    ? findEnv (envs, arc.pmIndexEnv) : e;
                PmVoice& v = pmVoices[arc.id];
                if (! v.prepared) { v.prepare (sampleRate); v.prepared = true; v.ops = ops; }
                const double fb = juce::jlimit (0.0, 1.0, arc.pmFeedback);
                // per-stage ratios (ops>=2: carrier uses pmRatio for stage 1)
                const double r[4] = { 1.0,
                                      juce::jlimit (0.1,32.0,arc.pmRatio),
                                      juce::jlimit (0.1,32.0,arc.pmR2),
                                      juce::jlimit (0.1,32.0,arc.pmR3), } ;
                const double id[4] = { 0.0,
                                      juce::jlimit (0.0,20.0,arc.pmIndex),
                                      juce::jlimit (0.0,20.0,arc.pmI2),
                                      juce::jlimit (0.0,20.0,arc.pmI3), } ;
                const upic::Waveform* carW = findWave (waves, arc.waveformId);

                for (int n = i0; n <= i1; ++n)
                {
                    const double t = playhead + (double) n * step;
                    const double cFreq = juce::jlimit (10.0, 14000.0, arc.freqAt (t));
                    const float env = e != nullptr ? e->valueAt (arc.lifeAt (t)) : 1.0f;
                    const float idxE = (idxEnv != nullptr ? idxEnv->valueAt (arc.lifeAt (t)) : 1.0f);
                    const float edge = edgeFade (t, arcStart, arcEnd);

                    // advance topology (serial): top modulator ... -> carrier
                    // phases indexed 0=carrier, 1..ops-1 modulators
                    const int top = ops - 1;
                    double phTop = v.ph[top] + 2.0 * juce::MathConstants<double>::pi * fb * v.prev[top];
                    float modTop = std::sin ((float) phTop);
                    v.prev[top] = modTop;
                    v.ph[top] += 2.0 * juce::MathConstants<double>::pi * (cFreq * r[top]) * invSr;
                    float carry = modTop;
                    for (int k = top - 1; k >= 1; --k)
                    {
                        float m = std::sin ((float) (v.ph[k] + (double) id[k+1] * (double) idxE * carry));
                        carry = m;
                        v.ph[k] += 2.0 * juce::MathConstants<double>::pi * (cFreq * r[k]) * invSr;
                    }
                    const float carPhaseMod = (float) (v.ph[0] + (double) id[1] * (double) idxE * carry);
                    const float carPhase01 = (carPhaseMod >= 0.0f)
                        ? (carPhaseMod / (float)(2.0*juce::MathConstants<double>::pi) - std::floor (carPhaseMod / (float)(2.0*juce::MathConstants<double>::pi)))
                        : 0.0f;
                    const float osc = (carW != nullptr) ? carW->valueAt (carPhase01) : std::sin (carPhaseMod);

                    v.ph[0] += 2.0 * juce::MathConstants<double>::pi * cFreq * invSr;
                    for (int k = 0; k < ops; ++k)
                        while (v.ph[k] >= 2.0 * juce::MathConstants<double>::pi) v.ph[k] -= 2.0 * juce::MathConstants<double>::pi;

                    // 2-op legacy single-mod path unused when ops==2? keep for compat:


                    scratch[(size_t) n] += osc * env * edge * (float) arc.intensity * arc.intensityAt (t);
                }
                continue;
            }

            // ---- mode 0: wavetable oscillator ----
            const upic::Waveform* w = findWave(waves, arc.waveformId);
            if (w == nullptr) continue;

            double& ph = phases[arc.id]; // 0..1, kept across blocks for continuity

            for (int n = i0; n <= i1; ++n)
            {
                const double t = playhead + (double) n * step;

                float env = e != nullptr ? e->valueAt(arc.lifeAt(t)) : 1.0f;
                const float edge = edgeFade(t, arcStart, arcEnd);

                const float freq = (float) juce::jlimit(10.0, 14000.0, arc.freqAt(t));
                const float osc = w->valueAt(ph);
                ph += freq * invSr;
                if (ph >= 1.0) ph -= std::floor(ph);

                scratch[(size_t) n] += osc * env * edge * intensity * arc.intensityAt(t);
            }
        }

        // soft clip + master gain, accumulate into output (buffer was zeroed by caller)
        for (int n = 0; n < numSamples; ++n)
        {
            const float out = (float) std::tanh(scratch[(size_t) n] * 0.6) * masterGain * 0.9f;
            left[n] += out;
            if (right != nullptr) right[n] += out;
        }
    }

    // Offline render of a whole page to a buffer (stereo interleaved in two buffers)
    void renderPageOffline(const upic::Page& page,
                           const std::vector<upic::Waveform>& waves,
                           const std::vector<upic::Envelope>& envs,
                           const std::vector<upic::SampleSlot>& slots,
                           juce::AudioBuffer<float>& dest, double destSampleRate, float masterGain)
    {
        prepare(destSampleRate > 0 ? destSampleRate : 44100.0);
        int total = dest.getNumSamples();
        int block = 8192;
        std::vector<float> L((size_t)block), R((size_t)block);
        // local voice state so offline doesn't depend on realtime state
        std::map<int,double> localPh;
        auto saved = phases; phases.clear();
        // live input is realtime-only: offline voice reads nothing
        const float* savedLA = liveRingA;
        const float* savedLB = liveRingB;
        const int savedLSize = liveRingSize;
        const long long savedLWrite = liveWriteEnd;
        const bool savedLActive = liveActive;
        liveRingA = nullptr; liveRingB = nullptr; liveRingSize = 0;
        liveWriteEnd = 0; liveActive = false;

        for (int pos = 0; pos < total; pos += block)
        {
            int n = juce::jmin(block, total - pos);
            // temporarily swap in local voice state
            phases = localPh;
            juce::FloatVectorOperations::clear(dest.getWritePointer(0) + pos, n);
            if (dest.getNumChannels() > 1) juce::FloatVectorOperations::clear(dest.getWritePointer(1) + pos, n);
            std::fill(L.begin(), L.begin() + n, 0.0f);
            std::fill(R.begin(), R.begin() + n, 0.0f);
            double ph0 = (double) pos / sampleRate;
            render(page, ph0, waves, envs, slots, L.data(), R.data(), n, masterGain);
            juce::FloatVectorOperations::copy(dest.getWritePointer(0) + pos, L.data(), n);
            if (dest.getNumChannels() > 1) juce::FloatVectorOperations::copy(dest.getWritePointer(1) + pos, R.data(), n);
            localPh = phases;
        }
        phases = saved;
        // sample voices are recreated on the next realtime contact (they may
        // hold vocoder state from this pass)
        sampleVoices.clear();
        liveRingA = savedLA; liveRingB = savedLB; liveRingSize = savedLSize;
        liveWriteEnd = savedLWrite; liveActive = savedLActive;
    }

private:
    double sampleRate = 44100.0;
    std::map<int,double> phases;
    std::map<int, upic::SampleVoice> sampleVoices; // mode-1 voice state per arc
    struct PmVoice { double carPhase = 0.0, modPhase = 0.0, prevModOut = 0.0; double ph[4] = {}; double prev[4] = {}; int ops = 2; bool prepared = false; void prepare(double){} };
    std::map<int, PmVoice> pmVoices; // mode-3 PM voice state per arc
    std::vector<float> scratch; // per-block accumulation, grows only

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
    std::map<int, upic::LiveShifter> liveShift; // live-mode voice state per arc
    std::atomic<int> livePitchMode { 0 }; // written by the message thread
    int livePitchModeApplied = 0;         // audio thread: mode of the live voices
    std::atomic<int> samplePitchMode { 0 }; // written by the message thread
    int samplePitchModeApplied = 0;         // audio thread: mode of the sample voices
    bool scrubPreview = false;              // scrub engine: vocoder maps to the arc, tape speed follows the drag
    std::atomic<int> sampleGrainMs { 40 };  // granular grain size (normal playback)
    std::atomic<int> scrubGrainMs { 40 };   // granular grain size while freeze-holding

    static float edgeFade(double t, double arcStart, double arcEnd)
    {
        double edge = juce::jmin(t - arcStart, arcEnd - t) / 0.012;
        edge = juce::jlimit(0.0, 1.0, edge);
        return (float) (edge * edge * (3.0 - 2.0 * edge));
    }

    // Varispeed material position for a scrub reset: integrate the arc's
    // pitch ratio from the arc start to `t`, so a falling pitch curve still
    // advances the sample forward instead of flipping direction.
    static double pitchScaledPos(const upic::Arc& arc, double t, double ref, double matDur)
    {
        const double start = arc.startTime();
        const double end = juce::jmin(t, arc.endTime());
        if (end <= start) return 0.0;
        double pos = 0.0;
        const auto& pts = arc.points;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            const double a = juce::jmax(start, pts[i - 1].t);
            const double b = juce::jmin(end, pts[i].t);
            if (b <= a) continue;
            const int steps = 8;
            const double dt = (b - a) / (double) steps;
            for (int k = 0; k < steps; ++k)
            {
                const double t0 = a + dt * (double) k;
                const double t1 = t0 + dt;
                const double f0 = juce::jlimit(10.0, 14000.0, arc.freqAt(t0));
                const double f1 = juce::jlimit(10.0, 14000.0, arc.freqAt(t1));
                const double r0 = juce::jlimit(1.0 / 16.0, 16.0, f0 / ref);
                const double r1 = juce::jlimit(1.0 / 16.0, 16.0, f1 / ref);
                pos += 0.5 * (r0 + r1) * dt;
            }
        }
        return std::fmod(pos, matDur);
    }

    // Material read: mono mix of the slot at a position in seconds, wrapped
    // over the seamless loop, linear interpolation (shared by every mode).
    // Reads the material at `pos` seconds. The interpolation and the wrap
    // rules are the same in both directions; a reversed slot mirrors the
    // read (the material's last frame comes first), so every pitch mode
    // works on the mirrored material without touching the shifter.
    static float readMaterial(const upic::SampleSlot& smp, double pos, bool mirrored)
    {
        if (smp.data == nullptr) return 0.0f;
        const auto& buf = *smp.data;
        const int nFrames = buf.getNumSamples();
        const double matDur = smp.durationSec();
        const double matSr = smp.sampleRate;
        const int nCh = juce::jmin(2, buf.getNumChannels());

        while (pos >= matDur) pos -= matDur;
        while (pos < 0.0) pos += matDur;

        const double x = pos * matSr;
        int ia = (int) x;
        float fr = (float) (x - (double) ia);
        if (ia >= nFrames) ia = nFrames - 1;
        if (ia < 0) ia = 0;
        int ib = ia + 1; if (ib >= nFrames) ib = 0;

        if (mirrored && nFrames > 1)
        {
            // The mirrored read: the material's frame (n-1-ia) comes first
            // and the one before it (n-ia) is the interpolation partner. The
            // fraction runs the other way, so the read moves backwards inside
            // the sample - and it stays continuous over the loop seam (a
            // seamless material ends where it starts).
            const int ra = (nFrames - 1 - ia + nFrames) % nFrames;
            const int rb = (nFrames - ia) % nFrames;
            ia = ra; ib = rb;
            fr = 1.0f - fr;
        }

        float v = buf.getSample(0, ia) * (1.0f - fr) + buf.getSample(0, ib) * fr;
        if (nCh > 1)
        {
            const float v1 = buf.getSample(1, ia) * (1.0f - fr) + buf.getSample(1, ib) * fr;
            v = 0.5f * (v + v1);
        }
        return v;
    }

    // Mode-1 voice: pitch follows the arc's frequency curve (ratio = arcFreq
    // / refFreq). Varispeed (the original behaviour) and granular (no
    // stretch) advance the material position at the pitch ratio, so the
    // output pitch is ratio x material pitch EXACTLY for any curve.
    // Granular (stretch) maps the whole slot onto the arc's x-axis in both
    // playback and scrub; the phase vocoder keeps its slot length during
    // playback and maps onto the arc only in the scrub preview.
    // Voices anchor on first contact (fresh after seek/reset) and wrap over
    // the slot's seamless loop.
    void renderSampleArc(const upic::Arc& arc, const upic::SampleSlot& smp,
                         double playhead, int i0, int i1, double invSr,
                         const upic::Envelope* e, double rate = 1.0, bool freeze = false)
    {
        const int nFrames = smp.data != nullptr ? smp.data->getNumSamples() : 0;
        const double matDur = smp.durationSec();
        if (nFrames < 2 || matDur <= 0.0) return;
        const double arcStart = arc.startTime(), arcEnd = arc.endTime();
        const double arcDur = juce::jmax(1.0e-6, arcEnd - arcStart);
        const double ref = juce::jmax(1.0, arc.refFreq);
        // v1.21 (wave 19): the arc's own pitch mode, or the tab's setting
        int mode = arc.pitchMode >= 0 ? arc.pitchMode : samplePitchModeApplied;
        if (freeze) mode = 1; // granular grain-hold for a stable freeze
        const bool mirrored = smp.reversed != (rate < 0.0);
        const double step = rate * invSr;
        auto it = sampleVoices.find(arc.id);
        if (it == sampleVoices.end())
        {
            // voice created on first contact (fresh after seek/reset): anchor
            // at the arc's start - varispeed / granular (no stretch) at the
            // integrated pitch-scaled position, granular (stretch) at the arc
            // progress; the scrub preview maps the vocoder onto the arc too
            const double t0 = playhead + (double) i0 * step;
            const double elapsed = juce::jmax(0.0, t0 - arcStart);
            upic::SampleVoice v;
            v.prepare(sampleRate);
            const double progress = juce::jlimit(0.0, 1.0, elapsed / arcDur);
            double anchorPos;
            if (mode == 0 || mode == 3)
                anchorPos = pitchScaledPos(arc, t0, ref, matDur);
            else if (mode == 1)
                anchorPos = progress * matDur; // stretch granular maps the slot to the arc
            else
                anchorPos = scrubPreview ? progress * matDur : elapsed;
            const double anchor = mirrored
                ? std::fmod(matDur - anchorPos, matDur)
                : std::fmod(anchorPos, matDur);
            v.reset(anchor);
            v.lastMode = mode;
            it = sampleVoices.emplace(arc.id, std::move(v)).first;
        }
        else if (it->second.lastMode != mode)
        {
            // v1.21: the arc's own mode changed - the voice's shifter state
            // (the grain taps, the vocoder's overlap-add) belongs to the old
            // algorithm, so it has to re-anchor
            it->second.lastMode = mode;
            it->second.grain = {};
            it->second.voc = {};
            it->second.grain.prepare(upic::GrainCore::kHopSec, 1.0 / sampleRate);
            it->second.voc.prepare(sampleRate);
            it->second.reset(it->second.pos);
        }
        auto& v = it->second;
        // Granular (stretch) maps the whole sample onto the arc's x-axis in
        // both playback and scrub. The vocoder keeps its original slot length
        // during playback and only maps onto the arc in the scrub preview.
        // Direction is handled by the mirrored read.
        const double timeScale = sampleArcTimeScale(mode, scrubPreview, rate, matDur, arcDur);
        v.grain.setTimeScale(timeScale);
        const int grainMs = freeze ? scrubGrainMs.load(std::memory_order_relaxed)
                                   : sampleGrainMs.load(std::memory_order_relaxed);
        v.grain.setHop(juce::jmax(0.005, (double) grainMs / 1000.0));

        for (int n = i0; n <= i1; ++n)
        {
            const double t = playhead + (double) n * step;

            const double freq = juce::jlimit(10.0, 14000.0, arc.freqAt(t));
            const double ratio = juce::jlimit(1.0 / 16.0, 16.0, freq / ref);

            const float env = e != nullptr ? e->valueAt(arc.lifeAt(t)) : 1.0f;
            const float edge = edgeFade(t, arcStart, arcEnd);

            float s;
            if (mode == 1)
            {
                // granular: the grain-start timeline follows the arc x-axis;
                // the reads inside the grains run at `ratio` for pitch
                const float wA = v.grain.crossfadeWeight();
                const double pa = v.grain.tapA(ratio);
                const double pb = v.grain.tapBForScale(ratio, timeScale);
                if (freeze) v.grain.advanceHold(); else v.grain.advance();
                s = wA * readMaterial(smp, pa, mirrored) + (1.0f - wA) * readMaterial(smp, pb, mirrored);
            }
            else if (mode == 2)
            {
                // phase vocoder: the material stream follows the arc x-axis
                const float in = readMaterial(smp, v.vocInPos, mirrored);
                v.vocInPos += invSr * timeScale;
                v.vocInPos = std::fmod(v.vocInPos, matDur);
                if (v.vocInPos < 0.0) v.vocInPos += matDur;
                const double vocRatio = juce::jlimit(1.0 / 16.0, 16.0,
                    ratio / juce::jmax(1.0e-6, timeScale));
                s = v.voc.next(in, vocRatio);
            }
            else if (mode == 3)
            {
                // granular with linked pitch/speed (no time stretch): the
                // grain-start timeline advances at the pitch ratio, so the
                // slot speeds up/down with the arc like tape.
                const double qScale = ratio * (scrubPreview ? std::abs(rate) : 1.0);
                v.grain.setTimeScale(qScale);
                const float wA = v.grain.crossfadeWeight();
                const double pa = v.grain.tapA(ratio);
                const double pb = v.grain.tapBForScale(ratio, qScale);
                if (freeze) v.grain.advanceHold(); else v.grain.advance();
                s = wA * readMaterial(smp, pa, mirrored)
                    + (1.0f - wA) * readMaterial(smp, pb, mirrored);
            }
            else
            {
                s = readMaterial(smp, v.pos, mirrored);
                v.pos += invSr * ratio * (scrubPreview ? std::abs(rate) : 1.0);
                v.pos = std::fmod(v.pos, matDur);
                if (v.pos < 0.0) v.pos += matDur;
            }

            scratch[(size_t) n] += s * env * edge * arc.intensity * arc.intensityAt(t);
        }
    }

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
                       double invSr, const upic::Envelope* e, double rate = 1.0)
    {
        // the arc's own mode, or the tab's setting
        const int mode = arc.pitchMode >= 0
                            ? arc.pitchMode
                            : livePitchMode.load(std::memory_order_relaxed);
        // the arc reads the reversed ring when it asks for it
        const bool useRev = (arc.reversed != (rate < 0.0)) && revActive && revRingA != nullptr && revRingSize >= 64;
        const float* ringA = useRev ? revRingA : liveRingA;
        const float* ringB = useRev ? revRingB : liveRingB;
        const int ringSize = useRev ? revRingSize : liveRingSize;
        const long long ringEnd = useRev ? revWriteEnd : liveWriteEnd;
        const bool ringActive = useRev ? revActive : liveActive;

        if (! ringActive || ringA == nullptr || ringSize < 64) return;
        const double arcStart = arc.startTime(), arcEnd = arc.endTime();
        const double ref = juce::jmax(1.0, arc.refFreq);

        auto it = liveShift.find(arc.id);
        if (it == liveShift.end())
        {
            upic::LiveShifter ls;
            ls.prepare(sampleRate);
            ls.setMode((upic::LiveShifter::Mode) mode);
            ls.reset(ringEnd);
            it = liveShift.emplace(arc.id, std::move(ls)).first;
        }
        auto& ls = it->second;
        ls.setMode((upic::LiveShifter::Mode) mode); // the arc's own mode wins

        for (int n = i0; n <= i1; ++n)
        {
            const double t = playhead + (double) n * (rate * invSr);

            const double freq = juce::jlimit(10.0, 14000.0, arc.freqAt(t));
            const double ratio = juce::jlimit(upic::kLiveMinRatio,
                                              upic::kLiveMaxRatio, freq / ref);

            const float env = e != nullptr ? e->valueAt(arc.lifeAt(t)) : 1.0f;
            const float edge = edgeFade(t, arcStart, arcEnd);

            // v1.14: the live ring is filled per block, so the write head
            // only advances every block. A shifter that reads AT the write
            // head would see the same frame for the whole block (a
            // staircase) - the vocoder re-synthesises that as a buzz at the
            // block rate. The per-sample frame keeps the read continuous.
            const std::int64_t frameNow = ringEnd - (std::int64_t) (i1 - n) - 1;

            const float v = ls.next(ringA, ringB, ringSize, frameNow, ratio);

            scratch[(size_t) n] += v * env * edge * arc.intensity * arc.intensityAt(t);
        }
    }

    static const upic::Waveform* findWave(const std::vector<upic::Waveform>& v, int id)
    {
        for (auto& w : v) if (w.id == id) return &w;
        return v.empty() ? nullptr : &v.front();
    }
    static const upic::Envelope* findEnv(const std::vector<upic::Envelope>& v, int id)
    {
        for (auto& e : v) if (e.id == id) return &e;
        return v.empty() ? nullptr : &v.front();
    }
    // Exact slot if it has audio, else the first loaded slot (so material
    // still sounds if the arc points at an empty slot by mistake).
    static const upic::SampleSlot* findSlot(const std::vector<upic::SampleSlot>& v, int id)
    {
        const upic::SampleSlot* first = nullptr;
        for (auto& s : v)
        {
            if (! s.loaded()) continue;
            if (s.id == id) return &s;
            if (first == nullptr) first = &s;
        }
        return first;
    }
};

//==============================================================================
// Stems: render one WAV per voice (wavetable for synth arcs / sample slot for
// sample-mode arcs) into outDir. Muted arcs and voices with no audible
// material are skipped. Returns the files written. `samplePitchMode` mirrors
// UpicEngine::setSamplePitchMode so stems match what the session hears.
inline std::vector<juce::File> renderStems(const upic::Page& page,
                                           const std::vector<upic::Waveform>& waves,
                                           const std::vector<upic::Envelope>& envs,
                                           const std::vector<upic::SampleSlot>& slots,
                                           const juce::File& outDir,
                                           const juce::String& prefix,
                                           double sr, float masterGain,
                                           int samplePitchMode = 0)
{
    std::vector<juce::File> written;
    if (page.arcs.empty() || sr <= 0.0 || prefix.isEmpty()) return written;

    // mirror of engine slot lookup: is there any material for this slot id?
    auto slotUsable = [&](int sid) {
        bool any = false;
        for (auto& s : slots)
        {
            if (! s.loaded()) continue;
            if (s.id == sid) return true;
            any = true;
        }
        return any;
    };

    std::vector<int> wIds, sIds;
    for (auto& a : page.arcs)
    {
        if (a.muted || a.points.size() < 2) continue;
        if (a.mode == 1)
        {
            if (! slotUsable(a.sampleId)) continue;
            if (std::find(sIds.begin(), sIds.end(), a.sampleId) == sIds.end())
                sIds.push_back(a.sampleId);
        }
        else if (a.mode == 0 && std::find(wIds.begin(), wIds.end(), a.waveformId) == wIds.end())
        {
            wIds.push_back(a.waveformId);
        }
    }
    if (wIds.empty() && sIds.empty()) return written;

    outDir.createDirectory();

    const int total = (int) std::ceil(page.durationSec * sr);
    auto renderOne = [&](const upic::Page& one, const juce::File& f) {
        juce::AudioBuffer<float> buf(2, juce::jmax(1, total));
        buf.clear();
        UpicEngine eng;
        eng.prepare(sr, 8192);
        eng.setSamplePitchMode(samplePitchMode);
        eng.renderPageOffline(one, waves, envs, slots, buf, sr, masterGain);
        juce::WavAudioFormat fmt;
        // createOutputStream() appends on Windows: delete first (fresh file)
        f.deleteFile();
        if (auto out = f.createOutputStream())
        {
            if (auto* w = fmt.createWriterFor(out.release(), sr, (unsigned int) 2, 16, {}, 0))
            {
                w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
                delete w;
                written.push_back(f);
            }
        }
    };

    for (int id : wIds)
    {
        upic::Page one = page;
        one.arcs.clear();
        for (auto& a : page.arcs)
            if (! a.muted && a.mode == 0 && a.waveformId == id) one.arcs.push_back(a);
        renderOne(one, outDir.getChildFile(prefix + "_wave" + juce::String(id) + ".wav"));
    }
    for (int id : sIds)
    {
        upic::Page one = page;
        one.arcs.clear();
        for (auto& a : page.arcs)
            if (! a.muted && a.mode == 1 && a.sampleId == id) one.arcs.push_back(a);
        renderOne(one, outDir.getChildFile(prefix + "_sample" + juce::String(id) + ".wav"));
    }
    return written;
}
