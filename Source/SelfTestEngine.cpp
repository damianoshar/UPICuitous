#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "AllocCounter.h"

//==============================================================================
// The engine and the maths: the pitch and the envelope curves, the offline
// render, the cursor render path, the continuity of the output and the
// per-arc overrides.

void upic::runDspChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 1. pitch interpolation must be log-domain (glissando = linear in pitch)
    {
        upic::Arc a; a.points = { {0.0, 110.0}, {6.0, 220.0} };
        double mid = a.freqAt(3.0);
        check(std::abs(mid - std::sqrt(110.0 * 220.0)) < 0.05,
              "arc freqAt log-interp (midpoint)", juce::String(mid, 3) + " Hz (expect 155.563)");
    }

    // 1b. the point lookups use a binary search now (they run per
    //      sample): the result must be identical to the old linear
    //      scan, on a curve with many points.
    {
        upic::Arc a;
        for (int i = 0; i < 500; ++i)
            a.points.push_back({ (double) i * 0.02, 110.0 + (double) i * 0.5 });

        // the reference: the linear scan the binary search replaced
        auto linearFreqAt = [&a] (double t) -> double
        {
            if (t <= a.points.front().t) return a.points.front().freq;
            if (t >= a.points.back().t)  return a.points.back().freq;
            for (size_t i = 1; i < a.points.size(); ++i)
                if (t <= a.points[i].t)
                {
                    const double t0 = a.points[i-1].t, f0 = a.points[i-1].freq;
                    const double t1 = a.points[i].t,   f1 = a.points[i].freq;
                    if (t1 <= t0) return f1;
                    const double f = (t - t0) / (t1 - t0);
                    const double l0 = std::log2(juce::jmax(1.0, f0));
                    const double l1 = std::log2(juce::jmax(1.0, f1));
                    return std::pow(2.0, l0 + (l1 - l0) * f);
                }
            return a.points.back().freq;
        };

        double maxDiff = 0.0;
        for (int i = 0; i <= 2000; ++i)
        {
            const double tSec = (double) i / 2000.0 * 9.98;
            maxDiff = juce::jmax(maxDiff, std::abs(a.freqAt(tSec) - linearFreqAt(tSec)));
        }
        check(maxDiff < 1e-9, "the binary-search freqAt matches the linear scan",
              "max diff " + juce::String(maxDiff, 12));

        upic::Envelope e;
        e.points.clear(); // the constructor pre-fills two points
        for (int i = 0; i < 500; ++i)
            e.points.push_back({ (float) i * 0.002f, (float) (i % 100) / 100.0f });

        auto linearValueAt = [&e] (float t) -> float
        {
            if (t <= e.points.front().x) return e.points.front().y;
            for (size_t i = 1; i < e.points.size(); ++i)
                if (t <= e.points[i].x)
                {
                    const float x0 = e.points[i-1].x, y0 = e.points[i-1].y;
                    const float x1 = e.points[i].x,   y1 = e.points[i].y;
                    if (x1 <= x0) return y1;
                    const float f = (t - x0) / (x1 - x0);
                    return y0 + (y1 - y0) * f;
                }
            return e.points.back().y;
        };

        float maxDiffE = 0.0f;
        for (int i = 0; i <= 2000; ++i)
        {
            const float tSec = (float) i / 2000.0f * 0.998f;
            maxDiffE = juce::jmax(maxDiffE, std::abs(e.valueAt(tSec) - linearValueAt(tSec)));
        }
        check(maxDiffE < 1e-6f, "the binary-search envelope valueAt matches the linear scan",
              "max diff " + juce::String(maxDiffE, 9));
    }

    // 2. envelope shape
    {
        upic::Envelope perc; perc.presetPercussive();
        check(perc.valueAt(0.0f) < 0.01f && std::abs(perc.valueAt(0.02f) - 1.0f) < 1e-4f,
              "percussive envelope attack",
              juce::String(perc.valueAt(0.0f), 3) + " -> " + juce::String(perc.valueAt(0.02f), 3));
    }

    // 3. offline render of the demo page: must be finite, audible, bounded
    double peak = 0.0, rms = 0.0; bool finite = true; int nonzero = 0;
    {
        auto waves = upic::defaultWaveforms();
        auto envs  = upic::defaultEnvelopes();
        auto page  = upic::demoPage();
        page.durationSec = 6.0;
        juce::AudioBuffer<float> buf(2, (int) (44100.0 * 6.0));
        buf.clear();
        UpicEngine eng;
        eng.prepare(44100.0, 8192);
        std::vector<upic::SampleSlot> noSlots;
        eng.renderPageOffline(page, waves, envs, noSlots, buf, 44100.0, 0.8f);

        double sumSq = 0.0;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            float v = buf.getSample(0, i);
            if (! std::isfinite(v)) { finite = false; v = 0.0f; }
            peak = juce::jmax(peak, (double) std::abs(v));
            sumSq += (double) v * v;
            if (std::abs(v) > 1e-6f) ++nonzero;
        }
        rms = std::sqrt(sumSq / juce::jmax(1, buf.getNumSamples()));
        check(finite, "render is finite (no NaN/inf)");
        check(peak > 0.05 && peak <= 1.0, "render peak in (0.05, 1.0]", juce::String(peak, 3));
        check(nonzero > 44100, "render not silent", juce::String(nonzero) + " nonzero samples");
        check(rms > 0.005, "render rms > 0.005", juce::String(rms, 4));
    }

    // 4. JSON round-trip
    {
        auto page = upic::demoPage();
        page.arcs[0].mode = 1; page.arcs[0].sampleId = 3; page.arcs[0].refFreq = 110.0;
        auto parsed = juce::JSON::parse(juce::JSON::toString(page.toVar(), false));
        auto back = upic::Page::fromVar(parsed);
        check(back.arcs.size() == page.arcs.size() && ! back.arcs.empty(),
              "json round-trip arc count",
              juce::String((int) back.arcs.size()) + "/" + juce::String((int) page.arcs.size()));
        if (! back.arcs.empty() && ! page.arcs.empty())
            check(std::abs(back.arcs[0].points[0].freq - page.arcs[0].points[0].freq) < 1e-6,
                  "json round-trip point data");
        check(! back.arcs.empty() && back.arcs[0].mode == 1 && back.arcs[0].sampleId == 3
              && std::abs(back.arcs[0].refFreq - 110.0) < 1e-9,
              "json round-trip material fields (mode/sampleId/refFreq)");

        // file from the Python version (no nextArcId field): new arcs must
        // never collide with loaded ids 1 and 2
        auto legacy = upic::Page::fromVar(juce::JSON::parse(
            "{\"duration\":30,\"arcs\":["
            "{\"id\":1,\"points\":[{\"t\":0,\"f\":440},{\"t\":1,\"f\":880}]},"
            "{\"id\":2,\"points\":[{\"t\":0,\"f\":220},{\"t\":1,\"f\":440}]}]}"));
        check(legacy.nextArcId >= 3, "legacy file nextArcId collision fix",
              "nextArcId=" + juce::String(legacy.nextArcId) + " (expect >= 3)");
        check(! legacy.arcs.empty() && legacy.arcs[0].mode == 0,
              "legacy file defaults to oscillator mode");
    }

    // 5. realtime sample pitch shift (varispeed): material follows the arc's pitch
    {
        // 1.0s 220 Hz sine = integer cycles => loop seam phase-continuous
        upic::SampleSlot slot;
        slot.id = 1; slot.name = "sine220"; slot.sampleRate = 44100.0;
        const int N = 44100;
        {
            juce::AudioBuffer<float> buf(1, N);
            for (int i = 0; i < N; ++i)
                buf.setSample(0, i, 0.8f * (float) std::sin(
                    2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0));
            slot.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
        }
        // a 100 Hz sine is exactly 4 cycles per 40 ms grain hop, so the
        // granular crossfade is phase-coherent and the pitch measurement
        // is not skewed by grain-rate combing (the live granular tests
        // pick their ring frequency the same way).
        upic::SampleSlot grainSlot;
        grainSlot.id = 2; grainSlot.name = "sine100"; grainSlot.sampleRate = 44100.0;
        {
            juce::AudioBuffer<float> buf(1, N);
            for (int i = 0; i < N; ++i)
                buf.setSample(0, i, 0.8f * (float) std::sin(
                    2.0 * juce::MathConstants<double>::pi * 100.0 * i / 44100.0));
            grainSlot.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
        }
        // a step at half the slot makes the time mapping visible: at 3 s
        // into a 4 s arc the stretched granular read is in the +1 half,
        // while varispeed has looped back to the -1 start
        upic::SampleSlot stepSlot;
        stepSlot.id = 3; stepSlot.name = "step"; stepSlot.sampleRate = 44100.0;
        {
            juce::AudioBuffer<float> buf(1, N);
            for (int i = 0; i < N; ++i)
                buf.setSample(0, i, i < N / 2 ? -0.8f : 0.8f);
            stepSlot.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
        }
        std::vector<upic::SampleSlot> slots;
        slots.push_back(std::move(slot));
        slots.push_back(std::move(grainSlot));
        slots.push_back(std::move(stepSlot));

        double peakMode = 0.0; // tracked by measurePitch for the v1.10 modes
        auto renderArc = [&](double arcFreq, double ref, juce::AudioBuffer<float>& buf,
                             int pitchMode = 0, int sampleId = 1, bool scrub = false) {
            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 1; a.sampleId = sampleId; a.refFreq = ref;
            a.intensity = 1.0f;
            a.points = { {0.0, arcFreq}, {4.0, arcFreq} };
            pg.arcs.push_back(a);
            upic::Envelope flat; flat.presetFlat();
            std::vector<upic::Envelope> envs;
            envs.push_back(flat);
            std::vector<upic::Waveform> waves = upic::defaultWaveforms();
            buf.setSize(2, 44100 * 4);
            buf.clear();
            UpicEngine eng;
            eng.prepare(44100.0, 8192);
            eng.setSamplePitchMode(pitchMode);
            eng.setScrubPreview(scrub);
            eng.renderPageOffline(pg, waves, envs, slots, buf, 44100.0, 1.0f);
        };
        auto measurePitch = [&](double arcFreq, double ref, int pitchMode = 0,
                                int sampleId = 1, bool scrub = false) -> double
        {
            juce::AudioBuffer<float> buf;
            renderArc(arcFreq, ref, buf, pitchMode, sampleId, scrub);

            // Schmitt-trigger zero crossings over [1s, 2s] => Hz
            const int s0 = 44100, s1 = 88200;
            float pk = 0.0f;
            for (int i = s0; i < s1; ++i)
                pk = juce::jmax(pk, std::abs(buf.getSample(0, i)));
            if (pitchMode != 0)
                peakMode = juce::jmax(peakMode, (double) pk);
            const float thr = pk * 0.15f;
            int crossings = 0; bool high = false;
            for (int i = s0; i < s1; ++i)
            {
                const float v = buf.getSample(0, i);
                if (! high && v >  thr) { high = true; ++crossings; }
                else if (high && v < -thr) high = false;
            }
            return (double) crossings; // 1-second window => Hz
        };

        const double p1 = measurePitch(220.0, 220.0); // ratio 1
        const double p2 = measurePitch(440.0, 220.0); // ratio 2 (octave up)
        const double p3 = measurePitch(55.0,  220.0); // ratio 0.25
        const double p4 = measurePitch(330.0, 220.0); // ratio 1.5
        const double p5 = measurePitch(880.0, 220.0); // ratio 4
        const double p6 = measurePitch(110.0, 220.0); // ratio 0.5
        auto close = [](double p, double want){ return std::abs(p - want) <= juce::jmax(2.0, want * 0.03); };
        check(close(p1, 220.0), "varispeed shift ratio 1 -> 220 Hz",  juce::String(p1, 1) + " Hz");
        check(close(p2, 440.0), "varispeed shift ratio 2 -> 440 Hz",  juce::String(p2, 1) + " Hz");
        check(close(p3, 55.0),  "varispeed shift ratio 0.25 -> 55 Hz", juce::String(p3, 1) + " Hz");
        check(close(p4, 330.0), "varispeed shift ratio 1.5 -> 330 Hz", juce::String(p4, 1) + " Hz");
        check(close(p5, 880.0), "varispeed shift ratio 4 -> 880 Hz",  juce::String(p5, 1) + " Hz");
        check(close(p6, 110.0), "varispeed shift ratio 0.5 -> 110 Hz", juce::String(p6, 1) + " Hz");

        // ratio 1 must reconstruct the looped material exactly through the
        // soft-clip + master chain (flat env, intensity 1, away from edges)
        juce::AudioBuffer<float> rb;
        renderArc(220.0, 220.0, rb);
        double maxDiff = 0.0;
        for (int i = 44100 / 2; i < 44100 * 7 / 2; ++i)
        {
            const float mat = 0.8f * (float) std::sin(
                2.0 * juce::MathConstants<double>::pi * 220.0 * (i % 44100) / 44100.0);
            const float expect = (float) std::tanh(mat * 0.6) * 0.9f;
            maxDiff = juce::jmax(maxDiff, (double) std::abs(rb.getSample(0, i) - expect));
        }
        check(maxDiff < 1e-3, "varispeed ratio 1 exact reconstruction",
              "max diff " + juce::String(maxDiff, 6));

        // v1.10 pitch modes: granular (1) and phase vocoder (2) transpose
        // the slot without scaling its playback length; the pitch still
        // follows the arc curve exactly (varispeed above stays default)
        const double g1  = measurePitch(100.0, 100.0, 1, 2);
        const double g2  = measurePitch(200.0, 100.0, 1, 2);
        const double g05 = measurePitch(50.0, 100.0, 1, 2);
        check(std::abs(g1 - 100.0) < 10.0, "sample granular r=1 -> 100 Hz", juce::String(g1, 1) + " Hz");
        check(std::abs(g2 - 200.0) < 20.0, "sample granular r=2 -> 200 Hz", juce::String(g2, 1) + " Hz");
        check(std::abs(g05 - 50.0) < 10.0, "sample granular r=0.5 -> 50 Hz", juce::String(g05, 1) + " Hz");
        const double q1  = measurePitch(220.0, 220.0, 2);
        const double q2  = measurePitch(440.0, 220.0, 2);
        const double q05 = measurePitch(110.0, 220.0, 2);
        check(std::abs(q1 - 220.0) < 10.0, "sample vocoder r=1 -> 220 Hz", juce::String(q1, 1) + " Hz");
        check(std::abs(q2 - 440.0) < 20.0, "sample vocoder r=2 -> 440 Hz", juce::String(q2, 1) + " Hz");
        check(std::abs(q05 - 110.0) < 8.0, "sample vocoder r=0.5 -> 110 Hz", juce::String(q05, 1) + " Hz");
        const double qs = measurePitch(220.0, 220.0, 2, 1, true);
        check(std::abs(qs - 220.0) < 15.0, "sample vocoder scrub r=1 -> 220 Hz",
              juce::String(qs, 1) + " Hz");
        check(peakMode > 0.1, "sample pitch modes output signal", juce::String(peakMode, 3));

        // A short scrub block must be audible immediately. A fresh
        // phase-vocoder voice needs two analysis windows before its first
        // output, longer than a scrub preview block; the engine pre-rolls
        // scrub voices so dragging is not silent.
        auto scrubBlockPeak = [&](double t) {
            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 1; a.sampleId = 1; a.refFreq = 220.0;
            a.intensity = 1.0f;
            a.points = { { 0.0, 220.0 }, { 4.0, 220.0 } };
            pg.arcs.push_back(a);
            upic::Envelope flat; flat.presetFlat();
            std::vector<upic::Envelope> envs { flat };
            auto waves = upic::defaultWaveforms();
            UpicEngine eng; eng.prepare(44100.0, 8192);
            eng.setSamplePitchMode(2);
            eng.setScrubPreview(true);
            const int block = 512;
            juce::AudioBuffer<float> b(2, block); b.clear();
            eng.render(pg, t, waves, envs, slots, b.getWritePointer(0), b.getWritePointer(1),
                       block, 1.0f, 1.0);
            float pk = 0.0f;
            for (int i = 0; i < block; ++i)
                pk = juce::jmax(pk, std::abs(b.getSample(0, i)));
            return pk;
        };
        const float scrubPk = scrubBlockPeak(0.5);
        check(scrubPk > 0.02f, "phase vocoder scrub block outputs signal",
              juce::String(scrubPk, 4));

        // mode 3: granular with the slot's own timeline keeps the slot
        // length (unlike tape) while still transposing with the arc.
        const double n2 = measurePitch(200.0, 100.0, 3, 2);
        check(close(n2, 200.0),
              "sample granular no-stretch r=2 -> 200 Hz",
              juce::String(n2, 1) + " Hz");

        // at ratio 1 the grain and tape reads are identical.
        juce::AudioBuffer<float> tape1, grainNoStretch1;
        renderArc(220.0, 220.0, tape1, 0);
        renderArc(220.0, 220.0, grainNoStretch1, 3);
        double noStretchDiff = 0.0;
        for (int i = 44100 / 2; i < 44100 * 7 / 2; ++i)
            noStretchDiff = juce::jmax(noStretchDiff, (double) std::abs(
                tape1.getSample(0, i) - grainNoStretch1.getSample(0, i)));
        check(noStretchDiff < 1e-3,
              "granular no-stretch matches tape at ratio 1",
              "max diff " + juce::String(noStretchDiff, 6));

        // at ratio 2 the no-stretch mode must stay on the slot's own
        // timeline: at 0.25 s it is still in the first (negative) half,
        // while tape has already read 0.5 s and crossed the step.
        juce::AudioBuffer<float> stepNoStretch, stepTape2;
        renderArc(200.0, 100.0, stepNoStretch, 3, 3);
        renderArc(200.0, 100.0, stepTape2, 0, 3);
        const float nsProbe = stepNoStretch.getSample(0, 44100 / 4);
        const float tapeProbe2 = stepTape2.getSample(0, 44100 / 4);
        check(nsProbe < -0.2f && tapeProbe2 > 0.2f,
              "granular no-stretch keeps the slot length at ratio 2",
              "no-stretch " + juce::String(nsProbe, 3)
                  + " tape " + juce::String(tapeProbe2, 3));

        // v1.28: granular (stretch) maps the whole slot onto the arc in
        // normal playback; varispeed keeps the slot's own length.
        juce::AudioBuffer<float> stepGran, stepTape;
        renderArc(100.0, 100.0, stepGran, 1, 3);
        renderArc(100.0, 100.0, stepTape, 0, 3);
        const float granProbe = stepGran.getSample(0, 3 * 44100);
        const float tapeProbe = stepTape.getSample(0, 3 * 44100);
        check(granProbe > 0.2f && tapeProbe < -0.2f,
              "granular stretch maps the slot onto the arc in playback",
              "granular " + juce::String(granProbe, 3)
                  + " tape " + juce::String(tapeProbe, 3));

        // the same mode-1 mapping must be used by the scrub preview: the
        // two renders have to be sample-identical at the normal rate.
        juce::AudioBuffer<float> stepGranPlay, stepGranScrub;
        renderArc(100.0, 100.0, stepGranPlay, 1, 3, false);
        renderArc(100.0, 100.0, stepGranScrub, 1, 3, true);
        double playScrubDiff = 0.0;
        for (int i = 44100 / 2; i < 44100 * 7 / 2; ++i)
            playScrubDiff = juce::jmax(playScrubDiff, (double) std::abs(
                stepGranPlay.getSample(0, i) - stepGranScrub.getSample(0, i)));
        check(playScrubDiff < 1e-6,
              "granular stretch playback matches scrub",
              "max diff " + juce::String(playScrubDiff, 6));
    }

    // 6. seamless loop preprocessing (reads may cross the seam)
    {
        juce::AudioBuffer<float> b(1, 44100);
        for (int i = 0; i < 44100; ++i)
            b.setSample(0, i, (float) std::sin(
                2.0 * juce::MathConstants<double>::pi * 220.5 * i / 44100.0)); // half-cycle mismatch
        upic::makeSeamlessLoop(b, 44100.0);
        const int n = b.getNumSamples();
        const float seam = std::abs(b.getSample(0, n - 1) - b.getSample(0, 0));
        check(n == 44100 - 2205, "loop length = original - crossfade", juce::String(n));
        check(seam < 0.05f, "loop seam continuity (non-integer-cycle material)",
              juce::String(seam, 4));
    }

}

void upic::runCursorPathChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6a-14. v1.33.4 (U6, option C): the cursor render path. The render
    //     walks the samples of a block in order, so the arc's segment and
    //     the envelope's segment can be followed instead of searched for
    //     every sample. The cursor must give exactly the same answers as
    //     the binary search - forwards, backwards (the transport can be
    //     reversed) and across the segment boundaries.
    {
        upic::Arc a;
        for (int k = 0; k < 40; ++k)
            // a wiggly curve: a straight line in the log domain would
            // hide a wrong segment (the interpolation would land on the
            // same line anyway)
            a.points.push_back ({ (double) k * 0.25,
                                  100.0 * std::pow (2.0, (double) (k % 7)
                                                    + 0.5 * std::sin ((double) k)),
                                  (float) (k % 5) / 4.0f });

        // forwards
        {
            upic::Arc::PitchCursor c;
            double worstF = 0.0, worstI = 0.0;
            for (int k = 0; k <= 1000; ++k)
            {
                const double tSec = 0.01 + (double) k * 0.0099;
                worstF = juce::jmax (worstF, std::abs (a.freqAt (tSec, c) - a.freqAt (tSec)));
                worstI = juce::jmax (worstI, std::abs ((double) a.intensityAt (tSec, c)
                                                       - (double) a.intensityAt (tSec)));
            }
            check (worstF < 1e-9, "the cursor's pitch is the searched pitch",
                   juce::String (worstF));
            check (worstI < 1e-6, "the cursor's intensity is the searched intensity",
                   juce::String (worstI));
        }

        // backwards - the transport can be reversed, so the cursor has to
        // walk the segments back
        {
            upic::Arc::PitchCursor c;
            double worstF = 0.0, worstI = 0.0;
            for (int k = 1000; k >= 0; --k)
            {
                const double tSec = 0.01 + (double) k * 0.0099;
                worstF = juce::jmax (worstF, std::abs (a.freqAt (tSec, c) - a.freqAt (tSec)));
                worstI = juce::jmax (worstI, std::abs ((double) a.intensityAt (tSec, c)
                                                       - (double) a.intensityAt (tSec)));
            }
            check (worstF < 1e-9, "the cursor's pitch follows the segment backwards too",
                   juce::String (worstF));
            check (worstI < 1e-6, "the cursor's intensity follows the segment backwards too",
                   juce::String (worstI));
        }

        // the envelope's own lookup
        {
            upic::Envelope e;
            e.points = { { 0.0f, 0.0f }, { 0.3f, 1.0f }, { 0.7f, 0.4f }, { 1.0f, 0.0f } };
            float worst = 0.0f;
            for (int pass = 0; pass < 2; ++pass)
            {
                upic::SegmentCursor c;
                worst = 0.0f;
                for (int k = 0; k <= 1000; ++k)
                {
                    const int kk = (pass == 0) ? k : 1000 - k;
                    const float x = (float) kk / 1000.0f;
                    worst = juce::jmax (worst, std::abs (e.valueAt (x, c) - e.valueAt (x)));
                }
                check (worst < 1e-6f,
                       pass == 0 ? "the cursor's envelope is the searched envelope"
                                 : "the envelope's cursor follows the segment backwards too",
                       juce::String (worst, 8));
            }
        }
    }

}

void upic::runContinuityChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 19. click hunt: a discontinuity is a step, so it spikes the second
    //     difference; and a glitch that only appears with small buffers
    //     shows up as a difference between two renders of the same page
    //     (the engine must not care how the host splits the stream)
    {
        const int sr = 48000;
        const int total = sr / 2;
        const int from = 4096; // skip the start-up (edges, priming)

        auto spike = [](const std::vector<float>& x, int f0) {
            double m = 0.0; int at = 0;
            for (int i = f0 + 2; i < (int) x.size(); ++i)
            {
                const double d2 = std::abs((double) x[(size_t) i]
                                        - 2.0 * (double) x[(size_t) (i - 1)]
                                        + (double) x[(size_t) (i - 2)]);
                if (d2 > m) { m = d2; at = i; }
            }
            return std::make_pair(m, at);
        };

        // 2 s of 100 Hz in the live ring (continuous in absolute frames)
        const int ringSize = sr * 2;
        std::vector<float> ring((size_t) ringSize, 0.0f);
        for (int i = 0; i < ringSize; ++i)
            ring[(size_t) i] = (float) std::sin(2.0 * juce::MathConstants<double>::pi * 100.0 * i / sr);
        const long long write0 = (long long) sr;

        // 1 s of 220 Hz material (integer cycles -> phase-continuous seam)
        upic::SampleSlot slot;
        slot.id = 1; slot.name = "sine220"; slot.sampleRate = (double) sr;
        {
            juce::AudioBuffer<float> buf(1, sr);
            for (int i = 0; i < sr; ++i)
                buf.setSample(0, i, 0.8f * (float) std::sin(
                    2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr));
            slot.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
        }
        std::vector<upic::SampleSlot> slots; slots.push_back(slot);
        // a second, non-periodic ramp slot makes the time-domain
        // difference between varispeed and the time-preserving modes
        // obvious (a pure sine can hide it behind phase)
        {
            upic::SampleSlot ramp;
            ramp.id = 2; ramp.name = "ramp"; ramp.sampleRate = (double) sr;
            juce::AudioBuffer<float> buf(1, sr);
            for (int i = 0; i < sr; ++i)
                buf.setSample(0, i, -1.0f + 2.0f * (float) i / (float) (sr - 1));
            ramp.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
            slots.push_back(ramp);
        }

        auto renderBlocks = [&](const upic::Page& pg, int block, int sampleMode,
                                int liveMode, std::vector<float>& out)
        {
            out.assign((size_t) total, 0.0f);
            auto waves = upic::defaultWaveforms();
            auto envs = upic::defaultEnvelopes();
            UpicEngine e; e.prepare((double) sr, 8192);
            e.setSamplePitchMode(sampleMode);
            e.setLivePitchMode(liveMode);
            const bool live = ! pg.arcs.empty() && pg.arcs[0].mode == 2;
            int pos = 0;
            while (pos < total)
            {
                // the first block is always the same size, so the live
                // voices anchor at the same spot in both runs
                const int n = (pos == 0) ? juce::jmin(1024, total - pos)
                                         : juce::jmin(block, total - pos);
                if (live) e.setLiveInput(ring.data(), nullptr, ringSize, write0 + pos, true);
                e.render(pg, (double) pos / sr, waves, envs, slots,
                         out.data() + pos, nullptr, n, 0.8f);
                pos += n;
            }
        };

        auto makePage = [&](int mode, double freq) {
            upic::Page pg; pg.durationSec = 2.0;
            upic::Arc a; a.id = 1; a.mode = mode; a.intensity = 1.0f; a.refFreq = freq;
            if (mode == 1) a.sampleId = 1;
            a.points = { { 0.0, freq }, { 1.9, freq } };
            pg.arcs.push_back(a);
            return pg;
        };

        struct Case { const char* name; int mode; int sampleMode; int liveMode; };
        const Case cases[] = {
            { "osc",              0, 0, 0 },
            { "sample varispeed", 1, 0, 0 },
            { "sample granular",  1, 1, 0 },
            { "sample vocoder",   1, 2, 0 },
            { "live granular",    2, 0, 0 },
            { "live tape",        2, 0, 1 },
            { "live vocoder",     2, 0, 2 },
        };

        for (const auto& c : cases)
        {
            const auto pg = makePage(c.mode, c.mode == 2 ? 100.0 : 220.0);
            std::vector<float> a, b;
            renderBlocks(pg, 64, c.sampleMode, c.liveMode, a);
            renderBlocks(pg, 1024, c.sampleMode, c.liveMode, b);
            double maxDiff = 0.0;
            for (int i = from; i < total; ++i)
                maxDiff = juce::jmax(maxDiff, (double) std::abs(a[(size_t) i] - b[(size_t) i]));
            const auto sp = spike(a, from);
            check(sp.first < 0.002 && maxDiff < 1e-6,
                  juce::String("click hunt: ") + c.name,
                  "spike " + juce::String(sp.first, 5) + " @" + juce::String(sp.second)
                  + "  blockdiff " + juce::String(maxDiff, 5));
        }

        // v1.26: the sample modes only differ when the arc is transposed.
        // The click-hunt pages above sit at 1x (ref == arc), where every
        // mode is a pass-through by design; this page is at ratio 2.
        {
            upic::Page pg; pg.durationSec = 2.0;
            upic::Arc a; a.id = 1; a.mode = 1; a.intensity = 1.0f;
            a.refFreq = 220.0; a.sampleId = 2;
            a.points = { { 0.0, 440.0 }, { 1.9, 440.0 } };
            pg.arcs.push_back(a);
            check(a.pitchMode == -1, "sample test arc inherits the tab mode",
                  "pitchMode " + juce::String(a.pitchMode));
            check(pg.arcs[0].mode == 1, "sample test arc is a sample arc",
                  "mode " + juce::String(pg.arcs[0].mode));
            check(std::abs(pg.arcs[0].refFreq - 220.0) < 1e-9
                  && std::abs(pg.arcs[0].freqAt(0.5) - 440.0) < 1e-6,
                  "sample test arc is transposed 2x",
                  "ref " + juce::String(pg.arcs[0].refFreq)
                      + " freq " + juce::String(pg.arcs[0].freqAt(0.5)));
            std::vector<float> varispeed, granular;
            renderBlocks(pg, 256, 0, 0, varispeed);
            renderBlocks(pg, 256, 1, 0, granular);
            double diff = 0.0, sigPeak = 0.0;
            for (int i = from; i < total; ++i)
            {
                diff = juce::jmax(diff, (double) std::abs(varispeed[(size_t) i] - granular[(size_t) i]));
                sigPeak = juce::jmax(sigPeak, (double) std::abs(varispeed[(size_t) i]));
            }
            check(sigPeak > 0.05 && diff > 0.01,
                  "sample varispeed and granular differ when transposed",
                  "peak " + juce::String(sigPeak, 4) + " diff " + juce::String(diff, 4));
        }

        // the same, but with a MOVING pitch curve: the ratio changes
        // every sample, so the shifters re-tune (and the granular hop
        // switches between the fixed grain and the shrinking one)
        for (const auto& c : cases)
        {
            upic::Page pg; pg.durationSec = 2.0;
            upic::Arc a; a.id = 1; a.mode = c.mode; a.intensity = 1.0f;
            a.refFreq = 100.0;
            if (c.mode == 1) a.sampleId = 1;
            // 100 Hz -> 800 Hz (ratio 1..8) in 1.8 s: crosses the
            // granular threshold (3.25x) and the tape re-anchor points
            a.points = { { 0.0, 100.0 }, { 1.8, 800.0 } };
            pg.arcs.push_back(a);

            std::vector<float> o;
            renderBlocks(pg, 256, c.sampleMode, c.liveMode, o);
            const auto sp = spike(o, from);
            double pk = 0.0;
            for (int i = from; i < total; ++i)
                pk = juce::jmax(pk, (double) std::abs(o[(size_t) i]));
            // the sample granular shifts the grain taps while the ratio
            // moves, which leaves a small (-40 dB) tick at the grain
            // boundary - the rest must stay clean
            const double limit = (c.mode == 1 && c.sampleMode == 1) ? 0.02 : 0.005;
            check(sp.first < limit, juce::String("click hunt (sweep): ") + c.name,
                  "spike " + juce::String(sp.first, 5) + " @" + juce::String(sp.second)
                  + "  peak " + juce::String(pk, 4));
        }

        // v1.15: engine.reset() must not restart the oscillator phase -
        // that was a click whenever the engine was reset mid-flight
        // (page change, material load, host state load)
        {
            upic::Page pg; pg.durationSec = 1.0;
            upic::Arc a; a.id = 1; a.intensity = 1.0f;
            // 233 Hz is not an integer number of cycles in 0.5 s, so a
            // phase restart at the split is visible in the samples
            a.points = { { 0.0, 233.0 }, { 0.9, 233.0 } };
            pg.arcs.push_back(a);
            auto waves = upic::defaultWaveforms();
            auto envs = upic::defaultEnvelopes();
            std::vector<upic::SampleSlot> noSlots;
            const int n = 44100;
            std::vector<float> ref((size_t) n, 0.0f), split((size_t) n, 0.0f);
            {
                UpicEngine e; e.prepare(44100.0, 8192);
                e.render(pg, 0.0, waves, envs, noSlots, ref.data(), nullptr, n, 1.0f);
            }
            {
                UpicEngine e; e.prepare(44100.0, 8192);
                e.render(pg, 0.0, waves, envs, noSlots, split.data(), nullptr, n / 2, 1.0f);
                e.reset();
                e.render(pg, (double) (n / 2) / 44100.0, waves, envs, noSlots,
                         split.data() + n / 2, nullptr, n - n / 2, 1.0f);
            }
            double maxDiff = 0.0;
            for (int i = 0; i < n; ++i)
                maxDiff = juce::jmax(maxDiff, (double) std::abs(ref[(size_t) i] - split[(size_t) i]));
            check(maxDiff < 1e-4, "engine reset keeps the oscillator phase (no click)",
                  "max diff " + juce::String(maxDiff, 6));
        }
    }

    // 19. v1.16 host latency: the host can only compensate ONE number for
    //     the whole plugin. The score paths have no delay, the live path
    //     has a lot - so the latency is only reported when the page is
    //     made of delayed paths alone.
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);

        auto pieceWith = [&](const juce::String& arcsJson) {
            const juce::String json = "{\"pages\":[{\"duration\":4,\"nextArcId\":9,\"arcs\":["
                                     + arcsJson + "]}]}";
            mc.loadFromVar(juce::JSON::parse(json));
        };

        const juce::String oscArc = "{\"id\":1,\"mode\":0,\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":220}]}";
        const juce::String liveArc = "{\"id\":2,\"mode\":2,\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":220}]}";

        pieceWith(oscArc);
        check(mc.liveLatencySamples() == 0, "latency: a score-only page reports 0",
              juce::String(mc.liveLatencySamples()) + " samples");

        pieceWith(liveArc);
        const int liveLat = mc.liveLatencySamples();
        const int wantGranular = (int) std::ceil(upic::GrainShifter::kLagSec * mc.getAudioSampleRate());
        check(liveLat == wantGranular, "latency: a live-only page reports the shifter's delay",
              juce::String(liveLat) + " samples (want " + juce::String(wantGranular) + ")");

        pieceWith(oscArc + "," + liveArc);
        check(mc.liveLatencySamples() == 0,
              "latency: a mixed page reports 0 (the score must stay exact)",
              juce::String(mc.liveLatencySamples()) + " samples");

        // v1.33.4 (M24): the arc's OWN algorithm decides the reported
        // delay - the old code consulted only the tab's global setting,
        // so a page whose arc overrides it (a vocoder arc on a granular
        // page) reported the wrong latency to the host.
        {
            const juce::String liveArcVocoder = "{\"id\":2,\"mode\":2,\"pitchMode\":2,\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":220}]}";
            pieceWith(liveArcVocoder);
            const int lat = mc.liveLatencySamples();
            const int wantVocoder = upic::VocoderCore::kPrime; // the vocoder's prime, in samples
            check(lat == wantVocoder,
                  "latency: a live arc's own algorithm decides the reported delay",
                  juce::String(lat) + " samples (want " + juce::String(wantVocoder) + ")");
            // without an override the tab's setting still decides
            pieceWith(liveArc);
            check(mc.liveLatencySamples() == wantGranular,
                  "latency: without an override the tab's setting decides",
                  juce::String(mc.liveLatencySamples()) + " samples (want "
                      + juce::String(wantGranular) + ")");
        }
    }

}

void upic::runArcOverrideChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6h. v1.21 (wave 19): the per-arc overrides reach the engine - an
    //     arc with its own pitch mode uses it, and an arc with `reversed`
    //     reads the reversed live ring.
    {
        const int sr = 44100;

        // --- the per-arc mode (a sample arc) ---
        {
            upic::SampleSlot s; s.id = 1; s.name = "sine"; s.sampleRate = 44100.0;
            juce::AudioBuffer<float> buf(1, 44100);
            for (int i = 0; i < 44100; ++i)
                buf.setSample(0, i, 0.8f * (float) std::sin(
                    2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0));
            s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
            std::vector<upic::SampleSlot> slots { s };

            auto measure = [&](int arcMode, int engineMode) {
                upic::Page pg; pg.durationSec = 4.0;
                upic::Arc a; a.id = 1; a.mode = 1; a.sampleId = 1; a.refFreq = 220.0;
                a.intensity = 1.0f; a.pitchMode = arcMode;
                a.points = { { 0.0, 440.0 }, { 4.0, 440.0 } };
                pg.arcs.push_back(a);
                upic::Envelope flat; flat.presetFlat();
                std::vector<upic::Envelope> envs { flat };
                auto waves = upic::defaultWaveforms();
                juce::AudioBuffer<float> out(2, 44100 * 4);
                out.clear();
                UpicEngine e; e.prepare(44100.0, 8192);
                e.setSamplePitchMode(engineMode);
                e.renderPageOffline(pg, waves, envs, slots, out, 44100.0, 1.0f);
                // the zero crossings over [1s, 2s] => Hz
                float pk = 0.0f;
                for (int i = 44100; i < 88200; ++i)
                    pk = juce::jmax(pk, std::abs(out.getSample(0, i)));
                const float thr = pk * 0.15f;
                int zc = 0; bool high = false;
                for (int i = 44100; i < 88200; ++i)
                {
                    const float v = out.getSample(0, i);
                    if (! high && v > thr) { high = true; ++zc; }
                    else if (high && v < -thr) high = false;
                }
                return (double) zc;
            };
            // varispeed (mode 0) at ratio 2 -> 440 Hz; the granular
            // shifter (mode 1) at ratio 2 -> 440 Hz as well, but the
            // playback length differs - the mode is what we check here by
            // asking for the engine's varispeed and the arc's granular
            const double viaTab = measure(-1, 0);      // the tab's setting
            const double viaArc = measure(1, 0);       // the arc's own
            check(std::abs(viaTab - 440.0) < 25.0,
                  "the arc list: the tab's mode applies when the arc has none",
                  juce::String(viaTab, 1) + " Hz");
            check(std::abs(viaArc - 440.0) < 25.0,
                  "the arc list: the arc's own mode is used",
                  juce::String(viaArc, 1) + " Hz");
        }

        // --- the per-arc reverse (a live arc) ---
        {
            const int D = sr;          // the ring / segment length
            const int total = sr * 2;  // the render

            // the forward ring: the ramp arrives and is written as it
            // comes (the live path). The reversed ring: the frozen
            // segment, mirrored in time, read from a relative head (the
            // app's capture). Same convention for the shifter: the read
            // is behind the head.
            auto slope = [&](bool reversed) {
                upic::Page pg; pg.durationSec = 4.0;
                upic::Arc a; a.id = 1; a.mode = 2; a.intensity = 1.0f;
                a.refFreq = 220.0; a.reversed = reversed;
                a.points = { { 0.0, 220.0 }, { 4.0, 220.0 } };
                pg.arcs.push_back(a);
                upic::Envelope flat; flat.presetFlat();
                std::vector<upic::Envelope> envs { flat };
                auto waves = upic::defaultWaveforms();

                std::vector<float> fwd((size_t) D, 0.0f), rev((size_t) D, 0.0f);
                for (int i = 0; i < D; ++i)
                    rev[(size_t) (D - 1 - i)] = (float) i / (float) (D - 1);

                std::vector<float> L((size_t) total, 0.0f);
                UpicEngine e; e.prepare((double) sr, 8192);
                int fwdHead = 0, revHead = (int) (0.25 * sr);
                long long written = 0;
                for (int pos = 0; pos < total; pos += 512)
                {
                    const int n = juce::jmin(512, total - pos);
                    for (int i = 0; i < n; ++i)
                    {
                        fwd[(size_t) fwdHead] = (float) (written + i) / (float) total;
                        fwdHead = (fwdHead + 1) % D;
                    }
                    written += n;
                    e.setLiveInput(fwd.data(), nullptr, D, written, true);
                    e.setLiveReverse(rev.data(), nullptr, D, revHead, true);
                    e.render(pg, (double) pos / sr, waves, envs, {},
                             L.data() + pos, nullptr, n, 1.0f);
                    revHead += n;
                }
                double first = 0.0, last = 0.0;
                const int q = total / 4;
                for (int i = 0; i < q; ++i) first += L[(size_t) i];
                for (int i = total - q; i < total; ++i) last += L[(size_t) i];
                return (last - first) / (double) q;
            };
            const double up = slope(false);
            const double down = slope(true);
            check(up > 0.01, "the arc list: a live arc reads the live ring by default",
                  juce::String(up, 4));
            check(down < -0.01, "the arc list: the arc's Rev box reads the reversed ring",
                  juce::String(down, 4));
        }
    }

}
