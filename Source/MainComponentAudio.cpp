#include "MainComponent.h"
#include <atomic>
#include <cmath>
#include <memory>

//==============================================================================
// WP7: one route token arbitrates every preview material. The extraction
// route carries its snapshot generation; the ordinary routes carry their
// reset serial so every publication has a distinct, ordered token.
void MainComponent::setAudioPreviewRoute (AudioPreviewRoute route)
{
    // The lock is re-entrant. Route owners take it before preparing their
    // state, so this nested lock keeps direct callers safe too.
    juce::ScopedLock lock (audioPreviewRouteLock);
    std::uint64_t generation = 0;
    switch (route)
    {
        case AudioPreviewRoute::sampleExtract:
            generation = sampleExtractPreviewState.generation;
            break;
        case AudioPreviewRoute::scrub:
        case AudioPreviewRoute::freeze:
            generation = ordinaryPreviewGeneration.load (
                std::memory_order_acquire);
            break;
        case AudioPreviewRoute::none:
        default:
            break;
    }
    audioPreviewRouteToken.store (makeAudioPreviewRouteToken (route, generation),
                                  std::memory_order_release);
}

void MainComponent::releaseAudioPreviewRouteIf (AudioPreviewRoute route)
{
    juce::ScopedLock lock (audioPreviewRouteLock);
    const auto none = makeAudioPreviewRouteToken (AudioPreviewRoute::none, 0);
    auto token = audioPreviewRouteToken.load (std::memory_order_acquire);
    while (audioPreviewRouteFromToken (token) == route)
    {
        if (audioPreviewRouteToken.compare_exchange_weak (
                token, none, std::memory_order_acq_rel,
                std::memory_order_acquire))
            break;
    }
}

void MainComponent::releaseAudioPreviewRouteTokenIf (std::uint64_t expectedToken)
{
    juce::ScopedLock lock (audioPreviewRouteLock);
    const auto none = makeAudioPreviewRouteToken (AudioPreviewRoute::none, 0);
    auto token = audioPreviewRouteToken.load (std::memory_order_acquire);
    while (token == expectedToken)
    {
        if (audioPreviewRouteToken.compare_exchange_weak (
                token, none, std::memory_order_acq_rel,
                std::memory_order_acquire))
            break;
    }
}

std::uint64_t MainComponent::audioPreviewRouteEpochForClaim() const
{
    return audioPreviewLifecycleEpoch.load (std::memory_order_acquire);
}

bool MainComponent::audioPreviewRouteClaimIsCurrent (std::uint64_t epoch) const
{
    // The caller holds audioPreviewRouteLock. The cancelling flag closes the
    // short interval in which a lifecycle boundary has begun but has not
    // yet taken the lock. The epoch is odd while the boundary is active and
    // even between boundaries, so a publisher that captured either side of
    // the boundary is rejected rather than merely delayed.
    return ! audioPreviewLifecycleCancelling.load (std::memory_order_acquire)
        && (epoch & 1u) == 0
        && epoch == audioPreviewLifecycleEpoch.load (std::memory_order_acquire);
}

//==============================================================================
// WP7: the ordinary scrub/freeze request publication.
//
// The request is an immutable record handed to the audio thread through the
// same allocation-free snapshot handover as the model. A seqlock made from
// independent atomics is not enough here: the reader could legally combine
// fields from two generations. The handover guarantees that every field came
// from one publication, and the audio thread then owns that generation's
// private transport.
std::uint64_t MainComponent::ordinaryPreviewCoherenceTag (
    const OrdinaryPreviewState& state) noexcept
{
    const auto mix = [] (std::uint64_t hash, std::uint64_t value) noexcept
    {
        hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        return hash;
    };
    const auto doubleBits = [] (double value) noexcept
    {
        std::uint64_t bits = 0;
        std::memcpy (&bits, &value, sizeof (bits));
        return bits;
    };

    std::uint64_t hash = 0xcbf29ce484222325ull;
    hash = mix (hash, state.generation);
    hash = mix (hash, state.scrubTransport);
    hash = mix (hash, state.freezeTransport);
    hash = mix (hash, state.freezePhase);
    hash = mix (hash, state.scrubActive ? 1u : 0u);
    hash = mix (hash, state.freezeActive ? 1u : 0u);
    hash = mix (hash, doubleBits (state.scrubPlayhead));
    hash = mix (hash, doubleBits (state.scrubRate));
    hash = mix (hash, doubleBits (state.freezePlayhead));
    hash = mix (hash, doubleBits (state.freezeStopAtMs));
    hash = mix (hash, doubleBits (state.transitionStartMs));
    hash = mix (hash, doubleBits (state.transitionEndMs));
    hash = mix (hash, static_cast<std::uint64_t> (
                          static_cast<std::uint32_t> (state.scrubRemaining)));
    return hash;
}

MainComponent::OrdinaryPreviewState
MainComponent::copyOrdinaryPreviewStateForMessageThread() const noexcept
{
    // Every caller holds audioPreviewRouteLock, so the source state cannot
    // change while this copy is made.
    return ordinaryPreviewState;
}

void MainComponent::publishOrdinaryPreviewState (
    const OrdinaryPreviewState& state)
{
    // The caller holds audioPreviewRouteLock, so there is one writer. Build
    // the complete record first, then hand that immutable object to the
    // audio thread. The allocation happens on the writer's side only.
    OrdinaryPreviewState published = state;
    published.coherenceTag = ordinaryPreviewCoherenceTag (published);
    ordinaryPreviewState = published;

    ordinaryPreviewGeneration.store (published.generation,
                                     std::memory_order_release);
    ordinaryPreviewScrubTransport.store (published.scrubTransport,
                                         std::memory_order_release);
    ordinaryPreviewFreezeTransport.store (published.freezeTransport,
                                          std::memory_order_release);
    ordinaryPreviewFreezePhase.store (published.freezePhase,
                                      std::memory_order_release);

    ordinaryPreviewSnapshots.publish (
        new OrdinaryPreviewSnapshot (published));
}

void MainComponent::clearOrdinaryPreviewRoutes()
{
    OrdinaryPreviewState state = copyOrdinaryPreviewStateForMessageThread();

    ++state.generation;
    ++state.scrubTransport;
    ++state.freezeTransport;
    state.scrubActive = false;
    state.scrubPlayhead = 0.0;
    state.scrubRemaining = 0;
    state.scrubRate = 1.0;
    state.freezeActive = false;
    state.freezePlayhead = 0.0;
    state.freezeStopAtMs = 0.0;
    state.transitionStartMs = 0.0;
    state.transitionEndMs = 0.0;
    publishOrdinaryPreviewState (state);
}

void MainComponent::cancelOrdinaryPreviewRoutes()
{
    juce::ScopedLock routeLock (audioPreviewRouteLock);

    // Capture the exact token before publishing the inactive record. Only
    // that ordinary owner may be released; an extraction route is never
    // stolen by this helper.
    const auto token = audioPreviewRouteToken.load (std::memory_order_acquire);
    const auto route = audioPreviewRouteFromToken (token);

    OrdinaryPreviewState state = copyOrdinaryPreviewStateForMessageThread();
    ++state.generation;
    ++state.scrubTransport;
    ++state.freezeTransport;
    state.scrubActive = false;
    state.scrubPlayhead = 0.0;
    state.scrubRemaining = 0;
    state.scrubRate = 1.0;
    state.freezeActive = false;
    state.freezePlayhead = 0.0;
    state.freezeStopAtMs = 0.0;
    state.transitionStartMs = 0.0;
    state.transitionEndMs = 0.0;
    publishOrdinaryPreviewState (state);

    if (route == AudioPreviewRoute::scrub || route == AudioPreviewRoute::freeze)
        releaseAudioPreviewRouteTokenIf (token);
}

bool MainComponent::claimScrubPreviewRoute (double timeSec, double rate)
{
    // A stopped score may be auditioned, but a running transport owns the
    // score output. Refuse the preview rather than letting it resume after
    // Stop with stale transport state.
    if (transportActive())
        return false;

    const auto routeEpoch = audioPreviewRouteEpochForClaim();

    // Selftest-only: hold the claim after its epoch capture so the host
    // lifecycle boundary can run first. Production never enables this.
    if (audioPreviewRouteClaimPauseRequestedForTest.exchange (
            false, std::memory_order_acq_rel))
    {
        audioPreviewRouteClaimPausedFlagForTest.store (true, std::memory_order_release);
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        while (! audioPreviewRouteClaimResumeForTest.load (std::memory_order_acquire)
               && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::Thread::sleep (1);
        // A missed test handshake must never hang the suite. The claim is
        // stale by construction if its resume signal did not arrive.
        if (! audioPreviewRouteClaimResumeForTest.load (std::memory_order_acquire))
            return false;
    }

    juce::ScopedLock routeLock (audioPreviewRouteLock);
    if (! audioPreviewRouteClaimIsCurrent (routeEpoch))
        return false;

    // The ordinary scrub and the extraction A/B are exclusive routes. A
    // scrub cancels the extraction preview instead of mixing two materials.
    if (sampleExtractPreviewState.route != SampleExtractRoute::none)
        cancelSampleExtractPreview();

    OrdinaryPreviewState state = copyOrdinaryPreviewStateForMessageThread();

    ++state.generation;
    ++state.scrubTransport;
    ++state.freezeTransport;
    state.scrubActive = true;
    state.scrubPlayhead = timeSec;
    state.scrubRemaining = scrubPreviewLen.load (std::memory_order_relaxed);
    state.scrubRate = rate;
    state.freezeActive = false;
    state.freezePlayhead = 0.0;
    state.freezeStopAtMs = 0.0;
    state.transitionStartMs = 0.0;
    state.transitionEndMs = 0.0;
    publishOrdinaryPreviewState (state);

    // The token is published last. Its generation identifies exactly the
    // request above, and an acquire load makes every field visible.
    setAudioPreviewRoute (AudioPreviewRoute::scrub);
    return true;
}

bool MainComponent::claimScrubFreezeRoute (double timeSec)
{
    if (transportActive())
        return false;

    const auto routeEpoch = audioPreviewRouteEpochForClaim();
    juce::ScopedLock routeLock (audioPreviewRouteLock);
    if (! audioPreviewRouteClaimIsCurrent (routeEpoch))
        return false;

    // The freeze route replaces the extraction route; the two never sound
    // together.
    if (sampleExtractPreviewState.route != SampleExtractRoute::none)
        cancelSampleExtractPreview();

    OrdinaryPreviewState state = copyOrdinaryPreviewStateForMessageThread();

    ++state.generation;
    ++state.scrubTransport;
    ++state.freezeTransport;
    ++state.freezePhase;
    state.scrubActive = true;
    state.scrubPlayhead = timeSec;
    state.scrubRemaining = scrubPreviewLen.load (std::memory_order_relaxed);
    state.scrubRate = 1.0;
    state.freezeActive = true;
    state.freezePlayhead = timeSec;
    state.freezeStopAtMs = 0.0;
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    state.transitionStartMs = nowMs;
    state.transitionEndMs = nowMs + scrubCrossMs;
    publishOrdinaryPreviewState (state);

    // The phase handoff and the playhead reset are consumed by the audio
    // thread when it sees this generation. Publish the request before the
    // route token, never after it.
    setAudioPreviewRoute (AudioPreviewRoute::freeze);
    return true;
}

void MainComponent::updateScrubFreezeStop (double stopAtMs)
{
    juce::ScopedLock routeLock (audioPreviewRouteLock);
    const auto token = audioPreviewRouteToken.load (std::memory_order_acquire);
    if (audioPreviewRouteFromToken (token) != AudioPreviewRoute::freeze)
        return;

    OrdinaryPreviewState state = copyOrdinaryPreviewStateForMessageThread();
    if (state.generation != audioPreviewGenerationFromToken (token)
        || ! state.freezeActive)
        return;

    // A tail update is a new record generation, but it does not advance the
    // freeze transport or phase. The audio thread therefore keeps its held
    // voice and only adopts the new stop time.
    ++state.generation;
    state.freezeStopAtMs = stopAtMs;
    publishOrdinaryPreviewState (state);
    setAudioPreviewRoute (AudioPreviewRoute::freeze);
}

void MainComponent::cancelScrubFreezeRoute()
{
    juce::ScopedLock routeLock (audioPreviewRouteLock);
    const auto token = audioPreviewRouteToken.load (std::memory_order_acquire);
    if (audioPreviewRouteFromToken (token) != AudioPreviewRoute::freeze)
        return;

    OrdinaryPreviewState state = copyOrdinaryPreviewStateForMessageThread();
    ++state.generation;
    ++state.scrubTransport;
    ++state.freezeTransport;
    state.scrubActive = false;
    state.scrubRemaining = 0;
    state.freezeActive = false;
    state.freezeStopAtMs = 0.0;
    state.transitionStartMs = 0.0;
    state.transitionEndMs = 0.0;
    publishOrdinaryPreviewState (state);

    // Release this exact freeze token. A stale cancel cannot steal a scrub
    // or extraction route that was claimed after this freeze began.
    releaseAudioPreviewRouteTokenIf (token);
}

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

    const auto sampleVoices = needSample ? arcs : 0;
    const auto liveVoices = needLive ? arcs : 0;

    // Publish and apply the requirement under one gate. A host-thread
    // preparation may be changing the engine at the same time; whichever
    // side owns the gate last leaves the engine prepared for the latest
    // published requirement.
    juce::ScopedLock engineLock (audioEnginePreparationLock);
    publishedSampleVocoderVoices.store (sampleVoices, std::memory_order_release);
    publishedLiveVocoderVoices.store (liveVoices, std::memory_order_release);

    // the audio thread hands the voice slots out in order, so the first N
    // arcs to sound are the ones that find a prepared voice
    engine.prepareSampleVocoderMemory (sampleVoices);
    engine.prepareLiveVocoderMemory (liveVoices);
    scrubEngine.prepareSampleVocoderMemory (sampleVoices);
    scrubEngine.prepareLiveVocoderMemory (liveVoices);
}

void MainComponent::preparePublishedVocoderMemory()
{
    // The caller owns audioEnginePreparationLock. The message thread
    // published the counts; this path applies them without reading the
    // model or running on the host's setup thread.
    const auto sampleVoices = publishedSampleVocoderVoices.load (
        std::memory_order_acquire);
    const auto liveVoices = publishedLiveVocoderVoices.load (
        std::memory_order_acquire);
    engine.prepareSampleVocoderMemory (sampleVoices);
    engine.prepareLiveVocoderMemory (liveVoices);
    scrubEngine.prepareSampleVocoderMemory (sampleVoices);
    scrubEngine.prepareLiveVocoderMemory (liveVoices);
}

void MainComponent::publishModelSnapshot()
{
    // v1.33.4 (M21): the vocoder's memory is prepared before the snapshot
    // goes out, so an arc that plays through the vocoder finds it there
    prepareVocodersIfNeeded (engine.getSamplePitchMode(), engine.getLivePitchMode());
    auto* snap = new ModelSnapshot();
    snap->waveforms = waveforms;
    snap->envelopes = envelopes;
    snap->amplitudeTables = ampTables;
    snap->sequences = sequences;
    snap->channels = channels;
    snap->pages = pages;
    snap->slots = sampleSlots; // shared buffers: only the pointers are copied
    // WP7: the sample extraction panel's A/B preview is built here, on the
    // message thread. The audio thread only reads the finished page.
    addSampleExtractPreview (*snap);

    // Keep a message-thread-owned copy for the selftest. The audio reader's
    // liveSnapshot pointer is not safe to inspect from this thread: the
    // reader may retire and the writer may free that snapshot at any time.
    publishedSampleExtractPreview.state = snap->sampleExtractPreview;
    publishedSampleExtractPreview.page = snap->samplePreviewPage;
    publishedSampleExtractPreview.slots = snap->samplePreviewSlots;
    publishedSampleExtractPreview.waves = snap->samplePreviewWaves;
    publishedSampleExtractPreview.envelopes = snap->samplePreviewEnvelopes;

    {
        const auto route = static_cast<std::uint64_t> (
            snap->sampleExtractPreview.route);
        const bool hasPayload = snap->samplePreviewPage != nullptr
                             && (! snap->samplePreviewSlots.empty()
                                 || ! snap->samplePreviewWaves.empty());
        publishedSampleExtractSnapshotState.store (
            route | (hasPayload ? std::uint64_t { 4 } : std::uint64_t { 0 }),
            std::memory_order_release);
    }
    snap->currentPage = currentPage;
    snap->pageEpoch = pageEpoch.load(std::memory_order_relaxed);
    snap->seqPages = seqPages;
    snap->loopPage = loopPage;
    // WP1: compile the drawn frequency tables for the pages that have one.
    // Only the spaces with a curve get a LUT, so a preset-only piece pays
    // nothing for the feature.
    snap->pageLuts.resize (snap->pages.size());
    for (size_t pi = 0; pi < snap->pages.size(); ++pi)
    {
        const upic::Page& page = snap->pages[pi];
        for (int s = 0; s < 4; ++s)
        {
            const upic::FreqSpaceDef& def = page.spaceDefs[(size_t) s];
            if (! (def.editable || def.inverted)) continue;
            auto lut = std::make_shared<upic::FreqLut> (
                upic::buildFreqLut (def, page.pitchLo, page.pitchHi));
            snap->pageLuts[pi].lut[s] = lut.get();
            snap->ownedLuts.push_back (std::move (lut));
        }
    }
    // WP3: resolve the groups once per publication. The audio thread reads
    // one override per arc; solo is already decided here.
    snap->arcOverrides.reserve (snap->pages.size());
    for (auto& page : snap->pages)
        snap->arcOverrides.push_back (page.resolveArcOverrides());

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

    // WP7: the score and the preview routes are separate. Play ends both
    // the ordinary scrub/freeze request and the extraction preview; neither
    // may resume with stale transport state after Stop.
    cancelOrdinaryPreviewRoutes();
    cancelSampleExtractPreview();

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
    // WP4 fix: the ordinary Stop returns the transport to the internal
    // source; the sequence is re-armed only by its own Play.
    seqPlayActive.store (false, std::memory_order_relaxed);
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
    sampleExtractBuf.setSize (2, n, false, false, true);
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // A host preparation is a request, not permission for the host thread to
    // touch the model or its engines. Publish the request first; the
    // requested and published generations then tell the audio callback
    // whether the matching preparation is live.
    std::uint64_t generation = 0;
    {
        juce::ScopedLock requestLock (audioPreparationRequestLock);
        generation = requestedAudioPreparationGeneration.fetch_add (
            1, std::memory_order_acq_rel) + 1;
    }

    auto* const messages = juce::MessageManager::getInstanceWithoutCreating();
    const bool onMessageThread = messages != nullptr
                              && messages->isThisTheMessageThread();
    if (onMessageThread)
    {
        applyAudioPreparation (generation, samplesPerBlockExpected, sampleRate);
        return;
    }

    // End the old device's routes immediately, even though the new engine
    // preparation has to wait for the message thread. The actual preparation
    // repeats the boundary when it applies the request.
    cancelPreviewRoutesForHostLifecycle();

    if (messages != nullptr)
    {
        postToMessageThread ([generation, samplesPerBlockExpected, sampleRate]
                            (MainComponent& component)
        {
            component.applyAudioPreparation (generation,
                                             samplesPerBlockExpected,
                                             sampleRate);
        });
    }
    else
    {
        // There is no message thread to own the model/engine preparation.
        // Leave the requested generation unpublished; the callback stays
        // silent rather than mutating message-owned state on this thread.
        juce::Logger::writeToLog (
            "UPICuitous: host preparation could not reach the message thread");
    }
}

void MainComponent::invalidateAudioPreparation()
{
    juce::ScopedLock requestLock (audioPreparationRequestLock);
    requestedAudioPreparationGeneration.fetch_add (1, std::memory_order_acq_rel);
}

void MainComponent::applyAudioPreparation (std::uint64_t generation,
                                            int samplesPerBlockExpected,
                                            double sampleRate)
{
    if (requestedAudioPreparationGeneration.load (std::memory_order_acquire)
        != generation)
        return;

    // The lifecycle boundary covers the engine reset. A route publisher that
    // starts during this window captures the odd epoch and is refused.
    juce::ScopedLock lifecycleLock (audioPreviewLifecycleLock);
    if (requestedAudioPreparationGeneration.load (std::memory_order_acquire)
        != generation)
        return;

    const auto extractionGeneration = beginPreviewRoutesForHostLifecycle();
    struct LifecycleEnd
    {
        MainComponent& owner;
        ~LifecycleEnd() { owner.endPreviewRoutesForHostLifecycle(); }
    } lifecycleEnd { *this };
    cancelSampleExtractPreviewForHostLifecycle (extractionGeneration);

    juce::ScopedLock engineLock (audioEnginePreparationLock);
    if (requestedAudioPreparationGeneration.load (std::memory_order_acquire)
        != generation)
        return;

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
    // WP7: the extraction A/B preview has its own engine. It is prepared on
    // the message thread just like the score and scrub engines, so its first
    // audio block cannot allocate.
    sampleExtractEngine.prepare(audioSampleRate(), samplesPerBlockExpected);
    sampleExtractEngine.setScrubPreview(true);
    // v1.33.4 (M21): the voice pools exist now, so the vocoder's working
    // memory can be prepared from the requirement published by the model.
    preparePublishedVocoderMemory();
    scrubPreviewLen.store (juce::jmax (256, (int) (audioSampleRate() * 0.05)),
                            std::memory_order_relaxed); // ~50 ms
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

    // Publish only the request that is still current. A release or a newer
    // preparation that arrived while this one was running leaves the
    // requested and published generations different, so the audio callback
    // stays silent until the newest request is applied.
    if (requestedAudioPreparationGeneration.load (std::memory_order_acquire)
        == generation)
        publishedAudioPreparationGeneration.store (generation,
                                                    std::memory_order_release);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    // Preparation may run on a host setup thread while this callback is
    // active. The callback never waits for it: if preparation owns the
    // engine gate, this block is silent and the next one continues from the
    // fully prepared state.
    juce::CriticalSection::ScopedTryLockType engineLock (audioEnginePreparationLock);
    const auto requestedPreparation =
        requestedAudioPreparationGeneration.load (std::memory_order_acquire);
    const auto publishedPreparation =
        publishedAudioPreparationGeneration.load (std::memory_order_acquire);
    if (! engineLock.isLocked() || requestedPreparation == 0
        || requestedPreparation != publishedPreparation)
    {
        const int outCh = juce::jmin (outChannels.load (std::memory_order_relaxed),
                                      info.buffer->getNumChannels());
        for (int c = 0; c < outCh; ++c)
            juce::FloatVectorOperations::clear (
                info.buffer->getWritePointer (c, info.startSample),
                info.numSamples);
        return;
    }

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
    // The scratch buffers are allocated on the message thread in
    // prepareAudioBuffers/prepareToPlay. Never grow them here: a host that
    // sends a block larger than the prepared capacity gets silence, not an
    // audio-thread allocation (and never an out-of-bounds write).
    if (scoreBuf.getNumChannels() < 2 || scoreBuf.getNumSamples() < n
        || freezeBuf.getNumChannels() < 2 || freezeBuf.getNumSamples() < n
        || sampleExtractBuf.getNumChannels() < 2 || sampleExtractBuf.getNumSamples() < n)
        return;

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
    // host may send (prepareAudioBuffers). The capacity check above refuses
    // an oversized host block instead of allocating on the audio thread.
    scoreBuf.clear();
    float* SL = scoreBuf.getWritePointer(0);
    float* SR = scoreBuf.getWritePointer(1);

    freezeBuf.clear();
    float* FL = freezeBuf.getWritePointer(0);
    float* FR = freezeBuf.getWritePointer(1);

    sampleExtractBuf.clear();
    float* XL = sampleExtractBuf.getWritePointer(0);
    float* XR = sampleExtractBuf.getWritePointer(1);

    bool rendered = false;
    bool modelChanged = false;
    const bool playingAtStart = transportActive();
    const bool transportStarting = (transport() == TransportState::starting);
    {
        // v1.18 (wave 15): no lock on the audio thread. Take the newest
        // immutable model snapshot and render from it - the message thread
        // owns the live model and frees retired snapshots on its own thread.
        ModelSnapshot* snap = audioPreviewSkipSnapshotForTest.load (
                                  std::memory_order_relaxed)
                                ? nullptr
                                : snapshots.take(liveSnapshot);

        // The route is chosen once for the whole block. The extraction
        // generation must also match the snapshot, so an old extraction
        // request cannot survive a cancellation or an ordinary scrub.
        std::uint64_t routeToken = audioPreviewRouteToken.load (
            std::memory_order_acquire);

        // Selftest-only: reproduce a route change after the snapshot was
        // taken but before the route token is read. The effective token is
        // local to this block, so the test cannot corrupt the published
        // route. The old snapshot and the new route still cannot mix: the
        // same generation gate below decides the extraction material.
        const auto forcedOverride = audioPreviewRouteTestOverride.load (
            std::memory_order_acquire);
        const int forcedRoute = forcedOverride == ~std::uint64_t { 0 }
            ? -1 : static_cast<int> (static_cast<std::uint32_t> (forcedOverride));
        if (forcedRoute >= 0)
        {
            const int generationOffset = static_cast<int> (forcedOverride >> 32);
            switch (static_cast<AudioPreviewRoute> (forcedRoute))
            {
                case AudioPreviewRoute::scrub:
                    routeToken = makeAudioPreviewRouteToken (
                        AudioPreviewRoute::scrub, 0);
                    break;
                case AudioPreviewRoute::freeze:
                    routeToken = makeAudioPreviewRouteToken (
                        AudioPreviewRoute::freeze, 0);
                    break;
                case AudioPreviewRoute::sampleExtract:
                {
                    const auto snapshotGeneration = snap != nullptr
                        ? snap->sampleExtractPreview.generation
                        : std::uint64_t { 0 };
                    routeToken = makeAudioPreviewRouteToken (
                        AudioPreviewRoute::sampleExtract,
                        snapshotGeneration
                            + static_cast<std::uint64_t> (generationOffset));
                    break;
                }
                case AudioPreviewRoute::none:
                default:
                    routeToken = makeAudioPreviewRouteToken (
                        AudioPreviewRoute::none, 0);
                    break;
            }
        }

        audioPreviewRouteTokenSeenForTest.store (routeToken,
                                                  std::memory_order_release);
        const auto route = audioPreviewRouteFromToken (routeToken);
        const auto routeGeneration = audioPreviewGenerationFromToken (routeToken);

        // a seek / scrub from the UI moves the audio playhead
        const int seek = seekSerial.load(std::memory_order_relaxed);
        if (seek != seenSeekSerial)
        {
            seenSeekSerial = seek;
            audioPlayhead = seekTo.load(std::memory_order_relaxed);
            engine.reset();
        }

        // The ordinary route's request is one immutable record. Taking it
        // through the handover cannot mix fields from two generations. The
        // token generation must match it; otherwise a block that loaded an
        // old token cannot consume a newer scrub/freeze record.
        OrdinaryPreviewState ordinaryState;
        bool ordinaryStateValid = false;
        if (const auto* const ordinarySnapshot =
                ordinaryPreviewSnapshots.take (liveOrdinaryPreviewSnapshot);
            ordinarySnapshot != nullptr)
        {
            ordinaryState = ordinarySnapshot->state;
            if (ordinaryState.coherenceTag != ordinaryPreviewCoherenceTag (ordinaryState))
                ordinaryPreviewCoherenceFailureCount.fetch_add (
                    1, std::memory_order_relaxed);
            ordinaryStateValid = ordinaryState.generation == routeGeneration;
        }

        if (ordinaryStateValid)
        {
            if (ordinaryState.generation != seenOrdinaryPreviewGeneration)
            {
                seenOrdinaryPreviewGeneration = ordinaryState.generation;
                // A freeze-tail update changes the record generation but not
                // its transport or phase. Adopt the new deadline without
                // restarting the held voice. Re-adopt the active flag too:
                // an older block may have expired its own generation after
                // this record was published.
                audioFreezeGeneration = ordinaryState.generation;
                audioFreezeExpired = false;
                audioFreezeStopAtMs = ordinaryState.freezeStopAtMs;
                if (route == AudioPreviewRoute::freeze)
                    audioFreezeActive = ordinaryState.freezeActive;
            }

            if (ordinaryState.scrubTransport != seenScrubPreviewTransport)
            {
                seenScrubPreviewTransport = ordinaryState.scrubTransport;
                audioScrubPreviewActive = ordinaryState.scrubActive;
                audioScrubPreviewPlayhead = ordinaryState.scrubPlayhead;
                audioScrubPreviewRemaining = ordinaryState.scrubRemaining;
                audioScrubRate = ordinaryState.scrubRate;
                scrubEngine.reset(); // re-anchor to the dragged playhead
            }

            if (route == AudioPreviewRoute::freeze)
            {
                if (ordinaryState.freezeTransport != seenFreezeTransport)
                {
                    seenFreezeTransport = ordinaryState.freezeTransport;
                    audioFreezeActive = ordinaryState.freezeActive;
                    audioFreezeGeneration = ordinaryState.generation;
                    audioFreezeExpired = false;
                }

                if (ordinaryState.freezePhase != seenFreezePhase)
                {
                    seenFreezePhase = ordinaryState.freezePhase;
                    audioPlayhead = ordinaryState.freezePlayhead;
                    engine.reset();
                    // The phase handoff belongs to the audio thread: both
                    // engines' phase pools are audio-thread state. The
                    // destination pool is preallocated, so this fixed-
                    // capacity copy does not allocate.
                    engine.copyOscillatorPhasesFrom(scrubEngine);
                }
            }
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
            double previewGain = 0.0;
            double freezeGain = 0.0;
            if (ordinaryStateValid)
            {
                if (route == AudioPreviewRoute::scrub
                    && audioScrubPreviewActive)
                    previewGain = 1.0;

                if (route == AudioPreviewRoute::freeze)
                {
                    if (audioScrubPreviewActive)
                        previewGain = 1.0;
                    if (audioFreezeActive)
                        freezeGain = 1.0;

                    const double ts = ordinaryState.transitionStartMs;
                    const double te = ordinaryState.transitionEndMs;
                    if (audioFreezeActive && te > ts && nowMs < te)
                    {
                        const double prog = juce::jlimit(
                            0.0, 1.0, (nowMs - ts) / (te - ts));
                        freezeGain = prog;
                        if (audioScrubPreviewActive)
                            previewGain = 1.0 - prog;
                    }
                    else if (audioFreezeActive)
                    {
                        // The short preview belongs to the crossfade only.
                        // Once the freeze has taken over, the two ordinary
                        // materials never sound together.
                        previewGain = 0.0;
                    }
                }
            }

            // v1.28: scrub audition (short preview while dragging the playhead)
            // and the freeze sustain (the click without a drag)
            AudioBlock block { L, R, SL, SR, FL, FR, XL, XR, n, playingAtStart, sr };
            block.sr = sr;   // v1.33.4 (L14): the rate, read once per block
            renderScrubPreview (*snap, block, previewGain, routeGeneration);
            renderScrubFreeze (*snap, block, freezeGain, routeGeneration);
            // WP7: the extraction preview is a separate route. It never
            // replaces the ordinary score scrub and never reads the score's
            // sample slots. Its generation must match the route token.
            const bool extractionRoute = route == AudioPreviewRoute::sampleExtract
                                      && snap->sampleExtractPreview.route != SampleExtractRoute::none
                                      && snap->sampleExtractPreview.generation
                                           == audioPreviewGenerationFromToken (routeToken);
            renderSampleExtractPreview (*snap, block, extractionRoute ? 1.0 : 0.0);

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

void MainComponent::renderScrubPreview (const ModelSnapshot& snap, AudioBlock& b,
                                        double previewGain,
                                        std::uint64_t routeGeneration)
{
    if (b.playingAtStart || previewGain <= 0.0
        || ! audioScrubPreviewActive)
        return;

    // A newer route publication owns the request now. The old block keeps
    // its private transport, but it must not mix that material with the new
    // route or write anything back into the published record.
    if (ordinaryPreviewGeneration.load (std::memory_order_acquire)
        != routeGeneration)
        return;

    const int remaining = audioScrubPreviewRemaining;
    if (remaining <= 0 || snap.pages.empty())
        return;

    const int p = juce::jlimit(0, (int) snap.pages.size() - 1, audioPage);
    const upic::Page& pg = snap.pages[(size_t) p];
    const int chunk = juce::jmin(b.numSamples, remaining);
    juce::FloatVectorOperations::clear (b.scoreL, chunk);
    if (b.scoreR != nullptr) juce::FloatVectorOperations::clear (b.scoreR, chunk);

    const double ph = audioScrubPreviewPlayhead;
    const double rate = audioScrubRate;

    // WP7: the extraction preview has its own renderer and transport. The
    // ordinary score scrub always renders the real page and the real slots.
    scrubEngine.render (pg, ph, snap.waveforms, snap.envelopes, snap.slots,
                        b.scoreL, (b.scoreR != nullptr ? b.scoreR : b.scoreL),
                        chunk, 1.0f, rate, false,
                        snap.lutsFor (p), &snap.amplitudeTables,
                        snap.overridesFor (p), &snap.channels);

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

    const int left = remaining - chunk;
    // The transport belongs to this exact published generation. If a newer
    // drag or lifecycle boundary claimed the route while this block was
    // rendering, the next block will adopt that new record. This block only
    // advances its private copy and never overwrites published state.
    if (ordinaryPreviewScrubTransport.load (std::memory_order_acquire)
            == seenScrubPreviewTransport
        && ordinaryPreviewGeneration.load (std::memory_order_acquire)
            == routeGeneration)
    {
        audioScrubPreviewPlayhead = ph + (double) chunk * rate / b.sr;
        audioScrubPreviewRemaining = left;
        if (left <= 0)
            audioScrubPreviewActive = false;
    }
}

//==============================================================================
// WP7: the sample extraction A/B preview. It is deliberately a separate
// route: its engine, playhead and remaining count are private, and its
// material is the snapshot's isolated preview-slot vector. An invalid route
// is silence - it never falls through to the score.
void MainComponent::renderSampleExtractPreview (const ModelSnapshot& snap, AudioBlock& b,
                                                double previewGain)
{
    if (b.playingAtStart || previewGain <= 0.0
        || snap.sampleExtractPreview.route == SampleExtractRoute::none)
        return;

    const auto& request = snap.sampleExtractPreview;
    if (request.route == SampleExtractRoute::none || snap.samplePreviewPage == nullptr
        || (snap.samplePreviewSlots.empty() && snap.samplePreviewWaves.empty()))
        return;

    // A generation is adopted as one unit. The message thread publishes the
    // request and its material together; an older audio block can never
    // write a playhead or a remaining count back over the new request.
    if (request.generation != seenSampleExtractPreviewGeneration)
    {
        seenSampleExtractPreviewGeneration = request.generation;
        sampleExtractPreviewPlayhead = 0.0;
        sampleExtractPreviewRemaining = request.durationFrames;
        sampleExtractEngine.reset();
    }

    if (sampleExtractPreviewRemaining <= 0) return;

    const int chunk = juce::jmin (b.numSamples, sampleExtractPreviewRemaining);
    juce::FloatVectorOperations::clear (b.extractL, chunk);
    if (b.extractR != nullptr) juce::FloatVectorOperations::clear (b.extractR, chunk);

    // The extracted side owns an isolated wavetable bank. Reading the
    // piece's bank here could select a different waveform with the same id
    // and break the promise that A/B and "To wave" use the exact same
    // converted asset. The original side has no oscillator arc, so the
    // piece's bank is harmless for that route.
    const auto& routeWaves = snap.wavesForSampleExtractRoute();

    sampleExtractEngine.render (*snap.samplePreviewPage, sampleExtractPreviewPlayhead,
                                 routeWaves, snap.samplePreviewEnvelopes,
                                 snap.samplePreviewSlots,
                                 b.extractL, (b.extractR != nullptr ? b.extractR : b.extractL),
                                 chunk, 1.0f, 1.0, false,
                                 nullptr, nullptr, nullptr);

    // No block-local fade: applying one to every block modulates the
    // preview at the host's block rate. The transport's own gain envelope
    // owns the route boundary.
    const float mg = masterGain.load (std::memory_order_relaxed) * (float) previewGain;
    for (int i = 0; i < chunk; ++i)
    {
        b.outL[i] += b.extractL[i] * mg;
        if (b.outR != nullptr)
            b.outR[i] += (b.extractR != nullptr ? b.extractR[i] : b.extractL[i]) * mg;
    }

    sampleExtractPreviewPlayhead += (double) chunk / b.sr;
    sampleExtractPreviewRemaining -= chunk;
}

void MainComponent::renderScrubFreeze (const ModelSnapshot& snap, AudioBlock& b,
                                       double freezeGain,
                                       std::uint64_t routeGeneration)
{
    if (b.playingAtStart || freezeGain <= 0.0
        || ! audioFreezeActive)
        return;

    // A freeze block may only use the state that belongs to its exact token.
    // A newer freeze, scrub, extraction route or lifecycle boundary changes
    // the ordinary generation before publishing its token. Expiry is tagged
    // with the same generation, so an old block cannot expire a newer tail.
    if (audioFreezeGeneration != routeGeneration || audioFreezeExpired)
        return;
    if (ordinaryPreviewGeneration.load (std::memory_order_acquire)
        != routeGeneration)
        return;

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double stopAt = audioFreezeStopAtMs;
    if (stopAt > 0.0 && nowMs >= stopAt)
    {
        // Expiry is private, generation-tagged audio-thread state. It never
        // writes the published record or clears the shared active flag.
        audioFreezeExpired = true;
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
                   b.numSamples, 1.0f, 0.0, true, snap.lutsFor (p), &snap.amplitudeTables,
                   snap.overridesFor (p), &snap.channels);

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
            // UI must not be touched from the audio thread. The shared
            // callback gate makes the queued stop safe even if a host
            // destroys the component on another thread.
            postToMessageThread ([] (MainComponent& component)
            {
                component.stopPlayback (true);
            });
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

    // UI must not be touched from the audio thread. The shared callback gate
    // keeps the queued notification safe if the host destroys the component
    // on another thread. The epoch guard drops a late notification if the
    // user picked another page meanwhile.
    const auto notifyPage = [this](int next)
    {
        const int epoch = pageEpoch.load(std::memory_order_relaxed);
        postToMessageThread ([next, epoch] (MainComponent& component)
        {
            if (component.pageEpoch.load(std::memory_order_relaxed) != epoch)
                return;
            component.currentPage = juce::jlimit (
                0, (int) component.pages.size() - 1, next);
            component.refreshBoxes();
            component.refreshScoreBinding();
        });
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

        // WP4: sequence playback. The clock and the page/position resolution
        // live on the audio thread; the points come from the snapshot. The
        // page's own time map still applies inside the chunk, because the
        // engine is called exactly as the internal transport calls it.
        {
            const bool seqOn = seqPlayActive.load (std::memory_order_relaxed);
            const upic::PerformanceSequence* seq = nullptr;
            if (seqOn)
            {
                const int sid = seqActiveId.load (std::memory_order_relaxed);
                for (auto& s : snap.sequences)
                    if (s.id == sid) { seq = &s; break; }
            }
            if (seqOn && (seq == nullptr || seq->points.size() < 2))
            {
                seqPlayActive.store (false, std::memory_order_relaxed);
            }
            else if (seqOn)
            {
                const double len = seq->lengthSec();
                double t0 = seqClock.load (std::memory_order_relaxed);
                if (seq->loop && len > 1.0e-6)
                    t0 = upic::sequenceLoopTime (t0, len);
                else if (len > 0.0 && t0 >= len)
                {
                    // WP4 fix: do not cut the buffer off in the middle - hand
                    // the last recorded position to the internal path, which
                    // then runs the ordinary end-of-page fade.
                    seqPlayActive.store (false, std::memory_order_relaxed);
                    const auto& last = seq->points.back();
                    audioPage = juce::jlimit (0, (int) snap.pages.size() - 1, last.page);
                    audioPlayhead = juce::jlimit (0.0, snap.pages[(size_t) audioPage].durationSec,
                                                  last.pos);
                    seqClock.store (0.0, std::memory_order_relaxed);
                    continue;
                }

                int chunk = juce::jlimit (1, 256, n - offset);
                // WP4 fix: split the chunk at the next recorded point (and at
                // the loop seam), so a page switch or a wrap lands on the
                // sample it was recorded at instead of up to 5 ms late.
                {
                    const auto nextIt = std::upper_bound (seq->points.begin(), seq->points.end(), t0,
                        [] (double v, const upic::PerformancePoint& p) { return v < p.t; });
                    if (nextIt != seq->points.end() && nextIt->t > t0)
                        chunk = juce::jmin (chunk, juce::jmax (1, (int) std::ceil ((nextIt->t - t0) * sr)));
                    if (seq->loop && len > 1.0e-6 && len > t0)
                        chunk = juce::jmin (chunk, juce::jmax (1, (int) std::ceil ((len - t0) * sr)));
                }
                upic::SequencePosition p0, p1;
                upic::sequencePositionAt (seq->points, t0, p0);
                upic::sequencePositionAt (seq->points, t0 + (double) chunk / sr, p1);

                const int target = juce::jlimit (0, (int) snap.pages.size() - 1, p0.page);
                if (target != audioPage)
                {
                    audioPage = target;
                    engine.reset();
                    notifyPage (audioPage);
                }
                const auto& spg = snap.pages[(size_t) audioPage];
                audioPlayhead = juce::jlimit (0.0, spg.durationSec, p0.pos);

                double seqRate = 1.0;
                if (p0.page == p1.page && p1.page == target)
                {
                    seqRate = (p1.pos - p0.pos) * (double) sr / (double) chunk;
                    seqRate = juce::jlimit (-4.0, 4.0, seqRate);
                    if (std::abs (seqRate) < 1.0e-4) seqRate = 1.0e-4; // a hold
                }
                engine.render (spg, audioPlayhead, snap.waveforms, snap.envelopes,
                               snap.slots, SL + offset,
                               SR != nullptr ? SR + offset : nullptr, chunk, 1.0f,
                               seqRate, false, snap.lutsFor (audioPage),
                               &snap.amplitudeTables, snap.overridesFor (audioPage),
                               &snap.channels);
                offset += chunk;
                const double nextT = t0 + (double) chunk / sr;
                seqClock.store (seq->loop && len > 1.0e-6
                                    ? upic::sequenceLoopTime (nextT, len) : nextT,
                                std::memory_order_relaxed);
                continue;
            }
        }

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
                        postToMessageThread ([] (MainComponent& component)
                        {
                            component.stopPlayback (true);
                        });
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
                    postToMessageThread ([] (MainComponent& component)
                    {
                        component.stopPlayback (true);
                    });
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
                        postToMessageThread ([] (MainComponent& component)
                        {
                            component.stopPlayback (true);
                        });
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
                    postToMessageThread ([] (MainComponent& component)
                    {
                        component.stopPlayback (true);
                    });
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
                      blockRate, false, snap.lutsFor (audioPage), &snap.amplitudeTables,
                      snap.overridesFor (audioPage), &snap.channels);

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

std::uint64_t MainComponent::beginPreviewRoutesForHostLifecycle()
{
    // WP7: a device/host restart cancels every preview route, not only the
    // extraction one. The caller holds audioPreviewLifecycleLock, so
    // overlapping preparation/release calls cannot end one another's gate.
    audioPreviewLifecycleCancelling.store (true, std::memory_order_release);
    juce::ScopedLock lock (audioPreviewRouteLock);

    // Stable epochs are even; an active lifecycle boundary is odd. A
    // publisher that captures the old even epoch or the active odd epoch is
    // rejected when it eventually tries to publish.
    audioPreviewLifecycleEpoch.fetch_add (1, std::memory_order_acq_rel);

    const auto extractionGeneration = sampleExtractPreviewGenerationPublished.load (
        std::memory_order_acquire);
    clearOrdinaryPreviewRoutes();
    audioPreviewRouteToken.store (makeAudioPreviewRouteToken (
        AudioPreviewRoute::none, 0), std::memory_order_release);
    return extractionGeneration;
}

void MainComponent::endPreviewRoutesForHostLifecycle()
{
    juce::ScopedLock lock (audioPreviewRouteLock);
    // Odd -> even: publishers that captured the active odd epoch now see a
    // different value and refuse their stale claim.
    audioPreviewLifecycleEpoch.fetch_add (1, std::memory_order_acq_rel);
    audioPreviewLifecycleCancelling.store (false, std::memory_order_release);
}

void MainComponent::cancelPreviewRoutesForHostLifecycle()
{
    juce::ScopedLock lifecycleLock (audioPreviewLifecycleLock);
    const auto extractionGeneration = beginPreviewRoutesForHostLifecycle();
    struct LifecycleEnd
    {
        MainComponent& owner;
        ~LifecycleEnd() { owner.endPreviewRoutesForHostLifecycle(); }
    } lifecycleEnd { *this };
    cancelSampleExtractPreviewForHostLifecycle (extractionGeneration);
}

void MainComponent::cancelSampleExtractPreviewForHostLifecycle (
    std::uint64_t generation)
{
    // The extraction state and panel belong to the message thread. The
    // generation was captured at the lifecycle boundary; a deferred
    // cancellation must not cancel a newer extraction preview that was
    // armed after that request.
    auto* const messages = juce::MessageManager::getInstanceWithoutCreating();
    if (messages != nullptr && messages->isThisTheMessageThread())
    {
        cancelSampleExtractPreviewGeneration (generation);
        return;
    }

    if (messages != nullptr)
    {
        postToMessageThread ([generation] (MainComponent& component)
        {
            component.cancelSampleExtractPreviewGeneration (generation);
        });
    }
}

void MainComponent::closeAudioPreviewCallbackGate()
{
    const auto gate = audioPreviewCallbackGate;
    if (gate == nullptr)
        return;

    std::lock_guard<std::mutex> lock (gate->mutex);
    gate->alive.store (false, std::memory_order_release);
    gate->closed.store (true, std::memory_order_release);
}

void MainComponent::releaseResources()
{
    // A release invalidates a preparation that may still be queued on the
    // message thread. The requested and published generations then differ,
    // so the audio callback stays silent until a newer prepare is applied.
    invalidateAudioPreparation();
    cancelPreviewRoutesForHostLifecycle();

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