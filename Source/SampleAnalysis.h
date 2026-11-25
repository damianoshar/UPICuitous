#pragma once
#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "Envelope.h"
#include "PieceLimits.h"

//==============================================================================
// WP7 (Wave F, 2026-11-21): the sample analysis.
//
// Two things the material needs and the app did not have:
//
//   - the CYCLE: the shortest loop that repeats inside the material. The
//     historical UPIC's sample slots were loops, and a recording that holds
//     a repeating figure (a bowed note, a drum loop, a synth patch) can be
//     turned into a wavetable by taking ONE cycle of it - the rest of the
//     take is the tail, not the sound.
//
//   - the ENVELOPE: the amplitude contour of the material, so a recording can
//     become the envelope of an arc (the "Slot -> Env" of the action list).
//
// Both run on the message thread, on an immutable view of the material's
// region. The analysis is deterministic: the same material always gives the
// same cycle, so a test can assert on it.
//==============================================================================
namespace upic
{
    // Where the cycle is and how long it is.
    struct CycleInfo
    {
        bool ok = false;
        int startSample = 0;      // the first frame of the cycle
        int lengthSamples = 0;    // its length, in frames
        double frequencyHz = 0.0; // the repetition rate
        double confidence = 0.0;  // 0..1, the correlation's peak
    };

    namespace SampleAnalysisDetail
    {
        // A normalised correlation with the two means removed. A constant
        // offset must not make an otherwise periodic signal look repetitive.
        inline double normalisedCorrelation (const float* data, int begin, int end,
                                             int lag)
        {
            const int count = end - begin - lag;
            if (data == nullptr || count <= 0) return -1.0;

            double meanA = 0.0, meanB = 0.0;
            for (int i = 0; i < count; ++i)
            {
                meanA += (double) data[begin + i];
                meanB += (double) data[begin + i + lag];
            }
            meanA /= (double) count;
            meanB /= (double) count;

            double numerator = 0.0, energyA = 0.0, energyB = 0.0;
            for (int i = 0; i < count; ++i)
            {
                const double a = (double) data[begin + i] - meanA;
                const double b = (double) data[begin + i + lag] - meanB;
                numerator += a * b;
                energyA += a * a;
                energyB += b * b;
            }

            const double denominator = std::sqrt (energyA * energyB);
            return denominator > 1.0e-12 ? numerator / denominator : -1.0;
        }
    }

    // The shortest repeating loop in the buffer.
    //
    // The coarse pass is a normalised autocorrelation over a decimated copy
    // of the material. The active part of the copy is used, so a long silent
    // lead-in cannot hide a loop that starts later. Every coarse peak is then
    // refined at the material's own rate; the full-rate peaks are the real
    // candidates, and the shortest credible one wins. The start is placed on
    // a zero crossing so the loop's seam does not click.
    //
    // A buffer with no repetition (noise, a decaying hit) comes back with
    // ok == false: the caller then says so instead of guessing.
    inline CycleInfo findBestCycle (const float* data, int numSamples, double sampleRate,
                                    int minLength, int maxLength)
    {
        CycleInfo info;
        if (data == nullptr || numSamples < 64 || ! (sampleRate > 0.0)) return info;

        minLength = juce::jmax (1, minLength);
        maxLength = juce::jmin (numSamples - 1, maxLength);
        if (minLength > maxLength) return info;

        // The coarse pass runs at about 4 kHz. That is enough for the
        // 25 Hz..2 kHz musical range, while the full-rate refinement keeps
        // the frame accuracy the 441 Hz fixture needs.
        const int decimation = juce::jmax (1, (int) std::lround (sampleRate / 4000.0));
        const int decimatedCount = (numSamples + decimation - 1) / decimation;
        if (decimatedCount < 8) return info;

        std::vector<float> coarse ((size_t) decimatedCount, 0.0f);
        float peak = 0.0f;
        for (int i = 0; i < decimatedCount; ++i)
        {
            const int from = i * decimation;
            const int to = juce::jmin (numSamples, from + decimation);
            double sum = 0.0;
            for (int j = from; j < to; ++j) sum += (double) data[j];
            const float value = (float) (sum / (double) juce::jmax (1, to - from));
            coarse[(size_t) i] = value;
            peak = juce::jmax (peak, std::abs (value));
        }
        if (peak <= 1.0e-6f) return info;

        // Ignore the quiet edges. This is what lets a loop after a silent
        // lead-in be found, without making the correlation run over minutes
        // of room noise.
        const float activeThreshold = juce::jmax (1.0e-5f, peak * 0.02f);
        int activeFirst = 0;
        while (activeFirst < decimatedCount
               && std::abs (coarse[(size_t) activeFirst]) < activeThreshold)
            ++activeFirst;
        int activeLast = decimatedCount - 1;
        while (activeLast > activeFirst
               && std::abs (coarse[(size_t) activeLast]) < activeThreshold)
            --activeLast;
        // A legal sample can be sixty seconds long. Correlating every lag
        // over all of it would freeze the message thread for seconds, so the
        // detector examines a bounded deterministic window of the active
        // material. Eight seconds is far longer than the shortest musical
        // period and keeps the work interactive.
        constexpr double kMaxAnalysisSeconds = 8.0;
        const int maxCoarseFrames = juce::jmax (8,
            (int) std::ceil (kMaxAnalysisSeconds * 4000.0));
        if (activeLast - activeFirst + 1 > maxCoarseFrames)
            activeLast = activeFirst + maxCoarseFrames - 1;
        const int activeCount = activeLast - activeFirst + 1;
        if (activeCount < 8) return info;

        // The caller's frame bounds are mapped to the coarse lag range. The
        // floor/ceil leaves a full-rate neighbourhood on both sides, which
        // is where the exact period is found.
        // The minimum period is rounded UP when it is mapped to the coarse
        // grid. Flooring it would turn a 16-frame lower bound into one
        // coarse frame; adjacent averaged bins then correlate strongly
        // simply because they share most of their samples.
        const int minLag = juce::jmax (1, (minLength + decimation - 1) / decimation);

        // A claimed period must fit at least twice in the active material.
        // Without this bound a lag close to the end leaves only one or two
        // overlapping samples, for which the normalised correlation is
        // numerically perfect for arbitrary noise.
        const int availableMaxLag = (activeCount - 1) / 2;
        const int requestedMaxLag = (int) std::ceil ((double) maxLength / (double) decimation);
        const int maxLag = juce::jmin (availableMaxLag,
            juce::jmax (minLag, requestedMaxLag));
        if (minLag > maxLag || maxLag < 1) return info;

        std::vector<double> lagScore ((size_t) maxLag + 2, -1.0);
        for (int lag = minLag; lag <= maxLag; ++lag)
            lagScore[(size_t) lag] = SampleAnalysisDetail::normalisedCorrelation (
                coarse.data(), activeFirst, activeLast + 1, lag);

        // Collect the coarse correlation's genuine local peaks. A periodic
        // signal has one at its shortest period and another at each multiple.
        // The comparison uses the actual correlation on either side, even at
        // the edge of the searched range. A missing neighbour used to be
        // represented by -1, which made an arbitrary edge value look like
        // a peak (and was the source of the 16-frame false cycle).
        std::vector<int> coarsePeaks;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const double score = lagScore[(size_t) lag];
            if (score < 0.20) continue;
            const double before = lag > minLag
                ? lagScore[(size_t) lag - 1]
                : SampleAnalysisDetail::normalisedCorrelation (
                    coarse.data(), activeFirst, activeLast + 1, lag - 1);
            const double after = lag < maxLag
                ? lagScore[(size_t) lag + 1]
                : SampleAnalysisDetail::normalisedCorrelation (
                    coarse.data(), activeFirst, activeLast + 1, lag + 1);
            if (score >= before && score >= after)
                coarsePeaks.push_back (lag);
        }
        // There is deliberately no "best score anywhere" fallback. A
        // maximum without a local peak is not evidence of a repeating
        // cycle, and accepting one made random noise look periodic.
        if (coarsePeaks.empty()) return info;

        const int fullBegin = juce::jmin (numSamples - 1, activeFirst * decimation);
        const int fullEnd = juce::jmax (fullBegin + 1,
            juce::jmin (numSamples, (activeLast + 1) * decimation));

        struct Candidate
        {
            int length = 0;
            double score = -1.0;
        };
        std::vector<Candidate> candidates;

        const auto addCandidate = [&candidates] (int length, double score)
        {
            if (length < 1) return;
            for (auto& c : candidates)
                if (c.length == length)
                {
                    c.score = juce::jmax (c.score, score);
                    return;
                }
            candidates.push_back ({ length, score });
        };

        for (int coarseLag : coarsePeaks)
        {
            const int from = juce::jmax (minLength, coarseLag * decimation - decimation);
            const int to = juce::jmin (maxLength, coarseLag * decimation + decimation);
            if (from > to || to >= fullEnd - fullBegin) continue;

            // Find the full-rate local peaks inside this coarse peak's
            // neighbourhood. Taking every lag would choose a near miss
            // (96 instead of 100 frames for the 441 Hz fixture). As in the
            // coarse pass, the actual neighbours are used at the edges;
            // a one-sided edge must not be accepted as a peak.
            for (int lag = from; lag <= to; ++lag)
            {
                const double score = SampleAnalysisDetail::normalisedCorrelation (
                    data, fullBegin, fullEnd, lag);
                const double before = lag > 0
                    ? SampleAnalysisDetail::normalisedCorrelation (
                        data, fullBegin, fullEnd, lag - 1)
                    : -1.0;
                const double after = SampleAnalysisDetail::normalisedCorrelation (
                    data, fullBegin, fullEnd, lag + 1);
                if (score >= 0.50 && score >= before && score >= after)
                    addCandidate (lag, score);
            }

            // There is deliberately no range-end fallback. A value at the
            // edge of a short correlation window is not a confirmed cycle.
        }

        if (candidates.empty()) return info;

        // The shortest credible full-rate peak wins. The confidence floor
        // keeps a weak accidental match from being presented as a loop.
        std::stable_sort (candidates.begin(), candidates.end(),
                          [] (const Candidate& a, const Candidate& b)
                          {
                              return a.length < b.length;
                          });

        const Candidate* chosen = nullptr;
        for (const auto& candidate : candidates)
        {
            if (candidate.score < 0.50) continue;
            chosen = &candidate;
            break;
        }
        if (chosen == nullptr) return info;

        // Put the start on the first zero crossing of the mean-removed active
        // signal. The whole cycle must fit inside the material; the start is
        // never allowed to make it run past the end.
        const int latestStart = numSamples - chosen->length;
        if (latestStart < 0) return info;

        const int meanCount = juce::jmin (chosen->length,
            juce::jmax (1, fullEnd - fullBegin));
        double mean = 0.0;
        for (int i = 0; i < meanCount; ++i) mean += (double) data[fullBegin + i];
        mean /= (double) meanCount;

        int start = juce::jlimit (0, latestStart, fullBegin);
        const int searchEnd = juce::jmin (latestStart,
            fullBegin + juce::jmax (1, chosen->length));
        for (int i = juce::jmax (1, fullBegin); i <= searchEnd; ++i)
        {
            const double a = (double) data[i - 1] - mean;
            const double b = (double) data[i] - mean;
            if ((a <= 0.0 && b > 0.0) || (a >= 0.0 && b < 0.0))
            {
                start = i;
                break;
            }
        }

        info.ok = true;
        info.startSample = start;
        info.lengthSamples = chosen->length;
        info.frequencyHz = sampleRate / (double) chosen->length;
        info.confidence = juce::jlimit (0.0, 1.0, chosen->score);
        return info;
    }

    // The buffer overload from the work plan. Channel 0 carries the cycle
    // analysis: averaging stereo first can cancel an antiphase recording.
    // `targetLength` is an optional hint and is narrowed with 64-bit maths,
    // so even INT_MAX cannot overflow the bounds.
    inline CycleInfo findBestCycle (const juce::AudioBuffer<float>& buffer,
                                    double sampleRate, int targetLength = 0)
    {
        const int n = buffer.getNumSamples();
        if (n <= 0 || buffer.getNumChannels() <= 0) return CycleInfo();

        int minLength = 16;
        int maxLength = n - 1;
        if (targetLength > 0)
        {
            // The bounds are worked out in 64-bit maths, so even INT_MAX
            // cannot overflow them. They deliberately do NOT use juce::jmax,
            // juce::jmin or juce::jlimit with a 64-bit integer: JUCE's
            // jmax/jmin carry SIMD overloads, so naming a 64-bit integer
            // makes the compiler instantiate dsp::SIMDRegister<long long>,
            // and JUCE's NEON operations only provide SIMDNativeOps<int64_t>
            // - which is 'long' on 64-bit ARM, not 'long long'. That is a
            // compile error on Android (and it compiled on Windows only
            // because MSVC's int64_t happens to be 'long long').
            const std::int64_t target = (std::int64_t) targetLength;
            const std::int64_t lo = std::max<std::int64_t> (16, target * 3 / 4);
            const std::int64_t hi = std::min<std::int64_t> ((std::int64_t) n - 1,
                                                            target * 5 / 4);
            minLength = (int) std::clamp<std::int64_t> (lo, 1,
                (std::int64_t) std::numeric_limits<int>::max());
            maxLength = (int) std::clamp<std::int64_t> (hi, 1,
                (std::int64_t) std::numeric_limits<int>::max());
        }
        return findBestCycle (buffer.getReadPointer (0), n, sampleRate,
                              minLength, maxLength);
    }

    // Moves a cycle's start to the nearest zero crossing (the loop's seam
    // should not click). Both crossing directions count, and the result is
    // constrained so the complete cycle still fits in the material.
    inline int alignCycleToZeroCrossing (const float* data, int numSamples,
                                          int startSample, int lengthSamples)
    {
        if (data == nullptr || numSamples <= 1 || lengthSamples <= 0) return startSample;
        if (lengthSamples >= numSamples) return 0;

        const int latestStart = numSamples - lengthSamples;
        startSample = juce::jlimit (0, latestStart, startSample);

        const int count = juce::jmin (lengthSamples, latestStart - startSample + 1);
        double mean = 0.0;
        for (int i = 0; i < count; ++i) mean += (double) data[startSample + i];
        if (count > 0) mean /= (double) count;

        const int window = juce::jmax (2, lengthSamples / 8);
        const int from = juce::jmax (1, startSample - window);
        const int to = juce::jmin (latestStart, startSample + window);
        int best = startSample;
        double bestDistance = std::numeric_limits<double>::max();
        double bestSeam = std::numeric_limits<double>::max();
        for (int i = from; i <= to; ++i)
        {
            const double a = (double) data[i - 1] - mean;
            const double b = (double) data[i] - mean;
            if (! ((a <= 0.0 && b > 0.0) || (a >= 0.0 && b < 0.0))) continue;

            const double distance = std::abs ((double) (i - startSample));
            const int seamFrame = (i + lengthSamples) % numSamples;
            const double seam = std::abs ((double) data[i] - (double) data[seamFrame]);
            if (distance < bestDistance
                || (distance == bestDistance && seam < bestSeam))
            {
                bestDistance = distance;
                bestSeam = seam;
                best = i;
            }
        }
        return best;
    }

    // The amplitude contour's two measures.
    enum class EnvelopeMode
    {
        RMS = 0,
        Peak,
        // Kept as an alias for callers written against the first WP7 draft.
        Rms = RMS
    };

    // The amplitude contour of a mono buffer: one point per bucket, RMS or
    // peak, normalised so the loudest bucket is 1.0. The x axis is the
    // buffer's own length (0..1), so the envelope can drive an arc directly.
    inline std::vector<EnvPoint> extractEnvelope (const float* data, int numSamples,
                                                  int numPoints, EnvelopeMode mode)
    {
        std::vector<EnvPoint> out;
        if (data == nullptr || numSamples <= 0) return out;

        numPoints = juce::jlimit (2, limits::kMaxPointsPerEnvelope, numPoints);
        out.reserve ((size_t) numPoints);

        std::vector<float> values ((size_t) numPoints, 0.0f);
        float globalPeak = 0.0f;
        for (int b = 0; b < numPoints; ++b)
        {
            const int from = (int) ((juce::int64) b * numSamples / numPoints);
            const int to = (int) ((juce::int64) (b + 1) * numSamples / numPoints);
            float sumSquares = 0.0f, peak = 0.0f;
            for (int i = from; i < to; ++i)
            {
                const float v = std::abs (data[i]);
                peak = juce::jmax (peak, v);
                sumSquares += v * v;
            }
            const float rms = to > from ? std::sqrt (sumSquares / (float) (to - from)) : 0.0f;
            values[(size_t) b] = mode == EnvelopeMode::Peak ? peak : rms;
            globalPeak = juce::jmax (globalPeak, values[(size_t) b]);
        }

        const float scale = globalPeak > 1.0e-6f ? 1.0f / globalPeak : 0.0f;
        for (int b = 0; b < numPoints; ++b)
            out.push_back ({ (float) b / (float) (numPoints - 1),
                             limits::clampFinite (values[(size_t) b] * scale, 0.0f, 1.0f) });
        return out;
    }

    // The channel-aware contour of a region. Peak is the largest absolute
    // sample over all channels; RMS is the power RMS over all channels.
    // Neither measure can cancel when the channels are in opposition.
    inline std::vector<EnvPoint> extractEnvelope (const juce::AudioBuffer<float>& buffer,
                                                  int startFrame, int endFrame,
                                                  int numPoints, EnvelopeMode mode)
    {
        std::vector<EnvPoint> out;
        const int channels = buffer.getNumChannels();
        const int total = buffer.getNumSamples();
        const int from = juce::jmax (0, startFrame);
        const int to = juce::jmin (total, endFrame);
        if (channels <= 0 || to <= from) return out;

        numPoints = juce::jlimit (2, limits::kMaxPointsPerEnvelope, numPoints);
        out.reserve ((size_t) numPoints);

        std::vector<float> values ((size_t) numPoints, 0.0f);
        float globalPeak = 0.0f;
        for (int b = 0; b < numPoints; ++b)
        {
            const int bucketFrom = from + (int) ((juce::int64) b * (to - from) / numPoints);
            const int bucketTo = from + (int) ((juce::int64) (b + 1) * (to - from) / numPoints);
            double sumSquares = 0.0;
            float peak = 0.0f;
            int count = 0;
            for (int c = 0; c < channels; ++c)
                for (int i = bucketFrom; i < bucketTo; ++i)
                {
                    const float v = std::abs (buffer.getSample (c, i));
                    peak = juce::jmax (peak, v);
                    sumSquares += (double) v * v;
                    ++count;
                }
            const float rms = count > 0 ? (float) std::sqrt (sumSquares / (double) count) : 0.0f;
            values[(size_t) b] = mode == EnvelopeMode::Peak ? peak : rms;
            globalPeak = juce::jmax (globalPeak, values[(size_t) b]);
        }

        const float scale = globalPeak > 1.0e-6f ? 1.0f / globalPeak : 0.0f;
        for (int b = 0; b < numPoints; ++b)
            out.push_back ({ (float) b / (float) (numPoints - 1),
                             limits::clampFinite (values[(size_t) b] * scale, 0.0f, 1.0f) });
        return out;
    }

    // The plan-compatible buffer overload. The sample rate is part of the
    // public shape even though the contour's x axis is normalised time.
    inline std::vector<EnvPoint> extractEnvelope (const juce::AudioBuffer<float>& buffer,
                                                  double sampleRate, EnvelopeMode mode,
                                                  int numPoints)
    {
        (void) sampleRate;
        return extractEnvelope (buffer, 0, buffer.getNumSamples(), numPoints, mode);
    }

    // The short buffer overload retained for the first WP7 draft.
    inline std::vector<EnvPoint> extractEnvelope (const juce::AudioBuffer<float>& buffer,
                                                  int numPoints, EnvelopeMode mode)
    {
        return extractEnvelope (buffer, 0, buffer.getNumSamples(), numPoints, mode);
    }
}
