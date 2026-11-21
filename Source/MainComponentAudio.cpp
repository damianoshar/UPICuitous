#include "MainComponent.h"
#include <atomic>
#include <cmath>
#include <memory>

//==============================================================================
// The realtime audio path: the snapshot the audio thread reads, the block
// callback, the score render and the device settings.

//==============================================================================
// v1.18 (wave 15): snapshot publication + the small message <-> audio
// signals. All of these are called on the message thread.
void MainComponent::prepareVocodersIfNeeded (int sampleMode, int liveMode)
{
    // v1.33.4 (M21): the vocoder's working memory is ~50 KB per voice plus
    // the FFT engine. It is prepared here, on the message thread, before the
    // page that needs it reaches the audio thread - an allocation in the
    // audio thread can block, and a blocked audio thread is a dropout.
    //
    // The message thread knows which arcs could use the vocoder: the tab's
    // setting, or the arc's own override (v1.33.4 / H7).
    bool needSample = false, needLive = false;
    int arcs = 0;
    // the model is the message thread's own (the audio thread reads the
    // snapshot, not these pages), so no lock is needed here - and taking it
    // would deadlock: publishModelSnapshot() is called with the lock held
    if (! pages.empty())
    {
        const auto& page = pages[(size_t) juce::jlimit (0, (int) pages.size() - 1, currentPage)];
        for (auto& a : page.arcs)
        {
            if (a.muted) continue;
            ++arcs;
            if (a.mode == 1 && engine.effectiveMode (a, sampleMode) == 2) needSample = true;
            else if (a.mode == 2 && engine.effectiveMode (a, liveMode) == 2) needLive = true;
        }
    }
    if (! needSample && ! needLive) return;
    // the audio thread hands the voice slots out in order, so the first N
    // arcs to sound are the ones that find a prepared voice
    engine.prepareSampleVocoderMemory (needSample ? arcs : 0);
    engine.prepareLiveVocoderMemory (needLive ? arcs : 0);
    scrubEngine.prepareSampleVocoderMemory (needSample ? arcs : 0);
    scrubEngine.prepareLiveVocoderMemory (needLive ? arcs : 0);
}

void MainComponent::publishModelSnapshot()
{
    // v1.33.4 (M21): the vocoder's memory is prepared before the snapshot
    // goes out, so an arc that plays through the vocoder finds it there
    prepareVocodersIfNeeded (engine.getSamplePitchMode(), engine.getLivePitchMode());
    auto* snap = new ModelSnapshot();
    snap->waveforms = waveforms;
    snap->envelopes = envelopes;
    snap->pages = pages;
    snap->slots = sampleSlots; // shared buffers: only the pointers are copied
    snap->currentPage = currentPage;
    snap->pageEpoch = pageEpoch.load(std::memory_order_relaxed);
    snap->seqPages = seqPages;
    snap->loopPage = loopPage;
    // v1.33.4 (M22): the revision this snapshot was built from
    snap->revision = modelRevision.load(std::memory_order_relaxed);
    snapshots.publish(snap); // frees the snapshots the reader retired
    lastPublishedRevision = snap->revision;
    lastModelPublishMs = (uint64_t) juce::Time::getMillisecondCounter();
    snapshotPublishCount.fetch_add(1, std::memory_order_relaxed);
}

void MainComponent::seekPlayhead(double t)
{
    playheadSec = t;
    playheadAtEnd.store(false, std::memory_order_relaxed);
    seekTo.store(t, std::memory_order_relaxed);
    seekSerial.fetch_add(1, std::memory_order_relaxed);
}

void MainComponent::bumpPageEpoch()
{
    pageEpoch.fetch_add(1, std::memory_order_relaxed);
}

void MainComponent::requestEngineReset()
{
    engineResetSerial.fetch_add(1, std::memory_order_relaxed);
}

double MainComponent::displayPlayhead() const
{
    return transportActive()
               ? playheadPublished.load(std::memory_order_relaxed)
               : playheadSec;
}

void MainComponent::startPlayback()
{
    const auto state = transport();

    // F1: Play while the transport is starting or playing is a no-op. The
    // accepted decision (2026-11-17) is explicit: a restart is its own
    // command, not something a second Play click can do by accident. The
    // old code reset the engine here, which cut every voice and re-anchored
    // it mid-phrase - the reported "Start/Stop" bug.
    if (state == TransportState::starting || state == TransportState::playing)
        return;

    // Play while the stop fade is still running: cancel it with the SAME
    // gain envelope. The fade had not finished, so the engine was not reset
    // and the voices still hold their positions; the gain simply ramps back
    // up from where the fade reached. (The old code cleared wantsStop and
    // forced the next block to full gain - a step at the block boundary.)
    if (state == TransportState::stopping)
    {
        lastStopClickMs = -1.0e9; // a Stop after this Play is a single click
        transportState.store ((int) TransportState::playing, std::memory_order_relaxed);
        score.setPlaying (true);
        playBtn.setButtonText ("Playing...");
        return;
    }

    // Stopped: a fresh start.
    // a Stop click after Play is a fresh single click, not a double-click
    lastStopClickMs = -1.0e9;
    bool hasLive = false;
    {
        juce::ScopedLock l(dataLock);
        // the audio thread owns the playhead: only seek when the piece ended
        const double rate = audioPlayRate;
        // direction-aware seek: playing backwards from the start wraps to the end
        if (rate < 0.0)
        {
            if (playheadAtEnd.load(std::memory_order_relaxed)
                || displayPlayhead() <= 0.05)
                seekPlayhead(curPage().durationSec);
        }
        else if (playheadAtEnd.load(std::memory_order_relaxed)
              || displayPlayhead() >= curPage().durationSec - 0.05)
        {
            seekPlayhead(0.0);
        }
        requestEngineReset(); // re-anchor every voice at the playhead
        transportState.store ((int) TransportState::starting, std::memory_order_relaxed);
        for (auto& a : curPage().arcs)
            if (a.mode == 2 && ! a.muted) { hasLive = true; break; }
    }
    // Live arcs are silent without input: enable it on first playback.
    if (hasLive && ! inputEnabled)
        ensureLiveInputFor([](bool){});
    score.setPlaying(true);
    playBtn.setButtonText("Playing...");
}

void MainComponent::stopPlayback(bool fromAudio)
{
    {
        juce::ScopedLock l(dataLock);
        const auto state = transport();
        if (state == TransportState::starting || state == TransportState::playing)
        {
            // F1: the stop is a state, not a flag. The audio thread keeps
            // rendering and ramps the score to silence over kStopFadeSec,
            // then halts on its own (Stopping -> Stopped).
            transportState.store ((int) TransportState::stopping, std::memory_order_relaxed);
        }
        else if (state == TransportState::stopped)
        {
            // Idle: a plain Stop click is a no-op. Only the stop posted by
            // the audio thread knows where the fade / page end landed -
            // syncing on a plain Stop click while idle would overwrite a
            // scrub with the last playback position.
            if (fromAudio)
            {
                const double p = playheadPublished.load(std::memory_order_relaxed);
                if (p > 0.0) playheadSec = p;
            }
        }
        // Stopping: a second Stop is a no-op - the fade is already running.
    }
    score.setPlaying(transportActive());
    playBtn.setButtonText("Play"); // reusable at once (Play cancels the fade)
}

//==============================================================================
// v1.33.4 (M21): the audio buffers are sized on the message thread, for the
// largest block a host may send. They used to be grown inside the audio
// callback - a malloc on the audio thread.
void MainComponent::prepareAudioBuffers (int samplesPerBlockExpected)
{
    const int n = juce::jmax (kMaxAudioBlock, samplesPerBlockExpected);
    scoreBuf.setSize (2, n, false, false, true);
    freezeBuf.setSize (2, n, false, false, true);
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // v1.33.4 (L14): the rate crosses the thread boundary, so it is an
    // atomic count of frames per second (the audio thread reads it).
    audioSampleRateHz.store ((int) std::lround (sampleRate > 0.0 ? sampleRate : 44100.0),
                             std::memory_order_relaxed);
    engine.prepare(audioSampleRate(), samplesPerBlockExpected);
    // v1.33.4 (U6-B): the realtime engine bounds its own work; the scrub
    // preview is a single block and stays exact.
    engine.setOverloadGuardEnabled (true);
    scrubEngine.prepare(audioSampleRate(), samplesPerBlockExpected);
    scrubEngine.setScrubPreview(true);
    // v1.33.4 (M21): the voice pools exist now, so the vocoder's working
    // memory can be prepared - the mode may have been chosen before the
    // device started (a piece that carries its own pitch modes)
    prepareVocodersIfNeeded (engine.getSamplePitchMode(), engine.getLivePitchMode());
    scrubPreviewLen = juce::jmax(256, (int) (audioSampleRate() * 0.05)); // ~50 ms
    liveRingSize = juce::jmax(1024, (int) (audioSampleRate() * kLiveRingSec));
    liveRingA.assign((size_t) liveRingSize, 0.0f);
    liveRingB.assign((size_t) liveRingSize, 0.0f);
    liveWritePos = 0;
    liveTapActive = false;
    // v1.20 (wave 18): the live reverse buffers. They are allocated once, at
    // the maximum duration (2 s); the chosen duration only uses the first
    // N frames - so the message thread never reallocates them while the
    // audio thread may be reading.
    {
        const int maxFrames = juce::jmax(1024, (int) std::lround(2.0 * audioSampleRate()));
        for (auto& b : revBuf)
        {
            b.setSize(2, maxFrames, false, false, true);
            b.clear();
        }
        revBufFrames.store(juce::jmax(1024, (int) std::lround(revBufSec * audioSampleRate())));
        revRestart.store(true, std::memory_order_relaxed);
    }
    // F1: a device/host restart is not a transport edge. Keep the state the
    // user asked for, but start the envelope at its target so the first
    // block does not ramp from a stale gain. A fade in progress is over -
    // its audio context is gone.
    {
        auto s = transport();
        if (s == TransportState::starting || s == TransportState::stopping)
            s = (s == TransportState::starting) ? TransportState::playing
                                                : TransportState::stopped;
        transportState.store ((int) s, std::memory_order_relaxed);
        transportGain = (s == TransportState::playing) ? 1.0f : 0.0f;
        transportRampTotal = 0;
        transportSeenState = (int) s;
    }
    // v1.33.4 (M21): the score and freeze buffers are sized here, on the
    // message thread, for the LARGEST block a host may send. They used to
    // be grown inside the audio callback, which is a malloc on the audio
    // thread (a glitch waiting to happen).
    prepareAudioBuffers (samplesPerBlockExpected);
    scoreGain = 1.0f;
    scoreTail = 0.0f;
    masterSmoothed = masterGain.load();
    inGainSmoothed = inputGain.load();
    // Device restarted under a running take: the buffered rate is stale, so
    // the next audio block hands the partial take off for discard.
    if (capturing.load()) capAbortPending = true;
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    // Live input runs before the clear: the player buffer initially carries
    // the input channels, and the clear would wipe them.
    const bool mon = tapLiveInput(info);
    // v1.33.4 (M24): only the OUTPUT channels are cleared and written. A
    // host layout can have more input channels than output ones (stereo in,
    // mono out), and the extra input channels may be read-only memory - the
    // old clear (which wiped every channel of the view) wrote to them.
    const int outCh = juce::jmin (outChannels.load (std::memory_order_relaxed),
                                  info.buffer->getNumChannels());
    for (int c = 0; c < outCh; ++c)
        juce::FloatVectorOperations::clear (info.buffer->getWritePointer (c, info.startSample),
                                            info.numSamples);

    const int n = info.numSamples;
    float* L = info.buffer->getWritePointer(0, info.startSample);
    float* R = outCh > 1 ? info.buffer->getWritePointer(1, info.startSample) : nullptr;
    // v1.33.4 (L14): the rate crosses the thread boundary, so it is read
    // ONCE per block (a relaxed atomic load) and handed down.
    const double sr = audioSampleRate();
    lastBlockRateHz.store ((int) std::lround (sr), std::memory_order_relaxed);
    const int scoreFadeN = juce::jmax(1, (int) std::lround(sr * (double) dragSmoothMs.load(std::memory_order_relaxed) / 1000.0));

    if (mon) // monitor mix: live passthrough, independent of the transport
        mixMonitorInput (L, R, n);

    // v1.15: the score renders into its own buffer, so a block that cannot
    // be rendered (the UI thread holds the model lock) can be faded out
    // instead of cut. A hard cut from the previous block's last sample to
    // zero is a click - and it happens whatever the host's buffer size is.
    //
    // v1.33.4 (M21): the buffers are preallocated for the largest block a
    // host may send (prepareAudioBuffers), so this only grows them for a
    // host that breaks the VST3 block-size limit - never in the normal case.
    if (scoreBuf.getNumChannels() < 2 || scoreBuf.getNumSamples() < n)
        scoreBuf.setSize(2, n, false, false, true);
    scoreBuf.clear();
    float* SL = scoreBuf.getWritePointer(0);
    float* SR = scoreBuf.getWritePointer(1);

    if (freezeBuf.getNumChannels() < 2 || freezeBuf.getNumSamples() < n)
        freezeBuf.setSize(2, n, false, false, true);
    freezeBuf.clear();
    float* FL = freezeBuf.getWritePointer(0);
    float* FR = freezeBuf.getWritePointer(1);

    bool rendered = false;
    bool modelChanged = false;
    const bool playingAtStart = transportActive();
    const bool transportStarting = (transport() == TransportState::starting);
    {
        // v1.18 (wave 15): no lock on the audio thread. Take the newest
        // immutable model snapshot and render from it - the message thread
        // owns the live model and frees retired snapshots on its own thread.
        ModelSnapshot* snap = snapshots.take(liveSnapshot);

        // a seek / scrub from the UI moves the audio playhead
        const int seek = seekSerial.load(std::memory_order_relaxed);
        if (seek != seenSeekSerial)
        {
            seenSeekSerial = seek;
            audioPlayhead = seekTo.load(std::memory_order_relaxed);
            engine.reset();
        }

        const int fs = scrubFreezeStartSerial.load(std::memory_order_relaxed);
        if (fs != seenFreezeStartSerial)
        {
            seenFreezeStartSerial = fs;
            audioPlayhead = scrubFreezePlayhead.load(std::memory_order_relaxed);
            engine.reset();
        }

        // Play asks for fresh voices (no fade: the transport is starting)
        const int reset = engineResetSerial.load(std::memory_order_relaxed);
        if (reset != seenEngineResetSerial)
        {
            seenEngineResetSerial = reset;
            engine.reset();
        }

        // v1.25 (wave 22): a direction change while playing crossfades over
        // ~20 ms instead of re-anchoring mid-stream (which clicked).
        const int dcs = dirChangeSerial.load(std::memory_order_relaxed);
        if (dcs != seenDirChangeSerial)
        {
            seenDirChangeSerial = dcs;
            dirFadeOutRemaining = kDirCrossfadeSamples;
            audioPlayRate = prevPlayRate.load(std::memory_order_relaxed);
            // v1.30: the live ring's read direction follows the transport
            // when reverse is on, so the live voices must re-anchor on the
            // other ring (forward <-> mirrored).
            engine.resetLiveVoices();
        }

        if (snap != nullptr)
        {
            // A page change must be adopted even while stopped, so the next
            // Play starts on the page the UI shows.
            if (snap->pageEpoch != seenPageEpoch)
            {
                seenPageEpoch = snap->pageEpoch;
                audioPage = snap->pages.empty() ? 0
                    : juce::jlimit(0, (int) snap->pages.size() - 1, snap->currentPage);
                audioPlayhead = 0.0;
                engine.reset();
            }

            // v1.28: gain crossfade between preview and freeze
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            double previewGain = scrubPreviewActive.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            double freezeGain = scrubFreezeActive.load(std::memory_order_relaxed) ? 1.0 : 0.0;
            const double ts = scrubFreezeTransitionStartMs.load(std::memory_order_relaxed);
            const double te = scrubFreezeTransitionEndMs.load(std::memory_order_relaxed);
            if (scrubFreezeActive.load(std::memory_order_relaxed) && te > ts && nowMs < te)
            {
                const double prog = juce::jlimit(0.0, 1.0, (nowMs - ts) / (te - ts));
                freezeGain = prog;
                if (scrubPreviewActive.load(std::memory_order_relaxed))
                    previewGain = 1.0 - prog;
            }

            // v1.28: scrub audition (short preview while dragging the playhead)
            // and the freeze sustain (the click without a drag)
            AudioBlock block { L, R, SL, SR, FL, FR, n, playingAtStart };
            block.sr = sr;   // v1.33.4 (L14): the rate, read once per block
            renderScrubPreview (*snap, block, previewGain);
            renderScrubFreeze (*snap, block, freezeGain);

            // Absorb model edits while stopped (no sound to fade).
            const int epoch = modelEpoch.load(std::memory_order_relaxed);
            modelChanged = (epoch != seenModelEpoch);
            seenModelEpoch = epoch;

            if (playingAtStart)
            {
                rendered = true;
                renderScoreBlock(*snap, SL, SR, n, sr);
                // v1.25 (wave 22): direction-change crossfade. Applied on top of
                // the rendered score; the scoreGain path is forced to 1.0 so it
                // does not double-ramp.
                if (dirFadeOutRemaining > 0 || dirFadeInRemaining > 0)
                {
                    scoreGain = 1.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        float g = 1.0f;
                        if (dirFadeOutRemaining > 0)
                        {
                            int done = kDirCrossfadeSamples - dirFadeOutRemaining;
                            int pos  = done + i;
                            if (pos > kDirCrossfadeSamples) pos = kDirCrossfadeSamples;
                            g = 1.0f - (float) pos / (float) kDirCrossfadeSamples;
                        }
                        else
                        {
                            int done = kDirCrossfadeSamples - dirFadeInRemaining;
                            int pos  = done + i;
                            if (pos > kDirCrossfadeSamples) pos = kDirCrossfadeSamples;
                            g = (float) pos / (float) kDirCrossfadeSamples;
                        }
                        SL[i] *= g;
                        if (SR != nullptr) SR[i] *= g;
                    }
                    if (dirFadeOutRemaining > 0)
                    {
                        dirFadeOutRemaining -= n;
                        if (dirFadeOutRemaining <= 0)
                        {
                            dirFadeOutRemaining = 0;
                            audioPlayRate = playRate.load(std::memory_order_relaxed);
                            engine.reset();
                            dirFadeInRemaining = kDirCrossfadeSamples;
                        }
                    }
                    else
                    {
                        dirFadeInRemaining -= n;
                        if (dirFadeInRemaining < 0) dirFadeInRemaining = 0;
                    }
                }
            }
        }
    }

    // v1.15: fade the score in/out, so that no block boundary can click
    applyScoreFade (SL, SR, n, scoreFadeN, rendered, modelChanged, transportStarting);

    // F1: the transport's one gain envelope (start and stop) belongs to the
    // score, not the monitor input - stopping must not fade a live mic.
    // `scoreAudible` is the same condition applyScoreFade fades in on: a
    // model-change block is silent, and the start fade must not be spent on
    // it (the first audible block would begin part-way up the ramp).
    applyTransportGain (SL, SR, n, sr, rendered && ! modelChanged);

    // add the score to the output (the monitor mix is already in there)
    mixMasterGain (L, R, SL, SR, n);

    // a model edit was audible this block: drop the old voices now, on the
    // audio thread (the message thread never touches the engine any more)
    if (modelChanged)
        engine.reset();
}

//==============================================================================
// v1.30 (5.3 refactor): the parts of the audio block that were inlined in
// getNextAudioBlock() are their own methods now. They all run on the audio
// thread, so they only touch the members - no message thread, no locks.
//==============================================================================
void MainComponent::mixMonitorInput (float* L, float* R, int n)
{
    const float* s0 = inScratch.getReadPointer(0);
    const float* s1 = inScratch.getNumChannels() > 1 ? inScratch.getReadPointer(1) : s0;
    for (int i = 0; i < n; ++i)
    {
        // v1.15: the monitor gain is smoothed per sample - a step in a
        // gain (a slider move, host automation) is a click
        inGainSmoothed += (inputGain.load() - inGainSmoothed) * kGainSmooth;
        L[i] += s0[i] * inGainSmoothed;
        if (R != nullptr) R[i] += s1[i] * inGainSmoothed;
    }
}

void MainComponent::applyScoreFade (float* SL, float* SR, int n, int scoreFadeN,
                                    bool rendered, bool modelChanged,
                                    bool transportStarting)
{
    if (rendered && ! modelChanged)
    {
        if (transportStarting)
        {
            // F1: the transport's gain envelope fades the start in. A second
            // fade here would make the first 30 ms twice as long and the two
            // ramps would multiply; one envelope owns both transport edges.
            scoreGain = 1.0f;
        }
        else if (scoreGain < 1.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                // v1.18: clamp the ramp - without it a block longer than
                // kScoreFadeSamples would keep raising the gain (a 2048-block
                // fade-in peaked at 21x, an audible blast after a model change)
                const float k = juce::jmin(1.0f, (float) i / (float) scoreFadeN);
                const float g = scoreGain + (1.0f - scoreGain) * k;
                SL[i] *= g; SR[i] *= g;
            }
            scoreGain = 1.0f;
        }
        scoreTail = (n > 0) ? SL[n - 1] : 0.0f;
    }
    else
    {
        // no score this block (or the model changed under it): ramp the last
        // sample down to zero instead of cutting it (the step from the
        // previous block would be a click)
        for (int i = 0; i < n; ++i)
        {
            const float k = juce::jmin(1.0f, (float) i / (float) scoreFadeN);
            const float v = scoreTail * scoreGain * (1.0f - k);
            SL[i] = v; SR[i] = v;
        }
        scoreGain = 0.0f;
        scoreTail = 0.0f;
    }
}

void MainComponent::mixMasterGain (float* L, float* R, const float* SL, const float* SR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        // v1.15: the master gain is smoothed per sample (a step = a click)
        masterSmoothed += (masterGain.load() - masterSmoothed) * kGainSmooth;
        L[i] += SL[i] * masterSmoothed;
        if (R != nullptr) R[i] += SR[i] * masterSmoothed;
    }
}

void MainComponent::renderScrubPreview (const ModelSnapshot& snap, AudioBlock& b, double previewGain)
{
    if (b.playingAtStart || ! scrubPreviewActive.load(std::memory_order_relaxed))
        return;

    int remaining = scrubPreviewRemaining.load(std::memory_order_relaxed);
    if (remaining <= 0 || snap.pages.empty())
    {
        scrubPreviewActive.store(false, std::memory_order_relaxed);
        return;
    }

    const int p = juce::jlimit(0, (int) snap.pages.size() - 1, audioPage);
    const upic::Page& pg = snap.pages[(size_t) p];
    const int chunk = juce::jmin(b.numSamples, remaining);
    juce::FloatVectorOperations::clear (b.scoreL, chunk);
    if (b.scoreR != nullptr) juce::FloatVectorOperations::clear (b.scoreR, chunk);

    double ph = scrubPreviewPlayhead.load(std::memory_order_relaxed);
    const double rate = scrubRate.load(std::memory_order_relaxed);
    const int sps = scrubPreviewSerial.load(std::memory_order_relaxed);
    if (sps != seenScrubPreviewSerial)
    {
        seenScrubPreviewSerial = sps;
        scrubEngine.reset(); // re-anchor to the dragged playhead
    }

    scrubEngine.render (pg, ph, snap.waveforms, snap.envelopes, snap.slots,
                        b.scoreL, (b.scoreR != nullptr ? b.scoreR : b.scoreL),
                        chunk, 1.0f, rate);

    const int fade = juce::jmin (chunk / 4, (int) (b.sr * 0.005));
    for (int i = 0; i < chunk; ++i)
    {
        float g = 1.0f;
        if (fade > 0)
        {
            if (i < fade) g = (float) i / (float) fade;
            else if (i >= chunk - fade) g = (float) (chunk - 1 - i) / (float) fade;
        }
        b.scoreL[i] *= g;
        if (b.scoreR != nullptr) b.scoreR[i] *= g;
    }

    const float mg = masterGain.load(std::memory_order_relaxed) * (float) previewGain;
    for (int i = 0; i < chunk; ++i)
    {
        b.outL[i] += b.scoreL[i] * mg;
        if (b.outR != nullptr)
            b.outR[i] += (b.scoreR != nullptr ? b.scoreR[i] : b.scoreL[i]) * mg;
    }

    scrubPreviewPlayhead.store (ph + (double) chunk * rate / b.sr, std::memory_order_relaxed);
    const int left = remaining - chunk;
    scrubPreviewRemaining.store (left, std::memory_order_relaxed);
    if (left <= 0) scrubPreviewActive.store (false, std::memory_order_relaxed);
}

void MainComponent::renderScrubFreeze (const ModelSnapshot& snap, AudioBlock& b, double freezeGain)
{
    if (b.playingAtStart || ! scrubFreezeActive.load(std::memory_order_relaxed))
        return;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double stopAt = scrubFreezeStopAtMs.load(std::memory_order_relaxed);
    if (stopAt > 0.0 && nowMs >= stopAt)
    {
        scrubFreezeActive.store (false, std::memory_order_relaxed);
        scrubFreezeStopAtMs.store (0.0, std::memory_order_relaxed);
        return;
    }

    if (snap.pages.empty())
        return;

    const int p = juce::jlimit(0, (int) snap.pages.size() - 1, audioPage);
    const upic::Page& pg = snap.pages[(size_t) p];
    juce::FloatVectorOperations::clear (b.freezeL, b.numSamples);
    if (b.freezeR != nullptr) juce::FloatVectorOperations::clear (b.freezeR, b.numSamples);

    engine.render (pg, audioPlayhead, snap.waveforms, snap.envelopes, snap.slots,
                   b.freezeL, (b.freezeR != nullptr ? b.freezeR : b.freezeL),
                   b.numSamples, 1.0f, 0.0, true);

    float gain = 1.0f;
    if (stopAt > 0.0)
    {
        const double rem = stopAt - nowMs;
        // v1.33.4 (L14): the tail length is written by the slider (the
        // message thread) and read here (the audio thread) - it is atomic.
        const int tailMs = scrubTailMs.load (std::memory_order_relaxed);
        gain = (float) juce::jlimit (0.0, 1.0, rem / juce::jmax (1, tailMs));
    }

    const float mg = masterGain.load(std::memory_order_relaxed) * gain * (float) freezeGain;
    for (int i = 0; i < b.numSamples; ++i)
    {
        b.outL[i] += b.freezeL[i] * mg;
        if (b.outR != nullptr)
            b.outR[i] += (b.freezeR != nullptr ? b.freezeR[i] : b.freezeL[i]) * mg;
    }
}

void MainComponent::applyTransportGain (float* SL, float* SR, int n, double sr,
                                        bool scoreAudible)
{
    const int state = transportState.load (std::memory_order_relaxed);

    // The first block after a load (and any block the score fades out of) is
    // silent. Hold the envelope at 0 and let the first audible block start
    // the ramp: spending the fade on silence would make that block begin
    // part-way up and step. The score fade owns the silent block's tail.
    if (state == (int) TransportState::starting && ! scoreAudible)
    {
        transportGain = 0.0f;
        transportRampTotal = 0;
        transportSeenState = -1; // the next block re-evaluates the edge
        return;
    }

    const int fadeSamples = juce::jmax (1, (int) std::lround (kStopFadeSec * sr));

    // A state change starts ONE ramp from wherever the gain is. That is what
    // makes a rapid sequence continuous: Play during Stopping ramps up from
    // the faded value instead of stepping back to 1.0 at the block boundary.
    if (state != transportSeenState)
    {
        transportSeenState = state;
        const float target = (state == (int) TransportState::starting
                           || state == (int) TransportState::playing) ? 1.0f : 0.0f;
        if (target != transportGain)
        {
            transportRampFrom = transportGain;
            transportRampTo = target;
            transportRampPos = 0;
            transportRampTotal = fadeSamples;
        }
        else
        {
            transportRampTotal = 0;
        }
    }

    for (int i = 0; i < n; ++i)
    {
        if (transportRampTotal > 0)
        {
            ++transportRampPos;
            const float k = juce::jmin (1.0f, (float) transportRampPos
                                                / (float) transportRampTotal);
            transportGain = transportRampFrom
                          + (transportRampTo - transportRampFrom) * k;
            if (transportRampPos >= transportRampTotal)
            {
                transportGain = transportRampTo;
                transportRampTotal = 0;
            }
        }
        SL[i] *= transportGain;
        if (SR != nullptr) SR[i] *= transportGain;
    }

    // Complete the two edges. The compare-exchange is the race fix: a Play
    // that cancelled the stop has already stored Playing, and the fade's
    // completion must neither overwrite that nor reset the voices.
    if (state == (int) TransportState::starting && transportGain >= 1.0f)
    {
        int expected = (int) TransportState::starting;
        transportState.compare_exchange_strong (expected,
                                                (int) TransportState::playing,
                                                std::memory_order_relaxed);
    }
    else if (state == (int) TransportState::stopping && transportGain <= 0.0f)
    {
        int expected = (int) TransportState::stopping;
        if (transportState.compare_exchange_strong (expected,
                                                    (int) TransportState::stopped,
                                                    std::memory_order_relaxed))
        {
            engine.reset();
            // UI must not be touched from the audio thread -> SafePointer.
            pendingStopAtEnd.store (true, std::memory_order_release);
        }
    }
}

//==============================================================================
// v1.15: the score render for one block, into its own buffer - the caller
// fades it, so a block that cannot be rendered is a dip instead of a click.
// v1.18 (wave 15): renders from an immutable snapshot; the audio thread never
// takes dataLock and never touches the message thread's model.
void MainComponent::renderScoreBlock(const ModelSnapshot& snap, float* SL, float* SR, int n, double sr)
{
    // v1.33.4 (L14): the rate is handed down from the callback, which read
    // it once for the block (it crosses the thread boundary, so it is
    // atomic).

    // v1.5: publish the live input ring view for mode-2 (live input) arcs
    const int revFrames = revBufFrames.load(std::memory_order_relaxed);
    if (revFrames > 0 && revBuf[revPlayIdx].getNumSamples() >= revFrames)
    {
        // v1.20/1.21: the reverse - the shifter can read the frozen, mirrored
        // segment instead of the live ring. The arc decides which one (the
        // arc list's Rev box). v1.30: the capture runs continuously, so the
        // global transport reverse can force the live input backwards even
        // when the Live tab's Rev button is off.
        const auto& play = revBuf[revPlayIdx];
        engine.setLiveReverse(play.getReadPointer(0),
                              play.getNumChannels() > 1 ? play.getReadPointer(1) : nullptr,
                              revFrames, (long long) revPlayHead, liveTapActive);
    }
    else
    {
        engine.setLiveReverse(nullptr, nullptr, 0, 0, false);
    }
    engine.setLiveInput(liveRingA.data(), liveRingB.data(), liveRingSize,
                        liveWritePos, liveTapActive);

    const auto publishPlayhead = [this]
    { playheadPublished.store(audioPlayhead, std::memory_order_relaxed); };

    // The audio thread must not allocate or lock, so it cannot use
    // MessageManager::callAsync - that does `new MessageBase` and takes the
    // message-queue lock. It publishes a packed (epoch, page) request instead,
    // and the 30 Hz timer applies it on the message thread. The epoch guard
    // drops a late request if the user changed the page structure meanwhile.
    const auto notifyPage = [this](int next)
    {
        const auto epoch = (uint64_t) (uint32_t) pageEpoch.load(std::memory_order_relaxed);
        const auto page  = (uint64_t) (uint32_t) (next + 1);   // +1: 0 means "none"
        pendingPageRequest.store ((epoch << 32) | page, std::memory_order_release);
    };

    // Render in sub-chunks split at the page boundary so looping never
    // leaves an unrendered (silent) tail at the seam.
    int offset = 0;
    while (offset < n)
    {
        if (snap.pages.empty())
        {
            transportState.store ((int) TransportState::stopped,
                                  std::memory_order_relaxed);
            engine.reset(); publishPlayhead(); return;
        }
        // the message thread may have deleted a page since the last snapshot
        if (audioPage < 0 || audioPage >= (int) snap.pages.size())
            audioPage = juce::jlimit(0, (int) snap.pages.size() - 1, audioPage);
        const auto& pg = snap.pages[(size_t) audioPage];

        if (pg.durationSec < 0.001)
        {
            transportState.store ((int) TransportState::stopped,
                                  std::memory_order_relaxed);
            engine.reset(); publishPlayhead(); return;
        }

        const double rate = playRate.load(std::memory_order_relaxed);
        const bool reverse = rate < 0.0;

        if (reverse ? (audioPlayhead <= 0.0) : (audioPlayhead >= pg.durationSec))
        {
            if (reverse)
            {
                if (snap.seqPages)
                {
                    if (audioPage > 0)
                    {
                        --audioPage; audioPlayhead = snap.pages[(size_t) audioPage].durationSec;
                        engine.reset(); notifyPage(audioPage);
                    }
                    else
                    {
                        audioPlayhead = 0.0;
                        transportState.store ((int) TransportState::stopped,
                                              std::memory_order_relaxed);
                        playheadAtEnd.store(false, std::memory_order_relaxed);
                        engine.reset(); publishPlayhead();
            pendingStopAtEnd.store (true, std::memory_order_release);
                        return;
                    }
                }
                else if (snap.loopPage)
                {
                    if (audioPage > 0)
                    {
                        --audioPage; audioPlayhead = snap.pages[(size_t) audioPage].durationSec;
                        engine.reset(); notifyPage(audioPage);
                    }
                    else
                    {
                        const int last = (int) snap.pages.size() - 1;
                        audioPage = last; audioPlayhead = snap.pages[(size_t) last].durationSec;
                        engine.reset(); notifyPage(audioPage);
                    }
                }
                else if (audioPage > 0)
                {
                    --audioPage; audioPlayhead = snap.pages[(size_t) audioPage].durationSec;
                    engine.reset(); notifyPage(audioPage);
                }
                else
                {
                    audioPlayhead = 0.0;
                    transportState.store ((int) TransportState::stopped,
                                          std::memory_order_relaxed);
                    playheadAtEnd.store(false, std::memory_order_relaxed);
                    engine.reset(); publishPlayhead();
            pendingStopAtEnd.store (true, std::memory_order_release);
                    return;
                }
            }
            else
            {
                if (snap.seqPages)
                {
                    const int next = upic::seqNextPage(audioPage, (int) snap.pages.size(), snap.loopPage);
                    if (next >= 0)
                    {
                        audioPage = next; audioPlayhead = 0.0; engine.reset(); notifyPage(next);
                    }
                    else
                    {
                        audioPlayhead = pg.durationSec;
                        transportState.store ((int) TransportState::stopped,
                                              std::memory_order_relaxed);
                        playheadAtEnd.store(true, std::memory_order_relaxed);
                        engine.reset(); publishPlayhead();
            pendingStopAtEnd.store (true, std::memory_order_release);
                        return;
                    }
                }
                else if (snap.loopPage)
                {
                    audioPlayhead = 0.0; engine.reset();
                }
                else if (audioPage + 1 < (int) snap.pages.size())
                {
                    ++audioPage; audioPlayhead = 0.0; engine.reset(); notifyPage(audioPage);
                }
                else
                {
                    audioPlayhead = pg.durationSec;
                    transportState.store ((int) TransportState::stopped,
                                          std::memory_order_relaxed);
                    playheadAtEnd.store(true, std::memory_order_relaxed);
                    engine.reset(); publishPlayhead();
            pendingStopAtEnd.store (true, std::memory_order_release);
                    return;
                }
            }
            continue;
        }

        // v1.33.4 (H8): the page's own time and the audio time are two
        // views of the same page. The exact map - the same one the export
        // uses - says how much audio the rest of the page needs and where
        // the score lands after this block, so the sound and the playhead
        // stay in step and a warped page is neither cut short nor run past
        // its end.
        const double a0 = pg.audioTimeAtScore (audioPlayhead);
        const double remainAudio = reverse ? a0 : (pg.audioDurationSec() - a0);
        const double absRate = juce::jmax (1.0e-6, std::abs (rate));
        int chunk = (int) std::floor(remainAudio * sr / absRate);
        if (chunk < 1) chunk = 1;
        chunk = juce::jlimit(1, n - offset, chunk);

        // the block's average rate, so the block's content matches the
        // playhead exactly (without a warp it is exactly the old rate)
        const double a1 = a0 + (double) chunk * rate / sr;
        const double s1 = pg.scoreAtAudioTime (a1);
        const double blockRate = rate * pg.averageRate (audioPlayhead, (double) chunk * rate / sr);

        engine.render(pg, audioPlayhead, snap.waveforms, snap.envelopes, snap.slots,
                      SL + offset, (SR != nullptr ? SR + offset : nullptr), chunk, 1.0f,
                      blockRate);

        if (! snap.loopPage && ((reverse && audioPage == 0) || (! reverse && audioPage + 1 >= (int) snap.pages.size())))
        {
            for (int k = 0; k < chunk; ++k)
            {
                const double tEnd = reverse
                    ? (audioPlayhead + (double) k / sr)
                    : (pg.durationSec - (audioPlayhead + (double) k / sr));
                const float g = (float) upic::stopFadeGain(tEnd, kStopFadeSec);
                SL[offset + k] *= g;
                SR[offset + k] *= g;
            }
        }

        audioPlayhead = s1;
        offset += chunk;
    }
    publishPlayhead();
}

void MainComponent::releaseResources()
{
    // v1.18: keep the live snapshot across a device/host restart - it is
    // immutable and valid; only the destructor hands it back (after the audio
    // thread has been shut down). Retiring it here would leave a restarted
    // host with no model until the 30 Hz timer ticks.
}

//==============================================================================
// The audio settings: the device selector as an in-app overlay (on
// Android JUCE has no floating windows, so a dialog could never be
// dismissed there).

void MainComponent::showAudioSettings()
{
    // v1.7: an in-app overlay instead of a DialogWindow - on Android JUCE
    // has no floating windows, so the dialog had no title bar and no close
    // button there and could never be dismissed.
    // Inputs are allowed: enabling them requests RECORD_AUDIO at runtime
    // (manifest) and falls back to output-only when denied or absent.
    closeSettings();
    if (audioSelector == nullptr)
        audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            deviceManager, 0, 2, 0, 2, false, false, true, false);
    audioOverlay.addAndMakeVisible(*audioSelector);
    // v1.26: keep the OK button above the selector. On some layouts the
    // selector's viewport can otherwise sit over the button and eat its click.
    audioOkBtn.toFront(false);
    audioOverlay.setBounds(getLocalBounds());
    audioOverlay.setVisible(true);
    audioOverlay.toFront(false);
    resized(); // position the selector + OK button inside the card
}

void MainComponent::closeAudioSettings()
{
    // device changes already applied live in the selector; OK / backdrop
    // tap / Esc just dismiss the overlay
    audioOverlay.setVisible(false);
    if (audioSelector != nullptr && audioSelector->getParentComponent() == &audioOverlay)
        audioOverlay.removeChildComponent(audioSelector.get());
}
