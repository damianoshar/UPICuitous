#include "MainComponent.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <utility>

//==============================================================================
// The live input and the capture: the input tap (lock-free, on the audio
// thread), the capture lifecycle and the device inputs.

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
    // UI must not be touched from the audio thread. The shared callback gate
    // keeps the queued finalisation safe if the host destroys the component
    // on another thread.
    postToMessageThread ([] (MainComponent& component)
    {
        component.finalizeCapture();
    });
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

    // WP7: the panel's captured take is now stale. Invalidate it before
    // the replacement is published, so no preview or save can mix the old
    // analysis with the new material.
    invalidateSampleExtractSource();

    {
        // model is read by the audio thread -> mutate under the lock
        juce::ScopedLock l(dataLock);
        // the capture replaces the material, not the slot: the reverse flag
        // (v1.19), the Ref (M5) and the level (M5) belong to the slot, so
        // they survive
        const bool wasReversed = sampleSlots[(size_t) slotIndex].reversed;
        const double wasRef = sampleSlots[(size_t) slotIndex].refFreq;
        const float wasLevel = sampleSlots[(size_t) slotIndex].level;
        sampleSlots[(size_t) slotIndex] = slot;
        sampleSlots[(size_t) slotIndex].reversed = wasReversed;
        sampleSlots[(size_t) slotIndex].refFreq = wasRef;
        sampleSlots[(size_t) slotIndex].level = wasLevel;
        // M5: the material's edits (the trim, the fades, the normalise
        // gain) belong to the material that was there - the new take has
        // none of them.
        sampleSlots[(size_t) slotIndex].resetEdits();
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
