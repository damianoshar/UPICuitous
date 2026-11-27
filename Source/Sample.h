#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "PieceLimits.h"
#include "UpicConstants.h"

namespace upic
{
    //---------------- Sample slot (material for realtime pitch-shifted arcs) ----------------
    // Holds one loadable audio sample. Arcs in mode 1 pitch-shift this material
    // along their frequency curve (varispeed: position advances at the pitch
    // ratio). Embedded in .upic.json as base64 PCM16 so pieces stay
    // self-contained (max 60s per slot on load).
    struct SampleSlot
    {
        int id = 1;
        juce::String name;
        double sampleRate = 44100.0;
        // v1.18 (wave 15): the audio buffer is immutable and shared, so a
        // model snapshot can copy a slot by copying one pointer. Never write
        // through it once it is in a slot: replace the pointer instead (the
        // old buffer stays alive while any snapshot still reads it).
        std::shared_ptr<const juce::AudioBuffer<float>> data; // 1..2 ch, seamless loop
        // v1.19 (wave 17): the material plays backwards. v1.33.4 (H7): the
        // direction changes the sound, so it travels with the piece and with
        // the host state (it used to be dropped on save, so a reopened piece
        // or session sounded different). The engine mirrors the read, so
        // every pitch mode works.
        bool reversed = false;

        // M5: the material's base pitch - the UPIC's per-slot REF. The
        // engine's ratio for a sample arc is arcPitch / this, so the
        // material has ONE unity point. Before M5 the Ref lived on the
        // arc, so two arcs on the same material could disagree about the
        // pitch it was recorded at.
        //
        // 0 means "not set": a piece written before M5 carries no Ref on
        // the slot, and the loader then gives the slot the Ref of the
        // first arc that uses it - so such a piece sounds exactly as it
        // did. A fresh slot starts at the app's default.
        double refFreq = upic::kDefaultRefFreq;

        // M5: the slot's level (the user's knob, 0..2) and the material's
        // own gain (what Normalize sets). Both change the sound, so both
        // travel with the piece.
        float level = 1.0f;
        float gain = 1.0f;

        // M5: the material's loop region - the trim. The frames outside it
        // are not played. loopEnd = 0 means "to the end of the material".
        int loopStart = 0;
        int loopEnd = 0;

        // M5: the fades at the two ends of the loop region (seconds). A
        // faded loop does not click at the seam.
        double fadeInSec = 0.0;
        double fadeOutSec = 0.0;

        bool loaded() const
        {
            return data != nullptr && data->getNumSamples() > 0 && data->getNumChannels() > 0;
        }

        // --- the material's loop region (M5: the trim) ---

        int totalFrames() const
        {
            return data != nullptr ? data->getNumSamples() : 0;
        }

        // the first frame of the region (clamped into the material)
        int regionStart() const
        {
            const int n = totalFrames();
            if (n <= 0) return 0;
            return juce::jlimit (0, n - 1, loopStart);
        }

        // the frame after the last one of the region (clamped into the
        // material; the region is always at least one frame long)
        int regionEnd() const
        {
            const int n = totalFrames();
            if (n <= 0) return 0;
            const int e = (loopEnd > 0 ? loopEnd : n);
            return juce::jlimit (regionStart() + 1, n, e);
        }

        int regionFrames() const
        {
            return juce::jmax (0, regionEnd() - regionStart());
        }

        // The length of the material as it is played: the region's, not the
        // whole file's (M5: the trim shortens it).
        double durationSec() const
        {
            return (regionFrames() > 0 && sampleRate > 0)
                       ? (double) regionFrames() / sampleRate : 0.0;
        }

        // --- the material's edits (M5) ---

        // The peak of the region, over both channels.
        float peakInRegion() const
        {
            if (data == nullptr) return 0.0f;
            const auto& buf = *data;
            const int a = regionStart(), b = regionEnd();
            const int ch = juce::jmin (2, buf.getNumChannels());
            float p = 0.0f;
            for (int c = 0; c < ch; ++c)
                for (int i = a; i < b; ++i)
                    p = juce::jmax (p, std::abs (buf.getSample (c, i)));
            return p;
        }

        // Normalize: the material's gain is set so the region's peak sits
        // just under full scale. It is a gain and not a rewrite of the
        // audio, so the Reset button can take it back, the undo stack can
        // step over it, and it travels with the piece.
        bool normalise()
        {
            const float p = peakInRegion();
            if (! (p > 1.0e-6f)) return false;
            gain = juce::jlimit (0.01f, 64.0f, 0.99f / p);
            return true;
        }

        // The fades at the two ends of the region (a click-free loop).
        // Without a length, a tenth of the region each.
        void applyFade (double seconds = 0.0)
        {
            const double d = durationSec();
            const double f = (seconds > 0.0 ? seconds : d * 0.1);
            fadeInSec  = juce::jlimit (0.0, d * 0.5, f);
            fadeOutSec = juce::jlimit (0.0, d * 0.5, f);
        }

        // The material's edits go back to how it was loaded.
        void resetEdits()
        {
            loopStart = 0;
            loopEnd = 0;
            fadeInSec = 0.0;
            fadeOutSec = 0.0;
            gain = 1.0f;
        }

        // R2.9: the file format of a sample slot is a different thing from
        // what a sample slot is. It lives at the edge
        // (Source/SampleCodec.h), where the JUCE value types are kept.
    };

    // Fold the tail of a buffer into its head with a smooth cosine crossfade so
    // the material loops without a click — playback reads may cross the seam.
    // Result is (original length - crossfade) samples, seamless end-to-start.
    inline void makeSeamlessLoop(juce::AudioBuffer<float>& buf, double sr)
    {
        const int n = buf.getNumSamples();
        const int ch = buf.getNumChannels();
        if (n < 64 || ch < 1 || sr <= 0.0) return;
        const int x = juce::jlimit(1, n / 4, (int) (0.05 * sr)); // <= 50 ms fade
        juce::AudioBuffer<float> out(ch, n - x);
        for (int c = 0; c < ch; ++c)
        {
            const float* s = buf.getReadPointer(c);
            float* d = out.getWritePointer(c);
            for (int i = 0; i < n - x; ++i)
            {
                if (i >= x) { d[i] = s[i]; continue; }
                const float u = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * (float) i / (float) x);
                d[i] = s[i] * u + s[i + (n - x)] * (1.0f - u);
            }
        }
        buf = std::move(out);
    }

    //---------------- loading a file as sample material (M19)----------------
    // The material keeps the file's own rate. The old importer clamped the
    // rate to 96 kHz and kept the frames, so a 192 kHz file was played at
    // half speed (and the seamless-loop crossfade used the wrong rate too).
    // Now: a file above the app's ceiling is refused with a message (it is
    // never retuned), the length is capped at the duration - 60 s counted
    // at the file's OWN rate - and at the memory budget, and the decoder's
    // return value is checked (it used to be ignored, so a file that could
    // not be decoded became silence). JUCE's reader pads a short file with
    // zeros and does not report a frame count, so a file that was cut short
    // by the cap is reported, and one that is merely broken is refused.
    struct LoadedSample
    {
        juce::AudioBuffer<float> data;
        double sampleRate = 44100.0;
        juce::String warning;   // set when the file was cut short
    };

    inline bool loadSampleFile (const juce::File& f, LoadedSample& out, juce::String& error)
    {
        out = LoadedSample();
        error.clear();

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr)
        {
            error = "Unsupported audio file: " + f.getFileName();
            return false;
        }

        // SECURITY (S-03): the rate and the length come from the file
        // header, so they are not trusted. The rate is bounded, and the
        // length by the duration (at the file's own rate) and the budget.
        const double srFile = limits::clampFinite (reader->sampleRate, 1000.0, 1.0e9);
        if (srFile > limits::kMaxSampleRate)
        {
            error = "This file is " + juce::String (srFile / 1000.0, 1) + " kHz - the app supports up to "
                    + juce::String (limits::kMaxSampleRate / 1000.0, 0)
                    + " kHz; convert it to 44.1 or 48 kHz";
            return false;
        }
        const auto fileFrames = reader->lengthInSamples;
        const auto maxBySeconds = (juce::int64) (limits::kMaxSampleSeconds * srFile);
        const auto maxFrames64 = juce::jmin (maxBySeconds,
                                             (juce::int64) limits::kMaxFramesPerSample,
                                             fileFrames);
        const int maxFrames = (int) juce::jmax ((juce::int64) 0, maxFrames64);
        if (maxFrames <= 0)
        {
            error = "The audio file is empty";
            return false;
        }

        const int ch = juce::jmax (1, juce::jmin (2, (int) reader->numChannels));
        juce::AudioBuffer<float> tmp (ch, maxFrames);
        tmp.clear();
        // The decoder's return value is checked (it used to be ignored, so a
        // file that could not be decoded was silently turned into silence).
        // JUCE's reader pads a short file with zeros and does not report a
        // frame count, so the length comes from the header.
        const bool readOk = reader->read (&tmp, 0, maxFrames, 0, true, ch > 1);
        reader.reset();
        if (! readOk)
        {
            error = "The audio file could not be decoded";
            return false;
        }
        if (fileFrames > maxFrames64)
        {
            out.warning = "Only the first " + juce::String ((double) maxFrames / srFile, 1)
                          + " s of the file were loaded (the limit)";
        }

        makeSeamlessLoop (tmp, srFile);
        out.data = std::move (tmp);
        out.sampleRate = srFile;
        return true;
    }

    // Live take -> sample material: copy (up to stereo) + seamless loop, the
    // same treatment as file-loaded slots so takes play back identically.
    inline SampleSlot slotFromCapture(int id, const juce::String& name,
                                      const juce::AudioBuffer<float>& take, double rate)
    {
        SampleSlot s;
        s.id = id;
        s.name = name;
        s.sampleRate = rate > 0.0 ? rate : 44100.0;
        const int n = take.getNumSamples();
        const int ch = juce::jmin(2, take.getNumChannels());
        if (n > 0 && ch > 0)
        {
            juce::AudioBuffer<float> buf(ch, n);
            for (int c = 0; c < ch; ++c)
                buf.copyFrom(c, 0, take, c, 0, n);
            makeSeamlessLoop(buf, s.sampleRate);
            s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
        }
        return s;
    }
}
