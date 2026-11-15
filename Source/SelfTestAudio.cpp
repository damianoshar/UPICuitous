#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "AllocCounter.h"

//==============================================================================
// The audio path: the lock-free snapshot, the audio thread (no allocation),
// the long jobs, the live input and the reverse, the realtime render and the
// transport (the signed playhead and the tape).

void upic::runAudioThreadChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

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

    // 6a-3. v1.33.4 (M22): the status timer must not copy the model 30
    //     times a second. It used to deep-copy every page, arc,
    //     waveform and envelope on EVERY tick - even while nothing
    //     changed and even with the plugin editor closed. Now it
    //     publishes only when the model changed (plus a 2 s safety
    //     net for a change path that forgets to mark itself).
    {
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByNameM22 =
            [&findByNameM22](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByNameM22(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };

        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);

        // idle: 30 ticks, nothing changes - the model must not be
        // copied on every tick
        const auto before = mc.modelSnapshotPublishCount();
        for (int i = 0; i < 30; ++i)
            mc.timerCallback();
        const auto idle = mc.modelSnapshotPublishCount() - before;
        check (idle <= 1,
               "the timer does not copy the model while nothing changes",
               juce::String (idle) + " publish(es) in 30 ticks (the 2 s safety net may fire once)");

        // a change is published: the loop toggle marks the model, and
        // the timer picks the new model up on its next tick
        auto* loop = dynamic_cast<juce::ToggleButton*> (findByNameM22 (mc, "loopBtn"));
        const auto beforeChange = mc.modelSnapshotPublishCount();
        if (loop != nullptr)
        {
            loop->setToggleState (! loop->getToggleState(), juce::dontSendNotification);
            loop->onClick();
        }
        mc.timerCallback();
        check (loop != nullptr && mc.modelSnapshotPublishCount() > beforeChange,
               "the timer publishes the model after a change",
               juce::String (mc.modelSnapshotPublishCount() - beforeChange) + " publish(es)");

        // the editor change paths mark the model too - the score, the
        // waveform editor and the envelope editor used to publish
        // nothing at all, and only the unconditional timer publish made
        // them audible
        auto* envFlat = dynamic_cast<juce::Button*> (findByNameM22 (mc, "envFlatBtn"));
        const auto beforeEnv = mc.modelSnapshotPublishCount();
        if (envFlat != nullptr)
            envFlat->onClick();
        mc.timerCallback();
        check (envFlat != nullptr && mc.modelSnapshotPublishCount() > beforeEnv,
               "an envelope edit reaches the audio thread",
               juce::String (mc.modelSnapshotPublishCount() - beforeEnv) + " publish(es)");
    }

    // 6a-4. v1.33.4 (M21): the audio thread must not allocate. A malloc
    //     there can block, and a blocked audio thread is a dropout; a
    //     free there can stall the whole thread. The score and freeze
    //     buffers, the engine's scratch and the voice pools are all
    //     sized before the render.
    {
        const double sr = 44100.0;
        const int blk = 512;

        auto waves = upic::defaultWaveforms();
        upic::Envelope flat; flat.presetFlat();
        std::vector<upic::Envelope> envs { flat };

        // a page with an oscillator, a sample, a PM and a live arc
        upic::Page pg; pg.durationSec = 10.0;
        upic::Arc osc; osc.id = 1; osc.mode = 0; osc.refFreq = 220.0; osc.intensity = 1.0f;
        osc.points = { { 0.0, 220.0 }, { 10.0, 220.0 } };
        upic::Arc smp; smp.id = 2; smp.mode = 1; smp.sampleId = 1; smp.refFreq = 220.0; smp.intensity = 1.0f;
        smp.points = { { 0.0, 220.0 }, { 10.0, 220.0 } };
        upic::Arc pm; pm.id = 3; pm.mode = 3; pm.refFreq = 220.0; pm.intensity = 1.0f;
        pm.points = { { 0.0, 220.0 }, { 10.0, 220.0 } };
        upic::Arc live; live.id = 4; live.mode = 2; live.refFreq = 220.0; live.intensity = 1.0f;
        live.points = { { 0.0, 220.0 }, { 10.0, 220.0 } };
        pg.arcs = { osc, smp, pm, live };

        // the material
        upic::SampleSlot slot; slot.id = 1; slot.name = "sine220"; slot.sampleRate = sr;
        {
            juce::AudioBuffer<float> buf (1, 44100);
            for (int i = 0; i < 44100; ++i)
                buf.setSample (0, i, 0.5f * (float) std::sin (
                    juce::MathConstants<double>::pi * 2.0 * 220.0 * i / 44100.0));
            slot.data = std::make_shared<const juce::AudioBuffer<float>> (std::move (buf));
        }
        std::vector<upic::SampleSlot> slots { slot };

        // the live input ring
        std::vector<float> ring ((size_t) sr * 2, 0.0f);
        for (size_t i = 0; i < ring.size(); ++i)
            ring[i] = 0.2f * (float) std::sin (
                juce::MathConstants<double>::pi * 2.0 * 100.0 * (double) i / sr);

        UpicEngine e; e.prepare (sr, blk);
        e.setLiveInput (ring.data(), nullptr, (int) ring.size(), (long long) ring.size(), true);
        e.setSamplePitchMode (1);  // granular - the usual path
        e.setLivePitchMode (0);    // granular - the usual path

        std::vector<float> out ((size_t) blk, 0.0f);

        // The counter itself, first: a deliberate allocation must be seen.
        // Without this the checks below would pass without counting anything
        // (the counter is a different translation unit from the engine now,
        // so a broken link would be invisible).
        upic::allocCount = 0; upic::allocCounting = true;
        void* probe = ::operator new (64);
        ::operator delete (probe);
        upic::allocCounting = false;
        check (upic::allocCount == 1,
               "the allocation counter sees an allocation",
               juce::String (upic::allocCount) + " allocation(s) for one new");

        // The first block creates the voices (the pools are already
        // there), so it must not allocate either.
        upic::allocCount = 0; upic::allocCounting = true;
        e.render (pg, 0.0, waves, envs, slots, out.data(), nullptr, blk, 1.0f);
        upic::allocCounting = false;
        const int firstBlock = upic::allocCount;
        check (firstBlock == 0,
               "the audio thread does not allocate while it renders",
               juce::String (firstBlock) + " allocation(s) in the first block");

        // and the steady state (the voices exist now) must not either
        upic::allocCount = 0; upic::allocCounting = true;
        for (int b = 0; b < 8; ++b)
            e.render (pg, (double) b * blk / sr, waves, envs, slots, out.data(), nullptr, blk, 1.0f);
        upic::allocCounting = false;
        const int steady = upic::allocCount;
        check (steady == 0,
               "the audio thread does not allocate in the steady state",
               juce::String (steady) + " allocation(s) in 8 blocks");

        // a reset (a seek, a page change) must not free the voices
        upic::allocCount = 0; upic::allocCounting = true;
        e.reset();
        e.render (pg, 0.0, waves, envs, slots, out.data(), nullptr, blk, 1.0f);
        upic::allocCounting = false;
        const int afterReset = upic::allocCount;
        check (afterReset == 0,
               "a reset does not allocate or free in the audio thread",
               juce::String (afterReset) + " allocation(s) after a reset");

        // The vocoder's working memory (~50 KB per voice plus the FFT
        // engine) is prepared on the message thread - that is what
        // prepareSampleVocoderMemory is for - so an arc that plays
        // through the vocoder must not allocate in the audio thread
        // either.
        {
            UpicEngine ev; ev.prepare (sr, blk);
            ev.setLiveInput (ring.data(), nullptr, (int) ring.size(), (long long) ring.size(), true);
            ev.setSamplePitchMode (2);  // phase vocoder
            ev.setLivePitchMode (2);    // phase vocoder
            ev.prepareSampleVocoderMemory (4);
            ev.prepareLiveVocoderMemory (4);

            upic::allocCount = 0; upic::allocCounting = true;
            ev.render (pg, 0.0, waves, envs, slots, out.data(), nullptr, blk, 1.0f);
            upic::allocCounting = false;
            const int vocFirst = upic::allocCount;
            check (vocFirst == 0,
                   "the audio thread does not allocate for the vocoder",
                   juce::String (vocFirst) + " allocation(s) in the first vocoder block");

            upic::allocCount = 0; upic::allocCounting = true;
            for (int b = 0; b < 4; ++b)
                ev.render (pg, (double) b * blk / sr, waves, envs, slots, out.data(), nullptr, blk, 1.0f);
            upic::allocCounting = false;
            const int vocSteady = upic::allocCount;
            check (vocSteady == 0,
                   "the vocoder does not allocate in the steady state",
                   juce::String (vocSteady) + " allocation(s) in 4 vocoder blocks");
        }
    }

    // 6a-5. v1.33.4 (M28): the long jobs - the render, the stems, the
    //     decode, the save - run on a worker thread. They used to run in
    //     the file chooser's callback: on the message thread, and mostly
    //     while holding dataLock, so a long export froze the window and
    //     held the audio thread up with it.
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);

        // The job runs on a worker thread: the message thread is free
        // while it works. The worker waits for the test to let it go,
        // so the check cannot pass by accident.
        std::atomic<bool> workerStarted { false };
        std::atomic<bool> releaseWorker { false };
        std::atomic<bool> workerFinished { false };
        mc.startJob ("selftest", [&] (MainComponent::Job& job)
        {
            workerStarted.store (true, std::memory_order_relaxed);
            // the wait has a deadline of its own: if the job ever ran on
            // the message thread, the test would end (slowly) instead of
            // hanging for ever
            const auto stopAt = juce::Time::getMillisecondCounter() + 5000;
            while (! releaseWorker.load (std::memory_order_relaxed)
                   && ! job.cancelled.load (std::memory_order_relaxed)
                   && juce::Time::getMillisecondCounter() < stopAt)
                juce::Thread::sleep (1);
            workerFinished.store (true, std::memory_order_relaxed);
        }, nullptr);

        auto deadline = juce::Time::getMillisecondCounter() + 5000;
        while (! workerStarted.load (std::memory_order_relaxed)
               && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (1);
        check (workerStarted.load (std::memory_order_relaxed),
               "the job starts on a worker thread");
        check (! workerFinished.load (std::memory_order_relaxed),
               "the message thread is free while the job runs");
        releaseWorker.store (true, std::memory_order_relaxed);
        deadline = juce::Time::getMillisecondCounter() + 5000;
        while (! workerFinished.load (std::memory_order_relaxed)
               && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (1);
        check (workerFinished.load (std::memory_order_relaxed),
               "the job finishes");

        // The worker clears the "busy" flag after the work returns, so the next
        // job may start - startJob runs one job at a time and refuses the second
        // one. Without this wait the cancel check below could race with the end
        // of the first job (and fail for no reason).
        deadline = juce::Time::getMillisecondCounter() + 5000;
        while (mc.jobRunning() && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (1);

        // The cancel reaches the job: the worker sees the flag and
        // stops.
        std::atomic<bool> cancelSeen { false };
        mc.startJob ("selftest cancel", [&] (MainComponent::Job& job)
        {
            while (! job.cancelled.load (std::memory_order_relaxed))
                juce::Thread::sleep (1);
            cancelSeen.store (true, std::memory_order_relaxed);
        }, nullptr);
        deadline = juce::Time::getMillisecondCounter() + 5000;
        while (mc.jobRunning() && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (1);
        mc.cancelJob();
        deadline = juce::Time::getMillisecondCounter() + 5000;
        while (! cancelSeen.load (std::memory_order_relaxed)
               && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (1);
        check (cancelSeen.load (std::memory_order_relaxed),
               "the running job can be cancelled");

        // And the render really writes the file when it runs as a job.
        const auto target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("upic_m28_export.wav");
        target.deleteFile();
        mc.exportPageAsJob (juce::URL (target));
        deadline = juce::Time::getMillisecondCounter() + 120000;
        while (mc.jobRunning() && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (5);
        check (! mc.jobRunning(), "the export job finishes");
        check (target.existsAsFile() && target.getSize() > 1000,
               "the export job wrote the file",
               target.existsAsFile() ? juce::String (target.getSize()) + " bytes"
                                     : juce::String ("no file"));
        target.deleteFile();
    }

}

void upic::runThreadValueChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6a-7. v1.33.4 (L14): two values cross the thread boundary. The
    //     sample rate (the device's prepareToPlay writes it, the audio
    //     thread reads it) and the scrub tail (the slider writes it, the
    //     audio thread reads it) were plain variables: a data race, and
    //     a torn read of the rate is possible.
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);

        // the device's rate is the rate the app uses
        mc.prepareToPlay (512, 48000.0);
        check (mc.getAudioSampleRate() == 48000.0,
               "the prepared sample rate is the device's rate",
               juce::String (mc.getAudioSampleRate(), 1));
        check (mc.exportSampleRate() == 48000.0,
               "the export uses the prepared rate",
               juce::String (mc.exportSampleRate(), 1));

        // the audio thread reads the rate ONCE per block, and it is the
        // prepared one
        juce::AudioBuffer<float> out (2, 512);
        juce::AudioSourceChannelInfo info (&out, 0, out.getNumSamples());
        mc.getNextAudioBlock (info);
        check (mc.lastBlockRate() == 48000,
               "the audio block uses the prepared rate",
               juce::String (mc.lastBlockRate()));

        // and it follows the rate when the device changes it
        mc.prepareToPlay (512, 96000.0);
        mc.getNextAudioBlock (info);
        check (mc.lastBlockRate() == 96000,
               "the audio block follows the rate when it changes",
               juce::String (mc.lastBlockRate()));

        // the tail: the slider writes it, the audio thread reads it
        juce::Slider* tail = nullptr;
        for (int i = 0; i < mc.getNumChildComponents() && tail == nullptr; ++i)
            if (auto* s = dynamic_cast<juce::Slider*> (mc.getChildComponent (i)))
                if (s->getName() == "scrubTailSlider") tail = s;
        check (tail != nullptr, "the scrub tail slider is there");
        if (tail != nullptr)
        {
            // sendNotificationSync: the slider's callback runs at once
            // (the selftest has no message loop)
            tail->setValue (250.0, juce::sendNotificationSync);
            check (mc.scrubTailMsValue() == 250,
                   "the slider writes the tail the audio thread reads",
                   juce::String (mc.scrubTailMsValue()));
            // the audio thread reads it while it is being written
            for (int i = 0; i < 8; ++i)
            {
                tail->setValue (100.0 + i * 10.0, juce::sendNotificationSync);
                mc.getNextAudioBlock (info);
            }
            check (mc.scrubTailMsValue() >= 100 && mc.scrubTailMsValue() <= 180,
                   "the tail survives being written while the audio runs",
                   juce::String (mc.scrubTailMsValue()));
        }
    }

}

void upic::runLivePathChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

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

        // v1.33.4 (H7): the flag changes the sound, so it travels with
        // the piece - it used to be dropped on save, so a reopened piece
        // played the material forwards
        check((bool) rampSlot(true).toVar().getProperty("reversed", false),
              "sample reverse: the flag travels with the piece",
              rampSlot(true).toVar().getProperty("reversed", "absent").toString());
        check(!(bool) rampSlot(false).toVar().getProperty("reversed", false),
              "sample reverse: a forward slot is not marked reversed");
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

}

void upic::runRealtimeChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

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

}

void upic::runTransportChecks (upic::SelfTest& t)
{
    auto& log = t.log;
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

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
    // Measured on the release build (48 kHz, 512-sample blocks), before
    // and after the cursor render path (v1.33.4, U6 option C):
    //   500 arcs x  20 points -> 16.4 ms/block  (154% of the budget)
    //                                           -> 7.4 ms/block  (69%)
    //   500 arcs x 1000 points -> 26.1 ms/block  (244%)
    //                                           -> 7.6 ms/block  (71%)
    // The cap is now the UPIC's 2024 arcs (U6 option A), so this check
    // renders 2024 arcs: 26.1 ms/block (244%) with 20 points, 28.0 ms
    // (263%) with 1000. The guard (option B) is off in this raw engine:
    // the check measures the render path itself, not the guard.
    // The cursor follows the arc's segment instead of searching for it
    // in every sample, so the point count barely costs anything now -
    // the arc count is the term that matters.
    //
    // The threshold is deliberately loose: it catches a real slowdown of
    // the render path, not a slow machine.
    {
        const int sr = 48000, blk = 512;
        const int arcs = upic::limits::kMaxArcsPerPage;
        auto waves = upic::defaultWaveforms();
        auto envs  = upic::defaultEnvelopes();
        std::vector<upic::SampleSlot> noSlots;

        for (int pts : { 20, 1000 })
        {
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
            log << "BENCH  " << arcs << " simultaneous arcs x " << pts << " points: "
                << juce::String (usPerBlock / 1000.0, 2) << " ms/block ("
                << juce::String (usPerBlock / budgetUs * 100.0, 0) << "% of the "
                << juce::String (budgetUs / 1000.0, 2) << " ms budget)\n";
            // the debug build is ~4.5x slower than the release one, so it gets
            // a looser threshold; both are far above the measured baseline,
            // which is what the check is for: catching a real slowdown of the
            // render path.
            //
            // v1.33.4 (Tier 1, step 3): the debug threshold moved from 12x to
            // 15x. The engine's code moved into its own translation unit
            // (UpicEngine.cpp), and in the unoptimized debug build the
            // incremental linker's layout costs ~15% there: 12.2x with
            // incremental linking, 9.1x without, against 10.5x before the
            // split. The release build - the one that matters - did not move
            // (26.5-27.5 ms per block before, 27.2-28.7 ms after). The debug
            // allowance keeps the headroom it had over its own baseline:
            // 12x over 9.8x was 22%, 15x over 12.2x is 23%.
            #if JUCE_DEBUG
                const double allowed = 15.0;
            #else
                const double allowed = 5.0;
            #endif
            check (usPerBlock < budgetUs * allowed,
                   "the worst-case page stays within the realtime budget",
                   juce::String (usPerBlock / budgetUs, 2) + "x (allowed "
                   + juce::String (allowed, 0) + "x)");
        }
    }

    // v1.33.4 (U6-B): the overload guard. The engine measures every block;
    // when the load stays over the budget it fades out the quietest sounding
    // arcs - over 5 ms, inside the block, so nothing clicks - and skips them
    // once silent; when the load drops they fade back in the same way. The
    // adaptive policy depends on the machine's speed, so the test drives the
    // limit directly (setGuardLimitForTest) and reads the gains back.
    {
        const int sr = 48000, blk = 512;
        auto waves = upic::defaultWaveforms();
        auto envs  = upic::defaultEnvelopes();
        std::vector<upic::SampleSlot> noSlots;

        upic::Page pg;
        pg.durationSec = 10.0;
        // two steady arcs at well-separated pitches (so the RMS of the sum
        // is stable over any block, not a function of their phase): the
        // loud one and the quiet one
        for (int i = 0; i < 2; ++i)
        {
            upic::Arc a;
            a.id = i + 1;
            a.intensity = (i == 0) ? 1.0f : 0.5f;
            const double f = (i == 0) ? 220.0 : 1000.0;
            a.points.push_back ({ 0.0, f });
            a.points.push_back ({ 10.0, f });
            pg.arcs.push_back (a);
        }

        UpicEngine e;
        e.prepare (sr, blk);
        e.setOverloadGuardEnabled (true);
        juce::AudioBuffer<float> buf (2, blk);
        auto renderAt = [&] (double t)
        {
            buf.clear();
            e.render (pg, t, waves, envs, noSlots, buf.getWritePointer (0),
                      buf.getWritePointer (1), blk, 0.8f, 1.0, false);
        };
        auto maxStep = [&] ()
        {
            float m = 0.0f;
            const float* d = buf.getReadPointer (0);
            for (int n = 1; n < blk; ++n)
                m = juce::jmax (m, std::abs (d[n] - d[n - 1]));
            return m;
        };
        const double dt = (double) blk / (double) sr;

        // past the arcs' 12 ms edge fade, so the blocks compare
        e.setGuardLimitForTest (0);
        renderAt (1.0);
        const float both = buf.getRMSLevel (0, 0, blk);
        const float stepRef = maxStep();

        // a limit of one: the quiet arc fades out, the loud one stays
        e.setGuardLimitForTest (1);
        renderAt (1.0 + dt);
        const float stepFade = maxStep();
        check (e.guardGainForTest (2) == 0.0f && e.guardGainForTest (1) == 1.0f,
               "the overload guard fades the quietest arc out and keeps the loud one",
               "gains " + juce::String (e.guardGainForTest (1), 2) + " / "
               + juce::String (e.guardGainForTest (2), 2));
        check (stepFade < stepRef + 0.05f,
               "the guard's fade is click-light",
               "step " + juce::String (stepFade, 3) + " vs " + juce::String (stepRef, 3));

        // the next block skips the silent arc entirely
        renderAt (1.0 + 2.0 * dt);
        check (e.guardState().silenced == 1, "the silent arc is skipped",
               juce::String (e.guardState().silenced));
        const float one = buf.getRMSLevel (0, 0, blk);
        check (one < both * 0.95f, "the guard removes the arc's energy",
               juce::String (one, 3) + " vs " + juce::String (both, 3));

        // the limit off: the arc fades back in and the energy returns
        e.setGuardLimitForTest (0);
        renderAt (1.0 + 3.0 * dt);
        check (e.guardGainForTest (2) == 1.0f, "the guard lets the arc back in",
               juce::String (e.guardGainForTest (2), 2));
        renderAt (1.0 + 4.0 * dt);
        const float back = buf.getRMSLevel (0, 0, blk);
        check (std::abs (back - both) < both * 0.05f,
               "the restored page matches the reference",
               juce::String (back, 3) + " vs " + juce::String (both, 3));

        // an export is exact: the offline render never sheds an arc, even
        // with a limit that would drop the quiet one in realtime. Both
        // engines start the offline pass with empty phase pools, so the
        // two files are sample-for-sample identical.
        e.setGuardLimitForTest (1);
        juce::AudioBuffer<float> off (2, (int) (0.5 * sr));
        e.renderPageOffline (pg, waves, envs, noSlots, off, sr, 0.8f);
        UpicEngine ref;
        juce::AudioBuffer<float> refBuf (2, (int) (0.5 * sr));
        ref.renderPageOffline (pg, waves, envs, noSlots, refBuf, sr, 0.8f);
        float maxDiff = 0.0f;
        for (int n = 0; n < refBuf.getNumSamples(); ++n)
            maxDiff = juce::jmax (maxDiff, std::abs (off.getReadPointer (0)[n]
                                                     - refBuf.getReadPointer (0)[n]));
        check (maxDiff < 1.0e-6f, "the offline render ignores the guard",
               juce::String (maxDiff, 6));
    }

}
