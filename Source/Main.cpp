#include <JuceHeader.h>
#include "MainComponent.h"
#include "AndroidBars.h"

class UpicApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "UPICuitous"; }
    const juce::String getApplicationVersion() override { return "0.28.1"; } // keep in sync with Plugin/CMakeLists.txt
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        if (commandLine.contains("--selftest"))
        {
            runSelfTest();
            quit();
            return;
        }
        mainWindow.reset(new MainWindow(getApplicationName()));
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

    // Headless verification: no window, no audio device, no screen capture.
    // Checks the DSP engine, envelope/pitch math, JSON round-trip (including
    // files written by the Python version), and snapshots the UI offscreen.
    static void runSelfTest()
    {
        juce::String log;
        int failures = 0;
        auto check = [&](bool ok, const juce::String& name, const juce::String& detail = {})
        {
            log << (ok ? "PASS  " : "FAIL  ") << name;
            if (detail.isNotEmpty()) log << "  [" << detail << "]";
            log << "\n";
            if (!ok) ++failures;
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
                const double t = (double) i / 2000.0 * 9.98;
                maxDiff = juce::jmax(maxDiff, std::abs(a.freqAt(t) - linearFreqAt(t)));
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
                const float t = (float) i / 2000.0f * 0.998f;
                maxDiffE = juce::jmax(maxDiffE, std::abs(e.valueAt(t) - linearValueAt(t)));
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

        // 6a. wave 15: the lock-free snapshot handover. The audio thread must
        //     take/publish without allocating or freeing; the message thread
        //     frees only what the reader retired.
        {
            struct TestSnap : SnapshotBase
            {
                int v = 0;
                static std::atomic<int>& alive()
                { static std::atomic<int> a { 0 }; return a; }
                TestSnap() { alive().fetch_add(1); }
                ~TestSnap() { alive().fetch_sub(1); }
            };
            {
                SnapshotHandover<TestSnap> ho;
                TestSnap* live = nullptr;
                auto* s1 = new TestSnap(); s1->v = 1;
                ho.publish(s1);
                check(ho.take(live) == s1 && live->v == 1,
                      "snapshot handover: take() sees the published model");
                auto* s2 = new TestSnap(); s2->v = 2;
                ho.publish(s2); // taking s2 retires s1, it is NOT freed on the reader
                check(ho.take(live) == s2 && live->v == 2,
                      "snapshot handover: take() switches to the newest model");
                check(TestSnap::alive() == 2,
                      "snapshot handover: the reader frees nothing",
                      juce::String(TestSnap::alive()) + " alive (expect 2)");
                ho.drain(); // the writer frees the retired s1
                check(TestSnap::alive() == 1,
                      "snapshot handover: the writer frees retired models",
                      juce::String(TestSnap::alive()) + " alive (expect 1)");
                auto* s3 = new TestSnap();
                ho.publish(s3); // pending; not taken yet
                auto* s4 = new TestSnap();
                ho.publish(s4); // s3 was never taken -> freed by publish()
                ho.retireReader(live); // s2 -> retired
                ho.clear();            // frees the retired s2 and the pending s4
                check(TestSnap::alive() == 0,
                      "snapshot handover: clear() releases everything",
                      juce::String(TestSnap::alive()) + " alive (expect 0)");
            }
        }

        // 6a-2. wave 15: the snapshot must carry the model unchanged - a
        //     render from the snapshot has to be bit-identical to a render
        //     from the live model.
        {
            auto waves = upic::defaultWaveforms();
            auto envs = upic::defaultEnvelopes();
            upic::Page pg;
            pg.durationSec = 2.0;
            for (int i = 0; i < 3; ++i)
            {
                upic::Arc a; a.id = i + 1; a.waveformId = 1; a.envelopeId = 1;
                a.intensity = 0.8f;
                a.points = { { 0.0, 220.0 * (i + 1) }, { 1.5, 440.0 * (i + 1) } };
                pg.arcs.push_back(a);
            }
            std::vector<upic::SampleSlot> slots;
            {
                upic::SampleSlot s; s.id = 1; s.name = "sine"; s.sampleRate = 44100.0;
                juce::AudioBuffer<float> buf(1, 44100);
                for (int i = 0; i < 44100; ++i)
                    buf.setSample(0, i, 0.5f * (float) std::sin(
                        2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0));
                s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
                slots.push_back(s);
            }

            // the snapshot the message thread would publish
            ModelSnapshot snap;
            snap.waveforms = waves;
            snap.envelopes = envs;
            snap.pages.push_back(pg);
            snap.slots = slots;
            snap.currentPage = 0;
            snap.pageEpoch = 0;

            const int n = 8192;
            std::vector<float> liveL((size_t) n), liveR((size_t) n);
            std::vector<float> snapL((size_t) n), snapR((size_t) n);
            {
                UpicEngine e; e.prepare(44100.0, 8192);
                e.render(pg, 0.0, waves, envs, slots, liveL.data(), liveR.data(), n, 0.8f);
            }
            {
                UpicEngine e; e.prepare(44100.0, 8192);
                e.render(snap.pages[0], 0.0, snap.waveforms, snap.envelopes, snap.slots,
                         snapL.data(), snapR.data(), n, 0.8f);
            }
            bool identical = true;
            float maxDiff = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float d1 = std::abs(liveL[(size_t) i] - snapL[(size_t) i]);
                const float d2 = std::abs(liveR[(size_t) i] - snapR[(size_t) i]);
                maxDiff = juce::jmax(maxDiff, juce::jmax(d1, d2));
                if (d1 != 0.0f || d2 != 0.0f) identical = false;
            }
            check(identical, "snapshot render is bit-identical to the live model render",
                  "max diff " + juce::String(maxDiff, 8));
            check(snap.slots.size() == 1 && snap.slots[0].data != nullptr
                  && snap.slots[0].data->getNumSamples() == 44100,
                  "snapshot carries the shared sample buffer");

            // the slot's own JSON round-trip must survive the shared buffer
            {
                const auto var = slots[0].toVar();
                const auto back = upic::SampleSlot::fromVar(var, 1);
                bool same = back.loaded() && back.data != nullptr
                    && back.data->getNumSamples() == 44100
                    && back.data->getNumChannels() == 1
                    && std::abs(back.sampleRate - 44100.0) < 1e-9;
                float slotDiff = 0.0f;
                if (same)
                    for (int i = 0; i < 44100; ++i)
                        slotDiff = juce::jmax(slotDiff, std::abs(
                            back.data->getSample(0, i) - slots[0].data->getSample(0, i)));
                check(same && slotDiff < 1e-4f,
                      "sample slot json round-trip keeps the audio",
                      "max diff " + juce::String(slotDiff, 6));
            }
        }

        // 6b. live take -> sample slot (v1.4 capture path, headless)
        {
            juce::AudioBuffer<float> take(2, 48000);
            for (int i = 0; i < 48000; ++i)
            {
                const float v = (float) std::sin(
                    2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0);
                take.setSample(0, i, v);
                take.setSample(1, i, v);
            }
            auto slot = upic::slotFromCapture(3, "Take 120000", take, 48000.0);
            check(slot.loaded(), "take slot loaded");
            check(slot.id == 3 && slot.name == "Take 120000"
                  && slot.data != nullptr && slot.data->getNumChannels() == 2
                  && std::abs(slot.sampleRate - 48000.0) < 1e-9,
                  "take slot fields (id/name/stereo/rate)");
            const double dur = slot.durationSec();
            check(dur > 0.9 && dur <= 1.0, "take duration ~1s minus loop crossfade",
                  juce::String(dur, 4));

            juce::AudioBuffer<float> empty;
            auto s2 = upic::slotFromCapture(1, "Take 0", empty, 44100.0);
            check(! s2.loaded(), "empty take -> unloaded slot");
        }

        // 6c. live pitch-follow shifter (v1.5: mode-2 arcs transpose input)
        {
            const double sr = 48000.0;
            const int ringSize = (int) sr * 2;
            std::vector<float> ring((size_t) ringSize, 0.0f);
            long long write = 0;
            double inPhase = 0.0;
            auto feed = [&](int n){
                for (int i = 0; i < n; ++i)
                {
                    ring[(size_t) (write % (long long) ringSize)] = (float) std::sin(inPhase);
                    inPhase += 2.0 * juce::MathConstants<double>::pi * 100.0 / sr;
                    ++write;
                }
            };
            auto measureHz = [&](double ratio){
                upic::GrainShifter gs;
                gs.prepare(sr);
                gs.reset(write); // anchors kLagSec behind the write head
                const int skip = (int) (sr * 0.10), span = (int) (sr * 0.40);
                int zc = 0; bool armed = false;
                for (int i = 0; i < skip + span; ++i)
                {
                    feed(1);
                    const float v = gs.next(ring.data(), nullptr, ringSize, write, ratio);
                    if (i >= skip)
                    {
                        if (! armed && v < -0.05f) armed = true;
                        else if (armed && v > 0.05f) { ++zc; armed = false; }
                    }
                }
                return (double) zc * sr / (double) span;
            };
            feed((int) sr); // prime 1 s of 100 Hz material
            const double hz1 = measureHz(1.0);
            const double hz2 = measureHz(2.0);
            const double hzHalf = measureHz(0.5);
            const double hz4 = measureHz(4.0);
            const double hzClamp = measureHz(100.0); // clamps to 16.0 (v1.13)
            check(std::abs(hz1 - 100.0) < 10.0, "grain shifter r=1 -> 100 Hz", juce::String(hz1, 1));
            check(std::abs(hz2 - 200.0) < 20.0, "grain shifter r=2 -> 200 Hz", juce::String(hz2, 1));
            check(std::abs(hzHalf - 50.0) < 10.0, "grain shifter r=0.5 -> 50 Hz", juce::String(hzHalf, 1));
            // v1.13: the live range is now the sample range (1/16x..16x)
            check(std::abs(hz4 - 400.0) < 40.0, "grain shifter r=4 -> 400 Hz", juce::String(hz4, 1));
            check(std::abs(hzClamp - 1600.0) < 240.0, "grain shifter clamps at 16x", juce::String(hzClamp, 1));

            // v1.9 alternatives: tape (varispeed) and phase vocoder, same
            // harness - 100 Hz sine ring, skip the start-up, count crossings.
            {
                double peakAlt = 0.0;
                auto measureAlt = [&](auto& sh, double ratio) {
                    sh.prepare(sr);
                    sh.reset(write);
                    const int skip = (int) (sr * 0.15), span = (int) (sr * 0.60);
                    int zc2 = 0; bool armed2 = false;
                    for (int i = 0; i < skip + span; ++i)
                    {
                        feed(1);
                        const float v = sh.next(ring.data(), nullptr, ringSize, write, ratio);
                        if (i >= skip)
                        {
                            peakAlt = juce::jmax(peakAlt, (double) std::abs(v));
                            if (! armed2 && v < -0.05f) armed2 = true;
                            else if (armed2 && v > 0.05f) { ++zc2; armed2 = false; }
                        }
                    }
                    return (double) zc2 * sr / (double) span;
                };

                upic::TapeShifter tp;
                const double tp1  = measureAlt(tp, 1.0);
                const double tp2  = measureAlt(tp, 2.0);
                const double tp05 = measureAlt(tp, 0.5);
                check(std::abs(tp1 - 100.0) < 10.0, "tape shifter r=1 -> 100 Hz", juce::String(tp1, 1));
                check(std::abs(tp2 - 200.0) < 20.0, "tape shifter r=2 -> 200 Hz", juce::String(tp2, 1));
                check(std::abs(tp05 - 50.0) < 8.0, "tape shifter r=0.5 -> 50 Hz", juce::String(tp05, 1));
                check(peakAlt > 0.15, "tape shifter outputs signal", juce::String(peakAlt, 3));

                upic::VocoderShifter vc;
                const double vc1  = measureAlt(vc, 1.0);
                const double vc2  = measureAlt(vc, 2.0);
                const double vc05 = measureAlt(vc, 0.5);
                check(std::abs(vc1 - 100.0) < 10.0, "vocoder shifter r=1 -> 100 Hz", juce::String(vc1, 1));
                check(std::abs(vc2 - 200.0) < 20.0, "vocoder shifter r=2 -> 200 Hz", juce::String(vc2, 1));
                check(std::abs(vc05 - 50.0) < 8.0, "vocoder shifter r=0.5 -> 50 Hz", juce::String(vc05, 1));
                check(peakAlt > 0.12, "vocoder shifter outputs signal", juce::String(peakAlt, 3));
            }
        }

        // 6d. engine live voice: mode-2 arc follows the ring; silent when off
        {
            upic::Page pg; pg.durationSec = 1.0;
            upic::Arc a; a.id = 1; a.mode = 2; a.intensity = 1.0f; a.refFreq = 440.0;
            a.points = { {0.0, 440.0, 1.0f}, {0.9, 440.0, 1.0f} };
            pg.arcs.push_back(a);

            const double sr = 48000.0;
            const int ringSize = (int) sr * 2;
            std::vector<float> ring((size_t) ringSize, 0.0f);
            for (int i = 0; i < ringSize; ++i) // 2 s of 100 Hz: wraps seamlessly
                ring[(size_t) i] = (float) std::sin(2.0 * juce::MathConstants<double>::pi * 100.0 * i / sr);

            auto renderLive = [&](bool active, float* L, float* R, int total){
                auto waves = upic::defaultWaveforms();
                auto envs = upic::defaultEnvelopes();
                std::vector<upic::SampleSlot> noSlots;
                UpicEngine e; e.prepare(sr, 256);
                const long long write0 = (long long) sr; // head 1 s in
                for (int pos = 0; pos < total; pos += 256)
                {
                    const int n = juce::jmin(256, total - pos);
                    e.setLiveInput(ring.data(), nullptr, ringSize, write0 + pos, active);
                    e.render(pg, (double) pos / sr, waves, envs, noSlots,
                             L + pos, R + pos, n, 0.8f);
                }
            };

            const int total = (int) (sr * 0.3), skip = (int) (sr * 0.05);
            std::vector<float> L((size_t) total, 0.0f), R((size_t) total, 0.0f);
            renderLive(true, L.data(), R.data(), total);
            double peakOn = 0.0;
            for (int i = skip; i < total; ++i) peakOn = juce::jmax(peakOn, (double) std::abs(L[(size_t) i]));
            int zc = 0; bool armed = false;
            for (int i = skip; i < total; ++i)
            {
                if (! armed && L[(size_t) i] < -0.02f) armed = true;
                else if (armed && L[(size_t) i] > 0.02f) { ++zc; armed = false; }
            }
            const double hz = (double) zc * sr / (double) (total - skip);
            check(peakOn > 0.05, "mode-2 live arc outputs the ring input", juce::String(peakOn, 4));
            check(std::abs(hz - 100.0) < 10.0, "mode-2 live arc follows arc pitch (100 Hz)", juce::String(hz, 1));

            // v1.13: the live path takes the sample path's range (1/16x..16x)
            check(upic::kLiveMinRatio == 1.0 / 16.0 && upic::kLiveMaxRatio == 16.0,
                  "live ratio range matches the sample path (1/16x ... 16x)");
            {
                // a live arc must reach its pitch at any ratio: the ring tone
                // is shifted by arcPitch / refPitch. The ring frequency is
                // picked so the grain crossfade lands on a whole number of
                // cycles (no comb cancellation in the measurement).
                auto measureShift = [&](double ringHz, double arcHz, double refHz){
                    std::vector<float> ring2((size_t) ringSize, 0.0f);
                    for (int i = 0; i < ringSize; ++i)
                        ring2[(size_t) i] = (float) std::sin(2.0 * juce::MathConstants<double>::pi * ringHz * i / sr);

                    upic::Page p2; p2.durationSec = 1.0;
                    upic::Arc a2; a2.id = 1; a2.mode = 2; a2.intensity = 1.0f; a2.refFreq = refHz;
                    a2.points = { {0.0, arcHz, 1.0f}, {0.9, arcHz, 1.0f} };
                    p2.arcs.push_back(a2);

                    std::vector<float> l2((size_t) total, 0.0f), r2((size_t) total, 0.0f);
                    auto waves = upic::defaultWaveforms();
                    auto envs = upic::defaultEnvelopes();
                    std::vector<upic::SampleSlot> noSlots;
                    UpicEngine e; e.prepare(sr, 256);
                    const long long write0 = (long long) sr;
                    for (int pos = 0; pos < total; pos += 256)
                    {
                        const int n = juce::jmin(256, total - pos);
                        e.setLiveInput(ring2.data(), nullptr, ringSize, write0 + pos, true);
                        e.render(p2, (double) pos / sr, waves, envs, noSlots,
                                 l2.data() + pos, r2.data() + pos, n, 0.8f);
                    }
                    int z = 0; bool arm = false;
                    double pk = 0.0;
                    for (int i = skip; i < total; ++i)
                    {
                        pk = juce::jmax(pk, (double) std::abs(l2[(size_t) i]));
                        if (! arm && l2[(size_t) i] < -0.02f) arm = true;
                        else if (arm && l2[(size_t) i] > 0.02f) { ++z; arm = false; }
                    }
                    return std::pair<double, double> { (double) z * sr / (double) (total - skip), pk };
                };

                const auto up4 = measureShift(100.0, 400.0, 100.0);
                check(up4.second > 0.05 && std::abs(up4.first - 400.0) < 40.0,
                      "live arc shifts 4x up (400 Hz)", juce::String(up4.first, 1));
                const auto up8 = measureShift(200.0, 1600.0, 200.0);
                check(up8.second > 0.05 && std::abs(up8.first - 1600.0) < 160.0,
                      "live arc shifts 8x up (1600 Hz)", juce::String(up8.first, 1));
                const auto dn4 = measureShift(100.0, 25.0, 100.0);
                check(dn4.second > 0.05 && std::abs(dn4.first - 25.0) < 5.0,
                      "live arc shifts 4x down (25 Hz)", juce::String(dn4.first, 1));
            }

            // v1.9: the Audio overlay's live pitch mode drives the live voices
            {
                auto renderMode = [&](int mode, std::vector<float>& out){
                    auto waves = upic::defaultWaveforms();
                    auto envs = upic::defaultEnvelopes();
                    std::vector<upic::SampleSlot> noSlots;
                    UpicEngine e; e.prepare(sr, 256);
                    e.setLivePitchMode(mode);
                    const long long write0 = (long long) sr;
                    for (int pos = 0; pos < total; pos += 256)
                    {
                        const int n = juce::jmin(256, total - pos);
                        e.setLiveInput(ring.data(), nullptr, ringSize, write0 + pos, true);
                        e.render(pg, (double) pos / sr, waves, envs, noSlots,
                                 out.data() + pos, nullptr, n, 0.8f);
                    }
                };
                auto modePeak = [&](int mode){
                    std::vector<float> o((size_t) total, 0.0f);
                    renderMode(mode, o);
                    double pk = 0.0;
                    for (int i = skip; i < total; ++i)
                        pk = juce::jmax(pk, (double) std::abs(o[(size_t) i]));
                    return pk;
                };
                const double pkTape = modePeak(1);
                check(pkTape > 0.02, "live pitch mode 1 (tape) drives mode-2 arcs", juce::String(pkTape, 4));
                const double pkVox = modePeak(2);
                check(pkVox > 0.02, "live pitch mode 2 (vocoder) drives mode-2 arcs", juce::String(pkVox, 4));
            }

            // v1.14: the live vocoder must not buzz. The live ring is filled
            // per block, so a shifter that reads AT the write head sees the
            // same frame for the whole block (a staircase); the vocoder
            // re-synthesises that as a buzz at the block rate. The block rate
            // here is 48000 / 256 = 187.5 Hz - it must stay well below the
            // fundamental.
            {
                upic::Page p3; p3.durationSec = 1.0;
                upic::Arc a3; a3.id = 1; a3.mode = 2; a3.intensity = 1.0f; a3.refFreq = 100.0;
                a3.points = { {0.0, 100.0, 1.0f}, {0.9, 100.0, 1.0f} };
                p3.arcs.push_back(a3);

                std::vector<float> l3((size_t) total, 0.0f), r3((size_t) total, 0.0f);
                auto waves = upic::defaultWaveforms();
                auto envs = upic::defaultEnvelopes();
                std::vector<upic::SampleSlot> noSlots;
                UpicEngine e; e.prepare(sr, 256);
                e.setLivePitchMode(2); // vocoder
                const long long write0 = (long long) sr;
                for (int pos = 0; pos < total; pos += 256)
                {
                    const int n = juce::jmin(256, total - pos);
                    e.setLiveInput(ring.data(), nullptr, ringSize, write0 + pos, true);
                    e.render(p3, (double) pos / sr, waves, envs, noSlots,
                             l3.data() + pos, r3.data() + pos, n, 0.8f);
                }
                auto goertzel = [&](double f){
                    const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
                    const double cw = std::cos(w);
                    double s1 = 0.0, s2 = 0.0;
                    for (int i = skip; i < total; ++i)
                    {
                        const double s0 = (double) l3[(size_t) i] + 2.0 * cw * s1 - s2;
                        s2 = s1; s1 = s0;
                    }
                    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - 2.0 * cw * s1 * s2))
                           / (double) (total - skip);
                };
                const double fund = goertzel(100.0);
                // the staircase's signature: sidebands at 100 Hz +/- the
                // block rate (187.5 Hz) and its harmonics
                const double sb1 = goertzel(87.5);
                const double sb2 = goertzel(287.5);
                const double sb3 = goertzel(475.0);
                const double buzz = (sb1 + sb2 + sb3) / juce::jmax(1e-6, fund);
                check(buzz < 0.25, "live vocoder has no block-rate buzz",
                      juce::String(buzz, 3) + " (sidebands/fundamental: "
                      + juce::String(sb1, 4) + " " + juce::String(sb2, 4) + " "
                      + juce::String(sb3, 4) + " / " + juce::String(fund, 4) + ")");
            }

            std::vector<float> L2((size_t) total, 0.0f), R2((size_t) total, 0.0f);
            renderLive(false, L2.data(), R2.data(), total);
            double peakOff = 0.0;
            for (int i = 0; i < total; ++i) peakOff = juce::jmax(peakOff, (double) std::abs(L2[(size_t) i]));
            check(peakOff < 1e-9, "mode-2 arc silent when input inactive", juce::String(peakOff, 8));

            // offline render must not read the ring even with a view published
            juce::AudioBuffer<float> ob(2, total); ob.clear();
            std::vector<upic::SampleSlot> noSlots2;
            UpicEngine e2; e2.prepare(sr, 8192);
            e2.setLiveInput(ring.data(), nullptr, ringSize, (long long) sr, true);
            e2.renderPageOffline(pg, upic::defaultWaveforms(), upic::defaultEnvelopes(),
                                 noSlots2, ob, sr, 0.8f);
            double opeak = 0.0;
            for (int i = 0; i < total; ++i) opeak = juce::jmax(opeak, (double) std::abs(ob.getSample(0, i)));
            check(opeak < 1e-9, "offline render leaves live arcs silent", juce::String(opeak, 8));
        }

        // 6e. v1.19 (wave 17): the sample reverse read. A reversed slot plays
        //     the material backwards - the engine mirrors the read, so the
        //     pitch and every pitch mode stay as they are.
        {
            const int N = 44100;
            auto waves = upic::defaultWaveforms();
            upic::Envelope flat; flat.presetFlat();
            std::vector<upic::Envelope> envs { flat };

            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 1; a.sampleId = 1; a.refFreq = 220.0;
            a.intensity = 1.0f;
            a.points = { { 0.0, 220.0 }, { 4.0, 220.0 } };
            pg.arcs.push_back(a);

            // a ramp: forward it rises, reversed it falls
            auto rampSlot = [N](bool reversed) {
                upic::SampleSlot s;
                s.id = 1; s.name = "ramp"; s.sampleRate = 44100.0;
                s.reversed = reversed;
                juce::AudioBuffer<float> buf(1, N);
                for (int i = 0; i < N; ++i)
                    buf.setSample(0, i, (float) i / (float) (N - 1));
                s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
                return s;
            };
            // a 220 Hz sine (integer cycles => a seamless loop)
            auto sineSlot = [N](bool reversed) {
                upic::SampleSlot s;
                s.id = 1; s.name = "sine220"; s.sampleRate = 44100.0;
                s.reversed = reversed;
                juce::AudioBuffer<float> buf(1, N);
                for (int i = 0; i < N; ++i)
                    buf.setSample(0, i, 0.8f * (float) std::sin(
                        2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0));
                s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
                return s;
            };

            // one material traversal (1 s), varispeed at ratio 1
            auto renderRamp = [&](bool reversed, std::vector<float>& out) {
                std::vector<upic::SampleSlot> slots { rampSlot(reversed) };
                out.assign((size_t) N, 0.0f);
                UpicEngine e; e.prepare(44100.0, 8192);
                e.render(pg, 0.0, waves, envs, slots, out.data(), nullptr, N, 1.0f);
            };
            std::vector<float> fwd, rev;
            renderRamp(false, fwd);
            renderRamp(true, rev);

            // the middle only: the arc's edge fade covers the first 12 ms
            double mirrorDiff = 0.0;
            for (int i = 2000; i < N - 2000; ++i)
                mirrorDiff = juce::jmax(mirrorDiff, (double) std::abs(
                    rev[(size_t) i] - fwd[(size_t) (N - 1 - i)]));
            check(mirrorDiff < 1e-3,
                  "sample reverse: the reversed render is the forward render mirrored",
                  "max diff " + juce::String(mirrorDiff, 6));

            // the pitch does not change with the direction - the read speed is
            // the same, only the time axis is mirrored. Measured at three
            // ratios, because at ratio 1 a reversed tone is just a phase
            // flip and would not prove anything.
            auto measureHz = [&](bool reversed, int pitchMode, double arcFreq) {
                upic::Page p2; p2.durationSec = 4.0;
                upic::Arc a2; a2.id = 1; a2.mode = 1; a2.sampleId = 1;
                a2.refFreq = 220.0; a2.intensity = 1.0f;
                a2.points = { { 0.0, arcFreq }, { 4.0, arcFreq } };
                p2.arcs.push_back(a2);
                std::vector<upic::SampleSlot> slots { sineSlot(reversed) };
                juce::AudioBuffer<float> buf(2, 44100 * 4);
                buf.clear();
                UpicEngine e; e.prepare(44100.0, 8192);
                e.setSamplePitchMode(pitchMode);
                e.render(p2, 0.0, waves, envs, slots, buf.getWritePointer(0), nullptr,
                         44100 * 4, 1.0f);
                // Schmitt-trigger zero crossings over [1s, 2s] => Hz
                float pk = 0.0f;
                for (int i = 44100; i < 88200; ++i)
                    pk = juce::jmax(pk, std::abs(buf.getSample(0, i)));
                const float thr = pk * 0.15f;
                int crossings = 0; bool high = false;
                for (int i = 44100; i < 88200; ++i)
                {
                    const float v = buf.getSample(0, i);
                    if (! high && v >  thr) { high = true; ++crossings; }
                    else if (high && v < -thr) high = false;
                }
                return std::make_pair((double) crossings, (double) pk);
            };
            struct RatioCase { double arcFreq; double want; };
            const RatioCase ratioCases[] = { { 220.0, 220.0 }, { 440.0, 440.0 }, { 110.0, 110.0 } };
            for (const auto& rc : ratioCases)
            {
                const auto f = measureHz(false, 0, rc.arcFreq);
                const auto r = measureHz(true, 0, rc.arcFreq);
                const juce::String what = juce::String(rc.arcFreq, 0) + " Hz";
                check(std::abs(f.first - rc.want) < 10.0,
                      "sample reverse: forward at " + what, juce::String(f.first, 1) + " Hz");
                check(std::abs(r.first - rc.want) < 10.0,
                      "sample reverse: reversed at " + what + " keeps the pitch",
                      juce::String(r.first, 1) + " Hz");
            }

            // every pitch mode works on the mirrored material
            for (int mode = 0; mode <= 2; ++mode)
            {
                const auto m = measureHz(true, mode, 220.0);
                check(m.second > 0.05,
                      "sample reverse: the pitch modes still sound (mode "
                      + juce::String(mode) + ")",
                      "peak " + juce::String(m.second, 3));
            }

            // the flag is session state: the piece format does not carry it
            check(rampSlot(true).toVar().getProperty("reversed", "absent").toString() == "absent",
                  "sample reverse: the flag is session-only (the piece format is untouched)");
        }

        // 6f. v1.20 (wave 18): the live reverse. The capture is written with
        //     a DECREASING head, so the buffer holds the last N seconds
        //     mirrored in time - reading it forward plays the input
        //     backwards, and the shifter needs no change at all.
        {
            auto mcHolder = std::make_unique<MainComponent>(true);
            auto& mc = *mcHolder;
            mc.prepareToPlay(512, 44100.0);
            mc.setPluginInputChannelCount(2);

            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };
            auto* rb = dynamic_cast<juce::TextButton*>(findByName(mc, "liveRevBtn"));
            check(rb != nullptr, "the live reverse button is present");
            if (rb != nullptr)
            {
                rb->setToggleState(true, juce::dontSendNotification);
                rb->onClick();
                check(mc.reverseIsOn(), "the live reverse turns on");
            }

            const int D = mc.reverseBufferFrames();
            check(D > 0, "the reverse buffer has the chosen length",
                  juce::String(D) + " frames");

            // feed a rising ramp through the input bus until the segment is
            // full (the tap reads the buffer's input channels)
            juce::AudioBuffer<float> buf(2, 512);
            juce::AudioSourceChannelInfo info(&buf, 0, 512);
            int fed = 0;
            while (fed < D + 512)
            {
                buf.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const float v = (float) juce::jmin(1.0, (double) (fed + i) / (double) D);
                    buf.setSample(0, i, v);
                    buf.setSample(1, i, v);
                }
                mc.getNextAudioBlock(info);
                fed += 512;
            }

            // the segment is mirrored: the value falls as the index rises
            // (the forward capture would rise)
            const auto& pb = mc.reversePlaybackBuffer();
            if (pb.getNumSamples() >= D && D > 8)
            {
                const float a = pb.getSample(0, D / 4);
                const float b = pb.getSample(0, (D * 3) / 4);
                check(a > b + 0.2f,
                      "the live reverse buffer holds the input mirrored",
                      "quarter " + juce::String(a, 3) + " vs three quarters "
                      + juce::String(b, 3));
                const float mid = pb.getSample(0, D / 2);
                check(mid > 0.2f && mid < 0.9f,
                      "the live reverse buffer keeps the input level",
                      juce::String(mid, 3));
            }
            else
            {
                check(false, "the live reverse buffer holds the segment",
                      juce::String(pb.getNumSamples()) + " vs " + juce::String(D));
            }
        }

        // 6g. v1.20 (wave 18): the property the live reverse is built on -
        //     a segment captured with a decreasing head holds the input
        //     mirrored, so the shifter (which reads the ring forward, and is
        //     not changed at all) plays the input BACKWARDS.
        {
            const int sr = 44100;
            const int D = sr * 2;      // the 2 s segment
            const int total = (int) (sr * 1.5); // the playback (no wrap)
            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 2; a.intensity = 1.0f; a.refFreq = 220.0;
            a.points = { { 0.0, 220.0 }, { 4.0, 220.0 } };
            pg.arcs.push_back(a);
            upic::Envelope flat; flat.presetFlat();
            std::vector<upic::Envelope> envs { flat };
            auto waves = upic::defaultWaveforms();

            // capture the rising ramp into the segment, then play the frozen
            // segment back (the live reverse's structure)
            auto slope = [&](bool mirrored) {
                std::vector<float> ring((size_t) D, 0.0f);
                int head = D - 1;
                for (int i = 0; i < D; ++i)
                {
                    const float v = (float) i / (float) (D - 1);
                    if (mirrored) head = (head - 1 + D) % D;
                    ring[(size_t) head] = v;
                    if (! mirrored) head = (head + 1) % D;
                }
                std::vector<float> L((size_t) total, 0.0f);
                UpicEngine e; e.prepare((double) sr, 8192);
                int playHead = (int) (0.25 * sr); // after the shifter's lag
                for (int pos = 0; pos < total; pos += 512)
                {
                    const int n = juce::jmin(512, total - pos);
                    e.setLiveInput(ring.data(), nullptr, D, playHead, true);
                    e.render(pg, (double) pos / sr, waves, envs, {},
                             L.data() + pos, nullptr, n, 1.0f);
                    playHead += n;
                }
                // the mean of the last quarter minus the mean of the first
                double first = 0.0, last = 0.0;
                const int q = total / 4;
                for (int i = 0; i < q; ++i) first += L[(size_t) i];
                for (int i = total - q; i < total; ++i) last += L[(size_t) i];
                return (last - first) / (double) q;
            };
            const double up = slope(false);
            const double down = slope(true);
            check(up > 0.01, "a forward segment plays the input forwards",
                  juce::String(up, 4));
            check(down < -0.01, "a mirrored segment plays the input backwards",
                  juce::String(down, 4));
        }

        // 6h. v1.30 (UPIC-55): the global transport reverse forces LIVE arcs
        //     onto the mirrored ring even when the per-arc Rev flag is off.
        {
            const int sr = 44100;
            const int D = sr * 2;
            const int total = sr / 2;
            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 2; a.intensity = 1.0f; a.refFreq = 220.0;
            a.points = { { 0.0, 220.0 }, { 4.0, 220.0 } };
            pg.arcs.push_back(a);
            upic::Envelope flat; flat.presetFlat();
            std::vector<upic::Envelope> envs { flat };
            auto waves = upic::defaultWaveforms();

            // two static ramps: the forward ring rises with the write index,
            // the mirrored ring falls (the input captured backwards)
            std::vector<float> fwd((size_t) D), rev((size_t) D);
            for (int i = 0; i < D; ++i)
            {
                fwd[(size_t) i] = (float) i / (float) (D - 1);
                rev[(size_t) i] = 1.0f - fwd[(size_t) i];
            }

            auto slope = [&](double rate) {
                UpicEngine e; e.prepare((double) sr, 8192);
                std::vector<float> L((size_t) total, 0.0f);
                int head = (int) (0.25 * sr);
                double ph = rate > 0.0 ? 0.0 : 0.5;
                for (int pos = 0; pos < total; pos += 512)
                {
                    const int n = juce::jmin(512, total - pos);
                    e.setLiveInput(fwd.data(), nullptr, D, head, true);
                    e.setLiveReverse(rev.data(), nullptr, D, head, true);
                    e.render(pg, ph, waves, envs, {}, L.data() + pos, nullptr,
                             n, 1.0f, rate);
                    head += n;
                    ph += rate * (double) n / (double) sr;
                }
                double first = 0.0, last = 0.0;
                const int q = total / 4;
                for (int i = 0; i < q; ++i) first += L[(size_t) i];
                for (int i = total - q; i < total; ++i) last += L[(size_t) i];
                return (last - first) / (double) q;
            };

            const double forward = slope(1.0);
            const double reverse = slope(-1.0);
            check(forward > 0.01, "live forward playback uses the forward ring",
                  juce::String(forward, 4));
            check(reverse < -0.01,
                  "global reverse forces LIVE playback onto the mirrored ring",
                  juce::String(reverse, 4));
        }

        // 6i. v1.31 (U3): the UPIC's pitch transformations - transpose and
        //     invert. The reverse already exists; these are the pitch ones.
        {
            upic::Arc a;
            a.points = { { 0.0, 220.0 }, { 1.0, 440.0 }, { 2.0, 880.0 } };

            // an octave up doubles every pitch
            a.transpose (12.0);
            check (std::abs (a.points[0].freq - 440.0) < 1e-6
                && std::abs (a.points[1].freq - 880.0) < 1e-6
                && std::abs (a.points[2].freq - 1760.0) < 1e-6,
                   "transpose up an octave doubles the drawn pitches",
                   juce::String (a.points[0].freq, 1) + " / "
                   + juce::String (a.points[1].freq, 1) + " / "
                   + juce::String (a.points[2].freq, 1));

            // and back down again
            a.transpose (-12.0);
            check (std::abs (a.points[0].freq - 220.0) < 1e-6
                && std::abs (a.points[2].freq - 880.0) < 1e-6,
                   "transpose down an octave is the inverse");

            // the inversion mirrors the contour: a rising line becomes a
            // falling one of the same size, around the same centre
            upic::Arc b;
            b.points = { { 0.0, 220.0 }, { 1.0, 440.0 }, { 2.0, 880.0 } };
            const double centre = std::sqrt (220.0 * 880.0); // the geometric mean
            b.invertPitch();
            check (std::abs (b.points[0].freq - centre * centre / 220.0) < 1e-6
                && std::abs (b.points[2].freq - centre * centre / 880.0) < 1e-6
                && b.points[0].freq > b.points[1].freq
                && b.points[1].freq > b.points[2].freq,
                   "invert mirrors the pitch contour",
                   juce::String (b.points[0].freq, 1) + " / "
                   + juce::String (b.points[1].freq, 1) + " / "
                   + juce::String (b.points[2].freq, 1));

            // and it is its own inverse
            b.invertPitch();
            check (std::abs (b.points[0].freq - 220.0) < 1e-6
                && std::abs (b.points[2].freq - 880.0) < 1e-6,
                   "inverting twice gives the arc back");
        }

        // 6i-e. v1.31: the time-axis half of the classical transformation
        //      family - retrograde (mirror in time) and augmentation /
        //      diminution (scale in time).
        {
            upic::Arc a;
            a.points = { { 0.0, 220.0 }, { 1.0, 440.0 }, { 2.0, 880.0 } };

            // the retrograde reads backwards inside the arc's own span
            upic::Arc r = a;
            r.retrograde();
            check (std::abs (r.points[0].t - 0.0) < 1e-9
                && std::abs (r.points[2].t - 2.0) < 1e-9,
                   "the retrograde keeps the arc's time span",
                   juce::String (r.points[0].t, 3) + " .. "
                   + juce::String (r.points[2].t, 3));
            check (std::abs (r.points[0].freq - 880.0) < 1e-6
                && std::abs (r.points[2].freq - 220.0) < 1e-6,
                   "the retrograde plays the contour backwards",
                   juce::String (r.points[0].freq, 1) + " -> "
                   + juce::String (r.points[2].freq, 1));
            bool sorted = true;
            for (size_t i = 1; i < r.points.size(); ++i)
                if (r.points[i].t < r.points[i-1].t) sorted = false;
            check (sorted, "the retrograde keeps the points sorted in time");

            // and it is its own inverse
            r.retrograde();
            check (std::abs (r.points[0].freq - 220.0) < 1e-6
                && std::abs (r.points[2].freq - 880.0) < 1e-6,
                   "retrograding twice gives the arc back");

            // the augmentation stretches the contour in time
            upic::Arc g = a;
            g.scaleTime (2.0);
            check (std::abs ((g.points[2].t - g.points[0].t) - 4.0) < 1e-6,
                   "the augmentation doubles the arc's time span",
                   juce::String (g.points[2].t - g.points[0].t, 3) + " s");
            check (std::abs (g.points[1].freq - 440.0) < 1e-6,
                   "the augmentation keeps the pitch");

            // and the diminution halves it
            upic::Arc d = a;
            d.scaleTime (0.5);
            check (std::abs ((d.points[2].t - d.points[0].t) - 1.0) < 1e-6,
                   "the diminution halves the arc's time span",
                   juce::String (d.points[2].t - d.points[0].t, 3) + " s");
        }

        // 6i-f. v1.32: the discrete frequency space - the UPIC's
        //      "discrete/non-discrete" function. The pitch that plays is
        //      moved through a temperament of n equal divisions per octave,
        //      so a glissando is heard as a staircase of discrete steps.
        {
            const double ref = upic::kDefaultRefFreq; // C4

            // the continuum leaves the pitch alone
            check (std::abs (upic::applyTemperament (445.0, 0, ref) - 445.0) < 1e-9,
                   "no temperament leaves the pitch alone");

            // a pitch on the chromatic scale stays where it is
            const double a4 = upic::applyTemperament (440.0, 12, ref);
            check (std::abs (a4 - 440.0) < 0.02,
                   "the chromatic scale leaves A4 where it is",
                   juce::String (a4, 3) + " Hz");

            // a pitch between two steps snaps to the nearest one
            const double snapped = upic::applyTemperament (445.0, 12, ref);
            check (std::abs (snapped - a4) < 0.02,
                   "a pitch between two steps snaps to the nearer one",
                   juce::String (snapped, 2) + " Hz");

            // a different temperament is a different scale: the diatonic
            // one (7 divisions) puts A4 between two of its steps
            const double seven = upic::applyTemperament (440.0, 7, ref);
            check (std::abs (seven - a4) > 1.0,
                   "the diatonic scale moves A4 to another step",
                   juce::String (seven, 2) + " Hz");
            check (std::abs (std::log2 (seven / ref) * 7.0
                             - std::round (std::log2 (seven / ref) * 7.0)) < 1e-9,
                   "the diatonic scale lands exactly on one of its steps");

            // the glissando becomes a staircase: every step of the sweep
            // lands on the scale, and the sweep visits several steps
            upic::Page plainPage;   // the page's own space: the identity
            upic::Arc g;
            g.points = { { 0.0, 400.0 }, { 1.0, 800.0 } };
            g.stepDivisions = 12;
            const auto plainMap = plainPage.spaceMap (0);
            bool allOnScale = true;
            std::set<double> visited;
            for (int i = 0; i <= 100; ++i)
            {
                const double f = UpicEngine::arcPitch (g, (double) i / 100.0, plainMap);
                const double l = std::log2 (f / ref) * 12.0;
                if (std::abs (l - std::round (l)) > 1e-9) allOnScale = false;
                visited.insert (std::round (l));
            }
            check (allOnScale, "the stepped glissando stays on the scale");
            check (visited.size() > 3,
                   "the stepped glissando is heard as several discrete steps",
                   juce::String ((int) visited.size()) + " steps");

            // and the continuum version of the same arc is not a staircase
            upic::Arc c = g;
            c.stepDivisions = 0;
            bool anyOffScale = false;
            for (int i = 0; i <= 100; ++i)
            {
                const double f = UpicEngine::arcPitch (c, (double) i / 100.0, plainMap);
                const double l = std::log2 (f / ref) * 12.0;
                if (std::abs (l - std::round (l)) > 1e-9) anyOffScale = true;
            }
            check (anyOffScale, "the continuum glissando is not stepped");

            // the drawing is untouched: the stepping is a property of the
            // sound, not of the drawn curve
            check (std::abs (g.points[0].freq - 400.0) < 1e-9
                && std::abs (g.points[1].freq - 800.0) < 1e-9,
                   "the stepping leaves the drawn curve alone");

            // it travels with the piece
            upic::Arc saved = upic::Arc::fromVar (g.toVar());
            check (saved.stepDivisions == 12,
                   "the discrete frequency space travels with the piece",
                   juce::String (saved.stepDivisions) + " divisions");
            upic::Arc plain = upic::Arc::fromVar (upic::Arc().toVar());
            check (plain.stepDivisions == 0,
                   "a piece without it is the continuum");
        }

        // 6i-f2. v1.33: the frequency spaces - the UPIC's four frequency
        //      tables. The page owns four spaces; an arc names the one it
        //      is played in, the way it names its wave or its envelope.
        {
            upic::Page pg;   // the ambitus: 20 Hz - 12 kHz
            const double mid = 2000.0;

            // space 0 IS the page's range: the pitch passes through
            // unchanged, bit for bit (the phase vocoder's bin index is a
            // truncated k*ratio, so an identity mapping must not round-trip
            // through log2/exp2)
            check (pg.placeInSpace (mid, 0) == mid,
                   "the page's own space is the identity",
                   juce::String (pg.placeInSpace (mid, 0), 6) + " Hz");

            // an octave up: the same drawing, one octave higher
            pg.spaces[1] = 3;   // "An octave up"
            const double up = pg.placeInSpace (mid, 1);
            check (std::abs (up - mid * 2.0) < 1e-6,
                   "a space an octave up sounds an octave up",
                   juce::String (up, 1) + " Hz");

            // half the range: the intervals are scaled with it
            pg.spaces[2] = 1;   // "Half the range"
            const double half = pg.placeInSpace (mid, 2);
            check (half < mid && half > mid * 0.4 && half < mid * 0.6,
                   "half the range compresses the drawing's intervals",
                   juce::String (half, 1) + " Hz");
            // the edges of the drawing land on the edges of the space
            double sLo = 0.0, sHi = 0.0;
            pg.spaceRange (2, sLo, sHi);
            check (std::abs (pg.placeInSpace (upic::kMinFreq, 2) - sLo) < 1e-6
                && std::abs (pg.placeInSpace (upic::kMaxFreq, 2) - sHi) < 1e-6,
                   "the space's edges are where the drawing's edges land",
                   juce::String (sLo, 1) + " - " + juce::String (sHi, 1) + " Hz");

            // the arcs are independent: each names its own space
            upic::Arc a1, a2;
            a1.points = { { 0.0, mid }, { 1.0, mid } };
            a2.points = { { 0.0, mid }, { 1.0, mid } };
            a1.space = 0;
            a2.space = 1;
            const auto m0 = pg.spaceMap (a1.space);
            const auto m1 = pg.spaceMap (a2.space);
            check (std::abs (UpicEngine::arcPitch (a1, 0.5, m0) - mid) < 1e-9
                && std::abs (UpicEngine::arcPitch (a2, 0.5, m1) - mid * 2.0) < 1e-3,
                   "two arcs can sit in two spaces at the same time",
                   juce::String (UpicEngine::arcPitch (a1, 0.5, m0), 1) + " / "
                   + juce::String (UpicEngine::arcPitch (a2, 0.5, m1), 1) + " Hz");

            // the space travels with the piece
            upic::Arc saved = upic::Arc::fromVar (a2.toVar());
            check (saved.space == 1, "the frequency space travels with the piece");
            check (upic::Arc::fromVar (a1.toVar()).space == 0,
                   "an arc without a space is in the page's own");
            const auto pageBack = upic::Page::fromVar (pg.toVar());
            check (pageBack.spaces[1] == 3 && pageBack.spaces[2] == 1,
                   "the page's four spaces travel with the piece");
        }

        // 6i-f3. v1.33: the ambitus - the interval the page's vertical axis
        //      covers. "The scale of the millimetric surface could be
        //      selected, so that the vertical axis could correspond to any
        //      interval."
        {
            // the mapping is the page's interval, not the whole world
            const double lo = 261.63 / 4.0, hi = 261.63 * 4.0;   // three octaves
            check (std::abs (upic::normYToFreqIn (0.0f, lo, hi) - lo) < 1e-6
                && std::abs (upic::normYToFreqIn (1.0f, lo, hi) - hi) < 1e-6,
                   "the page's interval spans its axis");
            check (std::abs (upic::freqToNormYIn (lo, lo, hi) - 0.0f) < 1e-6
                && std::abs (upic::freqToNormYIn (hi, lo, hi) - 1.0f) < 1e-6,
                   "the page's interval maps back to its axis");
            // the middle of the axis is the middle of the interval, in log
            // pitch: the geometric mean, not the arithmetic one
            check (std::abs (upic::normYToFreqIn (0.5f, lo, hi)
                             - std::sqrt (lo * hi)) < 1e-6,
                   "the middle of the page is the geometric middle",
                   juce::String (upic::normYToFreqIn (0.5f, lo, hi), 1) + " Hz");

            // a pitch outside the page is off the page (not clamped onto it)
            check (upic::freqToNormYIn (upic::kMinFreq, lo, hi) < 0.0f
                && upic::freqToNormYIn (upic::kMaxFreq, lo, hi) > 1.0f,
                   "a pitch outside the page is off the page");

            // the ambitus travels with the piece
            upic::Page pg;
            pg.pitchLo = lo;
            pg.pitchHi = hi;
            const auto back = upic::Page::fromVar (pg.toVar());
            check (std::abs (back.pitchLo - lo) < 1e-9
                && std::abs (back.pitchHi - hi) < 1e-9,
                   "the ambitus travels with the piece",
                   juce::String (back.pitchLo, 1) + " - "
                   + juce::String (back.pitchHi, 1) + " Hz");
            // and a piece without one is the default interval
            const auto plain = upic::Page::fromVar (upic::Page().toVar());
            check (plain.pitchLo == upic::kDefaultPitchLo
                && plain.pitchHi == upic::kDefaultPitchHi,
                   "a piece without an ambitus is the default interval");
        }

        // 6i-f5. v1.33: the range reaches infrasonic and ultrasonic. "The
        //      frequency range of the 1990s UPIC was extended to
        //      infrasonics, starting at 0.01 Hz" (Robindoré), and the
        //      frequency table could span "as large a range as
        //      0.01-22,050 Hz".
        {
            check (upic::kMinFreq <= 0.01 + 1e-9 && upic::kMaxFreq >= 22050.0 - 1e-9,
                   "the pitch range reaches infrasonic and ultrasonic",
                   juce::String (upic::kMinFreq, 3) + " - "
                   + juce::String (upic::kMaxFreq, 0) + " Hz");

            auto renderAt = [&](double f, juce::AudioBuffer<float>& buf) {
                upic::Page pg; pg.durationSec = 4.0;
                upic::Arc a; a.id = 1; a.mode = 0; a.intensity = 1.0f;
                a.points = { { 0.0, f }, { 4.0, f } };
                pg.arcs.push_back (a);
                auto waves = upic::defaultWaveforms();
                std::vector<upic::Envelope> envs;
                upic::Envelope flat; flat.presetFlat(); envs.push_back (flat);
                std::vector<upic::SampleSlot> noSlots;
                buf.setSize (2, 44100 * 4); buf.clear();
                UpicEngine e; e.prepare (44100.0, 8192);
                e.renderPageOffline (pg, waves, envs, noSlots, buf, 44100.0, 1.0f);
            };
            auto crossings = [](const juce::AudioBuffer<float>& buf) {
                int n = 0; bool high = false; bool finite = true;
                for (int i = 44100; i < 88200; ++i)
                {
                    const float v = buf.getSample (0, i);
                    if (! std::isfinite (v)) { finite = false; continue; }
                    if (! high && v > 0.0f) { high = true; ++n; }
                    else if (high && v < 0.0f) high = false;
                }
                return std::make_pair (n, finite);
            };

            // an infrasonic arc: one slow swell in a second, not a buzz
            juce::AudioBuffer<float> lo;
            renderAt (0.01, lo);
            const auto loX = crossings (lo);
            check (loX.second && loX.first <= 1,
                   "an infrasonic arc is a slow swell",
                   juce::String (loX.first) + " crossings in a second");

            // and an ultrasonic one
            juce::AudioBuffer<float> hi;
            renderAt (20000.0, hi);
            const auto hiX = crossings (hi);
            check (hiX.second && hiX.first > 15000,
                   "an ultrasonic arc is still a tone",
                   juce::String (hiX.first) + " crossings in a second");

            // the middle of the range is untouched
            juce::AudioBuffer<float> mid;
            renderAt (440.0, mid);
            const auto midX = crossings (mid);
            check (midX.second && std::abs (midX.first - 440) < 20,
                   "the middle of the range sounds as it always did",
                   juce::String (midX.first) + " crossings in a second");
        }

        // 6i-f7. v1.33: the staircase. The sieve's points are the step
        //      corners - the pitch holds until the next step and then
        //      jumps - so the sound is a staircase too.
        {
            upic::Arc a;
            // (0, 261) - (1, 277) - (1, 294) - (2, 294): a step at t = 1
            a.points = { { 0.0, 261.0 }, { 1.0, 277.0 }, { 1.0, 294.0 }, { 2.0, 294.0 } };
            check (std::abs (a.freqAt (0.5) - std::sqrt (261.0 * 277.0)) < 1.0,
                   "the staircase holds the pitch between the corners",
                   juce::String (a.freqAt (0.5), 1) + " Hz");
            check (std::abs (a.freqAt (1.0) - 277.0) < 1e-6,
                   "the staircase is at the old step on the corner",
                   juce::String (a.freqAt (1.0), 1) + " Hz");
            check (std::abs (a.freqAt (1.0001) - 294.0) < 1e-6,
                   "the staircase jumps to the new step right after it",
                   juce::String (a.freqAt (1.0001), 1) + " Hz");
            check (std::abs (a.freqAt (1.5) - 294.0) < 1e-6,
                   "the staircase holds the new step");
        }

        // 6i-f6. v1.33: the page's acceleration. The UPIC's page could be
        //      expanded and contracted in time "with the additional option
        //      of acceleration and deceleration" (Robindoré): the page's
        //      own time ran at a varying rate.
        {
            upic::Page pg;
            pg.durationSec = 4.0;
            check (pg.rateAt (0.0) == 1.0 && pg.rateAt (2.0) == 1.0,
                   "a straight page runs at its own rate");
            pg.warp = 0.6;
            check (std::abs (pg.rateAt (0.0) - 0.4) < 1e-9
                && std::abs (pg.rateAt (4.0) - 1.6) < 1e-9,
                   "an accelerating page starts slow and speeds up",
                   juce::String (pg.rateAt (0.0), 2) + " -> "
                   + juce::String (pg.rateAt (4.0), 2) + "x");
            pg.warp = -0.6;
            check (std::abs (pg.rateAt (0.0) - 1.6) < 1e-9,
                   "a decelerating page starts fast and slows down");

            // it travels with the piece
            upic::Page warped; warped.warp = 0.3;
            check (std::abs (upic::Page::fromVar (warped.toVar()).warp - 0.3) < 1e-9,
                   "the acceleration travels with the piece");
            check (upic::Page::fromVar (upic::Page().toVar()).warp == 0.0,
                   "a piece without one is a straight line");

            // and the render follows the sweep: a short arc at page time
            // 1 s arrives later when the page starts slowly
            auto clickAt = [&](double warp) {
                upic::Page p; p.durationSec = 4.0; p.warp = warp;
                upic::Arc a; a.id = 1; a.mode = 0; a.intensity = 1.0f;
                a.points = { { 0.9, 1000.0 }, { 1.1, 1000.0 } };
                p.arcs.push_back (a);
                auto waves = upic::defaultWaveforms();
                std::vector<upic::Envelope> envs;
                upic::Envelope flat; flat.presetFlat(); envs.push_back (flat);
                std::vector<upic::SampleSlot> noSlots;
                juce::AudioBuffer<float> buf (2, 44100 * 6);
                UpicEngine e; e.prepare (44100.0, 8192);
                e.renderPageOffline (p, waves, envs, noSlots, buf, 44100.0, 1.0f);
                int peak = -1; float best = 0.0f;
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    if (std::abs (buf.getSample (0, i)) > best)
                    { best = std::abs (buf.getSample (0, i)); peak = i; }
                return (double) peak / 44100.0;
            };
            const double straight = clickAt (0.0);
            const double swept = clickAt (0.6);
            check (std::abs (straight - 1.0) < 0.15,
                   "a straight page plays the arc at its own time",
                   juce::String (straight, 2) + " s");
            check (swept > straight + 0.4,
                   "an accelerating page delays the arc",
                   juce::String (straight, 2) + " s -> " + juce::String (swept, 2) + " s");
        }

        // 6i-b. v1.31 (U4): the digitised wave page - a take becomes one
        //      loop of the oscillator.
        {
            // a sine over four cycles, resampled into one cycle of 1024
            juce::AudioBuffer<float> take (1, 4096);
            for (int i = 0; i < 4096; ++i)
                take.setSample (0, i, 0.5f * (float) std::sin (
                    2.0 * juce::MathConstants<double>::pi * 4.0 * i / 4096.0));

            const auto w = upic::waveFromTake (take, 42, "take");
            check (w.id == 42 && w.name == "take",
                   "the digitised wave keeps its id and name");
            check ((int) w.samples.size () == upic::kWaveSize,
                   "the digitised wave is one cycle of the wavetable",
                   juce::String ((int) w.samples.size ()));
            float peak = 0.0f;
            for (auto s : w.samples) peak = juce::jmax (peak, std::abs (s));
            check (peak > 0.9f && peak <= 1.0f,
                   "the digitised wave is normalised",
                   juce::String (peak, 3));

            // the loop is seamless: the last sample meets the first
            const float first = w.samples.front();
            const float last  = w.samples.back();
            check (std::abs (first - last) < 0.05f,
                   "the digitised wave loops without a step",
                   juce::String (first, 3) + " -> " + juce::String (last, 3));
        }

        // 6i-c. v1.31 (U2): the micro-time end of the duration range. The
        //      real UPIC's page ran from a few milliseconds to 12 minutes.
        {
            check (upic::kMinPageSeconds <= 0.01
                && upic::kMaxPageSeconds >= 700.0,
                   "the page duration range reaches micro-time and macro-time",
                   juce::String (upic::kMinPageSeconds, 3) + " s .. "
                   + juce::String (upic::kMaxPageSeconds, 0) + " s");

            // a millisecond-scale page survives the parser
            const auto pg = upic::Page::fromVar (juce::JSON::parse (
                "{\"duration\":0.02,\"arcs\":[{\"id\":1,\"points\":["
                "{\"t\":0,\"f\":220},{\"t\":0.015,\"f\":440}]}]}"));
            check (std::abs (pg.durationSec - 0.02) < 1e-9,
                   "a millisecond-scale page loads",
                   juce::String (pg.durationSec, 4) + " s");
            check (pg.arcs.size () == 1 && pg.arcs[0].points.size () == 2
                && pg.arcs[0].points[1].t <= 0.02,
                   "the arcs of a millisecond-scale page keep their times",
                   juce::String (pg.arcs[0].points[1].t, 4));

            // and a 12-minute page too
            const auto longPg = upic::Page::fromVar (juce::JSON::parse (
                "{\"duration\":900,\"arcs\":[{\"id\":1,\"points\":["
                "{\"t\":0,\"f\":220},{\"t\":700,\"f\":440}]}]}"));
            check (std::abs (longPg.durationSec - 720.0) < 1e-9,
                   "a 12-minute page is clamped to the top of the range",
                   juce::String (longPg.durationSec, 1) + " s");
            check (longPg.arcs[0].points[1].t <= 720.0,
                   "an arc beyond the top of the range is clamped",
                   juce::String (longPg.arcs[0].points[1].t, 1));
        }

        // 6i-d. v1.31 (U1): the polyagogy. The same drawn curve can be the
        //      timbre AND a control signal - an amplitude LFO or a frequency
        //      LFO. The sound has to change when the arc's wave is put to
        //      that second use.
        {
            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 0; a.intensity = 1.0f;
            a.refFreq = 220.0;
            a.points = { { 0.0, 220.0 }, { 4.0, 220.0 } };
            pg.arcs.push_back (a);
            upic::Envelope flat; flat.presetFlat();
            std::vector<upic::Envelope> envs { flat };
            auto waves = upic::defaultWaveforms();

            // render the arc plain, then with its own wave as an amplitude
            // LFO, and compare the two
            auto renderArc = [&] (const upic::Arc& arc, juce::AudioBuffer<float>& buf)
            {
                upic::Page p2 = pg; p2.arcs[0] = arc;
                UpicEngine e; e.prepare (44100.0, 8192);
                buf.setSize (1, 44100);
                buf.clear();
                e.renderPageOffline (p2, waves, envs, {}, buf, 44100.0, 1.0f);
            };

            juce::AudioBuffer<float> plainBuf (1, 44100);
            renderArc (a, plainBuf);
            auto peakOf = [] (const juce::AudioBuffer<float>& b)
            {
                float pk = 0.0f;
                for (int i = 0; i < b.getNumSamples(); ++i)
                    pk = juce::jmax (pk, std::abs (b.getSample (0, i)));
                return pk;
            };
            const float plain = peakOf (plainBuf);
            check (plain > 0.01f, "the plain oscillator arc sounds",
                   juce::String (plain, 4));

            // the amplitude LFO: the same wave now shapes the loudness, so
            // the signal is no longer the same - and it tremolos (the
            // loudness in one eighth of a second differs from the next)
            juce::AudioBuffer<float> ampBuf (1, 44100);
            upic::Arc modAmp = a;
            modAmp.waveUse = upic::Arc::waveAlsoAmplitude;
            modAmp.modRate = 4.0;   // 4 sweeps a second
            modAmp.modDepth = 1.0;
            renderArc (modAmp, ampBuf);

            double maxDiff = 0.0;
            for (int i = 0; i < plainBuf.getNumSamples(); ++i)
                maxDiff = juce::jmax (maxDiff, (double) std::abs (
                    plainBuf.getSample (0, i) - ampBuf.getSample (0, i)));
            check (maxDiff > 0.01,
                   "the wave as an amplitude LFO changes the sound",
                   juce::String (maxDiff, 4));

            auto slicePeak = [&] (const juce::AudioBuffer<float>& b, int from, int to)
            {
                float pk = 0.0f;
                for (int i = from; i < to; ++i) pk = juce::jmax (pk, std::abs (b.getSample (0, i)));
                return pk;
            };
            // the loudest and the quietest 10 ms window of the first second:
            // a full-depth tremolo swings the gain from 0 to 2
            float loudest = 0.0f, quietest = 1e9f;
            for (int from = 0; from + 441 <= ampBuf.getNumSamples(); from += 441)
            {
                const float pk = slicePeak (ampBuf, from, from + 441);
                loudest = juce::jmax (loudest, pk);
                quietest = juce::jmin (quietest, pk);
            }
            check (loudest > quietest * 1.5f,
                   "the amplitude LFO tremolos the arc",
                   juce::String (quietest, 4) + " .. " + juce::String (loudest, 4));

            // the frequency LFO: the same wave now moves the pitch, so the
            // waveform itself changes (the peak stays the same - it is a
            // vibrato)
            juce::AudioBuffer<float> freqBuf (1, 44100);
            upic::Arc modFreq = a;
            modFreq.waveUse = upic::Arc::waveAlsoFrequency;
            modFreq.modRate = 4.0;
            modFreq.modDepth = 1.0;
            renderArc (modFreq, freqBuf);

            double maxDiffF = 0.0;
            for (int i = 0; i < plainBuf.getNumSamples(); ++i)
                maxDiffF = juce::jmax (maxDiffF, (double) std::abs (
                    plainBuf.getSample (0, i) - freqBuf.getSample (0, i)));
            check (maxDiffF > 0.01,
                   "the wave as a frequency LFO changes the sound",
                   juce::String (maxDiffF, 4));

            // and the settings travel with the piece
            upic::Arc saved = upic::Arc::fromVar (modAmp.toVar());
            check (saved.waveUse == upic::Arc::waveAlsoAmplitude
                && std::abs (saved.modRate - 4.0) < 1e-6
                && std::abs (saved.modDepth - 1.0) < 1e-6,
                   "the polyagogy settings survive a piece round trip",
                   juce::String (saved.waveUse) + " / "
                   + juce::String (saved.modRate, 2) + " / "
                   + juce::String (saved.modDepth, 2));
        }

        // 6j. v1.30 (S-03): a piece is untrusted input. The parsers clamp
        //     the counts and reject non-finite numbers, so a crafted piece
        //     cannot be used to freeze the UI or to push NaN into the audio
        //     thread.
        {
            upic::Arc arc = upic::Arc::fromVar (juce::JSON::parse (
                "{\"id\":1,\"mode\":0,\"intensity\":99,\"refFreq\":1e300,"
                "\"points\":[{\"t\":0,\"f\":440,\"i\":5},{\"t\":1e300,\"f\":NaN}]}"));
            check (arc.intensity >= 0.0f && arc.intensity <= 1.0f,
                   "an out-of-range intensity is clamped",
                   juce::String (arc.intensity, 3));
            check (arc.refFreq >= 20.0 && arc.refFreq <= 4000.0,
                   "an out-of-range refFreq is clamped",
                   juce::String (arc.refFreq, 1));
            bool allFinite = true;
            for (auto& pt : arc.points)
                if (! std::isfinite (pt.t) || ! std::isfinite (pt.freq))
                    allFinite = false;
            check (allFinite, "a non-finite arc point is clamped to a finite one");

            // a huge point count is truncated, not accepted
            {
                juce::String json = "{\"id\":2,\"points\":[";
                for (int i = 0; i < 5000; ++i)
                {
                    if (i > 0) json += ",";
                    json += "{\"t\":" + juce::String (i) + ",\"f\":440}";
                }
                json += "]}";
                const auto a2 = upic::Arc::fromVar (juce::JSON::parse (json));
                check ((int) a2.points.size () <= upic::limits::kMaxPointsPerArc,
                       "an oversized point list is truncated",
                       juce::String ((int) a2.points.size ()) + " of 5000");
            }

            // a huge declared frame count is capped before anything is allocated
            {
                const auto slot = upic::SampleSlot::fromVar (juce::JSON::parse (
                    "{\"id\":1,\"frames\":2000000000,\"channels\":2,\"sampleRate\":48000,"
                    "\"data\":\"AAAA\"}"), 1);
                check (slot.data == nullptr,
                       "an oversized sample slot is refused (no allocation)");
            }

            // a page with too many arcs is truncated
            {
                juce::String json = "{\"duration\":30,\"arcs\":[";
                for (int i = 0; i < 2000; ++i)
                {
                    if (i > 0) json += ",";
                    json += "{\"id\":" + juce::String (i + 1) + ",\"points\":[]}";
                }
                json += "]}";
                const auto pg = upic::Page::fromVar (juce::JSON::parse (json));
                check ((int) pg.arcs.size () <= upic::limits::kMaxArcsPerPage,
                       "an oversized arc list is truncated",
                       juce::String ((int) pg.arcs.size ()) + " of 2000");
            }

            // the clamp helper itself never returns NaN
            check (upic::limits::clampFinite (std::numeric_limits<double>::quiet_NaN (), 0.0, 1.0) == 0.0,
                   "clampFinite turns NaN into the lower bound");
            check (upic::limits::clampGain (99.0f) == upic::limits::kMaxMasterGain,
                   "clampGain caps the master gain at the slider's top");
        }

        // 6j. v1.30 (S-03): a hostile piece goes through the app's own load
        //     path. It must survive: no crash, and the model must end up
        //     in a usable state.
        {
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize (1280, 800);

            // a piece with a huge master gain, a NaN-ish duration and a
            // sample slot that claims two billion frames
            const auto hostile = juce::JSON::parse (
                "{\"master\":1e30,\"duration\":1e300,\"tempo\":1e300,"
                "\"samples\":[{\"id\":1,\"frames\":2000000000,\"channels\":2,"
                "\"sampleRate\":1e9,\"data\":\"AAAA\"}],"
                "\"pages\":[{\"duration\":1e300,\"arcs\":[{\"id\":1,\"mode\":0,"
                "\"intensity\":99,\"refFreq\":1e300,\"points\":[{\"t\":0,\"f\":1e300}]}]}]}");
            check (! hostile.isVoid (), "the hostile piece parses as JSON");

            bool survived = true;
            try
            {
                mc.loadFromVar (hostile);
            }
            catch (...)
            {
                survived = false;
            }
            check (survived, "a hostile piece does not take the app down");

            // the master gain came back clamped, not at 1e30
            const auto gain = (float)(double) mc.pieceToVar().getProperty ("master", -1.0);
            check (gain >= 0.0f && gain <= upic::limits::kMaxMasterGain,
                   "the master gain of a hostile piece is clamped",
                   juce::String (gain, 3));

            // and the app still renders afterwards
            juce::AudioBuffer<float> buf (2, 4096);
            buf.clear();
            mc.getNextAudioBlock (juce::AudioSourceChannelInfo (&buf, 0, 4096));
            bool finite = true;
            for (int i = 0; i < 4096; ++i)
                if (! std::isfinite (buf.getSample (0, i)) || ! std::isfinite (buf.getSample (1, i)))
                    finite = false;
            check (finite, "the audio after a hostile piece is finite");
        }

        // 7. pen pressure -> per-point intensity curve
        {
            upic::Arc a;
            a.points = { {0.0, 440.0, 1.0f}, {1.0, 440.0, 0.0f}, {2.0, 440.0, 1.0f} };
            const float i05 = a.intensityAt(0.5);
            const float i10 = a.intensityAt(1.0);
            const float ib  = a.intensityAt(-1.0); // before start -> front value
            const float ia  = a.intensityAt(5.0);  // after end -> back value
            check(std::abs(i05 - 0.5f) < 1e-5f && std::abs(i10 - 0.0f) < 1e-5f,
                  "pressure intensityAt linear interp",
                  juce::String(i05, 3) + "/" + juce::String(i10, 3));
            check(std::abs(ib - 1.0f) < 1e-6f && std::abs(ia - 1.0f) < 1e-6f,
                  "pressure intensityAt clamps outside arc");

            // pressure 0 everywhere => silence
            upic::Page pg; pg.durationSec = 1.0;
            upic::Arc z; z.id = 1; z.intensity = 1.0f;
            z.points = { {0.0, 440.0, 0.0f}, {1.0, 440.0, 0.0f} };
            pg.arcs.push_back(z);
            juce::AudioBuffer<float> zb(2, 44100); zb.clear();
            UpicEngine e1; e1.prepare(44100.0, 8192);
            auto waves = upic::defaultWaveforms();
            auto envs = upic::defaultEnvelopes();
            std::vector<upic::SampleSlot> noSlots;
            e1.renderPageOffline(pg, waves, envs, noSlots, zb, 44100.0, 1.0f);
            double zpeak = 0.0;
            for (int i = 0; i < zb.getNumSamples(); ++i)
                zpeak = juce::jmax(zpeak, (double) std::abs(zb.getSample(0, i)));
            check(zpeak < 1e-6, "pressure 0 => silence", juce::String(zpeak, 8));

            // JSON round-trip of point pressure (old files: absent -> 1.0)
            auto back = upic::Page::fromVar(juce::JSON::parse(juce::JSON::toString(pg.toVar(), false)));
            check(! back.arcs.empty() && ! back.arcs[0].points.empty()
                  && std::abs(back.arcs[0].points[0].i - 0.0f) < 1e-6f,
                  "json round-trip point pressure (i)");
            auto legacy = upic::Page::fromVar(juce::JSON::parse(
                "{\"duration\":10,\"arcs\":[{\"id\":1,\"points\":[{\"t\":0,\"f\":440}]}]}"));
            check(! legacy.arcs.empty() && legacy.arcs[0].points[0].i == 1.0f,
                  "legacy point without i defaults to 1");
        }

        // 8. sieve quantizer (Xenakis) on the MIDI-note axis
        {
            auto closeTo = [](double a, double b){ return std::abs(a - b) < 0.5; };
            check(closeTo(upic::sieveQuantizeHz(440.0, 12, {9}), 440.0),
                  "sieve on-grid pitch unchanged (m12 r9 = A)");
            const double off = upic::sieveQuantizeHz(450.0, 12, {9});
            check(closeTo(off, 440.0), "sieve snaps to nearest allowed pitch",
                  juce::String(off, 2) + " Hz (expect 440)");
            const double major = upic::sieveQuantizeHz(262.0, 12, {0,2,4,5,7,9,11});
            check(closeTo(major, 261.63), "sieve major scale -> C4", juce::String(major, 3) + " Hz");
            check(upic::sieveQuantizeHz(300.0, 12, {}) == 300.0,
                  "sieve empty remainders = identity");
            check(upic::sieveQuantizeHz(300.0, 0, {1}) == 300.0,
                  "sieve m < 2 = identity");
        }

        // 9. page sequencer order
        {
            check(upic::seqNextPage(0, 3, true) == 1, "seq advances page 0 -> 1");
            check(upic::seqNextPage(2, 3, true) == 0, "seq wraps last -> first when looping");
            check(upic::seqNextPage(2, 3, false) == -1, "seq stops after last page");
            check(upic::seqNextPage(0, 1, true) == 0, "seq single page wraps to itself");
        }

        // 10. stems: one WAV per audible voice, muted/material-less skipped
        {
            upic::Page pg; pg.durationSec = 1.0;
            auto mkArc = [&](int id, int wid, bool muted, double f){
                upic::Arc a; a.id = id; a.waveformId = wid; a.muted = muted;
                a.points = { {0.0, f}, {1.0, f} };
                return a;
            };
            pg.arcs.push_back(mkArc(1, 1, false, 440.0)); // voice 1
            pg.arcs.push_back(mkArc(2, 1, false, 660.0)); // voice 1 again (merged)
            pg.arcs.push_back(mkArc(3, 2, false, 880.0)); // voice 2
            pg.arcs.push_back(mkArc(4, 2, true,  220.0)); // muted -> skipped
            upic::Arc sa; sa.id = 5; sa.mode = 1; sa.sampleId = 1; // no material loaded
            sa.points = { {0.0, 440.0}, {1.0, 440.0} };
            pg.arcs.push_back(sa);

            auto waves = upic::defaultWaveforms();
            auto envs = upic::defaultEnvelopes();
            std::vector<upic::SampleSlot> noSlots;
            auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("upic_stems_test");
            dir.deleteRecursively();
            auto written = renderStems(pg, waves, envs, noSlots, dir, "stem", 44100.0, 0.8f);
            check(written.size() == 2, "stems: one file per audible voice",
                  juce::String((int) written.size()) + " files (expect 2)");
            bool namesOk = false, sizesOk = ! written.empty();
            for (auto& f : written)
            {
                namesOk = namesOk || f.getFileName().contains("wave1");
                namesOk = namesOk || f.getFileName().contains("wave2");
                sizesOk = sizesOk && f.existsAsFile() && f.getSize() > 4096;
            }
            check(namesOk, "stems: named by voice (stem_wave1/2)");
            check(sizesOk, "stems: files exist and non-empty");
            dir.deleteRecursively();
        }

        // 11. magnetic snap helpers: stickiness, time grid, click-free stop ramp
        {
            check(upic::stickify(450.0, 440.0, 0.0) == 450.0, "stickify 0 = free");
            check(upic::stickify(450.0, 440.0, 1.0) == 440.0, "stickify 1 = magnetic");
            check(std::abs(upic::stickify(450.0, 440.0, 0.5) - 445.0) < 1e-9, "stickify 0.5 = midpoint");
            check(std::abs(upic::timeGridSnap(1.13, 4.0) - 1.25) < 1e-9, "time grid 1.13 @4/s -> 1.25",
                  juce::String(upic::timeGridSnap(1.13, 4.0), 4));
            check(upic::timeGridSnap(2.0, 0.0) == 2.0, "time grid off = identity");
            check(std::abs(upic::timeGridSnap(1.0, 8.0) - 1.0) < 1e-9, "time grid on-step unchanged");

            check(upic::stopFadeGain(0.05, 0.03) == 1.0, "stop fade before window = 1");
            check(std::abs(upic::stopFadeGain(0.015, 0.03) - 0.5) < 1e-9, "stop fade midpoint = 0.5");
            check(upic::stopFadeGain(0.0, 0.03) == 0.0, "stop fade at end = 0");
            check(upic::stopFadeGain(-1.0, 0.03) == 0.0, "stop fade past end = 0");
            bool mono = true;
            double prev = 1.1;
            for (int i = 0; i <= 100; ++i)
            {
                const double g = upic::stopFadeGain(0.03 * (1.0 - i / 100.0), 0.03);
                mono = mono && g <= prev + 1e-12;
                prev = g;
            }
            check(mono, "stop fade ramp monotonic");
        }

        // 12. sieve preset library (UPISketch-style examples)
        {
            const auto ps = upic::sievePresets();
            check(ps.size() >= 8, "sieve presets >= 8", juce::String((int) ps.size()));
            bool valid = ! ps.empty();
            bool unique = true;
            for (size_t i = 0; i < ps.size(); ++i)
            {
                valid = valid && ps[i].m >= 2;
                for (size_t j = i + 1; j < ps.size(); ++j)
                    unique = unique && ps[i].name != ps[j].name;
                for (int r : ps[i].remainders) valid = valid && r >= 0 && r < ps[i].m;
            }
            check(valid, "sieve presets well-formed (m>=2, 0<=r<m)");
            check(unique, "sieve preset names unique");
            bool wt = false;
            for (auto& p : ps)
                wt = wt || (p.m == 12 && p.remainders == std::vector<int>({ 0,2,4,6,8,10 }));
            check(wt, "preset whole-tone = m12 {0,2,4,6,8,10}");
            bool chrom = false;
            for (auto& p : ps)
                chrom = chrom || (p.name == "Chromatic" && p.m == 12
                                  && (int) p.remainders.size() == 12);
            check(chrom, "preset Chromatic allows all twelve semitones");
        }

        // 13. sieve zones: containment, JSON, unified quantizePoint precedence
        {
            upic::SieveZone z;
            z.id = 7; z.t0 = 5; z.t1 = 10; z.f0 = 400; z.f1 = 800; // reversed corners allowed
            z.m = 12; z.remainders = { 0,1,2,3,4,5,6,7,8,9,10,11 }; z.timeGrid = 8;
            check(z.contains(7.5, 500.0), "zone contains interior point");
            check(z.contains(5.0, 400.0), "zone contains corners (inclusive)");
            check(! z.contains(4.999, 500.0), "zone excludes outside point");
            std::vector<upic::SieveZone> zones { z };
            check(upic::zoneAt(zones, 6.0, 500.0) != nullptr, "zoneAt finds zone");
            check(upic::zoneAt(zones, 1.0, 500.0) == nullptr, "zoneAt misses outside");

            z.stickiness = 0.4;
            auto zj = upic::SieveZone::fromVar(juce::JSON::parse(juce::JSON::toString(z.toVar(), false)));
            check(zj.id == 7 && std::abs(zj.t0 - 5.0) < 1e-9 && std::abs(zj.f1 - 800.0) < 1e-9
                  && zj.m == 12 && zj.remainders == z.remainders
                  && std::abs(zj.timeGrid - 8.0) < 1e-9
                  && std::abs(zj.stickiness - 0.4) < 1e-9, "zone JSON round-trip");
            {
                // a zone written before per-zone stickiness follows the global
                auto legacy = upic::SieveZone::fromVar(juce::JSON::parse(
                    "{\"id\":9,\"t0\":0,\"t1\":1,\"f0\":100,\"f1\":200,\"m\":12,\"r\":[0]}"));
                check(std::abs(legacy.stickiness + 1.0) < 1e-9,
                      "a legacy zone follows the global stick");
            }

            upic::Page pz; pz.durationSec = 20; pz.zones.push_back(z); pz.nextZoneId = 8;
            auto pb = upic::Page::fromVar(juce::JSON::parse(juce::JSON::toString(pz.toVar(), false)));
            check(pb.zones.size() == 1 && pb.nextZoneId == 8, "page zones JSON round-trip");
            auto oldPg = upic::Page::fromVar(juce::JSON::parse("{\"duration\":10,\"arcs\":[]}"));
            check(oldPg.zones.empty() && oldPg.nextZoneId == 1, "legacy page has no zones");

            // zone sieve beats global sieve; global grid applies outside the zone
            const std::vector<int> globalR { 0, 7 };
            double t = 6.0, f = 450.0; // inside the chromatic zone
            upic::quantizePoint(true, true, 12, globalR, 4.0, 1.0, zones, t, f);
            check(std::abs(f - 440.0) < 0.5, "zone sieve overrides global (450 -> 440)",
                  juce::String(f, 2));
            check(std::abs(t - 6.0) < 1e-9, "zone time grid 8/s keeps 6.0");
            t = 2.13; f = 450.0;       // outside -> global sieve {0,7} + global grid 4/s
            upic::quantizePoint(true, true, 12, globalR, 4.0, 1.0, zones, t, f);
            check(std::abs(f - 392.0) < 0.6, "global sieve outside zone (450 -> 392)",
                  juce::String(f, 2));
            check(std::abs(t - 2.25) < 1e-9, "global time grid snaps 2.13 -> 2.25",
                  juce::String(t, 4));
            t = 0.0; f = 450.0;        // chromatic snap softened by stickiness 0.5
            upic::quantizePoint(true, false, 12, { 0 }, 0.0, 0.5, {}, t, f);
            check(std::abs(f - 445.0) < 0.6, "stickiness 0.5 half-snaps (450 -> ~445)",
                  juce::String(f, 2));

            // v1.33.2: per-zone stickiness. The zone's own value wins inside
            // it; -1 means "follow the global stick" (old pieces).
            {
                upic::SieveZone zs;
                zs.t0 = 0; zs.t1 = 10; zs.f0 = 100; zs.f1 = 1000;
                zs.m = 12; zs.remainders = { 0 }; zs.timeGrid = 0; zs.stickiness = 0.0;
                std::vector<upic::SieveZone> zz { zs };
                t = 5.0; f = 450.0;
                upic::quantizePoint(true, true, 12, { 0, 7 }, 0.0, 1.0, zz, t, f);
                check(std::abs(f - 450.0) < 1e-9,
                      "zone stick 0 leaves the pitch free inside the zone",
                      juce::String(f, 2));
                zz[0].stickiness = -1.0;
                t = 5.0; f = 450.0;
                upic::quantizePoint(true, true, 12, { 0, 7 }, 0.0, 1.0, zz, t, f);
                check(std::abs(upic::freqToMidi(f) - 72.0) < 0.05,
                      "zone stick -1 follows the global stick (450 -> C5)",
                      juce::String(f, 2));
            }
        }

        // 14. freehand curve simplification (RDP): fewer points, shape kept
        {
            std::vector<upic::ArcPoint> pts;
            juce::Random jr(77);
            const int N = 300;
            for (int i = 0; i <= N; ++i)
            {
                const double t = (double) i / N * 10.0;
                const double f = 440.0 * std::pow(2.0, 0.8 * std::sin(t))
                               * (1.0 + (jr.nextFloat() - 0.5f) * 0.01);
                pts.push_back({ t, f, 1.0f });
            }
            const auto orig = pts;
            upic::simplifyArcPoints(pts, 0.01);
            check(pts.size() < orig.size() / 2, "simplify halves dense stroke",
                  juce::String((int) pts.size()) + " of " + juce::String((int) orig.size()));
            check(! pts.empty() && pts.front().t == orig.front().t
                  && pts.front().freq == orig.front().freq
                  && pts.back().t == orig.back().t
                  && pts.back().freq == orig.back().freq,
                  "simplify preserves endpoints");

            const double t0 = orig.front().t, tSpan = juce::jmax(1e-9, orig.back().t - t0);
            double lmin = 1e18, lmax = -1e18;
            for (auto& q : orig) { const double l = std::log2(q.freq); lmin = std::min(lmin, l); lmax = std::max(lmax, l); }
            const double lSpan = juce::jmax(1e-9, lmax - lmin);
            auto segDist = [](double px, double py, double ax, double ay, double bx, double by){
                const double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
                if (len2 < 1e-18) return std::hypot(px - ax, py - ay);
                const double u = juce::jlimit(0.0, 1.0, ((px - ax) * dx + (py - ay) * dy) / len2);
                return std::hypot(px - (ax + u * dx), py - (ay + u * dy));
            };
            double maxDev = 0.0;
            for (auto& q : orig)
            {
                const double px = (q.t - t0) / tSpan, py = (std::log2(q.freq) - lmin) / lSpan;
                double best = 1e18;
                for (size_t i = 1; i < pts.size(); ++i)
                {
                    const double ax = (pts[i-1].t - t0) / tSpan, ay = (std::log2(pts[i-1].freq) - lmin) / lSpan;
                    const double bx = (pts[i].t - t0) / tSpan,   by = (std::log2(pts[i].freq) - lmin) / lSpan;
                    best = std::min(best, segDist(px, py, ax, ay, bx, by));
                }
                maxDev = std::max(maxDev, best);
            }
            check(maxDev <= 0.021, "simplify deviation <= 2*eps", juce::String(maxDev, 4));
        }

        // 15. procedural arc generation (nUPIC-style generators)
        {
            upic::GenParams gp; gp.t0 = 0.0; gp.t1 = 8.0; gp.f0 = 90.0; gp.f1 = 3000.0;
            auto structural = [&](const std::vector<upic::Arc>& arcs, int minArcs){
                bool ok = (int) arcs.size() >= minArcs;
                for (auto& a : arcs)
                {
                    ok = ok && a.points.size() >= 2;
                    double lastT = -1.0;
                    for (auto& p : a.points)
                    {
                        ok = ok && p.t >= gp.t0 - 1e-9 && p.t <= gp.t1 + 1e-9;
                        ok = ok && p.t > lastT; // strictly increasing
                        ok = ok && p.freq >= upic::kMinFreq - 1e-6 && p.freq <= upic::kMaxFreq + 1e-6;
                        ok = ok && p.freq == p.freq; // no NaN
                        lastT = p.t;
                    }
                }
                return ok;
            };
            juce::Random r1(4242);
            auto stoch = upic::genStochasticArborescence(gp, r1);
            check(structural(stoch, 3), "gen stochastic arborescence (>=3 arcs)",
                  juce::String((int) stoch.size()) + " arcs");

            juce::Random r2(4242);
            auto conv = upic::genConvergentArborescence(gp, r2);
            check(structural(conv, 4), "gen convergent arborescence (>=4 arcs)",
                  juce::String((int) conv.size()) + " arcs");
            const double target = 0.5 * (std::log2(gp.f0) + std::log2(gp.f1));
            bool convOk = ! conv.empty();
            for (auto& a : conv)
                convOk = convOk && a.points.size() >= 2
                      && std::abs(std::log2(a.points.back().freq) - target) < 1e-6;
            check(convOk, "convergent arcs end on the shared target pitch");

            juce::Random r3(4242);
            auto ech = upic::genEchelon(gp, r3);
            check(structural(ech, 4), "gen echelon construction (>=4 ranks)",
                  juce::String((int) ech.size()) + " arcs");

            juce::Random r4(4242);
            auto spine = upic::genOrdinalSpine(gp, r4);
            check(structural(spine, 4), "gen ordinal spine + branches (>=4 arcs)",
                  juce::String((int) spine.size()) + " arcs");

            juce::Random d1(99), d2(99);
            auto A = upic::genStochasticArborescence(gp, d1);
            auto B = upic::genStochasticArborescence(gp, d2);
            bool same = A.size() == B.size() && ! A.empty();
            for (size_t i = 0; same && i < A.size(); ++i)
            {
                same = A[i].points.size() == B[i].points.size();
                for (size_t j = 0; same && j < A[i].points.size(); ++j)
                    same = A[i].points[j].t == B[i].points[j].t
                        && A[i].points[j].freq == B[i].points[j].freq;
            }
            check(same, "generators deterministic for a fixed seed");
        }

        // 16. per-arc entry/exit edge fade: square wave (worst case) starts/ends silent
        {
            upic::Waveform sq(99, "Square", juce::Colours::white);
            sq.makeSquare();
            std::vector<upic::Waveform> waves { sq };
            auto envs = upic::defaultEnvelopes();
            upic::Page pg; pg.durationSec = 1.0;
            upic::Arc a; a.id = 1; a.waveformId = 99; a.intensity = 1.0f;
            a.points = { { 0.2, 440.0 }, { 0.8, 440.0 } };
            pg.arcs.push_back(a);
            juce::AudioBuffer<float> buf(2, 44100); buf.clear();
            UpicEngine eng; eng.prepare(44100.0, 8192);
            std::vector<upic::SampleSlot> noSlots;
            eng.renderPageOffline(pg, waves, envs, noSlots, buf, 44100.0, 1.0f);
            const int i0 = (int) (0.2 * 44100.0), i1 = (int) (0.8 * 44100.0);
            const float first = std::abs(buf.getSample(0, i0));
            const float last  = std::abs(buf.getSample(0, i1));
            const float mid   = std::abs(buf.getSample(0, (i0 + i1) / 2));
            check(first < 0.01, "arc entry fade starts silent", juce::String(first, 4));
            check(last < 0.01, "arc exit fade ends silent", juce::String(last, 4));
            check(mid > 0.15, "arc mid-level audible", juce::String(mid, 4));
        }

        // 17. ScoreEditor interactions (in-process, no window): selection,
        //     shortcuts, arrow nudge, sieve-aware steps, zones, time-strip scrub
        {
            auto waves = upic::defaultWaveforms();
            double ph = 0.0;
            juce::CriticalSection lock;
            upic::Page pg;
            pg.durationSec = 10.0;
            for (int i = 0; i < 3; ++i)
            {
                upic::Arc a; a.id = i + 1;
                a.points = { { 1.0 + i, 220.0 * (i + 1) }, { 3.0 + i, 440.0 * (i + 1) } };
                pg.arcs.push_back(a);
            }
            ScoreEditor ed;
            ed.setModelLock(&lock);
            ed.bind(&pg, &waves, &ph, nullptr);
            ed.setSize(1000, 400);

            // v1.33.2: the global sieve cannot become empty (the checkbox is
            // the off switch); an empty list keeps the last valid set.
            ed.setSieve (true, 12, { 0 });
            ed.setSieve (true, 12, {});
            check (ed.getSieveR() == std::vector<int>({ 0 }),
                   "setSieve(empty) keeps the last remainders");
            ed.setSieve (false, 12, { 0 });

            ed.keyPressed(juce::KeyPress('A', juce::ModifierKeys::commandModifier, 0));
            check(ed.getSelectedArcs().size() == 3, "ctrl+A selects all arcs",
                  juce::String((int) ed.getSelectedArcs().size()));

            const double t0 = pg.arcs[0].points[0].t;
            ed.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(), 0));
            check(std::abs(pg.arcs[0].points[0].t - (t0 + 0.02)) < 1e-9,
                  "right arrow nudges selection +0.02s", juce::String(pg.arcs[0].points[0].t, 4));

            ed.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys(), 0));
            check(pg.arcs.empty(), "Del deletes the whole selection");

            upic::Arc one; one.id = pg.nextArcId++;
            one.points = { { 2.0, 300.0 }, { 5.0, 600.0 } };
            pg.arcs.push_back(one);
            ed.selectAll();
            ed.keyPressed(juce::KeyPress('D', juce::ModifierKeys::commandModifier, 0));
            check(pg.arcs.size() == 2, "ctrl+D duplicates", juce::String((int) pg.arcs.size()));
            check(pg.arcs.size() == 2 && pg.arcs[1].id != pg.arcs[0].id, "duplicate gets a fresh id");
            check(pg.arcs.size() == 2
                  && std::abs(pg.arcs[1].points[0].t - pg.arcs[0].points[0].t) > 0.05,
                  "duplicate is time-offset");

            // sieve-aware up/down: A440 with sieve {9} (A's only) -> A5 -> back
            upic::Page ap; ap.durationSec = 10.0;
            upic::Arc a1; a1.id = 1; a1.points = { { 1.0, 440.0 }, { 3.0, 440.0 } };
            ap.arcs.push_back(a1);
            ed.bind(&ap, &waves, &ph, nullptr);
            ed.setSieve(true, 12, { 9 });
            ed.setStickiness(1.0);
            ed.setTimeGrid(0.0);
            ed.setSelectedArc(1);
            ed.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys(), 0));
            check(std::abs(ap.arcs[0].points[0].freq - 880.0) < 0.5,
                  "sieve up-arrow -> next allowed pitch A5",
                  juce::String(ap.arcs[0].points[0].freq, 1));
            ed.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(), 0));
            check(std::abs(ap.arcs[0].points[0].freq - 440.0) < 0.5,
                  "sieve down-arrow -> back to A4",
                  juce::String(ap.arcs[0].points[0].freq, 1));

            // zone-aware quantization through the editor
            upic::Page zp; zp.durationSec = 10.0;
            ed.bind(&zp, &waves, &ph, nullptr);
            ed.setSieve(true, 12, { 0, 7 }); // global: C/G only
            double t = 1.0, f = 450.0;
            ed.quantizeXY(t, f);
            check(std::abs(f - 392.0) < 0.6, "editor global sieve 450 -> 392",
                  juce::String(f, 2));

            const int zid = ed.addZone(4.0, 6.0, 300.0, 600.0);
            check(zid > 0 && zp.zones.size() == 1, "editor addZone inserts a zone");
            zp.zones[0].m = 12;
            zp.zones[0].remainders = { 0,1,2,3,4,5,6,7,8,9,10,11 }; // chromatic zone
            t = 5.0; f = 450.0;
            ed.quantizeXY(t, f);
            check(std::abs(f - 440.0) < 0.5, "editor zone sieve overrides global",
                  juce::String(f, 2));
            t = 1.0; f = 450.0;
            ed.quantizeXY(t, f);
            check(std::abs(f - 392.0) < 0.6, "editor outside zone keeps global sieve",
                  juce::String(f, 2));

            ed.setTimeGrid(4.0);
            t = 5.13; f = 500.0; // inside zone (zone grid = 0 -> no time snap)
            ed.quantizeXY(t, f);
            check(std::abs(t - 5.13) < 1e-9, "editor zone timeGrid 0 overrides global grid");
            t = 2.13; f = 500.0; // outside -> global grid snaps
            ed.quantizeXY(t, f);
            check(std::abs(t - 2.25) < 1e-9, "editor outside zone global grid snaps 2.13 -> 2.25",
                  juce::String(t, 4));

            ed.deleteZone(zid);
            check(zp.zones.empty(), "editor deleteZone removes");

            // time-strip scrub moves the playhead
            ed.scrubTo(ed.timeToX(7.0));
            check(std::abs(ph - 7.0) < 1e-6, "centre line scrub sets playhead",
                  juce::String(ph, 3));

            // v1.25: the playhead triangle at the top is a drag handle
            const auto phHandle = ed.playheadHandleRect();
            check(phHandle.getWidth() >= 20.0f && phHandle.getHeight() >= 14.0f
                  && std::abs(phHandle.getCentreX() - ed.timeToX(7.0)) < 0.01f,
                  "playhead triangle handle sits on the playhead",
                  phHandle.toString());

            // v1.19: the scrub line runs across the middle of the canvas, so
            // the target is a narrow band at its TOP - a drawing stroke that
            // starts in the middle must not move the playhead.
            {
                const auto strip = ed.timeStripBounds();
                const auto target = ed.scrubTargetRect();
                check(target.getHeight() <= 6.0f,
                      "the scrub target is a narrow band",
                      juce::String(target.getHeight(), 1) + " dp");
                check(strip.contains(target),
                      "the scrub target stays inside the scrub line");
                check(! target.contains(strip.getCentre()),
                      "the middle of the scrub line is free for drawing");
            }
        }

        // 18. offscreen UI snapshot (no window shown, no screen access) +
        //     v1.2 in-process checks: piece JSON round-trip, named controls,
        //     translucent zone + centre scrub line pixel probes
        {
            auto png = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("upic_snapshot.png");
            bool written = false;
            juce::Image img(juce::Image::RGB, 1280, 800, true);
            juce::Rectangle<int> scoreBounds;
            {
                juce::Graphics g(img);
                auto mcHolder = std::make_unique<MainComponent>();
                auto& mc = *mcHolder;
                mc.setSize(1280, 800);

                // --- responsive layout: the control grid reflows at small sizes ---
                // recursive find: e.g. the audio overlay's OK button lives one
                // level below the MainComponent
                std::function<juce::Component*(juce::Component&, const juce::String&)> findIn;
                findIn = [&](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findIn(*c.getChildComponent(i), n))
                            return r;
                    return nullptr;
                };
                auto find = [&](const char* n) -> juce::Component* {
                    return findIn(mc, juce::String(n));
                };
                auto layoutOk = [&](int zw, int zh, juce::String& detail) -> bool
                {
                    mc.setSize(zw, zh);
                    bool ok = true;
                    static const char* watch[] = { "score", "genBox", "stickSlider", "gridBox",
                                                   "sievePresetBox", "kbdHintLab" };

                    // every visible direct child: non-empty and inside the
                    // window (hidden views keep stale bounds and are skipped)
                    for (int i = 0; i < mc.getNumChildComponents(); ++i)
                    {
                        auto* c = mc.getChildComponent(i);
                        if (! c->isVisible()) continue;
                        const auto cb = c->getBounds();
                        if (cb.isEmpty() || cb.getX() < 0 || cb.getY() < 0
                            || cb.getRight() > zw || cb.getBottom() > zh)
                        {
                            ok = false;
                            detail << c->getName() << cb.toString() << "  ";
                        }
                    }

                    // the layout must be a pure tiling: no overlaps ever
                    for (int i = 0; i < mc.getNumChildComponents() && ok; ++i)
                    {
                        auto* a = mc.getChildComponent(i);
                        if (! a->isVisible() || a->getBounds().isEmpty()) continue;
                        for (int j = i + 1; j < mc.getNumChildComponents(); ++j)
                        {
                            auto* c2 = mc.getChildComponent(j);
                            if (c2->isVisible() && ! c2->getBounds().isEmpty()
                                && a->getBounds().intersects(c2->getBounds()))
                            {
                                ok = false;
                                detail << "OVERLAP " << a->getName() << "/"
                                       << c2->getName() << " "
                                       << a->getBounds().toString() << " vs "
                                       << c2->getBounds().toString() << "  ";
                                break;
                            }
                        }
                    }

                    // named key controls must keep a usable size
                    for (auto* n : watch)
                    {
                        auto* c = find(n);
                        if (c != nullptr && ! c->isVisible()) continue; // e.g. hints hidden
                        if (c == nullptr || c->getBounds().getWidth() < 8 || c->getBounds().getHeight() < 8
                            || c->getBounds().getRight() > zw || c->getBounds().getBottom() > zh
                            || c->getBounds().getX() < 0 || c->getBounds().getY() < 0)
                        {
                            ok = false;
                            detail << n << "=" << (c != nullptr ? c->getBounds().toString()
                                                                : juce::String("null")) << "  ";
                        }
                    }
                    return ok;
                };
                {
                    struct Sz { int w, h; };
                    const Sz sizes[] = { {1280,800}, {1024,600}, {800,480}, {640,400}, {480,700}, {360,640} };
                    for (auto z : sizes)
                    {
                        juce::String detail;
                        // the layout check must run BEFORE the detail is read:
                        // the order of evaluation of the arguments is not
                        // specified, so reading detail.trim() in the same call
                        // would report an empty string.
                        const bool ok = layoutOk(z.w, z.h, detail);
                        check(ok,
                              juce::String("responsive layout ") + juce::String(z.w)
                              + "x" + juce::String(z.h), detail.trim());
                    }
                }

                // v1.33.3: the toolbar's new layout - the hints have their
                // own sidebar, the groups share the lines, and no button
                // cuts its own text off.
                {
                    mc.setSize (1280, 800);
                    auto* kbd = find ("kbdHintLab");
                    auto* mat = find ("matHintLab");
                    auto* page = find ("pageBox");
                    auto* play = find ("Play");
                    check (kbd != nullptr && mat != nullptr
                           && kbd->isVisible() && mat->isVisible(),
                           "v1.33.3 the hints have their own sidebar (both visible)");
                    check (kbd != nullptr && page != nullptr
                           && kbd->getBounds().getX() > page->getBounds().getRight(),
                           "v1.33.3 the hints sidebar is on the right of the buttons",
                           kbd != nullptr ? kbd->getBounds().toString() : juce::String());
                    check (kbd != nullptr && mat != nullptr
                           && kbd->getBounds().getY() > mat->getBounds().getY(),
                           "v1.33.3 the material hint sits above the shortcut list");
                    // the groups share a line: the transport and the pages
                    // are two groups, and they sit on the same line
                    check (play != nullptr && page != nullptr
                           && play->getBounds().getY() == page->getBounds().getY(),
                           "v1.33.3 two groups share one line",
                           play != nullptr && page != nullptr
                             ? play->getBounds().toString() + " vs " + page->getBounds().toString()
                             : juce::String());

                    // no button may be narrower than its own text
                    {
                        bool allFit = true;
                        juce::String worst;
                        for (auto* b : { find ("sieveBtn"), find ("wSine"), find ("Play") })
                        {
                            auto* tb = dynamic_cast<juce::TextButton*> (b);
                            if (tb == nullptr) continue;
                            const auto font = mc.getLookAndFeel().getTextButtonFont (*tb, tb->getHeight());
                            const int need = juce::GlyphArrangement::getStringWidthInt (font, tb->getButtonText());
                            if (tb->getWidth() < need)
                            {
                                allFit = false;
                                worst << tb->getName() << " " << tb->getWidth() << "<" << need << "  ";
                            }
                        }
                        check (allFit, "v1.33.3 no button cuts its text off", worst.trim());
                    }

                    // the capture button: green, and red while it records
                    {
                        auto* cap = dynamic_cast<juce::TextButton*> (find ("capBtn"));
                        check (cap != nullptr
                               && cap->findColour (juce::TextButton::buttonColourId) == juce::Colour (0xff2a6a3a)
                               && cap->findColour (juce::TextButton::buttonOnColourId) == juce::Colour (0xff6a2a2a),
                               "v1.33.3 the capture button is green, and red while it records");
                    }

                    // the wavetable's shape buttons follow the wave table's
                    // colours - the same palette the arcs use
                    {
                        auto* w0 = dynamic_cast<juce::TextButton*> (find ("wSine"));
                        check (w0 != nullptr
                               && w0->findColour (juce::TextButton::buttonOnColourId) == upic::paletteColour (0),
                               "v1.33.3 the wavetable buttons follow the wave table colours");
                    }

                    // every toolbar control must sit on a card. A group that
                    // is wider than its line wraps inside itself; its card
                    // has to follow it line by line, or the middle of the
                    // group ends up floating on the bare background.
                    {
                        const auto tb = mc.toolbarBoundsForTest();
                        const int nCards = mc.toolbarCardCountForTest();
                        bool allOnCards = true;
                        juce::String loose;
                        int toolbarControls = 0;
                        for (int i = 0; i < mc.getNumChildComponents(); ++i)
                        {
                            auto* c = mc.getChildComponent (i);
                            if (! c->isVisible() || c->getBounds().isEmpty()) continue;
                            if (! tb.contains (c->getBounds())) continue;
                            ++toolbarControls;
                            bool on = false;
                            for (int k = 0; k < nCards && ! on; ++k)
                                if (mc.toolbarCardBoundsForTest (k).contains (c->getBounds()))
                                    on = true;
                            if (! on)
                            {
                                allOnCards = false;
                                loose << c->getName() << c->getBounds().toString() << "  ";
                            }
                        }
                        check (allOnCards && toolbarControls > 0,
                               "v1.33.3 every toolbar control sits on a card",
                               loose.trim());
                    }
                }

                // v1.33: the four frequency spaces - each dropdown has to
                // reach the model, not just the first one.
                {
                    auto* s1 = dynamic_cast<juce::ComboBox*> (find ("spaceDefBox0"));
                    auto* s2 = dynamic_cast<juce::ComboBox*> (find ("spaceDefBox1"));
                    auto* s3 = dynamic_cast<juce::ComboBox*> (find ("spaceDefBox2"));
                    auto* s4 = dynamic_cast<juce::ComboBox*> (find ("spaceDefBox3"));
                    check (s1 != nullptr && s2 != nullptr && s3 != nullptr && s4 != nullptr,
                           "the four space controls exist");
                    if (s2 != nullptr && s3 != nullptr && s4 != nullptr)
                    {
                        // "An octave up" is the fourth preset (id 4)
                        s2->setSelectedId (4, juce::dontSendNotification); s2->onChange();
                        s3->setSelectedId (5, juce::dontSendNotification); s3->onChange();
                        s4->setSelectedId (2, juce::dontSendNotification); s4->onChange();
                        check (mc.pageSpaceForTest (0) == 0
                            && mc.pageSpaceForTest (1) == 3
                            && mc.pageSpaceForTest (2) == 4
                            && mc.pageSpaceForTest (3) == 1,
                               "every space dropdown reaches the model",
                               juce::String (mc.pageSpaceForTest (0)) + ", "
                               + juce::String (mc.pageSpaceForTest (1)) + ", "
                               + juce::String (mc.pageSpaceForTest (2)) + ", "
                               + juce::String (mc.pageSpaceForTest (3)));
                        // back to the defaults
                        s2->setSelectedId (1, juce::dontSendNotification); s2->onChange();
                        s3->setSelectedId (1, juce::dontSendNotification); s3->onChange();
                        s4->setSelectedId (1, juce::dontSendNotification); s4->onChange();
                    }
                }

                // v1.33.2: the toolbar's Sieve checkbox and its m/R fields
                // must reach the canvas. The unit tests above call setSieve()
                // directly, which is exactly how a broken checkbox can hide.
                {
                    // the layout loop above ends at 360x640; the drawing
                    // tests need the full-size canvas so a screen y maps to
                    // the page's real pitch range.
                    mc.setSize (1280, 800);
                    auto* sc = dynamic_cast<ScoreEditor*> (find ("score"));
                    auto* sb = dynamic_cast<juce::ToggleButton*> (find ("sieveBtn"));
                    auto* me = dynamic_cast<juce::TextEditor*> (find ("sieveMEdit"));
                    auto* re = dynamic_cast<juce::TextEditor*> (find ("sieveREdit"));
                    check (sc != nullptr && sb != nullptr && me != nullptr && re != nullptr,
                           "the Sieve checkbox and its m/R fields exist");
                    if (sc != nullptr && sb != nullptr && me != nullptr && re != nullptr)
                    {
                        me->setText ("7", juce::dontSendNotification); me->onTextChange();
                        re->setText ("0,2", juce::dontSendNotification); re->onTextChange();
                        sb->setToggleState (true, juce::dontSendNotification); sb->onClick();
                        check (sc->getSieveOn() && sc->getSieveM() == 7
                               && sc->getSieveR() == std::vector<int>({ 0, 2 }),
                               "the Sieve checkbox and fields reach the canvas",
                               juce::String (sc->getSieveOn() ? "on " : "off ")
                               + juce::String (sc->getSieveM()) + " {"
                               + ScoreEditor::remaindersTextForTest (sc->getSieveR()) + "}");
                        // End-to-end: the checked box must change the drawn
                        // pitches. m12 {0} is the octave sieve: every point
                        // lands on a C. With the box off the same line is
                        // chromatic (the Snap default), so it has non-C's.
                        sc->setSnap (true);
                        sc->setTimeGrid (0.0);
                        sc->setStickiness (1.0);
                        me->setText ("12", juce::dontSendNotification); me->onTextChange();
                        re->setText ("0", juce::dontSendNotification); re->onTextChange();
                        sc->commitLineForTest ({ sc->getWidth() * 0.15f, sc->getHeight() * 0.25f },
                                                { sc->getWidth() * 0.75f, sc->getHeight() * 0.80f });
                        const auto sieved = sc->lastArcFreqsForTest();
                        bool allC = ! sieved.empty();
                        for (double f : sieved)
                        {
                            const double n = upic::freqToMidi (f);
                            const int nn = (int) std::llround (n);
                            if (std::abs (n - (double) nn) > 1e-6
                                || ((nn % 12) + 12) % 12 != 0)
                                allC = false;
                        }
                        check (allC, "the Sieve box changes the drawn pitches (C-only)",
                               juce::String ((int) sieved.size()) + " points, first "
                               + juce::String (sieved.empty() ? 0.0 : upic::freqToMidi (sieved.front()), 2)
                               + "/" + juce::String (sieved.empty() ? 0.0 : upic::freqToMidi (sieved.back()), 2));

                        sb->setToggleState (false, juce::dontSendNotification); sb->onClick();
                        check (! sc->getSieveOn(), "unchecking the Sieve box turns the sieve off");
                        sc->commitLineForTest ({ sc->getWidth() * 0.15f, sc->getHeight() * 0.25f },
                                                { sc->getWidth() * 0.75f, sc->getHeight() * 0.80f });
                        const auto freeFreqs = sc->lastArcFreqsForTest();
                        bool anyNonC = false;
                        for (double f : freeFreqs)
                        {
                            const int nn = (int) std::llround (upic::freqToMidi (f));
                            if (((nn % 12) + 12) % 12 != 0) anyNonC = true;
                        }
                        check (anyNonC, "with the Sieve box off the line is chromatic again");
                        // back to the defaults for the snapshot
                        me->setText ("12", juce::dontSendNotification); me->onTextChange();
                        re->setText ("0", juce::dontSendNotification); re->onTextChange();
                        sb->setToggleState (false, juce::dontSendNotification); sb->onClick();
                        // an empty R field must keep the last valid list: the
                        // placeholder made it look like 0,7 while the real
                        // list was empty, so the checkbox quantised nothing.
                        re->setText ("", juce::dontSendNotification); re->onTextChange();
                        check (sc->getSieveR() == std::vector<int>({ 0 }),
                               "an empty R field keeps the last valid sieve",
                               ScoreEditor::remaindersTextForTest (sc->getSieveR()));
                        re->setText ("0", juce::dontSendNotification); re->onTextChange();
                    }
                }

                // v1.33: a sieve zone can be moved and resized. The zone
                // tool used to let you draw one and delete it, but not
                // touch it afterwards.
                {
                    auto* sc = dynamic_cast<ScoreEditor*> (find ("score"));
                    check (sc != nullptr, "the canvas exists");
                    if (sc != nullptr)
                    {
                        const int zid = sc->addZone (2.0, 4.0, 200.0, 800.0);
                        check (zid > 0, "a zone can be drawn");
                        // its rectangle on screen
                        const auto zr = sc->zoneRectForTest (zid);
                        check (zr.getWidth() > 1.0f && zr.getHeight() > 1.0f,
                               "the zone has a rectangle");

                        // drag it by the middle: it moves
                        const juce::Point<float> mid (zr.getCentreX(), zr.getCentreY());
                        sc->beginZoneDragForTest (mid);
                        const double tBefore = sc->zoneTimeForTest (zid, 0);
                        sc->dragZoneToForTest ({ mid.x + 40.0f, mid.y });
                        const double tAfter = sc->zoneTimeForTest (zid, 0);
                        check (std::abs (tAfter - tBefore) > 0.05,
                               "a zone can be dragged to a new place",
                               juce::String (tBefore, 2) + " s -> "
                               + juce::String (tAfter, 2) + " s");

                        // drag its right edge: it resizes
                        const auto zr2 = sc->zoneRectForTest (zid);
                        const double wBefore = zr2.getWidth();
                        sc->beginZoneDragForTest ({ zr2.getRight(), zr2.getCentreY() });
                        sc->dragZoneToForTest ({ zr2.getRight() + 60.0f, zr2.getCentreY() });
                        const double wAfter = sc->zoneRectForTest (zid).getWidth();
                        check (wAfter > wBefore + 10.0f,
                               "a zone can be resized by its edge",
                               juce::String (wBefore, 0) + " -> "
                               + juce::String (wAfter, 0) + " px");
                        sc->deleteZone (zid);
                    }
                }

                // v1.33.2: every zone-menu action must reach the zone, and a
                // zone's own stick must override the global stick inside it.
                {
                    auto* sc = dynamic_cast<ScoreEditor*> (find ("score"));
                    check (sc != nullptr, "the canvas exists for the zone menu test");
                    if (sc != nullptr)
                    {
                        sc->setSieve (true, 12, { 0, 7 });
                        sc->setTimeGrid (2.0);
                        sc->setStickiness (0.8);
                        const int zid = sc->addZone (1.0, 9.0, 120.0, 900.0);
                        check (zid > 0, "the zone-menu zone can be drawn");
                        check (std::abs (sc->zoneStickinessForTest (zid) - 0.8) < 1e-9,
                               "a new zone inherits the global stick",
                               juce::String (sc->zoneStickinessForTest (zid), 2));

                        // use the toolbar's current sieve
                        sc->setSieve (true, 5, { 1, 3 });
                        sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuSieveCurrent);
                        check (sc->zoneSieveMForTest (zid) == 5
                               && sc->zoneSieveRForTest (zid) == std::vector<int>({ 1, 3 }),
                               "zone menu: use the current sieve reaches the zone");

                        // a sieve preset (Major is index 2)
                        sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuSievePreset + 2);
                        check (sc->zoneSieveMForTest (zid) == 12
                               && sc->zoneSieveRForTest (zid)
                                      == std::vector<int>({ 0, 2, 4, 5, 7, 9, 11 }),
                               "zone menu: a sieve preset reaches the zone",
                               "m" + juce::String (sc->zoneSieveMForTest (zid)) + " {"
                               + ScoreEditor::remaindersTextForTest (sc->zoneSieveRForTest (zid)) + "}");

                        // a time grid (4/s is index 3)
                        sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuGridChoice + 3);
                        check (std::abs (sc->zoneTimeGridForTest (zid) - 4.0) < 1e-9,
                               "zone menu: a time grid reaches the zone");

                        // stick 0% -> the page's global stick (1.0) is ignored
                        sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuStickChoice);
                        check (std::abs (sc->zoneStickinessForTest (zid)) < 1e-9,
                               "zone menu: a stick value reaches the zone");
                        sc->setStickiness (1.0);
                        double t = 5.0, f = 450.0;
                        sc->quantizeXY (t, f);
                        check (std::abs (f - 450.0) < 0.5,
                               "a zone's own stick overrides the global stick",
                               juce::String (f, 1) + " Hz");

                        // follow the global stick again -> the major sieve bites
                        sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuStickFollow);
                        check (sc->zoneStickinessForTest (zid) < 0.0,
                               "zone menu: follow the global stick");
                        t = 5.0; f = 450.0;
                        sc->quantizeXY (t, f);
                        check (std::abs (f - 440.0) < 0.6,
                               "follow-global uses the global stick",
                               juce::String (f, 1) + " Hz, stick "
                               + juce::String (sc->zoneStickinessForTest (zid), 2)
                               + ", m" + juce::String (sc->zoneSieveMForTest (zid))
                               + " {" + ScoreEditor::remaindersTextForTest (sc->zoneSieveRForTest (zid)) + "}");

                        // delete through the menu
                        sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuDelete);
                        check (sc->zoneSieveMForTest (zid) == -1,
                               "zone menu: delete removes the zone");
                        sc->setStickiness (1.0);
                        sc->setSieve (false, 12, { 0 });
                        sc->setTimeGrid (0.0);
                    }
                }

                // v1.33: a quantised line must keep its points in order.
                // The grid snapping can pull a later point in front of an
                // earlier one; the engine's segment search assumes the
                // points are sorted, and a line whose points were not came
                // out distorted.
                {
                    auto* sc = dynamic_cast<ScoreEditor*> (find ("score"));
                    check (sc != nullptr, "the canvas exists for the line test");
                    if (sc != nullptr)
                    {
                        sc->setSieve (true, 12, { 0 });
                        sc->setTimeGrid (2.0);
                        sc->setStickiness (1.0);
                        sc->commitLineForTest ({ 60.0f, 60.0f }, { 340.0f, 300.0f });
                        check (sc->lastArcPointsSortedForTest(),
                               "a quantised line keeps its points in order");
                        check (sc->lastArcHasStaircaseForTest(),
                               "a magnetised line is a staircase, not a ramp");
                        sc->setStickiness (0.5);
                        sc->commitLineForTest ({ 60.0f, 60.0f }, { 340.0f, 300.0f });
                        check (! sc->lastArcHasStaircaseForTest(),
                               "a soft stick leaves the line smooth (no fake steps)");
                        sc->setStickiness (1.0);
                        sc->setSieve (false, 12, { 0 });
                        sc->setTimeGrid (0.0);
                    }
                }

                // v1.33: the ambitus reaches the canvas - the page's own
                // interval, not the whole world.
                {
                    auto* amb = dynamic_cast<juce::ComboBox*> (find ("ambitusBox"));
                    auto* sc  = dynamic_cast<ScoreEditor*> (find ("score"));
                    check (amb != nullptr && sc != nullptr,
                           "the ambitus control and the canvas exist");
                    if (amb != nullptr && sc != nullptr)
                    {
                        // "A semitone (C4 - C#4)" - the last preset. The
                        // combo's change notification is asynchronous and the
                        // selftest runs no message loop, so the handler is
                        // called directly.
                        amb->setSelectedId (6, juce::dontSendNotification);
                        amb->onChange();
                        const double lo = sc->ambitusLo(), hi = sc->ambitusHi();
                        check (std::abs (lo - 261.63) < 1e-6 && hi > lo && hi < 300.0,
                               "the ambitus control sets the page's interval",
                               juce::String (lo, 1) + " - " + juce::String (hi, 1) + " Hz");
                        // the canvas maps through it, both ways
                        const double mid = std::sqrt (lo * hi);
                        check (std::abs (sc->yToFreq (sc->freqToY (mid)) - mid) < 1e-6,
                               "the canvas maps the page's interval");
                        // and a pitch outside the page is off the page: it
                        // cannot be drawn back
                        const double outside = sc->yToFreq (sc->freqToY (mid * 4.0));
                        check (outside <= hi + 1e-6 && outside >= lo - 1e-6,
                               "a pitch outside the page cannot be drawn on it",
                               juce::String (outside, 1) + " Hz");
                        // back to the default interval for the snapshot
                        amb->setSelectedId (1, juce::dontSendNotification);
                        amb->onChange();
                        check (std::abs (sc->ambitusLo() - upic::kDefaultPitchLo) < 1e-6,
                               "the ambitus goes back to the default interval");
                    }
                }

                // v1.26: every material tab shows only its own controls and
                // still tiles cleanly. This catches controls that are in the
                // flow row but were never added to the component tree.
                {
                    // the tab toggles are named by their text (JUCE Button
                    // passes the button text to Component as its name)
                    struct TabCheck { const char* button; const char* tab; const char* shown; const char* hidden; };
                    const TabCheck tabs[] = {
                        { "Osc",    "Osc",    "waveBox",      "sampleBox" },
                        { "Sample", "Sample", "sampleBox",    "waveBox" },
                        { "Live",   "Live",   "livePitchBox", "sampleBox" },
                        { "PM",     "PM",     "pmOpsBox",     "sampleBox" },
                    };
                    for (auto t : tabs)
                    {
                        if (auto* tab = dynamic_cast<juce::Button*> (find (t.button)))
                        {
                            tab->setToggleState (true, juce::dontSendNotification);
                            tab->onClick();
                        }
                        juce::String detail;
                        check (layoutOk (1280, 800, detail),
                               juce::String ("tab layout ") + t.tab, detail.trim());
                        auto* shown = find (t.shown);
                        auto* hidden = find (t.hidden);
                        check (shown != nullptr && shown->isVisible(),
                               juce::String ("tab ") + t.tab + " shows " + t.shown);
                        check (hidden != nullptr && ! hidden->isVisible(),
                               juce::String ("tab ") + t.tab + " hides " + t.hidden);
                    }
                    if (auto* tab = dynamic_cast<juce::Button*> (find ("Osc")))
                    {
                        tab->setToggleState (true, juce::dontSendNotification);
                        tab->onClick();
                    }
                }

                // v1.25: the sample input/capture group is Sample-only, and
                // the sieve controls sit with the sieve preset on one row.
                {
                    auto clickTab = [&](const char* n)
                    {
                        if (auto* t = dynamic_cast<juce::Button*> (find (n)))
                        {
                            t->setToggleState (true, juce::dontSendNotification);
                            t->onClick();
                        }
                    };
                    auto* in  = find ("inGainSlider");
                    auto* mon = find ("Mon");
                    auto* cap = find ("capBtn");   // the text changes to "Stop" while it records
                    auto* sb  = find ("sieveBtn");
                    auto* sp  = find ("sievePresetBox");
                    auto* sm  = find ("sieveMEdit");
                    auto* sr  = find ("sieveREdit");

                    clickTab ("Sample");
                    check (in != nullptr && mon != nullptr && cap != nullptr
                           && in->isVisible() && mon->isVisible() && cap->isVisible(),
                           "Sample tab shows the input/capture group");
                    check (sb != nullptr && sp != nullptr && sm != nullptr && sr != nullptr
                           && sb->getY() == sp->getY() && sm->getY() == sp->getY()
                           && sr->getY() == sp->getY(),
                           "sieve controls share the preset row",
                           sb != nullptr && sp != nullptr && sm != nullptr && sr != nullptr
                               ? juce::String (sb->getY()) + "/" + juce::String (sp->getY())
                                 + "/" + juce::String (sm->getY()) + "/" + juce::String (sr->getY())
                               : juce::String ("missing"));

                    const char* others[] = { "Osc", "Live", "PM" };
                    for (auto* n : others)
                    {
                        clickTab (n);
                        check (in != nullptr && mon != nullptr && cap != nullptr
                               && ! in->isVisible() && ! mon->isVisible() && ! cap->isVisible(),
                               juce::String (n) + " hides the input/capture group");
                    }
                    clickTab ("Osc");
                }

                // PM tab snapshot: the densest material row, kept for visual
                // inspection of labels, text boxes and wrapping.
                {
                    if (auto* tab = dynamic_cast<juce::Button*> (find ("PM")))
                    {
                        tab->setToggleState (true, juce::dontSendNotification);
                        tab->onClick();
                    }
                    mc.setSize (1280, 800);
                    juce::Image pi (juce::Image::RGB, 1280, 800, true);
                    {
                        juce::Graphics pg (pi);
                        mc.paintEntireComponent (pg, true);
                    }
                    auto pf = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("upic_snapshot_pm.png");
                    pf.deleteFile();
                    bool pw = false;
                    if (auto os = pf.createOutputStream())
                    {
                        juce::PNGImageFormat fmt;
                        pw = fmt.writeImageToStream (pi, *os);
                        os->flush();
                    }
                    check (pw && pf.getSize() > 15000, "PM tab snapshot render",
                           juce::String (pf.getSize()) + " bytes -> " + pf.getFullPathName());
                    if (auto* tab = dynamic_cast<juce::Button*> (find ("Osc")))
                    {
                        tab->setToggleState (true, juce::dontSendNotification);
                        tab->onClick();
                    }
                }

                // --- v1.19: the Reverse button (the selected slot plays
                //     backwards) must be there and must toggle ---
                {
                    auto* rb = dynamic_cast<juce::TextButton*>(find("revBtn"));
                    check(rb != nullptr, "the Reverse button is present");
                    if (rb != nullptr)
                    {
                        check(rb->getButtonText() == "Rev",
                              "the Reverse button starts off",
                              rb->getButtonText());
                        // a real click toggles the button and then calls onClick
                        rb->setToggleState(true, juce::dontSendNotification);
                        rb->onClick();
                        check(rb->getButtonText() == "Rev ON",
                              "the Reverse button turns on",
                              rb->getButtonText());
                        check(rb->getToggleState(), "the Reverse button is toggled on");
                        rb->setToggleState(false, juce::dontSendNotification);
                        rb->onClick();
                        check(rb->getButtonText() == "Rev" && ! rb->getToggleState(),
                              "the Reverse button turns off again",
                              rb->getButtonText());
                    }
                }

                // --- v1.7 view toggles: hiding the bottom editors and/or the
                //     hints must keep every visible view in a valid layout ---
                {
                    auto* wt = dynamic_cast<juce::Button*>(find("Wave"));
                    auto* et = dynamic_cast<juce::Button*>(find("Env"));
                    auto* ht = dynamic_cast<juce::Button*>(find("Hints"));
                    check(wt != nullptr && et != nullptr && ht != nullptr,
                          "view toggles present (Wave / Env / Hints)");
                    if (wt != nullptr && et != nullptr && ht != nullptr)
                    {
                        auto setTog = [](juce::Button* b, bool on) {
                            b->setToggleState(on, juce::dontSendNotification);
                            if (b->onClick) b->onClick();
                        };
                        setTog(wt, false);
                        setTog(et, false);
                        setTog(ht, false);
                        auto* normBtn = find("Norm");
                        auto* strBtn  = find("Strings");
                        auto* kbd     = find("kbdHintLab");
                        check(normBtn != nullptr && ! normBtn->isVisible()
                              && strBtn != nullptr && ! strBtn->isVisible()
                              && kbd != nullptr && ! kbd->isVisible(),
                              "view toggles hide editors + hints");
                        juce::String detail;
                        bool ok = layoutOk(640, 400, detail);
                        ok = layoutOk(360, 640, detail) && ok;
                        check(ok, "hidden views: layout still tiles", detail.trim());

                        // offscreen snapshot of the hidden views for eyeballing
                        {
                            mc.setSize(1280, 800);
                            juce::Image hi(juce::Image::RGB, 1280, 800, true);
                            {
                                juce::Graphics hg(hi);
                                mc.paintEntireComponent(hg, true);
                            }
                            auto hf = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                          .getChildFile("upic_snapshot_noviews.png");
                            hf.deleteFile();
                            bool hw = false;
                            if (auto os = hf.createOutputStream())
                            {
                                juce::PNGImageFormat fmt;
                                hw = fmt.writeImageToStream(hi, *os);
                                os->flush();
                            }
                            check(hw && hf.getSize() > 15000, "hidden views snapshot render",
                                  juce::String(hf.getSize()) + " bytes -> " + hf.getFullPathName());
                        }

                        setTog(wt, true);
                        setTog(et, true);
                        setTog(ht, true);
                        juce::String d2;
                        check(layoutOk(1280, 800, d2), "views restored at 1280x800", d2.trim());
                    }
                    mc.setSize(1280, 800); // design size for the checks below
                }

                // --- v1.8 the one tab strip: Osc | Sample | Live - each tab
                //     is a material and its own canvas layer (the old Score /
                //     Live pitch buttons and the Material dropdown are gone) ---
                {
                    auto* mOsc = dynamic_cast<juce::Button*>(find("Osc"));
                    auto* mSam = dynamic_cast<juce::Button*>(find("Sample"));
                    auto* mLiv = dynamic_cast<juce::Button*>(find("Live"));
                    check(mOsc != nullptr && mSam != nullptr && mLiv != nullptr,
                          "material tabs present (Osc / Sample / Live)");
                    if (mOsc != nullptr && mSam != nullptr && mLiv != nullptr)
                    {
                        check(mOsc->getToggleState(), "Osc tab is the default layer");
                        mSam->setToggleState(true, juce::dontSendNotification);
                        mSam->onClick(); // pick the Sample tab
                        check(mSam->getToggleState() && ! mOsc->getToggleState()
                              && ! mLiv->getToggleState(),
                              "Sample tab is the single active tab");

                        // offscreen snapshot of the Sample tab (speed grid)
                        {
                            juce::Image si(juce::Image::RGB, 1280, 800, true);
                            {
                                juce::Graphics sg(si);
                                mc.paintEntireComponent(sg, true);
                            }
                            auto sf = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                          .getChildFile("upic_snapshot_sample.png");
                            sf.deleteFile();
                            bool sw = false;
                            if (auto os = sf.createOutputStream())
                            {
                                juce::PNGImageFormat fmt;
                                sw = fmt.writeImageToStream(si, *os);
                                os->flush();
                            }
                            check(sw && sf.getSize() > 15000, "sample tab snapshot render",
                                  juce::String(sf.getSize()) + " bytes -> " + sf.getFullPathName());
                        }

                        mOsc->setToggleState(true, juce::dontSendNotification);
                        mOsc->onClick(); // back to Osc
                        check(mOsc->getToggleState() && ! mSam->getToggleState(),
                              "Osc tab restores");
                    }
                }

                // --- v1.25: the Live tab always uses the multiply-ratio grid
                //     (the old Hz grid and its toggle are gone) ---
                {
                    auto* sc = dynamic_cast<ScoreEditor*>(find("score"));
                    auto* mLiv = dynamic_cast<juce::Button*>(find("Live"));
                    check(sc != nullptr, "score editor present");
                    check(find("Ratio grid") == nullptr,
                          "the Live ratio-grid toggle is gone");
                    if (sc != nullptr && mLiv != nullptr)
                    {
                        // v1.17: switching tabs is pure UI work - it must not
                        // open the audio device (that blocked the message
                        // thread for seconds on the first Live click)
                        const auto tabT0 = juce::Time::getMillisecondCounterHiRes();
                        mLiv->setToggleState(true, juce::dontSendNotification);
                        mLiv->onClick();
                        const auto tabT1 = juce::Time::getMillisecondCounterHiRes();
                        check(tabT1 - tabT0 < 500.0,
                              "Live tab click does not open the audio device",
                              juce::String(tabT1 - tabT0, 2) + " ms");
                        check(sc->getLayer() == 2, "Live tab activates the live layer");

                        juce::Image li(juce::Image::RGB, 1280, 800, true);
                        {
                            juce::Graphics lg(li);
                            mc.paintEntireComponent(lg, true);
                        }
                        auto lf = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("upic_snapshot_live_ratio.png");
                        lf.deleteFile();
                        bool lw = false;
                        if (auto os = lf.createOutputStream())
                        {
                            juce::PNGImageFormat fmt;
                            lw = fmt.writeImageToStream(li, *os);
                            os->flush();
                        }
                        check(lw && lf.getSize() > 15000, "live ratio grid snapshot render",
                              juce::String(lf.getSize()) + " bytes -> " + lf.getFullPathName());

                        // back to the Osc tab for the rest of the run
                        if (auto* mOsc = dynamic_cast<juce::Button*>(find("Osc")))
                        {
                            mOsc->setToggleState(true, juce::dontSendNotification);
                            mOsc->onClick();
                        }
                        check(sc->getLayer() == 0, "Osc tab restores the layer");
                    }
                }

                // --- v1.10: the sample pitch mode selector sits in the
                //     material row (varispeed default, granular, vocoder) ---
                {
                    auto* sp = dynamic_cast<juce::ComboBox*>(find("samplePitchBox"));
                    check(sp != nullptr && sp->getNumItems() == 4
                          && sp->getSelectedId() == 1,
                          "sample pitch selector present",
                          sp != nullptr ? sp->getText() : juce::String("selector missing"));
                    if (sp != nullptr)
                    {
                        sp->setSelectedId(3, juce::sendNotification); // vocoder
                        check(sp->getSelectedId() == 3 && sp->getText() == "Phase vocoder",
                              "sample pitch selector switches", sp->getText());
                        sp->setSelectedId(4, juce::sendNotification); // granular no stretch
                        check(sp->getSelectedId() == 4
                              && sp->getText() == "Granular (no stretch)",
                              "sample pitch selector adds no-stretch granular", sp->getText());
                        sp->setSelectedId(1, juce::sendNotification); // default on
                    }
                }

                // --- v1.7 audio settings: the in-app overlay must open with a
                //     usable OK button and close again (Android has no dialog
                //     windows, so this is the only way to dismiss it) ---
                {
                    auto* ab = dynamic_cast<juce::Button*>(find("Audio"));
                    check(ab != nullptr, "audio settings button present");
                    if (ab != nullptr)
                    {
                        ab->onClick(); // open
                        auto* ov = find("audioOverlay");
                        auto* ok = dynamic_cast<juce::Button*>(find("OK"));
                        check(ov != nullptr && ov->isVisible()
                              && ok != nullptr && ok->isVisible() && ok->getWidth() >= 8
                              && ov->getBounds().contains(ok->getBounds()),
                              "audio overlay opens with its OK button");
                        // v1.26: the selector is added on top of the overlay's
                        // other children; the OK button must still win the hit
                        // test at its centre (the reported "OK does nothing").
                        auto* hit = (ov != nullptr && ok != nullptr)
                                        ? ov->getComponentAt(ok->getBounds().getCentre())
                                        : nullptr;
                        check(hit == ok, "audio overlay OK is clickable at its centre",
                              hit != nullptr ? hit->getName() : juce::String("no component"));

                        // v1.9: the live pitch algorithm selector lives in the
                        // overlay (granular default, tape, phase vocoder)
                        auto* lp = dynamic_cast<juce::ComboBox*>(find("livePitchBox"));
                        check(lp != nullptr && lp->getNumItems() == 3
                              && lp->getSelectedId() == 1,
                              "audio overlay has the live pitch selector",
                              lp != nullptr ? lp->getText() : juce::String("selector missing"));
                        if (lp != nullptr)
                        {
                            lp->setSelectedId(3, juce::sendNotification); // vocoder
                            check(lp->getSelectedId() == 3 && lp->getText() == "Phase vocoder",
                                  "live pitch selector switches", lp->getText());
                            lp->setSelectedId(1, juce::sendNotification); // default on
                        }

                        // offscreen snapshot of the open overlay for eyeballing
                        {
                            juce::Image ai(juce::Image::RGB, 1280, 800, true);
                            {
                                juce::Graphics ag(ai);
                                mc.paintEntireComponent(ag, true);
                            }
                            auto af = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                          .getChildFile("upic_snapshot_audio.png");
                            af.deleteFile();
                            bool aw = false;
                            if (auto os = af.createOutputStream())
                            {
                                juce::PNGImageFormat fmt;
                                aw = fmt.writeImageToStream(ai, *os);
                                os->flush();
                            }
                            check(aw && af.getSize() > 15000, "audio overlay snapshot render",
                                  juce::String(af.getSize()) + " bytes -> " + af.getFullPathName());
                        }

                        if (ok != nullptr) ok->onClick(); // close
                        check(ov != nullptr && ! ov->isVisible(),
                              "audio overlay OK closes it");
                    }
                }

                // --- piece round trip through the public (de)serialization API ---
                mc.loadFromVar(juce::JSON::parse(
                    "{\"pages\":[{\"duration\":10,\"arcs\":[]}]}")); // legacy: no v1.1/v1.2 keys
                auto legacyOut = mc.pieceToVar();
                check((double) legacyOut.getProperty("stickiness", -1.0) == 1.0
                      && (double) legacyOut.getProperty("timeGrid", -1.0) == 0.0,
                      "legacy piece defaults: full stickiness, no time grid");
                {
                    auto* pgArr = legacyOut.getProperty("pages", juce::var()).getArray();
                    check(pgArr != nullptr && ! pgArr->isEmpty()
                          && ! pgArr->getReference(0).getProperty("zones", juce::var()).isArray(),
                          "legacy piece keeps zones key absent");
                }
                mc.loadFromVar(juce::JSON::parse(
                    "{\"stickiness\":0.45,\"timeGrid\":2,"
                    "\"pages\":[{\"duration\":12,\"zones\":[{\"id\":1,\"t0\":1,\"t1\":5,"
                    "\"f0\":200,\"f1\":900,\"m\":12,\"r\":[0,2,4],\"timeGrid\":4}]}]}"));
                auto out = mc.pieceToVar();
                check(std::abs((double) out.getProperty("stickiness", -1.0) - 0.45) < 1e-9,
                      "piece json round-trips stickiness");
                check(std::abs((double) out.getProperty("timeGrid", -1.0) - 2.0) < 1e-9,
                      "piece json round-trips timeGrid");
                {
                    bool zoneOk = false;
                    if (auto* pgArr = out.getProperty("pages", juce::var()).getArray();
                        pgArr != nullptr && ! pgArr->isEmpty())
                        if (auto* zs = pgArr->getReference(0).getProperty("zones", juce::var()).getArray();
                            zs != nullptr && zs->size() == 1)
                        {
                            const auto& z = zs->getReference(0);
                            zoneOk = (int) z.getProperty("m", 0) == 12
                                  && std::abs((double) z.getProperty("timeGrid", 0.0) - 4.0) < 1e-9
                                  && (double) z.getProperty("t0", -1.0) == 1.0;
                        }
                    check(zoneOk, "piece json round-trips sieve zones");
                }

                mc.setSize(1280, 800);

                // --- v1.2 controls exist with sane bounds ---
                auto findNamed = [&](const juce::String& s) -> juce::Component* {
                    for (int i = 0; i < mc.getNumChildComponents(); ++i)
                        if (mc.getChildComponent(i)->getName() == s)
                            return mc.getChildComponent(i);
                    return nullptr;
                };
                const char* v12names[] = { "score", "genBox", "stickSlider", "gridBox",
                                           "sievePresetBox", "kbdHintLab" };
                for (auto* n : v12names)
                {
                    auto* c = findNamed(n);
                    check(c != nullptr && ! c->getBounds().isEmpty(),
                          juce::String("v1.2 control present: ") + n,
                          c != nullptr ? c->getBounds().toString() : juce::String("not found"));
                }
                if (auto* sc = dynamic_cast<ScoreEditor*>(findNamed("score")))
                {
                    scoreBounds = sc->getBounds();
                    sc->setShowGrid(false); // grid off so pixel probes hit zone/strip paint
                }

                // in-process identity check: which binary made this object?
                auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
                log << "EXE: " << exe.getFullPathName()
                    << "  size=" << exe.getSize() << "\n";
                juce::String kids;
                for (int i = 0; i < mc.getNumChildComponents(); ++i)
                    kids << mc.getChildComponent(i)->getBounds().toString() << "  ";
                log << "CHILDREN(" << mc.getNumChildComponents() << "): " << kids << "\n";
                mc.paintEntireComponent(g, true);

                // --- small-window snapshots (side-by-side AND stacked editor
                //     paths) for eyeball overlap checks ---
                struct Sz { int w, h; };
                const Sz smalls[] = { {640, 400}, {360, 640} };
                for (auto z : smalls)
                {
                    mc.setSize(z.w, z.h);
                    juce::Image sml(juce::Image::RGB, z.w, z.h, true);
                    {
                        juce::Graphics sg(sml);
                        mc.paintEntireComponent(sg, true);
                    }
                    auto smlPng = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("upic_snapshot_" + juce::String(z.w)
                                                    + "x" + juce::String(z.h) + ".png");
                    smlPng.deleteFile(); // createOutputStream() appends on Windows
                    bool wrote = false;
                    if (auto os = smlPng.createOutputStream())
                    {
                        juce::PNGImageFormat fmt;
                        wrote = fmt.writeImageToStream(sml, *os);
                        os->flush();
                    }
                    check(wrote && smlPng.getSize() > 4000,
                          juce::String(z.w) + "x" + juce::String(z.h) + " snapshot render",
                          juce::String(smlPng.getSize()) + " bytes -> " + smlPng.getFullPathName());
                }
                mc.setSize(1280, 800);
            } // Graphics released before encoding
            // JUCE's createOutputStream() APPENDS on Windows (OPEN_ALWAYS +
            // seek FILE_END) — delete first or old PNG data stays in front
            png.deleteFile();
            if (auto os = png.createOutputStream())
            {
                juce::PNGImageFormat fmt;
                written = fmt.writeImageToStream(img, *os);
                os->flush();
            }
            if (written)
                check(png.getSize() > 15000, "ui snapshot non-blank",
                      juce::String(png.getSize()) + " bytes -> " + png.getFullPathName());
            else
                check(false, "ui snapshot write");

            // --- pixel probes on the offscreen image ---
            if (scoreBounds.isEmpty())
            {
                check(false, "score component bounds for pixel probes");
            }
            else
            {
                // plot rect: ScoreEditor margins (top 4, bottom/left/right 6)
                const float px = (float) scoreBounds.getX() + 6.0f;
                const float py = (float) scoreBounds.getY() + 4.0f;
                const float pw = (float) scoreBounds.getWidth() - 12.0f;
                const float phh = (float) scoreBounds.getHeight() - 10.0f;
                // zone (t1..5 of 12s, 200..900Hz) -> pick t=3.07s, f=350Hz.
                // 3.07 (not 3.00) avoids the zone's 2/s time-grid line, and
                // 350 Hz sits between the zone's sieve guide lines.
                const float norm350 = (float) (std::log2(350.0 / 20.0) / std::log2(12000.0 / 20.0));
                const auto zonePx = img.getPixelAt((int) (px + pw * (3.07 / 12.0)),
                                                   (int) (py + phh * (1.0 - norm350)));
                check(zonePx.getRed() >= 34 && zonePx.getRed() <= 46
                      && zonePx.getGreen() >= 52 && zonePx.getGreen() <= 64
                      && zonePx.getBlue() >= 66 && zonePx.getBlue() <= 84,
                      "v1.2 sieve zone painted (translucent)", zonePx.toString());
                // centre scrub line (inside the zone's time range)
                const auto stripPx = img.getPixelAt((int) (px + pw * 0.30f),
                                                     (int) (py + phh * 0.5f - 7.0f));
                check(stripPx.getRed() >= 42 && stripPx.getRed() <= 56
                      && stripPx.getBlue() >= 76 && stripPx.getBlue() <= 92,
                      "centre scrub line painted", stripPx.toString());
                // the unused part of a toolbar line sits on the toolbar's
                // base panel, not on the window background - the cards must
                // not leave black holes between and after them
                const auto panelPx = img.getPixelAt (900, 100);
                check(panelPx == juce::Colour (0xff131720),
                      "v1.33.3 the toolbar has a base panel", panelPx.toString());
            }
        }

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
        }

        // 20. wave 15: the realtime path renders from the published snapshot
        //     with no audio device and no timer tick (the plugin-style manual
        //     processBlock calls that pluginval/hosts make).
        {
            auto mcHolder = std::make_unique<MainComponent>(true);
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);
            mc.prepareToPlay(512, 44100.0);

            const juce::String json =
                "{\"pages\":[{\"duration\":4,\"nextArcId\":9,\"arcs\":["
                "{\"id\":1,\"mode\":0,\"intensity\":1,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":3,\"f\":220}]}]}]}";
            mc.loadFromVar(juce::JSON::parse(json)); // publishes synchronously

            // click Play exactly as the UI does (startPlayback is private)
            bool clicked = false;
            for (auto* c : mc.getChildren())
                if (auto* b = dynamic_cast<juce::Button*>(c))
                    if (b->getButtonText() == "Play") { b->onClick(); clicked = true; break; }
            check(clicked, "lock-free render: found the Play button");

            juce::AudioBuffer<float> out(2, 2048);
            juce::AudioSourceChannelInfo info(&out, 0, out.getNumSamples());
            float outPeak = 0.0f;
            juce::String blockPeaks;
            for (int block = 0; block < 6; ++block)
            {
                out.clear();
                mc.getNextAudioBlock(info);
                float bp = 0.0f;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                        bp = juce::jmax(bp, std::abs(out.getSample(c, i)));
                outPeak = juce::jmax(outPeak, bp);
                blockPeaks += juce::String(bp, 2) + " ";
            }
            check(outPeak > 0.01f && outPeak <= 1.5f,
                  "lock-free render: getNextAudioBlock plays the snapshot",
                  juce::String(outPeak, 3) + " peak; blocks " + blockPeaks);
            check(mc.timerTicks.load() == 0,
                  "lock-free render: no timer tick was needed (plugin-style)",
                  juce::String(mc.timerTicks.load()) + " ticks");

            // --- the page switch: the audio thread must adopt the new page
            //     (the page epoch), and the piece must end cleanly ---
            {
                const juce::String twoPages =
                    "{\"pages\":["
                    "{\"duration\":1,\"nextArcId\":9,\"arcs\":[{\"id\":1,\"mode\":0,\"intensity\":1,"
                    "\"points\":[{\"t\":0,\"f\":220},{\"t\":1,\"f\":220}]}]},"
                    "{\"duration\":1,\"nextArcId\":9,\"arcs\":[{\"id\":2,\"mode\":0,\"intensity\":1,"
                    "\"points\":[{\"t\":0,\"f\":880},{\"t\":1,\"f\":880}]}]}]}";
                mc.loadFromVar(juce::JSON::parse(twoPages));
                // click Play again (the piece was reloaded, the transport stopped)
                for (auto* c : mc.getChildren())
                    if (auto* b = dynamic_cast<juce::Button*>(c))
                        if (b->getButtonText() == "Play") { b->onClick(); break; }

                // render the first page: it must sound (220 Hz arc)
                out.clear();
                mc.getNextAudioBlock(info);
                float p1 = 0.0f;
                for (int i = 0; i < out.getNumSamples(); ++i)
                    p1 = juce::jmax(p1, std::abs(out.getSample(0, i)));
                check(p1 > 0.01f, "page switch: the first page renders",
                      juce::String(p1, 3) + " peak");

                // switch to page 2 through the page box (the UI path)
                for (int i = 0; i < mc.getNumChildComponents(); ++i)
                {
                    auto* cb = dynamic_cast<juce::ComboBox*>(mc.getChildComponent(i));
                    if (cb == nullptr || cb->getName() != "pageBox") continue;
                    cb->setSelectedId(2, juce::dontSendNotification);
                    if (cb->onChange) cb->onChange();
                    break;
                }
                // The first block after the change is faded out on purpose (a
                // page change replaces the sound -> fade across it), so render
                // a few blocks and look at the last one.
                float p2 = 0.0f;
                int crossings = 0;
                for (int block = 0; block < 3; ++block)
                {
                    out.clear();
                    mc.getNextAudioBlock(info);
                    p2 = 0.0f;
                    float prev = out.getSample(0, 0);
                    crossings = 0;
                    for (int i = 1; i < out.getNumSamples(); ++i)
                    {
                        const float v = out.getSample(0, i);
                        p2 = juce::jmax(p2, std::abs(v));
                        if ((prev < 0.0f) != (v < 0.0f)) ++crossings;
                        prev = v;
                    }
                }
                check(p2 > 0.01f, "page switch: the audio thread adopts the new page",
                      juce::String(p2, 3) + " peak");
                // page 2 holds an 880 Hz arc (page 1 had 220 Hz)
                const double hz = (double) crossings * 0.5 * 44100.0 / (double) out.getNumSamples();
                check(hz > 600.0 && hz < 1200.0,
                      "page switch: the new page is what is sounding",
                      juce::String(hz, 0) + " Hz (want ~880)");
            }
        }

        // 20a. v1.28: the phase-vocoder scrub preview. A drag must be
        //      audible on its first block and must stay continuous between
        //      mouse moves; the engine pre-rolls the vocoder and MainComponent
        //      keeps its stream alive while the drag continues.
        {
            MainComponent mc(true);
            mc.setSize(1280, 800);
            mc.prepareToPlay(512, 44100.0);

            upic::SampleSlot slot;
            slot.id = 1; slot.name = "scrub sine"; slot.sampleRate = 44100.0;
            juce::AudioBuffer<float> sb(1, 44100);
            for (int i = 0; i < sb.getNumSamples(); ++i)
                sb.setSample(0, i, 0.8f * (float) std::sin(
                    2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0));
            slot.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(sb));

            auto* root = new juce::DynamicObject();
            juce::Array<juce::var> samples;
            samples.add(slot.toVar());
            root->setProperty("samples", samples);

            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 1; a.sampleId = 1; a.refFreq = 220.0;
            a.intensity = 1.0f;
            a.points = { { 0.0, 220.0 }, { 4.0, 220.0 } };
            pg.arcs.push_back(a);
            juce::Array<juce::var> pages;
            pages.add(pg.toVar());
            root->setProperty("pages", pages);
            mc.loadFromVar(juce::var(root));

            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };
            auto* sp = dynamic_cast<juce::ComboBox*>(findByName(mc, "samplePitchBox"));
            check(sp != nullptr, "scrub preview: sample pitch selector present");
            if (sp != nullptr)
                sp->setSelectedId(3, juce::sendNotification); // phase vocoder

            std::function<ScoreEditor*(juce::Component&)> findScore =
                [&findScore](juce::Component& c) -> ScoreEditor* {
                    if (auto* s = dynamic_cast<ScoreEditor*>(&c)) return s;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findScore(*c.getChildComponent(i))) return r;
                    return nullptr;
                };
            ScoreEditor* sc = findScore(mc);
            check(sc != nullptr, "scrub preview: score editor present");
            if (sc != nullptr)
            {
                juce::AudioBuffer<float> out(2, 512);
                juce::AudioSourceChannelInfo info(&out, 0, out.getNumSamples());
                auto blockPeak = [&]() {
                    out.clear();
                    mc.getNextAudioBlock(info);
                    float p = 0.0f;
                    for (int c = 0; c < out.getNumChannels(); ++c)
                        for (int i = 0; i < out.getNumSamples(); ++i)
                            p = juce::jmax(p, std::abs(out.getSample(c, i)));
                    return p;
                };

                sc->scrubTo(sc->timeToX(0.5));
                const float first = blockPeak();
                check(first > 0.01f,
                      "scrub preview: phase vocoder is audible on the first block",
                      juce::String(first, 4));

                float minDrag = 1.0f;
                for (int step = 1; step <= 8; ++step)
                {
                    sc->scrubTo(sc->timeToX(0.5 + 0.04 * step));
                    minDrag = juce::jmin(minDrag, blockPeak());
                }
                check(minDrag > 0.005f,
                      "scrub preview: continuous vocoder drag stays audible",
                      juce::String(minDrag, 4));

                // The freeze sustain: a click without a drag holds the page.
                // The editor calls onScrubFreezeStart, the app renders the
                // held score into the freeze buffer (v1.30: that path moved
                // out of getNextAudioBlock() into renderScrubFreeze()).
                if (sc->onScrubFreezeStart != nullptr)
                {
                    sc->onScrubFreezeStart (0.5);
                    juce::Thread::sleep (60); // the crossfade from the preview
                    float held = 0.0f;
                    for (int i = 0; i < 60; ++i) // ...and past the short preview
                        held = juce::jmax (held, blockPeak());
                    check (held > 0.001f,
                           "scrub freeze: the held page keeps sounding",
                           juce::String(held, 4));
                }
            }
        }

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

        // 21. v1.21 (wave 19): the arc list - the inventory of the arcs, with
        //     the per-arc controls (the Rev box and the mode dropdown)
        {
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);
            const juce::String json =
                "{\"pages\":["
                "{\"duration\":4,\"nextArcId\":9,\"arcs\":["
                "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"intensity\":0.8,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":880}]}]},"
                "{\"duration\":4,\"nextArcId\":9,\"arcs\":["
                "{\"id\":2,\"mode\":2,\"refFreq\":220,\"intensity\":0.8,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":3,\"f\":440}]},"
                "{\"id\":3,\"mode\":2,\"refFreq\":330,\"intensity\":0.7,"
                "\"points\":[{\"t\":0,\"f\":330},{\"t\":1,\"f\":660}]}]},"
                "{\"duration\":4,\"nextArcId\":9,\"arcs\":["
                "{\"id\":4,\"mode\":3,\"pmRatio\":3.5,\"pmIndex\":4.5,\"pmIndexEnv\":1,"
                "\"pmFeedback\":0.2,\"pmOps\":4,\"intensity\":0.6,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":1,\"f\":220}]}]}]}";
            mc.loadFromVar(juce::JSON::parse(json));

            // open the panel
            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };
            auto* ab = dynamic_cast<juce::TextButton*>(findByName(mc, "arcListBtn"));
            check(ab != nullptr, "the arc list toggle is present");
            if (ab != nullptr)
            {
                ab->setToggleState(true, juce::dontSendNotification);
                ab->onClick();
            }
            // the panel is a child of the component
            bool found = false;
            for (int i = 0; i < mc.getNumChildComponents() && ! found; ++i)
                found = dynamic_cast<ArcListPanel*>(mc.getChildComponent(i)) != nullptr;
            check(found, "the arc list panel is in the component");

            // a snapshot of the panel (for eyeballing)
            {
                ArcListPanel* panel = nullptr;
                for (int i = 0; i < mc.getNumChildComponents() && panel == nullptr; ++i)
                    panel = dynamic_cast<ArcListPanel*>(mc.getChildComponent(i));
                if (panel != nullptr)
                {
                    mc.timerCallback();          // build the rows
                    const juce::File png = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                               .getChildFile("upic_snapshot_arclist.png");
                    juce::Image hi = panel->createComponentSnapshot (panel->getLocalBounds(), false);
                    if (hi.isValid())
                    {
                        if (auto os = png.createOutputStream())
                        {
                            juce::PNGImageFormat fmt;
                            fmt.writeImageToStream (hi, *os);
                            os->flush();
                        }
                    }
                    // and the whole window with the panel open
                    {
                        const juce::File png2 = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                    .getChildFile("upic_snapshot_arclist_window.png");
                        mc.setSize (1280, 800);
                        mc.resized();
                        juce::Image hi2 = mc.createComponentSnapshot (mc.getLocalBounds(), false);
                        if (hi2.isValid())
                            if (auto os = png2.createOutputStream())
                            {
                                juce::PNGImageFormat fmt;
                                fmt.writeImageToStream (hi2, *os);
                                os->flush();
                            }
                    }
                }
            }

            // v1.26: the arc list selects across layers and supports a
            // Ctrl/Cmd multi-selection. Selecting a PM arc loads its PM
            // controls; PM rows no longer show the sample/live mode menu.
            {
                ArcListPanel* panel = nullptr;
                for (int i = 0; i < mc.getNumChildComponents() && panel == nullptr; ++i)
                    panel = dynamic_cast<ArcListPanel*>(mc.getChildComponent(i));
                auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
                check(panel != nullptr && sc != nullptr, "arc list selection test setup");
                if (panel != nullptr && sc != nullptr)
                {
                    mc.timerCallback();
                    bool pmRow = false, pmMode = true;
                    for (const auto& r : panel->getRows())
                        if (r.kind == ArcListPanel::Row::arc && r.arcId == 4)
                        { pmRow = true; pmMode = r.showMode; }
                    check(pmRow && ! pmMode, "PM arcs have no sample/live mode menu");

                    panel->onArcClicked(2, 1, false);
                    check(sc->getLayer() == 2, "arc list selection switches the canvas layer");
                    check(sc->getSelectedArc() == 2, "arc list selection selects the arc");
                    auto* liveTab = dynamic_cast<juce::Button*>(findByName(mc, "Live"));
                    check(liveTab != nullptr && liveTab->getToggleState(),
                          "arc list selection checks the Live tab");

                    panel->onArcClicked(3, 1, true);
                    check(sc->getSelectedArcs().size() == 2
                          && panel->getSelectedArcs().size() == 2,
                          "arc list ctrl-click multi-selects");

                    panel->onArcClicked(4, 2, false);
                    auto* pr = dynamic_cast<juce::Slider*>(findByName(mc, "pmRatioSlider"));
                    auto* pi = dynamic_cast<juce::Slider*>(findByName(mc, "pmIndexSlider"));
                    check(pr != nullptr && pi != nullptr
                          && std::abs(pr->getValue() - 3.5) < 1e-6
                          && std::abs(pi->getValue() - 4.5) < 1e-6,
                          "arc list selection loads the PM ratio/index",
                          pr != nullptr && pi != nullptr
                              ? juce::String(pr->getValue()) + "/" + juce::String(pi->getValue())
                              : juce::String("missing"));
                    auto* pmTab = dynamic_cast<juce::Button*>(findByName(mc, "PM"));
                    check(pmTab != nullptr && pmTab->getToggleState(),
                          "arc list selection checks the PM tab");
                }
            }
        }

        // 21b. v1.26: the toolbar is a live editor for the selected arcs.
        // Wave / intensity changes apply immediately, Ref retunes the selected
        // arc, and the Ref control is also present on the Live tab.
        {
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);
            const juce::String json =
                "{\"pages\":[{\"duration\":4,\"nextArcId\":9,\"arcs\":["
                "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":880}]},"
                "{\"id\":2,\"mode\":1,\"sampleId\":1,\"refFreq\":220,\"intensity\":0.7,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]},"
                "{\"id\":3,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]},"
                "{\"id\":4,\"mode\":1,\"sampleId\":1,\"refFreq\":220,\"intensity\":0.7,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]}]}]}";
            mc.loadFromVar(juce::JSON::parse(json));

            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };
            auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
            auto* wave = dynamic_cast<juce::ComboBox*>(findByName(mc, "waveBox"));
            auto* inten = dynamic_cast<juce::Slider*>(findByName(mc, "intenSlider"));
            auto* ref = dynamic_cast<juce::Slider*>(findByName(mc, "refSlider"));
            auto* sample = dynamic_cast<juce::ComboBox*>(findByName(mc, "sampleBox"));
            check(sc != nullptr && wave != nullptr && inten != nullptr && ref != nullptr
                  && sample != nullptr,
                  "toolbar live-edit test setup");
            if (sc != nullptr && wave != nullptr && inten != nullptr && ref != nullptr
                && sample != nullptr)
            {
                auto clickTab = [&](const char* n)
                {
                    if (auto* t = dynamic_cast<juce::Button*>(findByName(mc, n)))
                    {
                        t->setToggleState(true, juce::dontSendNotification);
                        t->onClick();
                    }
                };
                clickTab("Osc");
                sc->setSelectedArcs({1, 3});
                check(sc->getLayer() == 0 && sc->getSelectedArcs().size() == 2,
                      "toolbar live-edit setup selects the osc arcs",
                      "layer " + juce::String(sc->getLayer()) + " sel "
                          + juce::String((int) sc->getSelectedArcs().size()));
                wave->setSelectedId(3, juce::sendNotificationSync); // waveform id 2
                inten->setValue(0.5, juce::sendNotificationSync);
                auto out = mc.pieceToVar();
                auto* pagesArr = out.getProperty("pages", juce::var()).getArray();
                auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                    ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                    : nullptr;
                double wid1 = -1.0, wid3 = -1.0, amp1 = -1.0, amp3 = -1.0;
                if (arcsArr != nullptr)
                    for (const auto& av : *arcsArr)
                    {
                        const int id = (int) av.getProperty("id", -1);
                        if (id == 1)
                        {
                            wid1 = (double) av.getProperty("waveformId", -1.0);
                            amp1 = (double) av.getProperty("intensity", -1.0);
                        }
                        else if (id == 3)
                        {
                            wid3 = (double) av.getProperty("waveformId", -1.0);
                            amp3 = (double) av.getProperty("intensity", -1.0);
                        }
                    }
                check(std::abs(wid1 - 2.0) < 1e-6 && std::abs(amp1 - 0.5) < 1e-6
                      && std::abs(wid3 - 2.0) < 1e-6 && std::abs(amp3 - 0.5) < 1e-6,
                      "toolbar wave/intensity edit all selected arcs",
                      juce::String(wid1) + "/" + juce::String(amp1) + " "
                          + juce::String(wid3) + "/" + juce::String(amp3));

                clickTab("Sample");
                check(ref->isVisible() && ref->getWidth() > 0,
                      "the Sample tab has the Ref control");
                sc->setSelectedArcs({2, 4});
                ref->setValue(330.0, juce::sendNotificationSync);
                out = mc.pieceToVar();
                pagesArr = out.getProperty("pages", juce::var()).getArray();
                arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                              ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                              : nullptr;
                double rf2 = -1.0, rf4 = -1.0;
                if (arcsArr != nullptr)
                    for (const auto& av : *arcsArr)
                    {
                        const int id = (int) av.getProperty("id", -1);
                        if (id == 2) rf2 = (double) av.getProperty("refFreq", -1.0);
                        else if (id == 4) rf4 = (double) av.getProperty("refFreq", -1.0);
                    }
                check(std::abs(rf2 - 330.0) < 1e-6 && std::abs(rf4 - 330.0) < 1e-6,
                      "Ref retunes all selected sample arcs",
                      juce::String(rf2) + "/" + juce::String(rf4));

                sample->setSelectedId(2, juce::sendNotificationSync); // slot id 2
                out = mc.pieceToVar();
                pagesArr = out.getProperty("pages", juce::var()).getArray();
                arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                              ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                              : nullptr;
                double sid2 = -1.0, sid4 = -1.0;
                if (arcsArr != nullptr)
                    for (const auto& av : *arcsArr)
                    {
                        const int id = (int) av.getProperty("id", -1);
                        if (id == 2) sid2 = (double) av.getProperty("sampleId", -1.0);
                        else if (id == 4) sid4 = (double) av.getProperty("sampleId", -1.0);
                    }
                check(std::abs(sid2 - 2.0) < 1e-6 && std::abs(sid4 - 2.0) < 1e-6,
                      "the sample slot applies to all selected sample arcs",
                      juce::String(sid2) + "/" + juce::String(sid4));

                clickTab("Live");
                check(ref->isVisible() && ref->getWidth() > 0,
                      "the Live tab has the Ref control");

                // arc ids repeat across pages: a list click must use the
                // row's page, not the first arc with that id
                {
                    const juce::String twoPages =
                        "{\"pages\":["
                        "{\"duration\":4,\"nextArcId\":2,\"arcs\":["
                        "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                        "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":880}]}]},"
                        "{\"duration\":4,\"nextArcId\":2,\"arcs\":["
                        "{\"id\":1,\"mode\":3,\"pmRatio\":3.5,\"pmIndex\":4.5,"
                        "\"pmFeedback\":0.25,\"pmOps\":4,\"intensity\":0.8,"
                        "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]}]}]}";
                    mc.loadFromVar(juce::JSON::parse(twoPages));
                    mc.timerCallback();
                    ArcListPanel* panel = nullptr;
                    for (int i = 0; i < mc.getNumChildComponents() && panel == nullptr; ++i)
                        panel = dynamic_cast<ArcListPanel*>(mc.getChildComponent(i));
                    auto* pr = dynamic_cast<juce::Slider*>(findByName(mc, "pmRatioSlider"));
                    auto* pmTab = dynamic_cast<juce::Button*>(findByName(mc, "PM"));
                    check(panel != nullptr && sc != nullptr && pr != nullptr && pmTab != nullptr,
                          "page-specific arc list test setup");
                    if (panel != nullptr && sc != nullptr && pr != nullptr && pmTab != nullptr)
                    {
                        panel->onArcClicked(1, 1, false);
                        check(sc->getLayer() == 3 && pmTab->getToggleState(),
                              "arc list uses the row's page, not the first matching id");
                        check(std::abs(pr->getValue() - 3.5) < 1e-6,
                              "arc list loads the page's own arc",
                              juce::String(pr->getValue()));
                        check(! panel->isArcSelected(0, 1) && panel->isArcSelected(1, 1),
                              "arc list highlights only the selected page's row");
                    }
                }
            }
        }

        // 21c. v1.26: a generated PM arc is born with the current PM toolbar
        // values (the generator used to leave the per-arc defaults).
        {
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);
            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };
            auto* pmTab = dynamic_cast<juce::Button*>(findByName(mc, "PM"));
            auto* pr = dynamic_cast<juce::Slider*>(findByName(mc, "pmRatioSlider"));
            auto* pi = dynamic_cast<juce::Slider*>(findByName(mc, "pmIndexSlider"));
            auto* fb = dynamic_cast<juce::Slider*>(findByName(mc, "pmFeedbackSlider"));
            auto* ops = dynamic_cast<juce::ComboBox*>(findByName(mc, "pmOpsBox"));
            auto* gen = dynamic_cast<juce::ComboBox*>(findByName(mc, "genBox"));
            check(pmTab != nullptr && pr != nullptr && pi != nullptr && fb != nullptr
                  && ops != nullptr && gen != nullptr,
                  "PM current-voice test setup");
            if (pmTab != nullptr && pr != nullptr && pi != nullptr && fb != nullptr
                && ops != nullptr && gen != nullptr)
            {
                pmTab->setToggleState(true, juce::dontSendNotification);
                pmTab->onClick();
                pr->setValue(3.5, juce::sendNotificationSync);
                pi->setValue(4.5, juce::sendNotificationSync);
                fb->setValue(0.25, juce::sendNotificationSync);
                ops->setSelectedId(2, juce::sendNotificationSync); // 4-op
                gen->setSelectedId(1, juce::sendNotificationSync); // stochastic arborescence

                auto out = mc.pieceToVar();
                auto* pagesArr = out.getProperty("pages", juce::var()).getArray();
                auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                    ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                    : nullptr;
                double ratio = -1.0, index = -1.0, feedback = -1.0;
                int pmOps = -1;
                if (arcsArr != nullptr)
                    for (const auto& av : *arcsArr)
                        if ((int) av.getProperty("mode", -1) == 3)
                        {
                            ratio = (double) av.getProperty("pmRatio", -1.0);
                            index = (double) av.getProperty("pmIndex", -1.0);
                            feedback = (double) av.getProperty("pmFeedback", -1.0);
                            pmOps = (int) av.getProperty("pmOps", -1);
                            break;
                        }
                check(std::abs(ratio - 3.5) < 1e-6 && std::abs(index - 4.5) < 1e-6
                      && std::abs(feedback - 0.25) < 1e-6 && pmOps == 4,
                      "generated PM arcs use the current PM controls",
                      juce::String(ratio) + "/" + juce::String(index) + "/"
                          + juce::String(feedback) + "/" + juce::String(pmOps));
            }
        }

        // 21d. v1.27: the bottom wavetable editor edits a shared wave. With a
        // selection it must fork a private copy instead of changing every arc
        // that names the same wave (the top dropdown still assigns normally).
        {
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);
            const juce::String json =
                "{\"pages\":[{\"duration\":4,\"nextArcId\":3,\"arcs\":["
                "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]},"
                "{\"id\":2,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]}]}]}";
            mc.loadFromVar(juce::JSON::parse(json));
            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };
            auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
            auto* saw = dynamic_cast<juce::Button*>(findByName(mc, "Saw"));
            auto* we = dynamic_cast<WaveformEditor*>(findByName(mc, "waveEd"));
            check(sc != nullptr && saw != nullptr && we != nullptr,
                  "wavetable edit test setup");
            if (sc != nullptr && saw != nullptr && we != nullptr)
            {
                auto arcWids = [&](int& wid1, int& wid2)
                {
                    wid1 = wid2 = -1;
                    auto out = mc.pieceToVar();
                    auto* pagesArr = out.getProperty("pages", juce::var()).getArray();
                    auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                        ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                        : nullptr;
                    if (arcsArr != nullptr)
                        for (const auto& av : *arcsArr)
                        {
                            const int id = (int) av.getProperty("id", -1);
                            if (id == 1) wid1 = (int) av.getProperty("waveformId", -1);
                            else if (id == 2) wid2 = (int) av.getProperty("waveformId", -1);
                        }
                };

                sc->setSelectedArcs({1});
                saw->onClick();
                int wid1 = -1, wid2 = -1;
                arcWids(wid1, wid2);
                auto out = mc.pieceToVar();
                auto* wavesArr = out.getProperty("waveforms", juce::var()).getArray();
                double oldAt256 = 0.0, newAt256 = 0.0;
                if (wavesArr != nullptr)
                    for (const auto& wv : *wavesArr)
                    {
                        const int id = (int) wv.getProperty("id", -1);
                        if (id != 0 && id != wid1) continue;
                        auto* samples = wv.getProperty("samples", juce::var()).getArray();
                        if (samples != nullptr && samples->size() > 256)
                        {
                            const double v = (double) samples->getReference(256);
                            if (id == 0) oldAt256 = v;
                            else newAt256 = v;
                        }
                    }
                check(wid1 != 0 && wid2 == 0,
                      "wavetable shape edit gives the selection a private copy",
                      juce::String(wid1) + "/" + juce::String(wid2));
                check(std::abs(oldAt256 - 1.0) < 0.01 && newAt256 < 0.2,
                      "the old wavetable is untouched and the selection gets the saw",
                      juce::String(oldAt256, 3) + "/" + juce::String(newAt256, 3));

                // Drawing in the same editor uses the same fork path.
                mc.loadFromVar(juce::JSON::parse(json));
                sc->setSelectedArcs({1});
                const auto now = juce::Time::getCurrentTime();
                const auto src = juce::Desktop::getInstance().getMainMouseSource();
                const auto p = juce::Point<float>((float) we->getWidth() * 0.5f,
                                                  (float) we->getHeight() * 0.5f);
                juce::MouseEvent down(src, p, juce::ModifierKeys(), 1.0f,
                                      0.0f, 0.0f, 0.0f, 0.0f, we, we, now, p, now, 1, false);
                we->mouseDown(down);
                we->mouseUp(down);
                arcWids(wid1, wid2);
                check(wid1 != 0 && wid2 == 0,
                      "drawing a wavetable also forks only the selection",
                      juce::String(wid1) + "/" + juce::String(wid2));
            }
        }

        // 21e. v1.27: double-click Stop rewinds; Clear warns and only deletes
        // after the red confirm button is pressed.
        {
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);
            const juce::String json =
                "{\"pages\":[{\"duration\":4,\"nextArcId\":3,\"arcs\":["
                "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]},"
                "{\"id\":2,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]}]}]}";
            mc.loadFromVar(juce::JSON::parse(json));
            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };
            auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
            auto* stop = dynamic_cast<juce::Button*>(findByName(mc, "Stop"));
            auto* time = dynamic_cast<juce::Label*>(findByName(mc, "timeLab"));
            auto* clear = dynamic_cast<juce::Button*>(findByName(mc, "Clear"));
            auto* overlay = dynamic_cast<juce::Component*>(findByName(mc, "clearOverlay"));
            auto* confirm = dynamic_cast<juce::Button*>(findByName(mc, "clearConfirmBtn"));
            auto* cancel = dynamic_cast<juce::Button*>(findByName(mc, "clearCancelBtn"));
            check(sc != nullptr && stop != nullptr && time != nullptr && clear != nullptr
                  && overlay != nullptr && confirm != nullptr && cancel != nullptr,
                  "transport safety test setup");
            if (sc != nullptr && stop != nullptr && time != nullptr && clear != nullptr
                && overlay != nullptr && confirm != nullptr && cancel != nullptr)
            {
                sc->scrubTo(sc->getLocalBounds().toFloat().getCentreX());
                mc.timerCallback();
                const auto before = time->getText();
                stop->onClick();
                stop->onClick();
                mc.timerCallback();
                const auto after = time->getText();
                check(after.startsWith("0.0") && ! before.startsWith("0.0"),
                      "double-click Stop returns to the start",
                      before + " -> " + after);

                check(clear->findColour(juce::TextButton::buttonColourId)
                          == juce::Colour(0xff7a2a2a),
                      "Clear is coloured as a destructive action");
                clear->onClick();
                check(overlay->isVisible(),
                      "Clear opens a warning before deleting");
                auto arcCount = [](const juce::var& v)
                {
                    auto* pagesArr = v.getProperty("pages", juce::var()).getArray();
                    auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                        ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                        : nullptr;
                    return arcsArr != nullptr ? arcsArr->size() : -1;
                };
                check(arcCount(mc.pieceToVar()) == 2,
                      "the warning leaves the arcs intact until confirmation");
                cancel->onClick();
                check(! overlay->isVisible() && arcCount(mc.pieceToVar()) == 2,
                      "Cancel closes the warning and leaves the arcs intact");
                clear->onClick();
                confirm->onClick();
                check(arcCount(mc.pieceToVar()) == 0 && ! overlay->isVisible(),
                      "confirming Clear deletes the page and closes the warning");
            }
        }

        // 22. v1.23 (wave 20): the MIDI input foundation - the device list,
        //     the activity monitor and the learn
        {
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);
            auto& hub = mc.midiHub();

            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };

            auto* dev = dynamic_cast<juce::ComboBox*>(findByName(mc, "midiDevBox"));
            check(dev != nullptr && dev->getNumItems() >= 1 && dev->getItemText(0) == "None",
                  "the MIDI device list is present (None when there is no device)");

            // the learn: a CC binds to the master gain and drives it
            hub.startLearn(MidiHub::Target::masterGain);
            check(hub.isLearning() && hub.learningTarget() == MidiHub::Target::masterGain,
                  "MIDI learn starts on the master gain");
            hub.injectForTest(MidiHub::kTypeCc, 2, 7, 100);
            mc.timerCallback();
            const auto bg = hub.binding(MidiHub::Target::masterGain);
            check(bg.bound && !bg.note && bg.number == 7 && bg.channel == 2 && !hub.isLearning(),
                  "MIDI learn binds the next CC");

            auto* gain = dynamic_cast<juce::Slider*>(findByName(mc, "masterSlider"));
            check(gain != nullptr && std::abs(gain->getValue() - 100.0 / 127.0) < 0.01,
                  "the learned CC drives the master gain",
                  gain != nullptr ? juce::String(gain->getValue(), 3) : juce::String("no slider"));

            auto* act = dynamic_cast<juce::Label*>(findByName(mc, "midiActLab"));
            check(act != nullptr && act->getText().contains("CC 7"),
                  "the activity monitor shows the last message",
                  act != nullptr ? act->getText() : juce::String());

            // a note can be learned for the intensity
            hub.startLearn(MidiHub::Target::drawIntensity);
            hub.injectForTest(MidiHub::kTypeNoteOn, 0, 60, 64);
            mc.timerCallback();
            const auto bi = hub.binding(MidiHub::Target::drawIntensity);
            auto* inten = dynamic_cast<juce::Slider*>(findByName(mc, "intenSlider"));
            check(bi.bound && bi.note && bi.number == 60
                  && inten != nullptr && std::abs(inten->getValue() - 64.0 / 127.0) < 0.02,
                  "a learned note drives the draw intensity");

            // a real-time message must not steal or cancel the learn
            hub.startLearn(MidiHub::Target::masterGain);
            hub.injectForTest(MidiHub::kTypeOther, 0, 0, 0);
            mc.timerCallback();
            check(hub.isLearning(), "a clock message does not cancel the learn");
            hub.cancelLearn();
            check(!hub.isLearning(), "the learn can be cancelled");

            // clearing a binding stops the parameter from following
            hub.clearBinding(MidiHub::Target::masterGain);
            if (gain != nullptr)
            {
                gain->setValue(0.5, juce::dontSendNotification);
                hub.injectForTest(MidiHub::kTypeCc, 2, 7, 127);
                mc.timerCallback();
                check(!hub.binding(MidiHub::Target::masterGain).bound
                      && std::abs(gain->getValue() - 0.5) < 1e-4,
                      "a cleared binding leaves the parameter alone");
            }

            // v1.30 (UPIC-53): the LIVE tab has its own granular grain size
            // (the Sample tab's grain control used to be the only one).
            {
                auto* liveTab = dynamic_cast<juce::Button*>(findByName(mc, "Live"));
                auto* lg = dynamic_cast<juce::Slider*>(findByName(mc, "liveGrainSlider"));
                check(liveTab != nullptr && lg != nullptr
                      && (int) lg->getMinimum() == 5 && (int) lg->getMaximum() == 500
                      && (int) lg->getValue() == 40,
                      "the LIVE tab has a 5-500 ms grain control",
                      lg != nullptr ? juce::String(lg->getValue(), 1) : juce::String("missing"));
                if (liveTab != nullptr && lg != nullptr)
                {
                    liveTab->onClick();
                    check(lg->isVisible(), "the live grain control follows the Live tab");
                    lg->setValue(200.0, juce::sendNotificationSync);
                    const auto grainVar = mc.pieceToVar().getProperty("liveGrainMs", 0);
                    check((int) grainVar == 200,
                          "the live grain size round-trips in the piece",
                          juce::String((int) grainVar) + " / "
                          + juce::String(lg->getValue(), 1));
                    lg->setValue(40.0, juce::sendNotificationSync);
                }
            }

            // v1.30 (UPIC-54/56): MIDI and OSC live in their own settings
            // overlay; the audio overlay is the device selector only. The
            // overlay with the MIDI section (for eyeballing) is still captured.
            auto* settings = dynamic_cast<juce::Button*>(findByName(mc, "settingsBtn"));
            check(settings != nullptr, "the MIDI + OSC settings button is present");
            if (settings != nullptr)
            {
                settings->onClick();
                auto* ov = findByName(mc, "settingsOverlay");
                auto* ok = dynamic_cast<juce::Button*>(findByName(mc, "settingsOkBtn"));
                auto* midiInOverlay = ov != nullptr ? findByName(*ov, "midiDevBox") : nullptr;
                check(ov != nullptr && ov->isVisible()
                      && ok != nullptr && ok->isVisible() && ok->getWidth() >= 8
                      && ov->getBounds().contains(ok->getBounds()),
                      "the settings overlay opens with its OK button");
                check(midiInOverlay == dev && dev != nullptr && dev->getParentComponent() == ov,
                      "MIDI lives in the settings overlay, not the audio overlay");
                check(mc.oscPortForTest() == 9000
                      && mc.oscMulticastPortForTest() == 54321
                      && mc.oscMulticastGroupForTest() == "224.0.0.1",
                      "OSC settings keep the old defaults",
                      juce::String(mc.oscPortForTest()) + " / "
                      + juce::String(mc.oscMulticastPortForTest()) + " / "
                      + mc.oscMulticastGroupForTest());
                auto* port = dynamic_cast<juce::Slider*>(findByName(mc, "oscPortSlider"));
                auto* apply = dynamic_cast<juce::Button*>(findByName(mc, "oscApplyBtn"));
                check(port != nullptr && (int) port->getValue() == 9000
                      && (int) port->getMinimum() == 1024
                      && (int) port->getMaximum() == 65535,
                      "the OSC UDP port is editable",
                      port != nullptr ? juce::String(port->getValue(), 0) : juce::String("missing"));
                if (apply != nullptr)
                    apply->onClick(); // rebind with the same values
                check(mc.oscIsListening(),
                      "Apply rebinds the OSC receivers from the settings window");

                auto* onBtn = dynamic_cast<juce::ToggleButton*>(findByName(mc, "oscOnBtn"));
                check(onBtn != nullptr, "the OSC on/off button is present");
                if (onBtn != nullptr && apply != nullptr)
                {
                    onBtn->setToggleState(false, juce::dontSendNotification);
                    apply->onClick();
                    check(!mc.oscIsListening() && !mc.oscEnabledForTest(),
                          "OSC can be turned off from settings");
                    onBtn->setToggleState(true, juce::dontSendNotification);
                    apply->onClick();
                    check(mc.oscIsListening(),
                          "OSC can be turned back on from settings");
                }

                const juce::File png = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                           .getChildFile("upic_snapshot_settings.png");
                juce::Image hi = mc.createComponentSnapshot(mc.getLocalBounds(), false);
                if (hi.isValid())
                    if (auto os = png.createOutputStream())
                    {
                        juce::PNGImageFormat fmt;
                        fmt.writeImageToStream(hi, *os);
                        os->flush();
                    }

                if (ok != nullptr) ok->onClick();
                check(ov != nullptr && ! ov->isVisible(),
                      "the settings overlay OK closes it");

                // the audio overlay must not contain the MIDI section any more
                if (auto* ab = dynamic_cast<juce::Button*>(findByName(mc, "Audio")))
                {
                    ab->onClick();
                    auto* aov = findByName(mc, "audioOverlay");
                    auto* aok = dynamic_cast<juce::Button*>(findByName(mc, "OK"));
                    check(aov != nullptr && aov->isVisible()
                          && findByName(*aov, "midiDevBox") == nullptr,
                          "the audio overlay no longer contains MIDI");
                    if (aok != nullptr) aok->onClick();
                }
            }
        }

        // 23. v1.24 (wave 21): the envelope points - + adds, - removes, the
        //     ends are free and the extra points survive the piece format
        {
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);
            mc.loadFromVar(juce::JSON::parse(
                "{\"envelopes\":[{\"id\":1,\"name\":\"Test\",\"points\":["
                "{\"x\":0,\"y\":0},{\"x\":0.25,\"y\":1},{\"x\":0.75,\"y\":0.2},{\"x\":1,\"y\":0}]}],"
                "\"pages\":[{\"duration\":30,\"nextArcId\":1,\"arcs\":[]}]}"));

            std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
                [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                    if (c.getName() == n) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                    return nullptr;
                };
            std::function<EnvelopeEditor*(juce::Component&)> findEnvEd =
                [&findEnvEd](juce::Component& c) -> EnvelopeEditor* {
                    if (auto* ed = dynamic_cast<EnvelopeEditor*>(&c)) return ed;
                    for (int i = 0; i < c.getNumChildComponents(); ++i)
                        if (auto* r = findEnvEd(*c.getChildComponent(i))) return r;
                    return nullptr;
                };

            auto* ed = findEnvEd(mc);
            check(ed != nullptr, "the envelope editor is present");
            if (ed != nullptr)
            {
                check(ed->pointCount() == 4, "the envelope keeps its points",
                      juce::String(ed->pointCount()));

                // select the attack point, then + splits the segment after it
                ed->selectPoint(1);
                const bool added = ed->addPoint();
                const auto mid = ed->pointAt(2);
                check(added && ed->pointCount() == 5, "the + adds a point",
                      juce::String(ed->pointCount()));
                check(std::abs(mid.x - 0.5f) < 0.005f && std::abs(mid.y - 0.6f) < 0.005f,
                      "the new point sits midway on the curve",
                      juce::String(mid.x, 3) + ", " + juce::String(mid.y, 3));
                check(ed->selectedPoint() == 2, "the new point becomes the selection");

                // a snapshot of the editor with a point selected (for eyeballing)
                {
                    const juce::File png = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                               .getChildFile("upic_snapshot_env.png");
                    juce::Image hi = ed->createComponentSnapshot(ed->getLocalBounds(), false);
                    if (hi.isValid())
                        if (auto os = png.createOutputStream())
                        {
                            juce::PNGImageFormat fmt;
                            fmt.writeImageToStream(hi, *os);
                            os->flush();
                        }
                }

                // the extra points survive a save/load round trip
                {
                    auto mc2Holder = std::make_unique<MainComponent>();
                    auto& mc2 = *mc2Holder;
                    mc2.setSize(1280, 800);
                    mc2.loadFromVar(mc.pieceToVar());
                    auto* ed2 = findEnvEd(mc2);
                    const auto p2 = ed2 != nullptr ? ed2->pointAt(2) : upic::EnvPoint{ -1.0f, -1.0f };
                    check(ed2 != nullptr && ed2->pointCount() == 5
                          && std::abs(p2.x - 0.5f) < 0.005f && std::abs(p2.y - 0.6f) < 0.005f,
                          "the extra envelope points survive the piece format");
                }

                // - removes the selected point; two must remain
                check(ed->removePoint() && ed->pointCount() == 4 && ed->selectedPoint() == -1,
                      "the - removes the selected point");
                check(!ed->removePoint(), "the - needs a selected point");
                ed->selectPoint(0);
                check(ed->removePoint() && ed->pointCount() == 3, "an end point can be removed");

                // a curve with no point at x=0 holds its first value (a late onset)
                upic::Envelope lateOnset;
                lateOnset.points = { { 0.25f, 1.0f }, { 0.75f, 0.2f }, { 1.0f, 0.0f } };
                check(std::abs(lateOnset.valueAt(0.0f) - 1.0f) < 1.0e-6f,
                      "a curve without a 0 point holds its first value");

                // tap a spot on the curve, then + inserts exactly there
                ed->placePointAt(0.9f);
                check(ed->addPoint() && ed->pointCount() == 4, "the + uses the tapped spot");
                {
                    const auto p = ed->pointAt(2);
                    check(std::abs(p.x - 0.9f) < 0.005f && std::abs(p.y - 0.08f) < 0.01f,
                          "the tapped point lands on the curve",
                          juce::String(p.x, 3) + ", " + juce::String(p.y, 3));
                }

                // v1.26: Ctrl/Cmd/Shift-click adds envelope points to the
                // selection; - removes the whole selected group.
                ed->selectPoint(0);
                ed->selectPoint(2, true);
                check(ed->selectedPoints().size() == 2 && ed->selectedPoint() == 0,
                      "ctrl-click adds envelope points to the selection",
                      juce::String((int) ed->selectedPoints().size()));

                // dragging one selected point moves the whole group together
                {
                    ed->selectPoint(1);
                    ed->selectPoint(2, true);
                    const auto p1 = ed->pointAt(1);
                    const auto p2 = ed->pointAt(2);
                    auto plot = ed->getLocalBounds().toFloat().reduced(6.0f);
                    plot.removeFromTop(26.0f);
                    auto px = [&](upic::EnvPoint p) {
                        return juce::Point<float>(plot.getX() + p.x * plot.getWidth(),
                                                  plot.getBottom() - p.y * plot.getHeight());
                    };
                    const auto start = px(p1);
                    const auto end = start + juce::Point<float>(12.0f, 0.0f);
                    const auto now = juce::Time::getCurrentTime();
                    const auto src = juce::Desktop::getInstance().getMainMouseSource();
                    juce::MouseEvent down(src, start, juce::ModifierKeys(), 1.0f,
                                          0.0f, 0.0f, 0.0f, 0.0f, ed, ed, now, start, now, 1, false);
                    ed->mouseDown(down);
                    juce::MouseEvent drag(src, end, juce::ModifierKeys(), 1.0f,
                                          0.0f, 0.0f, 0.0f, 0.0f, ed, ed, now, start, now, 1, true);
                    ed->mouseDrag(drag);
                    ed->mouseUp(drag);
                    const float dx = 12.0f / plot.getWidth();
                    const auto q1 = ed->pointAt(1);
                    const auto q2 = ed->pointAt(2);
                    check(std::abs((q1.x - p1.x) - dx) < 0.002f
                          && std::abs((q2.x - p2.x) - dx) < 0.002f
                          && std::abs(q1.y - p1.y) < 1.0e-4f
                          && ed->selectedPoints().size() == 2,
                          "dragging a selected envelope point moves the group",
                          juce::String(q1.x - p1.x, 3) + "/" + juce::String(q2.x - p2.x, 3));
                }

                const int beforeMultiRemove = ed->pointCount();
                check(ed->removePoint() && ed->pointCount() == beforeMultiRemove - 2,
                      "the - removes all selected envelope points",
                      juce::String(ed->pointCount()));

                // the cap keeps the per-sample scan cheap
                for (int i = 0; i < 40; ++i) ed->addPoint();
                check(ed->pointCount() == 32 && !ed->addPoint(), "the point cap is 32",
                      juce::String(ed->pointCount()));

                // the two buttons are in the header and follow the selection
                auto* addB = dynamic_cast<juce::Button*>(findByName(mc, "envAddBtn"));
                auto* delB = dynamic_cast<juce::Button*>(findByName(mc, "envDelBtn"));
                check(addB != nullptr && delB != nullptr && addB->isVisible() && delB->isVisible()
                      && ed->getLocalBounds().contains(addB->getBounds())
                      && ed->getLocalBounds().contains(delB->getBounds()),
                      "the + and - buttons sit in the editor header");
                check(addB != nullptr && !addB->isEnabled(), "the + is disabled at the cap");
                ed->selectPoint(1);
                check(delB != nullptr && delB->isEnabled() && delB->getParentComponent() == ed,
                      "the - follows the selection");
            }
        }

        // 24. v1.25 (wave 22): the signed playhead. Playing backwards (rate -1)
        //     renders the material in reverse without reversing the slot, and
        //     the oscillator contour runs backwards too.
        {
            const int N = 44100;
            auto waves = upic::defaultWaveforms();
            upic::Envelope flat; flat.presetFlat();
            std::vector<upic::Envelope> envs { flat };
            upic::Page pg; pg.durationSec = 1.0;
            upic::Arc a; a.id = 1; a.mode = 1; a.sampleId = 1; a.refFreq = 220.0;
            a.intensity = 1.0f;
            a.points = { { 0.0, 220.0 }, { 1.0, 220.0 } };
            pg.arcs.push_back(a);

            auto rampSlot = [N]() {
                upic::SampleSlot s;
                s.id = 1; s.name = "ramp"; s.sampleRate = 44100.0;
                s.reversed = false;
                juce::AudioBuffer<float> buf(1, N);
                for (int i = 0; i < N; ++i)
                    buf.setSample(0, i, (float) i / (float) (N - 1));
                s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
                return s;
            };

            std::vector<float> fwd, bwd;
            fwd.assign((size_t) N, 0.0f);
            bwd.assign((size_t) N, 0.0f);
            {
                std::vector<upic::SampleSlot> slots { rampSlot() };
                UpicEngine e; e.prepare(44100.0, 8192);
                e.render(pg, 0.0, waves, envs, slots, fwd.data(), nullptr, N, 1.0f, 1.0);
            }
            {
                std::vector<upic::SampleSlot> slots { rampSlot() };
                UpicEngine e; e.prepare(44100.0, 8192);
                e.render(pg, 1.0, waves, envs, slots, bwd.data(), nullptr, N, 1.0f, -1.0);
            }

            double mirrorDiff = 0.0;
            for (int i = 2000; i < N - 2000; ++i)
                mirrorDiff = juce::jmax(mirrorDiff, (double) std::abs(
                    bwd[(size_t) i] - fwd[(size_t) (N - 1 - i)]));
            check(mirrorDiff < 1e-3,
                  "the signed playhead renders the material in reverse",
                  "max diff " + juce::String(mirrorDiff, 6));
        }

        // 24b. v1.28: tape re-anchors on the integrated pitch position, not
        //      ratio * elapsed, so a falling pitch curve cannot flip direction.
        {
            const int N = 8000;
            auto waves = upic::defaultWaveforms();
            upic::Envelope flat; flat.presetFlat();
            std::vector<upic::Envelope> envs { flat };
            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 1; a.pitchMode = 0;
            a.sampleId = 1; a.refFreq = 220.0;
            a.intensity = 1.0f;
            a.points = { { 0.0, 880.0 }, { 0.9, 880.0 }, { 1.0, 55.0 }, { 4.0, 55.0 } };
            pg.arcs.push_back(a);

            upic::SampleSlot s;
            s.id = 1; s.name = "ramp"; s.sampleRate = 1000.0; s.reversed = false;
            juce::AudioBuffer<float> buf(1, N);
            for (int i = 0; i < N; ++i)
                buf.setSample(0, i, (float) i / (float) (N - 1));
            s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
            std::vector<upic::SampleSlot> slots { std::move(s) };

            auto firstSample = [&](double t, bool scrub) {
                UpicEngine e; e.prepare(44100.0, 8192);
                e.setScrubPreview(scrub);
                float out = 0.0f;
                e.render(pg, t, waves, envs, slots, &out, nullptr, 1, 1.0f, 1.0);
                return out;
            };
            const float early = firstSample(0.5, true);
            const float late  = firstSample(1.0, true);
            check(late > early + 0.02f,
                  "a falling tape pitch keeps the scrub moving forward",
                  juce::String(early, 3) + " -> " + juce::String(late, 3));
            const float earlyN = firstSample(0.5, false);
            const float lateN  = firstSample(1.0, false);
            check(lateN > earlyN + 0.02f,
                  "a falling tape pitch keeps playback moving forward",
                  juce::String(earlyN, 3) + " -> " + juce::String(lateN, 3));
        }

        // The worst case the piece limits allow: the maximum number of arcs
        // on a page, every one of them sounding at the same time (a
        // crafted piece can do that; a legitimate cluster can too). The
        // audio thread must not be handed more than a few times the block
        // budget, or the app stutters.
        //
        // Measured on the release build (48 kHz, 512-sample blocks):
        //   500 arcs x  20 points -> 16.4 ms/block  (154% of the budget)
        //   500 arcs x 1000 points -> 26.1 ms/block  (244%)
        //  2024 arcs x  20 points -> 65.8 ms/block  (616%)
        //  2024 arcs x 1000 points -> 104.3 ms/block (978%)
        // The arc COUNT is the dominant term (the per-arc work), not the
        // point count. The threshold is deliberately loose: it catches a
        // real slowdown of the render path, not a slow machine.
        {
            const int sr = 48000, blk = 512;
            const int arcs = upic::limits::kMaxArcsPerPage;
            const int pts = 20;
            upic::Page pg;
            pg.durationSec = 10.0;
            for (int i = 0; i < arcs; ++i)
            {
                upic::Arc a;
                a.id = i + 1;
                for (int k = 0; k < pts; ++k)
                    a.points.push_back ({ (double) k / (double) pts * pg.durationSec,
                                         100.0 * std::pow (2.0, (double) (k % 40) / 12.0) });
                pg.arcs.push_back (a);
            }
            auto waves = upic::defaultWaveforms();
            auto envs  = upic::defaultEnvelopes();
            std::vector<upic::SampleSlot> noSlots;
            UpicEngine e; e.prepare (sr, blk);
            juce::AudioBuffer<float> buf (2, blk);
            e.render (pg, 0.0, waves, envs, noSlots, buf.getWritePointer(0),
                      buf.getWritePointer(1), blk, 0.8f, 1.0, false);
            const auto t0 = juce::Time::getHighResolutionTicks();
            const int blocks = 20;
            for (int b = 0; b < blocks; ++b)
                e.render (pg, (double) b * blk / sr, waves, envs, noSlots,
                          buf.getWritePointer(0), buf.getWritePointer(1), blk, 0.8f, 1.0, false);
            const double secs = (double) (juce::Time::getHighResolutionTicks() - t0)
                                 / (double) juce::Time::getHighResolutionTicksPerSecond();
            const double usPerBlock = secs * 1e6 / blocks;
            const double budgetUs = blk * 1e6 / sr;
            log << "BENCH  " << arcs << " simultaneous arcs: "
                << juce::String (usPerBlock / 1000.0, 2) << " ms/block ("
                << juce::String (usPerBlock / budgetUs * 100.0, 0) << "% of the "
                << juce::String (budgetUs / 1000.0, 2) << " ms budget)\n";
            // the debug build is ~3x slower, so it gets a looser threshold;
            // both are far above the measured baseline, which is what the
            // check is for: catching a real slowdown of the render path.
            #if JUCE_DEBUG
                const double allowed = 12.0;
            #else
                const double allowed = 5.0;
            #endif
            check (usPerBlock < budgetUs * allowed,
                   "the worst-case page stays within the realtime budget",
                   juce::String (usPerBlock / budgetUs, 2) + "x (allowed "
                   + juce::String (allowed, 0) + "x)");
        }

        log << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : juce::String(failures) + " CHECK(S) FAILED") << "\n";
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("upic_selftest.txt").replaceWithText(log);
        DBG(log);
        // a failing selftest must not look successful to a script
        if (failures > 0) std::exit(1);
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow(juce::String name)
            : DocumentWindow(name, juce::Colours::black,
                             DocumentWindow::allButtons)
        {
            // JUCE 8 works in logical pixels (dp) everywhere: the window
            // peers multiply component bounds/fonts by the display scale
            // themselves, so all layout sizes below are density-independent
            // and controls keep a constant physical size on tablets/HiDPI.
            auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();

            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(460, 360, 100000, 100000); // dp — layout reflows below this

#if JUCE_ANDROID
            // Android (targetSdk 35 = enforced edge-to-edge): the content
            // frame spans the whole display, so start from the full display
            // area in logical px. The immersive switch + refit timer below
            // take it from there.
            if (display != nullptr)
                setSize(display->userArea.getWidth(), display->userArea.getHeight());
            else
                setSize(1024, 768);
#else
            // Default window: the 1280x800 dp design, clamped to the screen
            // (userArea is in logical px too).
            int w = 1280, h = 800;
            if (display != nullptr)
            {
                const int availW = display->userArea.getWidth()  - 16;
                const int availH = display->userArea.getHeight() - 48;
                w = juce::jmin(w, juce::jmax(460, availW));
                h = juce::jmin(h, juce::jmax(360, availH));
            }
            centreWithSize(w, h);
#endif
            setVisible(true);
#if JUCE_ANDROID
            if (getPeer() != nullptr)
                getPeer()->setFullScreen(true);
            // setFullScreen alone does NOT hide the bars (the peer only
            // raises its hide flags for the kiosk component, and those are
            // ignored under Android 15 edge-to-edge): drive them explicitly,
            // then keep re-asserting from the refitter.
            hideSystemBars();

            // Rotation/fold: JUCE refreshes its Displays data from the
            // activity's layout listener, but never re-applies top-level
            // bounds, so follow the display ourselves. The target mirrors
            // the peer's own fullscreen rule: totalArea while the bars are
            // hidden, userArea otherwise.
            refitter = std::make_unique<WindowRefitter>(*this);
#endif
        }
        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    private:
#if JUCE_ANDROID
        // Keeps the window glued to the display across rotations, folds and
        // other display changes (5 Hz - JUCE refreshes the Displays data for
        // us, but never re-applies top-level bounds itself).
        class WindowRefitter final : private juce::Timer
        {
        public:
            explicit WindowRefitter (juce::DocumentWindow& w) : window (w) { startTimerHz (5); }

        private:
            void timerCallback() override
            {
                auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
                if (d == nullptr) return;

                const auto target = (window.getPeer() != nullptr && window.getPeer()->isFullScreen())
                                        ? d->totalArea
                                        : d->userArea;

                if (! target.isEmpty() && window.getBounds() != target)
                    window.setBounds (target);

                // Re-assert immersive bars (AndroidBars.h): swipe reveals,
                // focus loss, volume panels and rotation all clear the hide
                // request otherwise.
                if (target == d->totalArea)
                    hideSystemBars();
            }

            juce::DocumentWindow& window;
        };

        std::unique_ptr<WindowRefitter> refitter;
#endif

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(UpicApplication)
