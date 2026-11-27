#pragma once

//==============================================================================
// R2.9: the sample slot's file format - the edge.
//
// The sample slot is the model's data (Source/Sample.h). How a slot reaches
// a piece file, and how it comes back, is a different thing: this is the
// edge, and the JUCE value types live here.
//
// The bodies are the members the slot used to have, body for body. The
// property names, their order, the keys that come only when they carry
// something, the clamps a crafted file gets, the size checks the payload
// gets and the PCM conversion are the file format itself, and they did not
// change by a character.
#include <JuceHeader.h>

#include "PieceLimits.h"
#include "Sample.h"
#include "UpicConstants.h"

namespace upic
{
    // The slot's audio as the PCM16 bytes the file carries. The recovery
    // snapshot writes the same bytes to a sidecar file, so the unchanged
    // audio is not embedded again and again - and the same conversion is
    // used everywhere, so the recovered audio is bit-identical to the saved
    // one.
    inline juce::MemoryBlock sampleSlotToPcm16 (const SampleSlot& s)
    {
        juce::MemoryBlock mb;
        const int ch = juce::jmin(2, s.data != nullptr ? s.data->getNumChannels() : 0);
        const int n = s.data != nullptr ? s.data->getNumSamples() : 0;
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
                const double v = (double) s.data->getSample(c, i) * (double) kPcmScale;
                *dst++ = (short) juce::jlimit (-32767.0, 32767.0, std::round (v));
            }
        return mb;
    }

    // The slot as a piece file carries it. With includeAudio = false the
    // slot carries no audio - the recovery snapshot keeps the audio in a
    // sidecar file, so the piece JSON stays small and the audio is not
    // re-encoded for every snapshot.
    inline juce::var sampleSlotToVar (const SampleSlot& s, bool includeAudio)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", s.id);
        o->setProperty("name", s.name);
        o->setProperty("sampleRate", s.sampleRate);
        // v1.33.4 (H7): the slot's direction changes the sound, so it
        // travels with the piece (and with the host state)
        if (s.reversed) o->setProperty("reversed", true);
        // M5: the material's Ref (its base pitch), its level, the
        // normalise gain, the trim and the fades. They all change the
        // sound, so they travel with the piece (and with the host
        // state). A Ref of 0 means "not set" - a piece written before
        // M5 has none, and the loader fills it in.
        o->setProperty("ref", s.refFreq);
        o->setProperty("level", s.level);
        o->setProperty("gain", s.gain);
        o->setProperty("loopStart", s.loopStart);
        o->setProperty("loopEnd", s.loopEnd);
        o->setProperty("fadeIn", s.fadeInSec);
        o->setProperty("fadeOut", s.fadeOutSec);
        const int ch = juce::jmin(2, s.data != nullptr ? s.data->getNumChannels() : 0);
        const int n = s.data != nullptr ? s.data->getNumSamples() : 0;
        o->setProperty("channels", ch);
        o->setProperty("frames", n);
        if (includeAudio && n > 0 && ch > 0)
        {
            const auto mb = sampleSlotToPcm16 (s);
            o->setProperty("data", juce::Base64::toBase64(mb.getData(), mb.getSize()));
        }
        return juce::var(o);
    }

    // The slot as a piece file carries it, with its audio.
    inline juce::var sampleSlotToVar (const SampleSlot& s)
    {
        return sampleSlotToVar (s, true);
    }

    // The slot a piece file carries, read back. Every value in the file is
    // clamped - a piece is untrusted input - and the audio is size-checked
    // before it is expanded and before anything is allocated.
    inline SampleSlot sampleSlotFromVar (const juce::var& v, int fallbackId)
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
        // M5: the material's Ref (0 = a piece from before M5 - the
        // loader gives the slot the Ref of the first arc that uses
        // it), its level, the normalise gain, the trim and the fades.
        // Every one of them is clamped: a piece is untrusted input.
        s.refFreq = limits::clampFinite ((double) v.getProperty ("ref", 0.0), 0.0, 20000.0);
        s.level = limits::clampFinite ((float) v.getProperty ("level", 1.0f), 0.0f, 2.0f);
        s.gain = limits::clampFinite ((float) v.getProperty ("gain", 1.0f), 0.01f, 64.0f);
        s.loopStart = juce::jlimit (0, limits::kMaxFramesPerSample,
                                    (int) v.getProperty ("loopStart", 0));
        s.loopEnd = juce::jlimit (0, limits::kMaxFramesPerSample,
                                  (int) v.getProperty ("loopEnd", 0));
        s.fadeInSec = limits::clampFinite ((double) v.getProperty ("fadeIn", 0.0), 0.0, 600.0);
        s.fadeOutSec = limits::clampFinite ((double) v.getProperty ("fadeOut", 0.0), 0.0, 600.0);
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
}
