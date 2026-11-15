#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The audio path: the snapshot the audio thread reads, the block callback,
// the score render, the live input and the capture.

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
    return isPlaying.load(std::memory_order_relaxed)
               ? playheadPublished.load(std::memory_order_relaxed)
               : playheadSec;
}

void MainComponent::startPlayback()
{
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
        isPlaying.store(true);
        wantsStop.store(false);   // cancel any pending fade-out
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
        if (isPlaying.load())
        {
            // click-free stop (UPISketch "playback now fades out smoothly"):
            // keep rendering and ramp the output to silence over kStopFadeSec,
            // then the audio callback halts on its own.
            wantsStop.store(true);
        }
        else
        {
            wantsStop.store(false); // idle: clear any stale stop request
            // Only the stop posted by the audio thread knows where the fade /
            // page end landed - syncing on a plain Stop click while idle would
            // overwrite a scrub with the last playback position.
            if (fromAudio)
            {
                const double p = playheadPublished.load(std::memory_order_relaxed);
                if (p > 0.0) playheadSec = p;
            }
        }
    }
    score.setPlaying(isPlaying.load() || wantsStop.load());
    playBtn.setButtonText("Play"); // reusable at once (startPlayback cancels the fade)
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
    stopFadeTotal = juce::jmax(1, (int) std::ceil(kStopFadeSec * audioSampleRate()));
    stopFadePos = 0;
    wantsStop.store(false);
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
    const bool playingAtStart = isPlaying.load(std::memory_order_relaxed);
    const bool stopping = wantsStop.load(std::memory_order_relaxed);
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
    applyScoreFade (SL, SR, n, scoreFadeN, rendered, modelChanged);

    // add the score to the output (the monitor mix is already in there)
    mixMasterGain (L, R, SL, SR, n);

    // a model edit was audible this block: drop the old voices now, on the
    // audio thread (the message thread never touches the engine any more)
    if (modelChanged)
        engine.reset();

    // Click-free manual stop: keep rendering the page for kStopFadeSec while
    // the output ramps to silence, then halt (playhead keeps its position).
    applyStopFade (L, R, n, stopping, sr);
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
                                    bool rendered, bool modelChanged)
{
    if (rendered && ! modelChanged)
    {
        if (scoreGain < 1.0f)
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

void MainComponent::applyStopFade (float* L, float* R, int n, bool stopping, double sr){
    if (stopping && isPlaying.load(std::memory_order_relaxed))
    {
        for (int i = 0; i < n; ++i)
        {
            const double rSec = (double) juce::jmax(0, stopFadeTotal - stopFadePos) / sr;
            const float g = (float) upic::stopFadeGain(rSec, (double) stopFadeTotal / sr);
            L[i] *= g;
            if (R != nullptr) R[i] *= g;
            ++stopFadePos;
        }
        if (stopFadePos >= stopFadeTotal)
        {
            isPlaying.store(false);
            wantsStop.store(false);
            stopFadePos = 0;
            engine.reset();
            // UI must not be touched from the audio thread -> SafePointer.
            juce::Component::SafePointer<MainComponent> sp(this);
            juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
        }
    }
    else
    {
        // no fade pending: the audio thread owns the counter, so it is the
        // one that resets it (the message thread only asks for the stop)
        stopFadePos = 0;
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

    // UI must not be touched from the audio thread, and `this` may be gone by
    // the time the callback runs -> SafePointer. The epoch guard drops a late
    // notification if the user picked another page meanwhile.
    const auto notifyPage = [this](int next)
    {
        const int epoch = pageEpoch.load(std::memory_order_relaxed);
        juce::Component::SafePointer<MainComponent> sp(this);
        juce::MessageManager::callAsync([sp, next, epoch]{
            if (sp == nullptr) return;
            if (sp->pageEpoch.load(std::memory_order_relaxed) != epoch) return;
            sp->currentPage = juce::jlimit(0, (int) sp->pages.size() - 1, next);
            sp->refreshBoxes();
            sp->refreshScoreBinding();
        });
    };

    // Render in sub-chunks split at the page boundary so looping never
    // leaves an unrendered (silent) tail at the seam.
    int offset = 0;
    while (offset < n)
    {
        if (snap.pages.empty())
        {
            isPlaying.store(false); wantsStop.store(false);
            engine.reset(); publishPlayhead(); return;
        }
        // the message thread may have deleted a page since the last snapshot
        if (audioPage < 0 || audioPage >= (int) snap.pages.size())
            audioPage = juce::jlimit(0, (int) snap.pages.size() - 1, audioPage);
        const auto& pg = snap.pages[(size_t) audioPage];

        if (pg.durationSec < 0.001)
        {
            isPlaying.store(false); wantsStop.store(false);
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
                        isPlaying.store(false); wantsStop.store(false);
                        playheadAtEnd.store(false, std::memory_order_relaxed);
                        engine.reset(); publishPlayhead();
                        juce::Component::SafePointer<MainComponent> sp(this);
                        juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
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
                    isPlaying.store(false); wantsStop.store(false);
                    playheadAtEnd.store(false, std::memory_order_relaxed);
                    engine.reset(); publishPlayhead();
                    juce::Component::SafePointer<MainComponent> sp(this);
                    juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
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
                        isPlaying.store(false); wantsStop.store(false);
                        playheadAtEnd.store(true, std::memory_order_relaxed);
                        engine.reset(); publishPlayhead();
                        juce::Component::SafePointer<MainComponent> sp(this);
                        juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
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
                    isPlaying.store(false); wantsStop.store(false);
                    playheadAtEnd.store(true, std::memory_order_relaxed);
                    engine.reset(); publishPlayhead();
                    juce::Component::SafePointer<MainComponent> sp(this);
                    juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->stopPlayback(true); });
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
// v1.11 VST3 plugin mode: the host's input bus count replaces the device input
// count. The process buffer itself follows the same in-place convention as a
// device callback buffer (channel k carries input k on entry), so the live tap
// below needs no special case.
void MainComponent::setPluginInputChannelCount(int numChannels)
{
    if (! pluginMode_) return;
    numLiveInputs = juce::jlimit(0, 2, numChannels);
}

//==============================================================================
// v1.4 live input: input tap (audio thread, lock-free) + capture lifecycle.
// The tap copies the player buffer's input channels to inScratch BEFORE the
// clear, and advances the capture ring. Slot install happens on the message
// thread via handoffCapture -> finalizeCapture (same SafePointer pattern as
// the transport callbacks above).
void MainComponent::updateRevBuffers()
{
    // The message thread only *asks*: the duration becomes a frame count the
    // audio thread reads, and the restart flag makes it reset its state. The
    // buffers themselves are allocated once (at the maximum duration), so
    // nothing is reallocated while the audio thread may be reading them.
    revBufFrames.store(juce::jmax(1024, (int) std::lround(revBufSec * audioSampleRate())));
    revRestart.store(true, std::memory_order_relaxed);
}

bool MainComponent::tapLiveInput(const juce::AudioSourceChannelInfo& info)
{
    // v1.33.4 (L14): the rate crosses the thread boundary, so it is read
    // once here (a relaxed atomic load) and used from there on.
    const double sr = audioSampleRate();

    if (capAbortPending.exchange(false)) // device restarted mid-take: drop it
    {
        capDiscard = true;
        capStopReason = 4;
        capStopRequested = true;
    }

    const int nIn = juce::jmin(2, numLiveInputs.load());
    if (nIn <= 0)
    {
        liveTapActive = false; // mode-2 live arcs go silent without input
        if (capturing.load()) { capStopReason = 3; capStopRequested = true; }
        if (capStopRequested.load()) handoffCapture();
        return false;
    }

    auto* buf = info.buffer;
    if (buf->getNumChannels() < 1 || info.numSamples <= 0)
    {
        liveTapActive = false;
        return monitoring.load();
    }

    const int n = info.numSamples;
    if (inScratch.getNumChannels() < 2 || inScratch.getNumSamples() < n)
        inScratch.setSize(2, n, false, false, true);
    const float* in0 = buf->getReadPointer(0, info.startSample);
    const float* in1 = (nIn > 1 && buf->getNumChannels() > 1)
                           ? buf->getReadPointer(1, info.startSample) : in0;
    inScratch.copyFrom(0, 0, in0, n);
    inScratch.copyFrom(1, 0, in1, n);

    // v1.5: feed the live ring (mode-2 arcs read it behind this write head)
    if (liveRingSize > 0)
    {
        int at = (int) (liveWritePos % (long long) liveRingSize);
        int off = 0, rem = n;
        const float* s0 = inScratch.getReadPointer(0);
        const float* s1 = inScratch.getReadPointer(1);
        while (rem > 0)
        {
            const int w = juce::jmin(rem, liveRingSize - at);
            std::copy(s0 + off, s0 + off + w, liveRingA.begin() + at);
            std::copy(s1 + off, s1 + off + w, liveRingB.begin() + at);
            off += w; rem -= w; at += w;
            if (at >= liveRingSize) at = 0;
        }
        liveWritePos += n;
        liveTapActive = true;
    }
    else
    {
        liveTapActive = false;
    }

    // v1.20 (wave 18): the live reverse capture. The write head moves
    // BACKWARDS, so the buffer holds the last D seconds mirrored in time:
    // reading it forward plays the input backwards - the same trick as the
    // sample path, and the shifter needs no change at all.
    // v1.30: capture continuously, so the global transport reverse can use
    // the mirrored segment immediately (the Live tab's Rev button only sets
    // the arcs' reverse flags; it no longer gates the capture).
    if (revBufFrames.load(std::memory_order_relaxed) > 0)
    {
        const int D = revBufFrames.load(std::memory_order_relaxed);
        if (revRestart.exchange(false, std::memory_order_relaxed))
        {
            // the message thread asked for a fresh start (or a new duration)
            revCapIdx = 0; revPlayIdx = 1;
            revCapCount = 0;
            revCapHead = D - 1;
            // the playback starts after the shifter's lag, so the first read
            // is already inside the segment (every live shifter reads behind
            // the head)
            revPlayHead = juce::jmin(D - 1, (int) std::lround(0.25 * sr));
        }
        if (D > 0 && revBuf[revCapIdx].getNumSamples() >= D)
        {
            auto& cap = revBuf[revCapIdx];
            const float* s0 = inScratch.getReadPointer(0);
            const float* s1 = inScratch.getReadPointer(1);
            const int fadeN = juce::jmax(1, (int) std::lround(0.005 * sr));
            for (int i = 0; i < n; ++i)
            {
                revCapHead = (revCapHead - 1 + D) % D;
                float g = 1.0f;
                if (revCapCount < fadeN)
                    g = (float) revCapCount / (float) fadeN;              // oldest end: in
                else if (revCapCount >= D - fadeN)
                    g = (float) (D - revCapCount) / (float) fadeN;        // newest end: out
                cap.setSample(0, revCapHead, s0[i] * g);
                cap.setSample(1, revCapHead, s1[i] * g);
                ++revCapCount;
                if (revCapCount >= D)
                {
                    // the segment is complete: it becomes the playback and
                    // the buffer that was playing becomes the new capture
                    // (nothing is copied, so there is no spike in the audio
                    // thread)
                    revPlayIdx = revCapIdx;
                    revPlayHead = juce::jmin(D - 1, (int) std::lround(0.25 * sr));
                    revCapIdx = 1 - revCapIdx;
                    revCapHead = D - 1;
                    revCapCount = 0;
                    // only re-anchor if something actually reads the reverse
                    // ring; the capture itself now runs continuously.
                    if (revLiveOn.load(std::memory_order_relaxed)
                        || playRate.load(std::memory_order_relaxed) < 0.0)
                        engine.resetLiveVoices(); // the live voices re-anchor
                }
            }
            // the playback head runs at 1x and stops at the segment's end
            revPlayHead = juce::jmin(D, revPlayHead + n);
        }
    }

    if (capturing.load() && capMax > 0)
    {
        const int at = capPos.load();
        const int w = juce::jmin(n, juce::jmax(0, capMax - at));
        if (w > 0)
        {
            capBuf.copyFrom(0, at, inScratch, 0, 0, w);
            capBuf.copyFrom(1, at, inScratch, 1, 0, w);
            capPos.store(at + w);
        }
        if (capPos.load() >= capMax) { capStopReason = 2; capStopRequested = true; }
    }

    if (capStopRequested.load()) handoffCapture();
    return monitoring.load();
}

//==============================================================================
// v1.33.4 (M24): the peak the input tap saw on a channel. The plugin smoke
// test uses it to prove that the host's right input channel reaches the app
// even when the host's output is mono (the old code passed only the output
// bus's view, so the right input channel was never seen).
float MainComponent::inputTapPeakForTest (int channel) const
{
    if (channel < 0 || channel >= inScratch.getNumChannels())
        return 0.0f;

    float peak = 0.0f;
    for (int i = 0; i < inScratch.getNumSamples(); ++i)
        peak = juce::jmax (peak, std::abs (inScratch.getSample (channel, i)));
    return peak;
}

void MainComponent::handoffCapture()
{
    capturing = false;
    capStopRequested = false;
    capFinalCount = capPos.load();
    capFinalReason = capStopReason.exchange(0);
    capReady = true;
    juce::Component::SafePointer<MainComponent> sp(this);
    juce::MessageManager::callAsync([sp]{ if (sp != nullptr) sp->finalizeCapture(); });
}

void MainComponent::requestCaptureStop(int reason)
{
    if (! capturing.load()) return;
    capStopReason = reason;
    capStopRequested = true;
}

void MainComponent::startCapture()
{
    if (capturing.load()) return;
    const double rate = audioSampleRate() > 0.0 ? audioSampleRate() : 44100.0;
    // v1.33.4 (M19): the take keeps the device rate, so a device above the
    // app's ceiling is refused: the material would not survive a save and a
    // load (the loader keeps the rate it is given).
    if (rate < 1000.0 || rate > upic::limits::kMaxSampleRate)
    {
        flash("Cannot capture at this sample rate (" + juce::String (rate / 1000.0, 1) + " kHz)");
        capBtn.setToggleState(false, juce::dontSendNotification);
        return;
    }
    const int maxFrames = (int) juce::jmin((juce::int64) (1LL << 28),
                                            (juce::int64) (kMaxCaptureSec * rate));
    if (maxFrames < 64)
    {
        flash("Cannot capture at this sample rate");
        capBtn.setToggleState(false, juce::dontSendNotification);
        return;
    }
    capBuf.setSize(2, maxFrames);
    capBuf.clear();
    capRate = rate;
    capMax = maxFrames;
    capPos = 0;
    capFinalCount = 0;
    capDiscard = false;
    capStopReason = 0;
    capFinalReason = 0;
    capturing = true;
    capBtn.setButtonText("Stop");
}

void MainComponent::finalizeCapture()
{
    if (! capReady.exchange(false)) return;
    capBtn.setToggleState(false, juce::dontSendNotification);
    capBtn.setButtonText("Capture");
    repaint();

    const int reason = capFinalReason;
    if (capDiscard.exchange(false) || capFinalCount < 64)
    {
        flash(reason == 4 ? "Take discarded (audio device changed)"
                          : "Take too short - discarded");
        return;
    }

    juce::AudioBuffer<float> take(2, capFinalCount);
    take.copyFrom(0, 0, capBuf, 0, 0, capFinalCount);
    take.copyFrom(1, 0, capBuf, 1, 0, capFinalCount);

    const int slotIndex = juce::jlimit(0, (int) sampleSlots.size() - 1, selSlot);
    const int sid = sampleSlots[(size_t) slotIndex].id;
    auto slot = upic::slotFromCapture(sid, takeName(), take, capRate);

    {
        // model is read by the audio thread -> mutate under the lock
        juce::ScopedLock l(dataLock);
        // the capture replaces the material, not the slot: the reverse flag
        // (v1.19) belongs to the slot, so it survives
        const bool wasReversed = sampleSlots[(size_t) slotIndex].reversed;
        sampleSlots[(size_t) slotIndex] = slot;
        sampleSlots[(size_t) slotIndex].reversed = wasReversed;
        markModelChanged(); // v1.15: new material -> fade across it
        publishModelSnapshot(); // v1.18: the next block renders the take
    }
    refreshBoxes();
    updateSampleView();
    pushVoice();
    repaint();

    const double secs = ((double) capFinalCount) / juce::jmax(1.0, capRate);
    juce::String msg = "Captured " + juce::String(secs, 1) + "s into slot " + juce::String(sid);
    if (reason == 2) msg += " (take full)";
    else if (reason == 3) msg += " (input lost)";
    flash(msg);
}

juce::String MainComponent::takeName()
{
    return "Take " + juce::Time::getCurrentTime().formatted("%H%M%S");
}

void MainComponent::ensureLiveInputFor(std::function<void(bool)> done)
{
    if (inputEnabled && numLiveInputs.load() > 0) { done(true); return; }
    requestLiveInput(std::move(done));
}

// v1.8: the one tab strip above the canvas - each material (Osc / Sample /
// Live) is a tab and its own canvas layer on the same page + timeline, so
// the tabs double as the old Score / Live pitch layer switch.
void MainComponent::setMaterial(int m)
{
    drawMatMode = juce::jlimit(0, 3, m);
    score.setLayer(drawMatMode);
    syncMaterialTabs();
    matHintLab.setText (drawMatMode == 0 ? "Osc: draw arcs on the wavetable\n"
                                          "Wave = carrier, Env = amplitude"
                      : drawMatMode == 1 ? "Sample: Pitch sets how arcs transpose the slot\n"
                                          "Ref = unison"
                      : drawMatMode == 2 ? "Live: arcs multiply the input pitch\n"
                                          "1x = input, 2x = octave up"
                      : "PM: Ratio x Index into the carrier phase\n"
                        "IdxEnv shapes the modulation index",
                      juce::dontSendNotification);
    resized();
    updateSampleView();
    pushVoice();
    // The mic is opened lazily, when it is actually needed: Mon/Capture on
    // the Sample tab, or Play with live arcs on the page. Opening the device
    // here blocked the message thread for seconds on the first Live-tab
    // click (the input is a device restart), which read as "the UI lags
    // when I click Live".
}

void MainComponent::syncMaterialTabs()
{
    matOscTog.setToggleState(drawMatMode == 0, juce::dontSendNotification);
    matSampleTog.setToggleState(drawMatMode == 1, juce::dontSendNotification);
    matLiveTog.setToggleState(drawMatMode == 2, juce::dontSendNotification);
    matPmTog.setToggleState (drawMatMode == 3, juce::dontSendNotification);
}

void MainComponent::requestLiveInput(std::function<void(bool)> done)
{
    juce::Component::SafePointer<MainComponent> sp(this);
    juce::RuntimePermissions::Callback cont = [sp, done](bool granted){
        if (sp == nullptr) return;
        if (! granted) { done(false); return; }
        done(sp->enableDeviceInputs());
    };
#if JUCE_ANDROID
    // RECORD_AUDIO is dangerous: the system dialog is async.
    juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, cont);
#else
    cont(true);
#endif
}

bool MainComponent::enableDeviceInputs()
{
    if (pluginMode_)
    {
        // no device to open here: the host's input bus is the live input
        const int nIn = numLiveInputs.load();
        inputEnabled = nIn > 0;
        if (! inputEnabled)
            flash("No host input: live arcs will be silent");
        return inputEnabled;
    }
    setAudioChannels(2, 2);
    int nIn = 0;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        nIn = dev->getActiveInputChannels().countNumberOfSetBits();
    numLiveInputs = nIn;
    if (nIn > 0) { inputEnabled = true; return true; }
    setAudioChannels(0, 2); // restore output-only
    inputEnabled = false;
    flash("No audio input channels on this device");
    return false;
}
