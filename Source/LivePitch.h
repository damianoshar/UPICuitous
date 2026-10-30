#pragma once
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

//==============================================================================
// Pitch-shift building blocks, shared by the LIVE-tab input shifters and the
// SAMPLE-tab material playback.
//
// Cores (source-agnostic, positions live in the caller's units):
//   GrainCore    - overlapping Hann grains: q (the current grain's start)
//                  advances at 1x by `hop`, the reads inside a grain run at
//                  `ratio`; the caller reads the two taps, mixes them with
//                  crossfadeWeight() and advance()s.
//   VocoderCore  - phase vocoder after smbPitchShift (Bernsee), fed one input
//                  sample per call (`in`, the stream advances at 1x) with the
//                  current `ratio`; returns the time-aligned output sample.
//                  Analysis frames every kHop measure each bin's true phase
//                  advance, the partials are remapped onto `ratio`x their bin
//                  and re-emitted with that true advance, so the overlap-add
//                  stays coherent for every ratio. Sustained material comes
//                  out clean; transients smear; ~kPrime of priming latency.
//
// Live input (the host feeds a ring of recent input frames - absolute write
// position, stereo ringA/ringB where B may be null):
//   GrainShifter   (v1.5, default) - grains swept at `ratio` every 40 ms;
//                  constant kLagSec latency (SMB-style). v1.13: the grain
//                  shrinks with the ratio above ~5.5x (the source window
//                  stays constant) so the read never needs more history than
//                  one grain - the latency does not grow with the shift.
//   TapeShifter    (v1.9) - true varispeed read at `ratio`, re-anchored with a
//                  short equal-power crossfade only when the read head drifts
//                  out of the 40-200 ms delay window: no periodic re-graining,
//                  so transients pass through intact. v1.13: it reads the mic
//                  faster than the mic delivers, so it keeps an octave
//                  up/down (the other two cover the full range).
//   VocoderShifter (v1.9) - VocoderCore on the ring read at 1x.
// UpicEngine picks one per voice via LiveShifter::setMode; a mode change
// clears the live voices so each re-anchors on its next contact.
//
// SAMPLE tab (mode-1 arcs, v1.10): SampleVoice holds one voice's material
// state - the engine reads the slot itself and picks by its sample pitch mode
// (UpicEngine::setSamplePitchMode): varispeed / granular (no stretch) = the
// material position advances at the pitch ratio, so pitch and playback length
// change together; granular (stretch) = the grain-start timeline maps the whole
// slot onto the arc's x-axis while the grains shift the pitch, so the slot is
// time-stretched to the arc in playback and scrub; phase vocoder = the slot's
// playback length stays intact and only the scrub preview maps it onto the arc.
//==============================================================================
namespace upic
{

// v1.13: the live path gets the SAMPLE path's range (1/16x ... 16x, four
// octaves) - the shift is read from the input, so nothing about the range
// depends on having the whole source available.
inline constexpr double kLiveMinRatio = 1.0 / 16.0; // 4 octaves down
inline constexpr double kLiveMaxRatio = 16.0;       // 4 octaves up

// The tape (varispeed) shifter is the exception: it reads the mic FASTER
// than the mic delivers, so above ~2x it would have to re-anchor every few
// milliseconds. It keeps an octave up / down; granular and vocoder cover
// the full range.
inline constexpr double kLiveTapeMinRatio = 0.5;
inline constexpr double kLiveTapeMaxRatio = 2.0;

// Shared ring read: mono mix of the stereo ring, linear interpolation, held
// at the newest written frame and silent outside the ring.
inline float readLiveRing(const float* ringA, const float* ringB, int size,
                          std::int64_t writeEnd, double pos)
{
    const double newest = (double) (writeEnd - 2);
    if (pos > newest) pos = newest;
    if (pos < (double) (writeEnd - (std::int64_t) size + 2)) return 0.0f;

    const std::int64_t ip = (std::int64_t) std::floor(pos);
    const float fr = (float) (pos - (double) ip);
    const int i0 = (int) (((ip % size) + size) % size);
    const int i1 = (i0 + 1 == size) ? 0 : i0 + 1;
    const float a = ringA[i0] + (ringB != nullptr ? ringB[i0] : 0.0f);
    const float b = ringA[i1] + (ringB != nullptr ? ringB[i1] : 0.0f);
    return 0.5f * (a * (1.0f - fr) + b * fr);
}

//==============================================================================
// Granular core: grain positions live in the caller's units (ring frames for
// live input, material seconds for samples).
class GrainCore
{
public:
    static constexpr double kHopSec = 0.04; // half a grain (40 ms)

    // hopUnits: grain hop in position units. uStepPerSample: how far q's
    // timeline (which advances at 1x) moves per output sample, i.e. 1 for
    // frames and 1/sampleRate for seconds.
    void prepare(double hopUnits, double uStepPerSample)
    {
        hop = std::max(1.0e-6, hopUnits);
        uStep = uStepPerSample;
        u = 0.0;
        q = 0.0;
    }

    void reset(double anchor) { q = anchor; u = 0.0; }

    // Scale the grain-start timeline (1.0 = normal material speed; negative
    // values read backwards). The read inside the grain still runs at the
    // pitch ratio, so time and pitch stay independent.
    void setTimeScale(double s) { qScale = s; }

    // Change the grain length without a click: the crossfade position
    // (u / hop) is kept, so a shrinking hop restarts the grain at the same
    // point of its fade instead of jumping.
    void setHop(double hopUnits)
    {
        const double nh = std::max(1.0e-6, hopUnits);
        if (nh == hop) return;
        const double frac = hop > 0.0 ? u / hop : 0.0;
        hop = nh;
        u = frac * hop;
    }
    double getHop() const { return hop; }

    double tapA(double ratio) const { return q + u * ratio; }
    double tapB(double ratio) const { return tapA(ratio) + (ratio - 1.0) * hop; }
    // Crossfade tap for a grain-start timeline that advances at `scale`
    // instead of 1x: at the grain boundary the current grain's tapA meets
    // the next grain's tapB, so the crossfade stays continuous (no click).
    double tapBForScale(double ratio, double scale) const
    {
        return tapA(ratio) + (ratio - scale) * hop;
    }
    float crossfadeWeight() const
    {
        return (float) (0.5 - 0.5 * std::cos(kPi * u / hop));
    }

    void advance()
    {
        u += uStep;
        if (u >= hop) { u -= hop; q += hop * qScale; }
    }

    // Hold the current grain: the read position inside the grain advances,
    // but the grain start (q) stays put, so the same short window repeats.
    void advanceHold()
    {
        u += uStep;
        if (u >= hop) u -= hop;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    double hop = 1764.0, uStep = 1.0, u = 0.0, q = 0.0, qScale = 1.0;
};

//==============================================================================
class GrainShifter
{
public:
    static constexpr double kMinRatio = kLiveMinRatio;
    static constexpr double kMaxRatio = kLiveMaxRatio;
    static constexpr double kHopSec   = GrainCore::kHopSec; // half a grain (40 ms)
    static constexpr double kSrcSec   = 0.04; // source material per grain (40 ms)
    static constexpr double kLagSec   = 0.22; // read lag behind the write head

    void prepare(double sampleRate_)
    {
        sampleRate = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
        core.prepare(sampleRate * kHopSec, 1.0);
        lag = sampleRate * kLagSec;
    }

    // Anchor the current grain kLagSec behind the write head (voice start).
    void reset(std::int64_t writeEnd) { core.reset((double) writeEnd - lag); }

    // Mono mix of the (stereo) ring, matching the engine's material path.
    // ringB may be null for mono input.
    float next(const float* ringA, const float* ringB, int size,
               std::int64_t writeEnd, double ratio)
    {
        if (ringA == nullptr || size < 8) return 0.0f;
        ratio = std::min(kLiveMaxRatio, std::max(kLiveMinRatio, ratio));

        // v1.13: a grain reads hop * ratio frames of input, and the second
        // (crossfade) tap sits (ratio - 1) * hop further on, so the read
        // reaches hop * (2 * ratio - 1) frames past the grain start. With
        // the fixed 40 ms grain that runs past the read lag above ~3.25x -
        // it would read the not-yet-written part of the ring. From there
        // the grain shrinks instead and the SOURCE window stays constant
        // (hop = sourceWindow / ratio): the read then never needs more than
        // one grain of history, so the latency does not grow with the
        // shift. Up to 3.25x the old fixed grain is kept, so the sound of
        // the usual range is unchanged.
        const double fixedHop = sampleRate * kHopSec;
        const double srcHop   = sampleRate * kSrcSec;
        core.setHop(fixedHop * (2.0 * ratio - 1.0) > lag
                        ? srcHop / std::max(1.0, ratio) : fixedHop);

        const float wA = core.crossfadeWeight();
        const float out = wA * readLiveRing(ringA, ringB, size, writeEnd, core.tapA(ratio))
                        + (1.0f - wA) * readLiveRing(ringA, ringB, size, writeEnd, core.tapB(ratio));
        core.advance();
        return out;
    }

private:
    double sampleRate = 44100.0, lag = 9702.0;
    GrainCore core;
};

//==============================================================================
// Varispeed read with crossfaded re-anchors. One read head advances at
// `ratio`; its delay behind the write head must stay inside
// [kLagMinSec, kLagMaxSec], so when it drifts out a second head is started
// kJumpSec away and the two are blended with a short equal-power fade (the
// delay keeps moving during the fade, which the trigger margin accounts for).
// Between jumps the material is read contiguously - transients and the
// waveform itself pass through un-granulated. At ratio 1 this is a pure
// delayed pass-through. No constant read lag is required, so the latency is
// much lower than the granular shifter's.
//==============================================================================
class TapeShifter
{
public:
    static constexpr double kLagMinSec = 0.04;
    static constexpr double kLagMaxSec = 0.20;
    static constexpr double kJumpSec   = 0.12; // re-anchor distance
    static constexpr double kFadeSec   = 0.02; // equal-power crossfade at a jump

    void prepare(double sampleRate_)
    {
        sampleRate = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
        lagMin = sampleRate * kLagMinSec;
        lagMax = sampleRate * kLagMaxSec;
        jump   = sampleRate * kJumpSec;
        fadeN  = std::max(8.0, sampleRate * kFadeSec);
        read0 = read1 = 0.0;
        fadePos = 0.0;
        fading = false;
    }

    // Anchor mid-window behind the write head (voice start).
    void reset(std::int64_t writeEnd)
    {
        read0 = (double) writeEnd - 0.5 * (lagMin + lagMax);
        read1 = read0;
        fadePos = 0.0;
        fading = false;
    }

    float next(const float* ringA, const float* ringB, int size,
               std::int64_t writeEnd, double ratio)
    {
        if (ringA == nullptr || size < 8 || fadeN <= 0.0) return 0.0f;
        // v1.13: varispeed on live input reads the mic faster than it
        // arrives, so this shifter keeps an octave up / down (the granular
        // and vocoder shifters cover the full 1/16x ... 16x range)
        ratio = std::min(kLiveTapeMaxRatio, std::max(kLiveTapeMinRatio, ratio));

        if (! fading)
        {
            // the delay keeps moving for the fade's duration: trigger early/
            // late by exactly that much so both heads stay in the window
            const double margin = std::abs(ratio - 1.0) * fadeN + 4.0;
            const double d = (double) writeEnd - read0;
            if (ratio > 1.0 && d <= lagMin + margin)      startFade(-jump);
            else if (ratio < 1.0 && d >= lagMax - margin) startFade(+jump);
        }

        float out;
        if (fading)
        {
            const float tw = (float) (0.5 * kPi * fadePos / fadeN); // 0 -> pi/2
            out = std::cos(tw) * readLiveRing(ringA, ringB, size, writeEnd, read0)
                + std::sin(tw) * readLiveRing(ringA, ringB, size, writeEnd, read1);
            read1 += ratio;
            fadePos += 1.0;
            if (fadePos >= fadeN) { fading = false; read0 = read1; }
        }
        else
        {
            out = readLiveRing(ringA, ringB, size, writeEnd, read0);
        }

        read0 += ratio;
        return out;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    void startFade(double delta) { fading = true; fadePos = 0.0; read1 = read0 + delta; }

    double sampleRate = 44100.0;
    double lagMin = 1764.0, lagMax = 8820.0, jump = 5292.0, fadeN = 882.0;
    double read0 = 0.0, read1 = 0.0, fadePos = 0.0;
    bool fading = false;
};

//==============================================================================
// Phase-vocoder core (see the file header). One input sample per call, the
// stream advancing at 1x; the returned sample is time-aligned (after the
// priming described in the header).
class VocoderCore
{
public:
    static constexpr int kFftOrder = 10;
    static constexpr int kFftSize  = 1 << kFftOrder; // 1024 (~21 ms)
    static constexpr int kHop      = kFftSize / 4;   // 75% overlap
    static constexpr int kPrime    = kFftSize;       // fill before the first read

    void prepare(double sampleRate_)
    {
        sampleRate = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;

        if ((int) window.size() != kFftSize)
        {
            window.resize((size_t) kFftSize);
            for (int n = 0; n < kFftSize; ++n)
                window[(size_t) n] = (float) (0.5 - 0.5 * std::cos(2.0 * kPi * n / kFftSize));
            fftData.assign((size_t) 2 * kFftSize, 0.0f);
            inFifo.assign((size_t) 2 * kFftSize, 0.0f);
            ola.assign((size_t) 4 * kFftSize, 0.0f);
            lastPhase.assign((size_t) (kFftSize / 2 + 1), 0.0f);
            sumPhase.assign((size_t) (kFftSize / 2 + 1), 0.0);
            synMag.assign((size_t) (kFftSize / 2 + 1), 0.0f);
            synFreq.assign((size_t) (kFftSize / 2 + 1), 0.0);
        }
        clearState();
    }

    void reset() { clearState(); }

    float next(float in, double ratio)
    {
        if (ola.empty()) return 0.0f;

        inFifo[inFifoPos % inFifo.size()] = in;
        ++inFifoPos;
        if (--frameCount <= 0)
        {
            frameCount = kHop;
            processFrame(ratio);
        }

        if (! started)
        {
            if (tailComplete < (double) kPrime) return 0.0f; // still priming
            readPos = std::max(0.0, tailComplete - (double) kPrime);
            started = true;
        }

        if (readPos > tailComplete) readPos = tailComplete; // hold at the write tail
        const float out = olaAt(readPos);
        readPos += 1.0;
        return out;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr float kOlaNorm = 2.0f / 3.0f; // 1 / sum(hann^2), 75% overlap

    void clearState()
    {
        inFifoPos = 0;
        frameCount = kFftSize;
        olaNext = 0.0;
        tailComplete = -1.0;
        readPos = 0.0;
        started = false;
        if (! ola.empty())
        {
            std::fill(ola.begin(), ola.end(), 0.0f);
            std::fill(inFifo.begin(), inFifo.end(), 0.0f);
            std::fill(lastPhase.begin(), lastPhase.end(), 0.0f);
            std::fill(sumPhase.begin(), sumPhase.end(), 0.0);
        }
    }

    static double princarg(double p)
    {
        p = std::fmod(p + kPi, 2.0 * kPi);
        if (p < 0.0) p += 2.0 * kPi;
        return p - kPi;
    }

    void processFrame(double ratio)
    {
        const std::size_t cap = (std::size_t) kFftSize;
        const std::size_t fifoSz = inFifo.size();
        const std::size_t base = (inFifoPos >= cap) ? inFifoPos - cap : 0;
        for (std::size_t n = 0; n < cap; ++n)
            fftData[n] = window[n] * inFifo[(base + n) % fifoSz];

        fft.performRealOnlyForwardTransform(fftData.data(), true);

        // ---- analysis: magnitude + true frequency (Hz) per bin ----
        const double expctPerHop = 2.0 * kPi * (double) kHop / (double) kFftSize;
        const double freqPerBin  = sampleRate / (double) kFftSize;
        const double osamp       = (double) kFftSize / (double) kHop;
        const int nb = kFftSize / 2 + 1;

        std::fill(synMag.begin(), synMag.end(), 0.0f);
        std::fill(synFreq.begin(), synFreq.end(), 0.0);

        for (int k = 0; k < nb; ++k)
        {
            const float re = fftData[(size_t) (2 * k)];
            const float im = fftData[(size_t) (2 * k + 1)];
            const float mag = std::sqrt(re * re + im * im);
            const float phase = std::atan2(im, re);

            double dev = princarg((double) phase - (double) lastPhase[(size_t) k]
                                  - expctPerHop * (double) k);
            lastPhase[(size_t) k] = phase;
            const double trueHz = (double) k * freqPerBin
                                + dev / (2.0 * kPi) * osamp * freqPerBin;

            // remap the partial onto the pitch-shifted bin (smbPitchShift uses
            // the same truncated index; rounding breaks the low ratios)
            const int idx = (int) ((double) k * ratio);
            if (idx <= kFftSize / 2)
            {
                synMag[(size_t) idx] += mag;
                synFreq[(size_t) idx] = trueHz * ratio;
            }
        }

        // ---- synthesis: each bin's phase advances at its true frequency per
        //      hop; re-emitting at the remapped bins keeps the OLA coherent ----
        const double advPerHz = 2.0 * kPi * (double) kHop / sampleRate;
        for (int k = 0; k < nb; ++k)
        {
            const double ph = sumPhase[(size_t) k] + advPerHz * synFreq[(size_t) k];
            sumPhase[(size_t) k] = ph;
            const float mag = synMag[(size_t) k];
            fftData[(size_t) (2 * k)]     = mag * (float) std::cos(ph);
            fftData[(size_t) (2 * k + 1)] = mag * (float) std::sin(ph);
        }

        fft.performRealOnlyInverseTransform(fftData.data());

        // integer-hop overlap-add (analysis x synthesis Hann -> rows sum 1.5).
        // The last hop's rows are the ones this frame writes FIRST (three
        // further contributions follow in the next frames), so they assign;
        // earlier rows accumulate. Without the assign, the stale value from
        // the previous ring wrap would echo into the row (audible as level
        // dips/beats that drift with the material).
        const std::int64_t i0 = (std::int64_t) std::llround(olaNext);
        const std::int64_t osz = (std::int64_t) ola.size();
        const double readSafe = started ? readPos : 0.0;
        const std::size_t firstN = (std::size_t) (kFftSize - kHop);
        for (std::size_t n = 0; n < cap; ++n)
        {
            const std::int64_t row = i0 + (std::int64_t) n;
            if (started && (double) (row + 2) > readSafe + (double) osz)
                break; // never overwrite rows the reader still needs
            const std::int64_t a = ((row % osz) + osz) % osz;
            if (n >= firstN)
                ola[(size_t) a] = fftData[n] * window[n] * kOlaNorm;
            else
                ola[(size_t) a] += fftData[n] * window[n] * kOlaNorm;
        }
        olaNext = (double) i0 + (double) kHop;
        // rows complete once the frame starting one hop later would be needed
        tailComplete = (double) i0 + (double) kHop - 2.0;
    }

    float olaAt(double pos) const
    {
        const std::int64_t ip = (std::int64_t) std::floor(pos);
        const float fr = (float) (pos - (double) ip);
        const std::int64_t osz = (std::int64_t) ola.size();
        const std::int64_t a = ((ip % osz) + osz) % osz;
        const std::size_t b = (std::size_t) (a + 1 == osz ? 0 : a + 1);
        return ola[(size_t) a] * (1.0f - fr) + ola[b] * fr;
    }

    juce::dsp::FFT fft { kFftOrder };
    double sampleRate = 44100.0;
    std::vector<float> window, fftData;      // analysis window, real FFT scratch
    std::vector<float> inFifo;               // sliding input window
    std::vector<float> ola;                  // output, circular
    std::vector<float> lastPhase;            // last analysis phase per bin
    std::vector<double> sumPhase;            // accumulated synthesis phase
    std::vector<float> synMag;               // remapped synthesis magnitudes
    std::vector<double> synFreq;             // remapped true frequencies (Hz)
    std::size_t inFifoPos = 0;               // absolute input frames pushed
    int frameCount = kFftSize;
    double olaNext = 0.0;                    // start of the next synthesis frame
    double tailComplete = -1.0;              // rows complete: read bound
    double readPos = 0.0;                    // output read position
    bool started = false;
};

//==============================================================================
// Live-input wrapper: reads the host ring at 1x into the core.
class VocoderShifter
{
public:
    void prepare(double sampleRate_)
    {
        sampleRate = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
        core.prepare(sampleRate);
    }

    void reset(std::int64_t)
    {
        core.reset();
        // v1.14: the input is read at the frame the engine says is current
        // for this sample, so no run position is kept here
    }

    float next(const float* ringA, const float* ringB, int size,
               std::int64_t writeEnd, double ratio)
    {
        if (ringA == nullptr || size < 64) return 0.0f;
        ratio = std::min(kLiveMaxRatio, std::max(kLiveMinRatio, ratio));

        // v1.14: read the frame that is current at this sample (2 frames
        // back - the ring's safety margin). The write head advances per
        // block, so a running read position would race into the clamp and
        // repeat the same frame for the whole block: the vocoder would
        // re-synthesise that staircase as a buzz at the block rate.
        const float in = readLiveRing(ringA, ringB, size, writeEnd, (double) writeEnd - 2.0);
        return core.next(in, ratio);
    }

private:
    double sampleRate = 44100.0;
    VocoderCore core;
};

//==============================================================================
// Per-voice material state for SAMPLE-tab (mode-1) arcs. Positions are in the
// slot's seconds and wrap over the seamless loop; the engine reads the slot
// itself, picking by its sample pitch mode (UpicEngine::setSamplePitchMode):
//   varispeed - `pos` advances at the pitch ratio (slot length scales)
//   granular  - `grain` taps at 1x, reads inside the grains at the ratio
//   vocoder   - `voc` shifts the stream fed from `vocInPos` (1x)
// A mode change drops the voices so each re-anchors at its next contact.
//==============================================================================
struct SampleVoice
{
    void prepare(double sampleRate_)
    {
        const double sr = sampleRate_ > 0.0 ? sampleRate_ : 44100.0;
        grain.prepare(GrainCore::kHopSec, 1.0 / sr); // material seconds
        voc.prepare(sr);
    }

    void reset(double anchorSec)
    {
        pos = anchorSec;
        grain.reset(anchorSec);
        vocInPos = anchorSec;
        voc.reset();
    }

    double pos = 0.0;        // varispeed read position (seconds)
    GrainCore grain;         // granular taps (material seconds)
    VocoderCore voc;         // phase vocoder state
    double vocInPos = 0.0;   // material position fed into `voc` (seconds)
    int lastMode = 0;        // v1.21: the algorithm this voice's state belongs to
};

//==============================================================================
// Per-voice selector: holds the three live shifters and dispatches next() to
// the chosen one, so the engine's live voices stay one map entry per arc.
//==============================================================================
class LiveShifter
{
public:
    enum class Mode { granular = 0, tape = 1, vocoder = 2 };

    void prepare(double sampleRate_)
    {
        grain.prepare(sampleRate_);
        tape.prepare(sampleRate_);
        vocoder.prepare(sampleRate_);
    }

    void reset(std::int64_t writeEnd)
    {
        grain.reset(writeEnd);
        tape.reset(writeEnd);
        vocoder.reset(writeEnd);
    }

    void setMode(Mode m) { mode = m; }

    float next(const float* ringA, const float* ringB, int size,
               std::int64_t writeEnd, double ratio)
    {
        switch (mode)
        {
            case Mode::tape:    return tape.next(ringA, ringB, size, writeEnd, ratio);
            case Mode::vocoder: return vocoder.next(ringA, ringB, size, writeEnd, ratio);
            default:            return grain.next(ringA, ringB, size, writeEnd, ratio);
        }
    }

private:
    Mode mode = Mode::granular;
    GrainShifter grain;
    TapeShifter tape;
    VocoderShifter vocoder;
};

} // namespace upic
