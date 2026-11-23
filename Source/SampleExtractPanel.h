#pragma once
#include <JuceHeader.h>

#include <cmath>
#include <functional>
#include <utility>
#include <vector>

#include "SampleAnalysis.h"
#include "Sample.h"
#include "UpicTheme.h"
#include "Waveform.h"

//==============================================================================
// WP7 (Wave F, 2026-11-21): the sample extraction panel.
//
// The Sample tab's material can be taken apart: the panel finds the shortest
// loop that repeats inside the material (the CYCLE) and the material's
// amplitude contour (the ENVELOPE), and hands either one to the piece:
//
//   "To wave"  - the cycle becomes a wavetable (one loop of the material);
//   "To env"   - the contour becomes an envelope in the envelope bank;
//   "A/B"      - the extracted cycle is auditioned in place of the material
//                (the preview goes through a dedicated scrub engine, so it
//                cannot disturb the score's ordinary preview).
//
// It is an in-app overlay for the same reason the other editors are: Android
// has no closable dialog windows.
//
// WP6's mixer panel was withdrawn from 0.34 after it overflowed the stack on
// open/teardown in the selftest, so this panel is built LAZILY (only when the
// user opens it) and only on the message thread, and the selftest opens and
// closes it once before it drives any command.
//==============================================================================
class SampleExtractPanel : public juce::Component
{
public:
    std::function<void()> onClose, onDetect, onAlign, onPreview, onToWave, onToEnv;
    std::function<void()> onChannelChanged;

    SampleExtractPanel()
    {
        setName ("sampleExtractPanel");
        setWantsKeyboardFocus (true);

        for (auto* b : { &detectBtn, &alignBtn, &previewBtn, &toWaveBtn, &toEnvBtn, &closeBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            addAndMakeVisible (b);
        }
        detectBtn.setName ("extractDetectBtn");
        alignBtn.setName ("extractAlignBtn");
        previewBtn.setName ("extractPreviewBtn");
        toWaveBtn.setName ("extractToWaveBtn");
        toEnvBtn.setName ("extractToEnvBtn");
        closeBtn.setName ("extractCloseBtn");

        detectBtn.setButtonText ("Detect");
        alignBtn.setButtonText ("Align");
        previewBtn.setButtonText ("A/B");
        toWaveBtn.setButtonText ("To wave");
        toEnvBtn.setButtonText ("To env");
        closeBtn.setButtonText ("Close");

        detectBtn.setTooltip ("Find the shortest loop that repeats in the material");
        alignBtn.setTooltip ("Move the loop's start to the nearest zero crossing");
        previewBtn.setTooltip ("Switch the A/B preview between the original material\n"
                               "and the extracted cycle");
        previewBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2f6f9f));
        toWaveBtn.setTooltip ("Make a wavetable from the extracted loop");
        toEnvBtn.setTooltip ("Make an envelope from the material's contour");
        closeBtn.setTooltip ("Close the extraction panel");

        detectBtn.onClick  = [this] { if (onDetect) onDetect(); };
        alignBtn.onClick   = [this] { if (onAlign) onAlign(); };
        previewBtn.onClick = [this] { if (onPreview) onPreview(); };
        toWaveBtn.onClick  = [this] { if (onToWave) onToWave(); };
        toEnvBtn.onClick   = [this] { if (onToEnv) onToEnv(); };
        closeBtn.onClick   = [this] { if (onClose) onClose(); };

        modeBox.setName ("extractEnvelopeModeBox");
        modeBox.setTooltip ("The contour's measure: RMS energy or the absolute peak");
        modeBox.addItem ("RMS", 1);
        modeBox.addItem ("Peak", 2);
        modeBox.setSelectedId (1, juce::dontSendNotification);
        modeBox.onChange = [this]
        {
            mode = modeBox.getSelectedId() == 2 ? upic::EnvelopeMode::Peak
                                                : upic::EnvelopeMode::RMS;
            extractEnvelopePoints();
            updateInfo();
            repaint();
        };
        addAndMakeVisible (modeBox);

        channelBox.setName ("extractChannelBox");
        channelBox.setTooltip ("The source channel used by the detector, the A/B preview and the saved assets");
        channelBox.onChange = [this]
        {
            const int channel = channelBox.getSelectedId() - 1;
            if (channel < 0 || channel >= sourceChannelCount())
                return;

            channelSelectionExplicit = true;
            analysisChannel = channel;
            analyse();
            if (onChannelChanged) onChannelChanged();
        };
        addAndMakeVisible (channelBox);

        infoLab.setName ("extractInfoLab");
        infoLab.setFont (juce::Font (juce::FontOptions (12.0f)));
        infoLab.setColour (juce::Label::textColourId, juce::Colour (0xffc9c9d4));
        infoLab.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (infoLab);
    }

    // The material to analyse. The panel captures the slot (the audio buffer
    // itself is immutable and shared), so a later load, undo or selection
    // change cannot make the preview and the saved result disagree.
    void setMaterial (const upic::SampleSlot* slot)
    {
        source = upic::SampleSlot();
        regionStart = 0;
        regionFrames = 0;
        sampleRate = 44100.0;
        analysisChannel = 0;
        channelSelectionExplicit = false;

        if (slot != nullptr && slot->loaded())
        {
            source = *slot;
            regionStart = source.regionStart();
            regionFrames = source.regionFrames();
            sampleRate = source.sampleRate;
        }

        rebuildChannelChoices();
        analyse();
    }

    // Releases the captured material and the extracted cycle. Closing the
    // panel must not keep a long recording alive behind the UI.
    void clearMaterial()
    {
        source = upic::SampleSlot();
        clearCycle();
        envPts.clear();
        analysisChannel = 0;
        channelSelectionExplicit = false;
        channelBox.clear (juce::dontSendNotification);
        previewOn = false;
        regionStart = 0;
        regionFrames = 0;
        updateInfo();
        repaint();
    }

    // Finds the cycle and the contour.
    void analyse()
    {
        // Any new analysis starts from "no cycle". The cached preview wave
        // and the effective extracted side go with it: a failed detect must
        // never leave the A/B route claiming material that is not there.
        const bool keepExtractedRequest = previewRequestedExtracted;
        clearCycle();

        if (! hasMaterial())
        {
            envPts.clear();
            updateInfo();
            repaint();
            return;
        }

        // The search range: 25 Hz to 2 kHz (the musical range), clamped to
        // the material's own length.
        const int minLen = juce::jmax (16, (int) (sampleRate / 2000.0));
        const int maxLen = juce::jmin (regionFrames, (int) (sampleRate / 25.0));

        upic::CycleInfo best;
        const int channels = sourceChannelCount();

        if (channelSelectionExplicit)
        {
            // The user's choice is authoritative: analyse only that mono
            // channel, even when another channel happens to contain a
            // stronger cycle.
            analysisChannel = juce::jlimit (0, channels - 1, analysisChannel);
            const float* signal = source.data->getReadPointer (analysisChannel) + regionStart;
            best = upic::findBestCycle (signal, regionFrames, sampleRate,
                                        minLen, maxLen);
        }
        else
        {
            // With no explicit choice, analyse the channels independently
            // and keep the strongest valid cycle. Averaging first would
            // cancel an antiphase recording; choosing channel 0 blindly would
            // miss a signal that lives only in the other channel.
            int bestChannel = 0;
            // When no channel yields a cycle, the richest channel is still
            // the one the original preview and the envelope describe.
            int fallbackChannel = 0;
            double fallbackRms = -1.0;
            for (int c = 0; c < channels; ++c)
            {
                const float* signal = source.data->getReadPointer (c) + regionStart;
                double sumSquares = 0.0;
                for (int i = 0; i < regionFrames; ++i)
                    sumSquares += (double) signal[i] * (double) signal[i];
                const double rms = std::sqrt (sumSquares / (double) juce::jmax (1, regionFrames));
                if (rms > fallbackRms)
                {
                    fallbackRms = rms;
                    fallbackChannel = c;
                }

                const auto candidate = upic::findBestCycle (signal, regionFrames, sampleRate,
                                                             minLen, maxLen);
                if (! candidate.ok) continue;
                if (! best.ok || candidate.confidence > best.confidence + 1.0e-9
                    || (std::abs (candidate.confidence - best.confidence) <= 1.0e-9
                        && candidate.lengthSamples < best.lengthSamples))
                {
                    best = candidate;
                    bestChannel = c;
                }
            }
            analysisChannel = best.ok ? bestChannel : fallbackChannel;
            channelBox.setSelectedId (analysisChannel + 1, juce::dontSendNotification);
        }

        cycleInfo = best;
        if (cycleInfo.ok && ! buildCycleBuffer())
            clearCycle();

        // A re-analysis keeps the requested A/B side when the new result
        // still has a cycle. A failed result remains the original side.
        if (keepExtractedRequest && previewOn)
        {
            previewRequestedExtracted = true;
            previewExtracted = hasPreviewWave();
            previewBtn.setToggleState (previewExtracted, juce::dontSendNotification);
        }

        extractEnvelopePoints();
        updateInfo();
        repaint();
    }

    // Moves the cycle's start to the nearest zero crossing.
    void alignCycle()
    {
        if (! cycleInfo.ok || ! hasMaterial()) return;
        const int start = upic::alignCycleToZeroCrossing (analysisSignal(), regionFrames,
                                                          cycleInfo.startSample,
                                                          cycleInfo.lengthSamples);
        if (start != cycleInfo.startSample)
        {
            cycleInfo.startSample = start;
            if (! buildCycleBuffer())
                clearCycle();
            updateInfo();
            repaint();
        }
    }

    // The A/B preview is always the same pair: the captured slot's original
    // region and the extracted cycle. `extracted` records which side the
    // user asked for; the effective side additionally requires a valid
    // cycle and its converted wavetable.
    void setPreviewState (bool active, bool extracted)
    {
        previewOn = active && hasMaterial();
        previewRequestedExtracted = previewOn && extracted;
        previewExtracted = previewOn && previewRequestedExtracted && hasPreviewWave();
        previewBtn.setToggleState (previewExtracted, juce::dontSendNotification);
        updateInfo();
    }

    //=== the results ========================================================
    const upic::CycleInfo& cycle() const { return cycleInfo; }
    const juce::AudioBuffer<float>& cycleBuffer() const { return cycleBuf; }
    // The exact wavetable "To wave" saves. Previewing this same vector is
    // what makes the A/B side and the saved asset agree, including the
    // wave conversion's resampling and normalisation.
    const upic::Waveform& previewWaveform() const { return previewWave; }
    const std::vector<upic::EnvPoint>& envelopePoints() const { return envPts; }
    int materialFrames() const { return regionFrames; }
    double materialRate() const { return sampleRate; }
    bool hasMaterial() const { return source.loaded() && regionFrames > 0; }
    const upic::SampleSlot& sourceSlot() const { return source; }
    int sourceSlotId() const { return source.id; }
    juce::String sourceName() const { return source.name; }
    upic::EnvelopeMode envelopeMode() const { return mode; }
    int analysisChannelForTest() const { return analysisChannel; }
    int sourceChannelCountForTest() const { return sourceChannelCount(); }

    // The original side of the A/B preview uses the same mono channel as
    // the detector. This keeps the comparison meaningful for stereo and
    // antiphase material instead of letting the engine's channel average
    // silently cancel one side.
    juce::AudioBuffer<float> originalPreviewBuffer() const
    {
        juce::AudioBuffer<float> out;
        if (! hasMaterial() || source.data == nullptr) return out;
        const int n = regionFrames;
        const int c = juce::jlimit (0, source.data->getNumChannels() - 1,
                                    analysisChannel);
        out.setSize (1, n, false, false, true);
        const float* src = source.data->getReadPointer (c) + regionStart;
        out.copyFrom (0, 0, src, n);
        return out;
    }

    //=== the selftest hooks =================================================
    void detectForTest()  { analyse(); }
    void alignForTest()   { alignCycle(); }
    void previewForTest() { if (onPreview) onPreview(); }
    bool previewOnForTest() const { return previewOn; }
    bool previewExtractedForTest() const { return previewExtracted; }
    bool previewRequestedExtractedForTest() const { return previewRequestedExtracted; }
    bool hasPreviewWaveForTest() const { return hasPreviewWave(); }
    const upic::Waveform& previewWaveformForTest() const { return previewWave; }
    bool hasCycleForTest() const { return cycleInfo.ok; }
    bool hasMaterialForTest() const { return hasMaterial(); }
    int cycleLengthForTest() const { return cycleInfo.lengthSamples; }
    int cycleStartForTest() const { return cycleInfo.startSample; }
    int materialFramesForTest() const { return regionFrames; }
    double materialRateForTest() const { return sampleRate; }
    int sourceSlotIdForTest() const { return sourceSlotId(); }
    void setSourceChannelForTest (int channel)
    {
        if (channel < 0 || channel >= sourceChannelCount()) return;
        channelSelectionExplicit = true;
        analysisChannel = channel;
        channelBox.setSelectedId (channel + 1, juce::dontSendNotification);
        analyse();
    }
    void setEnvelopeModeForTest (upic::EnvelopeMode newMode)
    {
        mode = newMode;
        modeBox.setSelectedId (newMode == upic::EnvelopeMode::Peak ? 2 : 1,
                               juce::dontSendNotification);
        extractEnvelopePoints();
        updateInfo();
    }
    std::vector<upic::EnvPoint> envelopePointsForTest() const { return envPts; }

    //=== the drawing ========================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff20202a));

        auto wave = getLocalBounds().reduced (8).withTrimmedTop (infoHeight + 8)
                                          .withTrimmedBottom (buttonRowHeight() + 8);
        if (wave.getWidth() <= 8 || wave.getHeight() <= 8) return;

        g.setColour (juce::Colour (0xff2c2c38));
        g.fillRect (wave);

        if (! hasMaterial())
        {
            g.setColour (juce::Colour (0xff7a7a86));
            g.drawText ("No material in this slot", wave, juce::Justification::centred);
            return;
        }

        // The material's waveform (the region's selected channel).
        const float* signal = analysisSignal();
        const int n = regionFrames;
        const int mid = wave.getCentreY();
        const float amp = (float) wave.getHeight() * 0.45f;

        // The cycle's band.
        if (cycleInfo.ok && n > 0)
        {
            const double x0 = (double) cycleInfo.startSample / (double) n;
            const double x1 = (double) (cycleInfo.startSample + cycleInfo.lengthSamples) / (double) n;
            const auto band = wave.withX ((int) std::lround (wave.getX() + x0 * wave.getWidth()))
                                   .withWidth (juce::jmax (2, (int) std::lround ((x1 - x0) * wave.getWidth())));
            g.setColour (juce::Colour (0xff5ac8fa).withAlpha (0.18f));
            g.fillRect (band);
        }

        g.setColour (juce::Colour (0xff9a9aa5));
        for (int px = 0; px < wave.getWidth(); px += 2)
        {
            const int i0 = (int) ((juce::int64) px * n / wave.getWidth());
            const int i1 = juce::jmax (i0 + 1, (int) ((juce::int64) (px + 2) * n / wave.getWidth()));
            float lo = 0.0f, hi = 0.0f;
            for (int i = i0; i < i1 && i < n; ++i)
            {
                lo = juce::jmin (lo, signal[i]);
                hi = juce::jmax (hi, signal[i]);
            }
            const float y0 = mid - hi * amp;
            const float y1 = mid - lo * amp;
            g.drawLine ((float) wave.getX() + px, y0, (float) wave.getX() + px, y1, 1.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto infoRow = r.removeFromTop (infoHeight);
        channelBox.setBounds (infoRow.removeFromRight (96).reduced (1));
        infoLab.setBounds (infoRow);

        const int h = buttonRowHeight();
        auto row = r.removeFromBottom (h);
        modeBox.setBounds (row.removeFromLeft (72).reduced (1));
        row.removeFromLeft (4);
        const int w = juce::jmax (40, row.getWidth() / 6 - 4);
        closeBtn.setBounds (row.removeFromRight (w).reduced (1));
        toEnvBtn.setBounds (row.removeFromRight (w).reduced (1));
        toWaveBtn.setBounds (row.removeFromRight (w).reduced (1));
        previewBtn.setBounds (row.removeFromRight (w).reduced (1));
        alignBtn.setBounds (row.removeFromRight (w).reduced (1));
        detectBtn.setBounds (row.removeFromRight (w).reduced (1));
    }

private:
    static constexpr int buttonRowHeight() { return 26; }
    static constexpr int infoHeight = 52;

    int sourceChannelCount() const
    {
        return source.data != nullptr ? source.data->getNumChannels() : 0;
    }

    void rebuildChannelChoices()
    {
        channelBox.clear (juce::dontSendNotification);
        const int channels = sourceChannelCount();
        for (int c = 0; c < channels; ++c)
            channelBox.addItem ("Channel " + juce::String (c + 1), c + 1);
        if (channels > 0)
            channelBox.setSelectedId (juce::jlimit (1, channels, analysisChannel + 1),
                                      juce::dontSendNotification);
    }

    const float* analysisSignal() const
    {
        if (! source.loaded() || source.data->getNumChannels() <= 0) return nullptr;
        const int c = juce::jlimit (0, source.data->getNumChannels() - 1,
                                    analysisChannel);
        return source.data->getReadPointer (c) + regionStart;
    }

    bool hasPreviewWave() const
    {
        return cycleInfo.ok
            && previewWave.samples.size() == (size_t) upic::kWaveSize;
    }

    // Forgets the cycle, its converted wavetable and the effective
    // extracted A/B side as one unit. Every path that invalidates the
    // analysis (a new detect, a failed align, new material) goes through
    // here, so no stale preview can survive.
    void clearCycle()
    {
        cycleInfo = upic::CycleInfo();
        cycleBuf.setSize (1, 0);
        previewWave = upic::Waveform();
        previewRequestedExtracted = false;
        previewExtracted = false;
        previewBtn.setToggleState (false, juce::dontSendNotification);
    }

    bool buildCycleBuffer()
    {
        if (! hasMaterial() || ! cycleInfo.ok) return false;

        int start = cycleInfo.startSample;
        const int length = cycleInfo.lengthSamples;
        if (start < 0 || length <= 0 || start + length > regionFrames)
        {
            start = regionFrames - length;
            if (start < 0) return false;
        }

        const float* signal = analysisSignal();
        if (signal == nullptr) return false;

        cycleBuf.setSize (1, length);
        for (int i = 0; i < length; ++i)
            cycleBuf.setSample (0, i, signal[start + i]);

        cycleInfo.startSample = start;
        cycleInfo.lengthSamples = length;
        cycleInfo.frequencyHz = sampleRate / (double) length;

        // Convert once, here. The preview and "To wave" then share the
        // exact same 1024-point, normalised vector - the preview is the
        // asset, not an approximation of it.
        const juce::String base = source.name.isNotEmpty()
                                    ? source.name + " cycle"
                                    : "Extracted cycle";
        previewWave = upic::waveFromTake (cycleBuf, 0, base);
        return hasPreviewWave();
    }

    void extractEnvelopePoints()
    {
        envPts.clear();
        if (! hasMaterial()) return;
        const float* signal = analysisSignal();
        if (signal == nullptr) return;
        // The contour describes the same selected channel as the cycle and
        // the original A/B side, so every output of the panel agrees.
        envPts = upic::extractEnvelope (signal, regionFrames, 256, mode);
    }

    void updateInfo()
    {
        juce::String s;
        if (! hasMaterial())
        {
            s = "No material in this slot - load a sample first.";
        }
        else if (! cycleInfo.ok)
        {
            s = "No repeating loop found in this material.\n"
                + juce::String (regionFrames) + " frames at "
                + juce::String (sampleRate / 1000.0, 1) + " kHz - the whole take can still be digitised with \"Slot -> Wave\".";
        }
        else
        {
            s = "Channel " + juce::String (analysisChannel + 1)
              + ".  Loop: " + juce::String (cycleInfo.lengthSamples) + " frames ("
              + juce::String (1000.0 * cycleInfo.lengthSamples / sampleRate, 1) + " ms), "
              + juce::String (cycleInfo.frequencyHz, 1) + " Hz, start at frame "
              + juce::String (cycleInfo.startSample)
              + ".\nConfidence " + juce::String ((int) (cycleInfo.confidence * 100.0)) + "%."
              + "  Envelope: " + (mode == upic::EnvelopeMode::Peak ? "Peak" : "RMS")
              + (previewOn ? (previewExtracted ? "  /  Preview: cycle"
                                               : "  /  Preview: original")
                           : juce::String());
        }
        infoLab.setText (s, juce::dontSendNotification);
    }

    upic::SampleSlot source; // the captured source; its audio is shared
    int analysisChannel = 0;
    bool channelSelectionExplicit = false;
    double sampleRate = 44100.0;
    int regionStart = 0;
    int regionFrames = 0;
    upic::CycleInfo cycleInfo;
    juce::AudioBuffer<float> cycleBuf;
    upic::Waveform previewWave;       // the exact asset "To wave" saves
    std::vector<upic::EnvPoint> envPts;
    upic::EnvelopeMode mode = upic::EnvelopeMode::RMS;
    bool previewOn = false;
    bool previewRequestedExtracted = false; // the side the user asked for
    bool previewExtracted = false;          // the effective side (cycle valid)

    juce::Label infoLab;
    juce::ComboBox modeBox, channelBox;
    juce::TextButton detectBtn, alignBtn, previewBtn, toWaveBtn, toEnvBtn, closeBtn;
};
