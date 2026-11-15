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

        bool loaded() const
        {
            return data != nullptr && data->getNumSamples() > 0 && data->getNumChannels() > 0;
        }
        double durationSec() const
        {
            return (data != nullptr && sampleRate > 0)
                       ? (double) data->getNumSamples() / sampleRate : 0.0;
        }

        // v1.33.4 (L15, part 3): the slot's audio as the PCM16 bytes the
        // piece's base64 carries. The recovery snapshot writes these bytes
        // to a sidecar file, so the unchanged audio is not embedded again
        // and again - and the same conversion is used everywhere, so the
        // recovered audio is bit-identical to the saved one.
        juce::MemoryBlock toPcm16() const
        {
            juce::MemoryBlock mb;
            const int ch = juce::jmin(2, data != nullptr ? data->getNumChannels() : 0);
            const int n = data != nullptr ? data->getNumSamples() : 0;
            if (n <= 0 || ch <= 0)
                return mb;
            mb.setSize ((size_t) n * (size_t) ch * sizeof (short));
            auto* dst = static_cast<short*>(mb.getData());
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c)
                {
                    // v1.33.4 (M26): the value is ROUNDED to the nearest
                    // step (the old code truncated, so every save/load
                    // cycle lost a step of level) and scaled with the same
                    // number the decoder divides by.
                    const double v = (double) data->getSample(c, i) * (double) kPcmScale;
                    *dst++ = (short) juce::jlimit (-32767.0, 32767.0, std::round (v));
                }
            return mb;
        }

        juce::var toVar() const
        {
            return toVar (true);
        }

        // v1.33.4 (L15, part 3): with includeAudio = false the slot carries
        // no audio - the recovery snapshot keeps the audio in a sidecar
        // file, so the piece JSON stays small and the audio is not
        // re-encoded for every snapshot.
        juce::var toVar (bool includeAudio) const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty("id", id);
            o->setProperty("name", name);
            o->setProperty("sampleRate", sampleRate);
            // v1.33.4 (H7): the slot's direction changes the sound, so it
            // travels with the piece (and with the host state)
            if (reversed) o->setProperty("reversed", true);
            const int ch = juce::jmin(2, data != nullptr ? data->getNumChannels() : 0);
            const int n = data != nullptr ? data->getNumSamples() : 0;
            o->setProperty("channels", ch);
            o->setProperty("frames", n);
            if (includeAudio && n > 0 && ch > 0)
            {
                const auto mb = toPcm16();
                o->setProperty("data", juce::Base64::toBase64(mb.getData(), mb.getSize()));
            }
            return juce::var(o);
        }
        static SampleSlot fromVar(const juce::var& v, int fallbackId)
        {
            SampleSlot s;
            s.id = juce::jlimit(1, kMaxSampleSlots, (int) v.getProperty("id", fallbackId));
            s.name = v.getProperty("name", "").toString();
            // v1.33.4 (M19): the stored rate is kept as it is (bounded to the
            // range the app supports). The old code clamped it to 96 kHz
            // while keeping the frames, so a 192 kHz take played at half
            // speed after a save and a load.
            s.sampleRate = limits::clampFinite((double) v.getProperty("sampleRate", 44100.0),
                                               1000.0, limits::kMaxSampleRate);
            // v1.33.4 (H7): the slot's direction (absent in older pieces ->
            // forwards)
            s.reversed = (bool) v.getProperty ("reversed", false);
            const int ch = juce::jlimit(1, 2, (int) v.getProperty("channels", 1));
            // The declared frame count is clamped before anything is
            // allocated: a crafted "frames" value must not be able to ask
            // for a gigabyte of buffer. The cap is the duration - 60 s at
            // the material's OWN rate - and the memory budget (M19: the old
            // cap was 60 s at 96 kHz whatever the material's rate was).
            const int maxBySeconds = (int) juce::jmin ((juce::int64) limits::kMaxFramesPerSample,
                                                       (juce::int64) (limits::kMaxSampleSeconds * s.sampleRate));
            const int n = juce::jlimit(0, maxBySeconds, (int) v.getProperty("frames", 0));
            auto b64 = v.getProperty("data", "").toString();
            // ...and the base64 string is checked against the size it claims
            // to carry BEFORE it is decoded, so an oversized payload is
            // rejected without ever being expanded.
            const size_t expectedBytes = (size_t) n * (size_t) ch * sizeof(short);
            if (n > 0 && ch > 0 && b64.isNotEmpty()
                && b64.getNumBytesAsUTF8() <= expectedBytes * limits::kBase64Overhead)
            {
                juce::MemoryOutputStream mo;
                if (juce::Base64::convertFromBase64(mo, b64)
                    && mo.getDataSize() >= expectedBytes)
                {
                    juce::AudioBuffer<float> buf(ch, n);
                    const auto* src = static_cast<const short*>(mo.getData());
                    for (int i = 0; i < n; ++i)
                        for (int c = 0; c < ch; ++c)
                            // v1.33.4 (M26): the same scale the encoder used,
                            // so a save/load cycle does not change the level.
                            buf.setSample(c, i, (float) (*src++) / kPcmScale);
                    s.data = std::make_shared<const juce::AudioBuffer<float>>(std::move(buf));
                }
            }
            return s;
        }
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
