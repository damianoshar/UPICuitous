#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "Sample.h"
#include "SampleCodec.h"
#include "PieceLimits.h"
#include "UpicConstants.h"

//==============================================================================
// R2.9: the sample slot's file format.
//
// A sample slot is the model's own type: one loadable piece of material, with
// its rate, its base pitch, its level, its trim and its fades. What a piece
// file carries of it is a different thing - the edge, where the JUCE value
// types live.
//
// This suite is that edge's contract. It pins
//
// - what a slot writes: the names of the keys and the order they are written
//   in, the keys that come only when they carry something, and the audio
//   itself;
// - the default slot: the name, the rate, the base pitch and the trims;
// - the round trip: a slot with everything comes back the way it went, with
//   its audio;
// - the id: the fallback, and the clamps;
// - the name and the rate: the defaults the file uses and the clamps;
// - the base pitch, the level and the gain: the defaults the file uses
//   (which are not the defaults the data uses) and the clamps;
// - the trim and the fades: the defaults and the clamps;
// - the audio: the frames and the channels, the payload that is too short,
//   the payload that is too long, and the frame count over the duration
//   cap.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it. The name is read through one more helper, so the suite does not
// care what the domain holds it in either.

namespace
{
    // The slot's codec, reached through one door.
    juce::var encode (const upic::SampleSlot& s) { return upic::sampleSlotToVar (s); }
    upic::SampleSlot decode (const juce::var& v, int fallbackId = 0) { return upic::sampleSlotFromVar (v, fallbackId); }

    // The slot's name as text - whatever the domain holds.
    juce::String textOf (const upic::SampleSlot& s) { return juce::String (s.name); }

    // The property names of an object, in the order they were written.
    juce::StringArray names (const juce::var& v)
    {
        juce::StringArray out;
        if (auto* o = v.getDynamicObject())
        {
            const auto& props = o->getProperties();
            for (const auto* nv = props.begin(); nv != props.end(); ++nv)
                out.add (nv->name.toString());
        }
        return out;
    }

    // A slot read from a JSON text - the way a crafted file arrives.
    upic::SampleSlot fromJson (const juce::String& json, int fallbackId = 0)
    {
        return decode (juce::JSON::parse (json), fallbackId);
    }

    // An object with one property - for the values JSON cannot carry (a NaN).
    juce::var objectWith (const char* key, const juce::var& value)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty (key, value);
        return juce::var (o);
    }

    // A slot with a little audio in it.
    upic::SampleSlot withAudio (int ch = 1, int n = 8)
    {
        upic::SampleSlot s;
        s.id = 3;
        s.name = "Take";
        s.sampleRate = 48000.0;
        juce::AudioBuffer<float> buf (ch, n);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < n; ++i)
                buf.setSample (c, i, (float) i / (float) n - 0.5f);
        s.data = std::make_shared<const juce::AudioBuffer<float>> (std::move (buf));
        return s;
    }
}

void upic::runSampleCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default slot -------------------------------------------------------
    {
        const upic::SampleSlot s;
        check (s.id == 1, "a new slot has the id one");
        check (textOf (s).isEmpty(), "a new slot has no name");
        check (s.sampleRate == 44100.0, "a new slot is at 44.1 kHz");
        check (! s.reversed, "a new slot plays forwards");
        check (s.refFreq == upic::kDefaultRefFreq,
               "a new slot has the default base pitch");
        check (s.level == 1.0f && s.gain == 1.0f,
               "a new slot is at unity");
        check (s.loopStart == 0 && s.loopEnd == 0,
               "a new slot has no trim");
        check (s.fadeInSec == 0.0 && s.fadeOutSec == 0.0,
               "a new slot has no fades");
        check (! s.loaded(), "a new slot holds no audio");
    }

    // --- what a slot writes --------------------------------------------------------
    {
        // only the keys that are always there
        const auto v = encode (upic::SampleSlot());
        check (names (v).joinIntoString (",")
                   == "id,name,sampleRate,ref,level,gain,loopStart,loopEnd,fadeIn,fadeOut,channels,frames",
               "a slot writes the twelve keys it always has",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("id", -1) == 1, "the id is in the file");
        check (v.getProperty ("name", juce::var()).toString().isEmpty(),
               "the name is in the file");
        check ((double) v.getProperty ("sampleRate", 0.0) == 44100.0,
               "the rate is in the file");
        check (v.getProperty ("reversed", juce::var()).isVoid()
               && v.getProperty ("data", juce::var()).isVoid(),
               "the keys that carry nothing are left out");
        check ((int) v.getProperty ("channels", -1) == 0
               && (int) v.getProperty ("frames", -1) == 0,
               "an empty slot still says how much audio it has");
    }
    {
        // the direction is written only when the slot is reversed
        upic::SampleSlot s;
        s.reversed = true;
        const auto v = encode (s);
        check (names (v).joinIntoString (",")
                   == "id,name,sampleRate,reversed,ref,level,gain,loopStart,loopEnd,fadeIn,fadeOut,channels,frames",
               "a reversed slot says so",
               names (v).joinIntoString (","));
        check ((bool) v.getProperty ("reversed", false),
               "the direction in the file is the slot's");
    }
    {
        // the audio is written when there is some
        const auto v = encode (withAudio());
        check ((int) v.getProperty ("channels", -1) == 1
               && (int) v.getProperty ("frames", -1) == 8,
               "the slot says how much audio it has",
               juce::String ((int) v.getProperty ("frames", -1)));
        check (v.getProperty ("data", juce::var()).toString().isNotEmpty(),
               "the audio is in the file");
    }
    {
        // ... and left out when the file is asked to carry no audio (the
        // recovery snapshot keeps it in a sidecar instead)
        const auto v = upic::sampleSlotToVar (withAudio(), false);
        check (v.getProperty ("data", juce::var()).isVoid(),
               "the audio can be left out of the file");
        check ((int) v.getProperty ("frames", -1) == 8,
               "the frame count is there even when the audio is not");
    }

    // --- the round trip ----------------------------------------------------------------
    {
        upic::SampleSlot s;
        s.id = 5;
        s.name = "Strings";
        s.sampleRate = 96000.0;
        s.reversed = true;
        s.refFreq = 220.0;
        s.level = 1.5f;
        s.gain = 2.0f;
        s.loopStart = 100;
        s.loopEnd = 2000;
        s.fadeInSec = 0.25;
        s.fadeOutSec = 0.5;
        const auto back = decode (encode (s));
        check (back.id == s.id && textOf (back) == textOf (s),
               "a slot with everything comes back the way it went");
        check (back.sampleRate == s.sampleRate && back.reversed == s.reversed
               && back.refFreq == s.refFreq,
               "the rate, the direction and the base pitch come back the way they went",
               juce::String ((double) back.sampleRate));
        check (back.level == s.level && back.gain == s.gain,
               "the level and the gain come back the way they went",
               juce::String ((double) back.gain));
        check (back.loopStart == s.loopStart && back.loopEnd == s.loopEnd
               && back.fadeInSec == s.fadeInSec && back.fadeOutSec == s.fadeOutSec,
               "the trim and the fades come back the way they went");
    }
    {
        // the audio comes back with it, to the nearest step
        const auto s = withAudio (2, 8);
        const auto back = decode (encode (s));
        check (back.loaded() && back.data != nullptr
               && back.data->getNumSamples() == 8
               && back.data->getNumChannels() == 2,
               "the audio comes back with the slot",
               juce::String (back.totalFrames()));
        float maxDiff = 0.0f;
        if (back.loaded())
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 8; ++i)
                    maxDiff = juce::jmax (maxDiff, std::abs (
                        back.data->getSample (c, i) - s.data->getSample (c, i)));
        check (maxDiff < 1.0e-4f,
               "the audio comes back to the nearest step",
               juce::String (maxDiff, 6));
    }

    // --- the id -----------------------------------------------------------------------
    {
        check (fromJson (R"({})", 5).id == 5,
               "a slot without an id takes the fallback");
        check (fromJson (R"({})", 0).id == 1,
               "a fallback of zero is pulled up to one");
        check (fromJson (R"({})", 99).id == upic::kMaxSampleSlots,
               "a fallback over the bank size is pulled down to it",
               juce::String (fromJson (R"({})", 99).id));
        check (fromJson (R"({"id":0})", 5).id == 1,
               "an id of zero becomes one - it does not take the fallback");
        check (fromJson (R"({"id":-4})").id == 1,
               "a negative id is pulled up to one");
        check (fromJson (R"({"id":99})").id == upic::kMaxSampleSlots,
               "an id over the bank size is pulled down to it");
        check (fromJson (R"({"id":2.7})").id == 2,
               "a fractional id is cut to a whole one");
    }

    // --- the name and the rate ------------------------------------------------------
    {
        check (fromJson (R"({})").name.isEmpty(),
               "a slot without a name has none");
        check (fromJson (R"({"name":""})").name.isEmpty(),
               "an empty name stays empty");
        check (fromJson (R"({"name":"Take"})").name == "Take",
               "a name comes back as it went");
        {
            // the slot's name is not cut - the channel's is
            juce::String longName;
            for (int i = 0; i < 100; ++i) longName += 'x';
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", longName);
            check (decode (juce::var (o)).name.length() == 100,
                   "a long name is not cut",
                   juce::String (decode (juce::var (o)).name.length()));
        }
        check (fromJson (R"({})").sampleRate == 44100.0,
               "a slot without a rate is at 44.1 kHz");
        check (fromJson (R"({"sampleRate":48000})").sampleRate == 48000.0,
               "a rate comes back as it went");
        check (fromJson (R"({"sampleRate":100})").sampleRate == 1000.0,
               "a rate below 1 kHz is pulled up to 1 kHz");
        check (fromJson (R"({"sampleRate":1000000000})").sampleRate
                   == upic::limits::kMaxSampleRate,
               "a rate over the ceiling is pulled down to it",
               juce::String ((double) fromJson (R"({"sampleRate":1000000000})").sampleRate));
        check (decode (objectWith ("sampleRate",
                                    juce::var (std::numeric_limits<double>::quiet_NaN()))).sampleRate
                   == 1000.0,
               "a rate that is not a number falls to 1 kHz");
    }

    // --- the direction -----------------------------------------------------------------
    {
        check (! fromJson (R"({})").reversed,
               "a slot without a direction plays forwards");
        check (fromJson (R"({"reversed":true})").reversed,
               "a reversed slot comes back reversed");
        check (! fromJson (R"({"reversed":false})").reversed,
               "a slot that says forwards plays forwards");
    }

    // --- the base pitch, the level and the gain -----------------------------------------
    {
        check (fromJson (R"({})").refFreq == 0.0,
               "a slot without a base pitch has none - not the default one");
        check (fromJson (R"({"ref":220})").refFreq == 220.0,
               "a base pitch comes back as it went");
        check (fromJson (R"({"ref":-1})").refFreq == 0.0,
               "a base pitch below zero is pulled up to zero");
        check (fromJson (R"({"ref":30000})").refFreq == 20000.0,
               "a base pitch over 20 kHz is pulled down to it");
        check (decode (objectWith ("ref",
                                    juce::var (std::numeric_limits<double>::quiet_NaN()))).refFreq
                   == 0.0,
               "a base pitch that is not a number falls to zero");

        check (fromJson (R"({})").level == 1.0f,
               "a slot without a level is at unity");
        check (fromJson (R"({"level":-1})").level == 0.0f,
               "a level below zero is pulled up to zero");
        check (fromJson (R"({"level":5})").level == 2.0f,
               "a level over two is pulled down to two");

        check (fromJson (R"({})").gain == 1.0f,
               "a slot without a gain is at unity");
        check (fromJson (R"({"gain":0})").gain == 0.01f,
               "a gain of zero is pulled up to the floor",
               juce::String ((double) fromJson (R"({"gain":0})").gain));
        check (fromJson (R"({"gain":1000})").gain == 64.0f,
               "a gain over 64 is pulled down to it");
        check (decode (objectWith ("gain",
                                    juce::var (std::numeric_limits<double>::quiet_NaN()))).gain
                   == 0.01f,
               "a gain that is not a number falls to the floor");
    }

    // --- the trim and the fades ------------------------------------------------------------
    {
        check (fromJson (R"({})").loopStart == 0 && fromJson (R"({})").loopEnd == 0,
               "a slot without a trim has none");
        check (fromJson (R"({"loopStart":-5})").loopStart == 0,
               "a trim below zero is pulled up to zero");
        check (fromJson (R"({"loopEnd":999999999})").loopEnd
                   == upic::limits::kMaxFramesPerSample,
               "a trim over the frame budget is pulled down to it");

        check (fromJson (R"({})").fadeInSec == 0.0 && fromJson (R"({})").fadeOutSec == 0.0,
               "a slot without fades has none");
        check (fromJson (R"({"fadeIn":-1})").fadeInSec == 0.0,
               "a fade below zero is pulled up to zero");
        check (fromJson (R"({"fadeOut":1000})").fadeOutSec == 600.0,
               "a fade over ten minutes is pulled down to it");
        check (decode (objectWith ("fadeIn",
                                    juce::var (std::numeric_limits<double>::quiet_NaN()))).fadeInSec
                   == 0.0,
               "a fade that is not a number falls to zero");
    }

    // --- the frames and the channels ----------------------------------------------------------
    {
        check (fromJson (R"({})").data == nullptr,
               "a slot without audio has none");
        check (fromJson (R"({"channels":0})").data == nullptr,
               "a slot without audio has none, whatever the channels say");
        check (fromJson (R"({"frames":-1})").data == nullptr,
               "a slot without audio has none, whatever the frames say");
    }

    // --- the audio -----------------------------------------------------------------------------
    {
        // a payload that is shorter than the frames it claims is refused
        juce::MemoryBlock mb;
        mb.setSize (4);
        auto* dst = static_cast<short*>(mb.getData());
        dst[0] = 1000;
        dst[1] = 2000;
        auto* o = new juce::DynamicObject();
        o->setProperty ("frames", 4);
        o->setProperty ("channels", 1);
        o->setProperty ("data", juce::Base64::toBase64 (mb.getData(), mb.getSize()));
        const auto s = decode (juce::var (o));
        check (! s.loaded(),
               "a payload shorter than the frames it claims is refused");
    }
    {
        // a payload that is longer than the size it claims allows is refused
        // before it is ever expanded
        juce::String big;
        for (int i = 0; i < 100; ++i) big += "A";
        auto* o = new juce::DynamicObject();
        o->setProperty ("frames", 1);
        o->setProperty ("channels", 1);
        o->setProperty ("data", big);
        const auto s = decode (juce::var (o));
        check (! s.loaded(),
               "a payload longer than the size it claims allows is refused");
    }
    {
        // the frame count is clamped to the duration - 60 s at the material's
        // OWN rate - BEFORE anything is allocated. The payload below is
        // exactly the size the clamped count allows, so it is taken and the
        // slot holds the capped number of frames.
        const int capped = 60000;   // 60 s at 1 kHz
        juce::MemoryBlock mb;
        mb.setSize ((size_t) capped * sizeof (short));
        auto* dst = static_cast<short*>(mb.getData());
        for (int i = 0; i < capped; ++i) dst[i] = (short) (i % 1000);
        auto* o = new juce::DynamicObject();
        o->setProperty ("sampleRate", 1000);
        o->setProperty ("frames", capped + 1);
        o->setProperty ("channels", 1);
        o->setProperty ("data", juce::Base64::toBase64 (mb.getData(), mb.getSize()));
        const auto s = decode (juce::var (o));
        check (s.loaded() && s.totalFrames() == capped,
               "a frame count over the duration cap is cut, not refused",
               juce::String (s.totalFrames()));
    }
}
