#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

//==============================================================================
// WP7 (Wave F, 2026-11-21): the sample extraction panel.
//
// The panel is deliberately in its own file: it is a self-contained WP7
// region, and the parallel WP8 branch does not touch it. Everything here runs
// on the message thread. The analysis and the preview material are prepared
// before they enter a ModelSnapshot; the audio thread only reads the
// finished snapshot and renders the isolated preview page.
namespace
{
    template <typename Bank>
    int nextId (const Bank& bank)
    {
        // Wavetable and envelope banks share the piece's id space, whose
        // first built-in asset is id 0. Reuse the same free-id rule as the
        // rest of the model instead of minting an id that may already be in
        // use.
        return upic::nextBankId (bank, 0);
    }

    template <typename Bank>
    bool hasName (const Bank& bank, const juce::String& name)
    {
        for (const auto& item : bank)
            if (item.name.equalsIgnoreCase (name)) return true;
        return false;
    }

    template <typename Bank>
    juce::String uniqueName (const Bank& bank, const juce::String& base)
    {
        if (! hasName (bank, base)) return base;
        for (int n = 2;; ++n)
        {
            const auto candidate = base + " " + juce::String (n);
            if (! hasName (bank, candidate)) return candidate;
        }
    }
}

//==============================================================================
void MainComponent::openSampleExtractPanel()
{
    // The preview engine is prepared only by prepareToPlay()/the host's
    // preparation lifecycle. Preparing it here could race the audio callback
    // that reads the same engine - prepare() releases every voice pool and
    // grows buffers.
    if (! sampleExtractPanel)
    {
        sampleExtractPanel = std::make_unique<SampleExtractPanel>();
        // The panel is added hidden: the material is captured (and analysed)
        // below, and that analysis is a message-thread autocorrelation - the
        // window must not show a frozen panel while it runs.
        addChildComponent (*sampleExtractPanel);
        sampleExtractPanel->setBounds (getLocalBounds());

        sampleExtractPanel->onDetect = [this]
        {
            sampleExtractPanel->analyse();
            if (sampleExtractPanel->isPreviewOn())
            {
                // Detect refreshes the current A/B side; it never toggles it.
                // The restart publishes the new analysis and a new transport
                // generation as one snapshot.
                restartSampleExtractPreview();
            }
        };
        sampleExtractPanel->onAlign = [this]
        {
            sampleExtractPanel->alignCycle();
            if (sampleExtractPanel->isPreviewOn())
            {
                // Align refreshes the current A/B side; it never toggles it.
                restartSampleExtractPreview();
            }
        };
        sampleExtractPanel->onChannelChanged = [this]
        {
            // The panel has already re-analysed the chosen mono channel.
            // Republish the current A/B side so the original preview, the
            // extracted cycle and the saved asset all name that channel.
            if (sampleExtractPanel->isPreviewOn())
                restartSampleExtractPreview();
        };
        sampleExtractPanel->onPreview = [this] { previewSampleExtract(); };
        sampleExtractPanel->onToWave = [this] { saveSampleCycleToWave(); };
        sampleExtractPanel->onToEnv = [this] { saveSampleEnvelope(); };
        sampleExtractPanel->onClose = [this] { closeSampleExtractPanel(); };
    }

    // Capture the source slot. The panel keeps this immutable context, so a
    // later selection, load or undo cannot pair old analysis with new audio.
    upic::SampleSlot material;
    {
        const juce::ScopedLock l (dataLock);
        if (selSlot >= 0 && selSlot < (int) sampleSlots.size())
            material = sampleSlots[(size_t) selSlot];
    }

    sampleExtractPanel->setMaterial (&material);
    const bool canPreview = sampleExtractPanel->hasMaterial();
    // Opening the panel never arms a preview that would start later by
    // itself. While the transport is running the A/B route stays idle.
    const bool autoPreview = canPreview && ! transportActive();
    sampleExtractPanel->setPreviewState (autoPreview, false);
    sampleExtractPanel->setVisible (true);
    sampleExtractPanel->toFront (true);
    sampleExtractPanel->grabKeyboardFocus();

    if (autoPreview)
        restartSampleExtractPreview();
    else
        cancelSampleExtractPreview();
}

//==============================================================================
void MainComponent::closeSampleExtractPanel()
{
    // Clear the panel first, then cancel and publish: the snapshot that goes
    // out must contain neither the route nor the old payload.
    if (sampleExtractPanel)
    {
        sampleExtractPanel->setPreviewState (false, false);
        sampleExtractPanel->setVisible (false);
        sampleExtractPanel->clearMaterial();
    }
    cancelSampleExtractPreview();
}

void MainComponent::advanceSampleExtractPreviewGeneration()
{
    ++sampleExtractPreviewState.generation;
    sampleExtractPreviewGenerationPublished.store (
        sampleExtractPreviewState.generation, std::memory_order_release);
}

void MainComponent::resetSampleExtractRoute()
{
    // Bumping the generation makes an old audio block harmless even if it
    // is already inside the renderer. The message thread alone writes this
    // state; the audio thread never writes it back.
    const auto cancelledGeneration = sampleExtractPreviewState.generation;
    advanceSampleExtractPreviewGeneration();
    sampleExtractPreviewState.route = SampleExtractRoute::none;
    sampleExtractPreviewState.durationFrames = 0;
    // Release only the exact extraction token this cancellation owns. An
    // ordinary route, or a newer extraction generation, must not be stolen.
    releaseAudioPreviewRouteTokenIf (makeAudioPreviewRouteToken (
        AudioPreviewRoute::sampleExtract, cancelledGeneration));
}

void MainComponent::cancelSampleExtractPreview()
{
    resetSampleExtractRoute();
    if (sampleExtractPanel)
        sampleExtractPanel->setPreviewState (false, false);
    // Cancellation is published immediately by every caller: a stale active
    // route in the last snapshot would revive the old preview on the audio
    // thread even though the panel is already closed.
    publishModelSnapshot();
}

void MainComponent::cancelSampleExtractPreviewGeneration (
    std::uint64_t generation)
{
    // Message thread only. The exact generation check is the ownership
    // rule: a lifecycle request for an old preview must leave a newer one
    // armed and untouched.
    if (sampleExtractPreviewState.generation == generation)
        cancelSampleExtractPreview();
}

void MainComponent::invalidateSampleExtractSource()
{
    if (! sampleExtractPanel && sampleExtractPreviewState.route == SampleExtractRoute::none)
        return;

    // A captured source is useful only while it still describes material in
    // the current piece. Closing it on replacement/load/undo avoids saving
    // a result derived from an older take.
    //
    // The caller publishes the model change that invalidated this source, so
    // the route and the new material travel in the same snapshot.
    resetSampleExtractRoute();
    if (sampleExtractPanel)
    {
        sampleExtractPanel->setPreviewState (false, false);
        sampleExtractPanel->setVisible (false);
        sampleExtractPanel->clearMaterial();
    }
}

//==============================================================================
void MainComponent::restartSampleExtractPreview()
{
    if (! sampleExtractPanel || ! sampleExtractPanel->isPreviewOn()
        || ! sampleExtractPanel->hasMaterial() || transportActive())
    {
        cancelSampleExtractPreview();
        return;
    }

    const auto routeEpoch = audioPreviewRouteEpochForClaim();
    juce::ScopedLock routeLock (audioPreviewRouteLock);
    if (! audioPreviewRouteClaimIsCurrent (routeEpoch))
        return;

    // The extraction route is exclusive with the ordinary scrub/freeze
    // routes. A preview never mixes with another preview. Clear every piece
    // of ordinary route state before publishing the new owner.
    clearOrdinaryPreviewRoutes();

    advanceSampleExtractPreviewGeneration();
    sampleExtractPreviewState.route = sampleExtractPanel->isPreviewExtracted()
        ? SampleExtractRoute::extracted : SampleExtractRoute::original;
    sampleExtractPreviewState.durationFrames = juce::jmax (256,
        (int) (audioSampleRate() * 2.0));
    // The extraction route wins the arbitration before its snapshot is
    // published. Its generation is part of the token, so an older snapshot
    // cannot render this request (or beside an ordinary route).
    setAudioPreviewRoute (AudioPreviewRoute::sampleExtract);
    publishModelSnapshot();
}

//==============================================================================
void MainComponent::previewSampleExtract()
{
    if (! sampleExtractPanel || ! sampleExtractPanel->hasMaterial())
    {
        flash ("There is no material to preview");
        return;
    }

    if (transportActive())
    {
        flash ("Stop the transport to hear the sample extraction preview");
        return;
    }

    const bool extracted = ! sampleExtractPanel->isPreviewExtracted();
    if (extracted && ! sampleExtractPanel->hasCycle())
    {
        flash ("No repeating loop was found - there is no cycle to preview");
        return;
    }

    sampleExtractPanel->setPreviewState (true, extracted);
    restartSampleExtractPreview();

    flash (extracted ? "A/B preview: the extracted cycle"
                      : "A/B preview: the original material");
}

//==============================================================================
bool MainComponent::saveSampleCycleToWave()
{
    if (! sampleExtractPanel || ! sampleExtractPanel->hasCycle()
        || ! sampleExtractPanel->hasPreviewWave())
    {
        flash ("No repeating loop was found - there is no cycle to save");
        return false;
    }
    if ((int) waveforms.size() >= upic::limits::kMaxWaveforms)
    {
        flash ("A piece holds at most " + juce::String (upic::limits::kMaxWaveforms)
               + " wavetables (the limit) - delete one before extracting another");
        return false;
    }

    beginEdit ("Extract the wavetable");
    bool ok = false;
    {
        const juce::ScopedLock l (dataLock);
        const int id = nextId (waveforms);
        // A fresh bank may legitimately mint id 0 (the built-in wave space
        // starts there); only -1 means that no free id is left.
        if (id < 0 || id > upic::limits::kMaxItemId)
        {
            flash ("The wavetable bank has no free id left");
        }
        else
        {
            juce::String base = "Extracted cycle";
            const auto sourceName = sampleExtractPanel->sourceName();
            if (sourceName.isNotEmpty()) base = sourceName + " cycle";

            // Save the EXACT vector the A/B preview auditioned. Converting
            // again here would resample or normalise differently from what
            // the user heard.
            upic::Waveform wave = sampleExtractPanel->previewWaveform();
            wave.id = id;
            wave.name = uniqueName (waveforms, base);
            wave.colour = upic::paletteColour (id);
            waveforms.push_back (wave);
            selWave = id;
            ok = true;
        }
    }

    if (ok)
    {
        waveLib.setSource (LibraryManager::Source::Piece);
        afterLibraryChange();
    }
    endEdit();

    if (ok)
        flash ("Saved the extracted cycle as wavetable " + juce::String (selWave));
    return ok;
}

//==============================================================================
bool MainComponent::saveSampleEnvelope()
{
    if (! sampleExtractPanel || sampleExtractPanel->envelopePoints().empty())
    {
        flash ("There is no material to extract an envelope from");
        return false;
    }
    if ((int) envelopes.size() >= upic::limits::kMaxEnvelopes)
    {
        flash ("A piece holds at most " + juce::String (upic::limits::kMaxEnvelopes)
               + " envelopes (the limit) - delete one before extracting another");
        return false;
    }

    beginEdit ("Extract the envelope");
    bool ok = false;
    {
        const juce::ScopedLock l (dataLock);
        const int id = nextId (envelopes);
        // As with the wave bank, id 0 is a valid envelope id.
        if (id < 0 || id > upic::limits::kMaxItemId)
        {
            flash ("The envelope bank has no free id left");
        }
        else
        {
            juce::String base = "Extracted envelope";
            const auto sourceName = sampleExtractPanel->sourceName();
            if (sourceName.isNotEmpty()) base = sourceName + " envelope";

            upic::Envelope envelope (id, uniqueName (envelopes, base),
                                     upic::paletteColour (id));
            envelope.points = sampleExtractPanel->envelopePoints();
            if (envelope.points.size() < 2) envelope.presetFlat();
            envelopes.push_back (envelope);
            selEnv = id;
            ok = true;
        }
    }

    if (ok)
    {
        envLib.setSource (LibraryManager::Source::Piece);
        afterLibraryChange();
    }
    endEdit();

    if (ok)
        flash ("Saved the extracted contour as envelope " + juce::String (selEnv));
    return ok;
}

//==============================================================================
void MainComponent::addSampleExtractPreview (ModelSnapshot& snap)
{
    snap.samplePreviewPage.reset();
    snap.samplePreviewSlots.clear();
    snap.samplePreviewWaves.clear();
    snap.samplePreviewEnvelopes.clear();
    snap.sampleExtractPreview = sampleExtractPreviewState;

    const auto route = sampleExtractPreviewState.route;
    if (route == SampleExtractRoute::none || ! sampleExtractPanel
        || ! sampleExtractPanel->isVisible() || ! sampleExtractPanel->isPreviewOn())
        return;

    // The panel's captured cycle is the one and only source of the pitch of
    // the comparison. Both sides are auditioned at the same reference, so
    // the A/B toggle changes the material, not the pitch.
    const double cycleHz = sampleExtractPanel->cycle().ok
                             ? sampleExtractPanel->cycle().frequencyHz : 0.0;
    double ref = cycleHz > 0.0 ? cycleHz : sampleExtractPanel->sourceSlot().refFreq;
    if (! (ref > 0.0)) ref = 440.0;
    ref = upic::limits::clampFinite (ref, 1.0, 20000.0);

    upic::Page page;
    page.durationSec = 4.0;

    upic::Arc arc;
    arc.id = 1;
    arc.refFreq = ref;
    arc.intensity = 1.0f;
    arc.amplitudeTableId = -1;
    arc.envelopeId = 0;         // the isolated flat envelope below
    arc.points = { { 0.0, ref }, { 4.0, ref } };

    if (route == SampleExtractRoute::extracted && sampleExtractPanel->hasPreviewWave())
    {

        // Preview the exact converted wavetable "To wave" saves. It lives
        // in the snapshot's own vector, outside the piece's wave bank.
        snap.samplePreviewWaves.push_back (sampleExtractPanel->previewWaveform());
        arc.mode = 0;              // the oscillator path
        arc.waveformId = snap.samplePreviewWaves.front().id;
    }
    else
    {
        // Use the same mono channel as the detector for the original side.
        // This avoids an antiphase recording averaging to silence in one
        // side of the A/B comparison.
        upic::SampleSlot slot = sampleExtractPanel->sourceSlot();
        if (! slot.loaded()) return;

        auto original = sampleExtractPanel->originalPreviewBuffer();
        if (original.getNumSamples() <= 0) return;

        slot.data = std::make_shared<const juce::AudioBuffer<float>> (std::move (original));
        // One explicit neutral comparison policy: no inherited direction,
        // fades, level or gain from the piece's slot.
        slot.reversed = false;
        slot.fadeInSec = 0.0;
        slot.fadeOutSec = 0.0;
        slot.level = 1.0f;
        slot.gain = 1.0f;
        slot.loopStart = 0;
        slot.loopEnd = slot.data->getNumSamples();
        slot.refFreq = ref;

        // The preview material is kept out of snap.slots. Normal playback
        // and freeze therefore continue to use the real material.
        snap.samplePreviewSlots.push_back (slot);

        arc.mode = 1;              // the material path
        arc.pitchMode = 0;         // varispeed: the preview allocates nothing
        arc.sampleId = slot.id;
    }

    page.arcs.push_back (arc);
    snap.samplePreviewPage = std::make_shared<const upic::Page> (std::move (page));

    // The preview owns a flat envelope. It cannot inherit an edited or
    // deleted envelope from the piece's global bank.
    upic::Envelope flat (0, "Flat", juce::Colours::white);
    flat.presetFlat();
    snap.samplePreviewEnvelopes.push_back (std::move (flat));
}

//==============================================================================
bool MainComponent::sampleExtractSnapshotUsesSourceChannelForTest (
    int channel, int frame) const
{
    if (sampleExtractPanel == nullptr
        || publishedSampleExtractPreview.slots.size() != 1)
        return false;

    const auto& previewSlot = publishedSampleExtractPreview.slots.front();
    if (! previewSlot.loaded() || previewSlot.data == nullptr
        || previewSlot.data->getNumChannels() != 1)
        return false;

    const auto& source = sampleExtractPanel->sourceSlot();
    if (! source.loaded() || source.data == nullptr
        || channel < 0 || channel >= source.data->getNumChannels()
        || frame < 0 || frame >= source.regionFrames())
        return false;

    const int sourceFrame = source.regionStart() + frame;
    if (sourceFrame < 0 || sourceFrame >= source.totalFrames())
        return false;

    // The original A/B side must contain one copy of the selected source
    // channel. It must not be a stereo average (which can cancel an
    // antiphase pair) and it must not come from the other channel.
    return previewSlot.data->getSample (0, frame)
             == source.data->getSample (channel, sourceFrame);
}

//==============================================================================
bool MainComponent::sampleExtractRenderedMatchesIsolatedWaveForTest (
    const juce::AudioBuffer<float>& rendered, int blockSize) const
{
    const auto& published = publishedSampleExtractPreview;
    if (blockSize <= 0
        || published.state.route != SampleExtractRoute::extracted
        || published.page == nullptr
        || published.waves.size() != 1
        || rendered.getNumChannels() <= 0
        || rendered.getNumSamples() < blockSize)
        return false;

    // Render an independent reference from the isolated vector. This does
    // not use wavesForSampleExtractRoute(), so a renderer that regressed to
    // the piece's same-id wave produces a different signal and fails here.
    UpicEngine reference;
    reference.prepare (audioSampleRate(), blockSize);
    reference.setScrubPreview (true);

    std::vector<float> left ((size_t) blockSize, 0.0f);
    std::vector<float> right ((size_t) blockSize, 0.0f);
    reference.render (*published.page, 0.0,
                      published.waves,
                      published.envelopes,
                      published.slots,
                      left.data(), right.data(), blockSize, 1.0f, 1.0, false);

    const float mg = masterGain.load (std::memory_order_relaxed);
    constexpr float tolerance = 1.0e-6f;
    for (int i = 0; i < blockSize; ++i)
    {
        if (std::abs (rendered.getSample (0, i) - left[(size_t) i] * mg) > tolerance)
            return false;
        if (rendered.getNumChannels() > 1
            && std::abs (rendered.getSample (1, i) - right[(size_t) i] * mg) > tolerance)
            return false;
    }
    return true;
}