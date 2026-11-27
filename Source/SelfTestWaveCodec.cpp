#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Waveform.h"
#include "WaveformCodec.h"
#include "UpicConstants.h"

//==============================================================================
// R2.7: the wave's file format.
//
// A wave is the model's own type: a named table of samples the oscillator
// reads. What a piece file carries of it is a different thing - the edge,
// where the JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what a wave writes: the names of the keys and the order they are written
//   in, and what a sample is in the file;
// - the default wave: the name, the colour and the table of zeros;
// - the id: the fallback, and the fact that it is not clamped;
// - the name and the colour: the defaults the file uses, which are not the
//   defaults the data uses;
// - the samples: the length the wave keeps, the cap, the empty table, the
//   values that are not clamped, and the round trip.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it. The name and the colour are read through two more helpers, so
// the suite does not care what the domain holds them in either.

namespace
{
    // The wave's codec, reached through one door.
    juce::var encode (const upic::Waveform& w) { return upic::waveformToVar (w); }
    upic::Waveform decode (const juce::var& v, int fallbackId = 0) { return upic::waveformFromVar (v, fallbackId); }

    // The wave's name as text, and its colour as a colour - whatever the
    // domain holds.
    juce::String textOf (const upic::Waveform& w) { return juce::String (w.name); }
    juce::uint32 argbOf (const upic::Waveform& w) { return juce::Colour (w.colour).getARGB(); }

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

    // The array a property holds. The property is held in a named variable
    // first: the array of a temporary dangles.
    juce::Array<juce::var>* arrayOf (const juce::var& v, const char* key)
    {
        const auto prop = v.getProperty (key, juce::var());
        return prop.getArray();
    }

    // A wave read from a JSON text - the way a crafted file arrives.
    upic::Waveform fromJson (const juce::String& json, int fallbackId = 0)
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
}

void upic::runWaveCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default wave ----------------------------------------------------
    {
        const upic::Waveform w;
        check (w.id == 0, "a new wave has no id yet");
        check (textOf (w) == "Sine", "a new wave is called Sine");
        check (argbOf (w) == 0xff00ffffu,
               "a new wave has the cyan of the palette");
        check (w.samples.size() == (size_t) upic::kWaveSize,
               "a new wave has the default table",
               juce::String ((int) w.samples.size()));
        check (! w.samples.empty() && w.samples.front() == 0.0f
               && w.samples.back() == 0.0f,
               "a new wave is a table of zeros");
    }

    // --- what a wave writes ---------------------------------------------------
    {
        const auto v = encode (upic::Waveform());
        check (names (v).joinIntoString (",") == "id,name,colour,samples",
               "a wave writes four keys",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("id", -1) == 0, "the id is in the file");
        check (v.getProperty ("name", juce::var()).toString() == "Sine",
               "the name is in the file");
        check ((int) v.getProperty ("colour", 0) == (int) 0xff00ffff,
               "the colour in the file is the wave's ARGB");
        const auto samples = arrayOf (v, "samples");
        check (samples != nullptr && samples->size() == upic::kWaveSize,
               "the table is in the file, at its own length",
               juce::String (samples != nullptr ? samples->size() : -1));
        if (samples != nullptr && ! samples->isEmpty())
            check ((*samples)[0].isDouble(),
                   "a sample is a double in the file");
    }

    // --- the round trip --------------------------------------------------------
    {
        upic::Waveform w;
        w.id = 7;
        w.name = "Ramp";
        w.colour = juce::Colour (0xff123456);
        w.samples.assign (4, 0.0f);
        for (int i = 0; i < 4; ++i) w.samples[(size_t) i] = (float) i / 4.0f;
        const auto back = decode (encode (w));
        check (back.id == w.id && textOf (back) == textOf (w)
               && argbOf (back) == argbOf (w),
               "a wave with everything comes back the way it went");
        check (back.samples.size() == 4,
               "the table comes back at its own length",
               juce::String ((int) back.samples.size()));
        bool same = back.samples.size() == w.samples.size();
        for (size_t i = 0; same && i < w.samples.size(); ++i)
            same = back.samples[i] == w.samples[i];
        check (same, "the samples come back the way they went");
    }

    // --- the id -------------------------------------------------------------------
    {
        check (fromJson (R"({"samples":[]})", 5).id == 5,
               "a wave without an id takes the fallback");
        check (fromJson (R"({"id":-4})").id == -4,
               "a negative id is not clamped");
        check (fromJson (R"({"id":999999999})").id == 999999999,
               "a huge id is not clamped");
        check (fromJson (R"({"id":2.7})").id == 2,
               "a fractional id is cut to a whole one");
    }

    // --- the name and the colour --------------------------------------------------
    {
        check (fromJson (R"({})").name == "Wave",
               "a wave without a name is called Wave");
        check (fromJson (R"({"name":""})").name == "",
               "an empty name stays empty");
        check (argbOf (fromJson (R"({})")) == 0xff5ac8fau,
               "a wave without a colour takes the file's blue");
        check (argbOf (fromJson (R"({"colour":-1})")) == 0xffffffffu,
               "a negative colour is read as the colour it stands for");
    }

    // --- the samples ------------------------------------------------------------------
    {
        // the length is the wave's own: the piece carries the samples, so the
        // table keeps the length it was written with
        const auto w = fromJson (R"({"samples":[0.25,-0.5,0.75]})");
        check (w.samples.size() == 3,
               "the table keeps the length the file gave it",
               juce::String ((int) w.samples.size()));
        check (w.samples.size() == 3
               && w.samples[0] == 0.25f && w.samples[1] == -0.5f
               && w.samples[2] == 0.75f,
               "the samples come back in order");
    }
    {
        // an empty table is not a table: the wave keeps the default one
        const auto w = fromJson (R"({"samples":[]})");
        check (w.samples.size() == (size_t) upic::kWaveSize,
               "an empty table leaves the default one",
               juce::String ((int) w.samples.size()));
    }
    {
        // no table at all is the same
        const auto w = fromJson (R"({})");
        check (w.samples.size() == (size_t) upic::kWaveSize,
               "a wave without a table has the default one",
               juce::String ((int) w.samples.size()));
    }
    {
        // the cap
        juce::Array<juce::var> samples;
        for (int i = 0; i < upic::kMaxWaveSize + 5; ++i)
            samples.add ((double) i / (double) (upic::kMaxWaveSize + 5));
        auto* o = new juce::DynamicObject();
        o->setProperty ("samples", samples);
        const auto w = decode (juce::var (o));
        check ((int) w.samples.size() == upic::kMaxWaveSize,
               "the table is capped",
               juce::String ((int) w.samples.size()));
    }
    {
        // the samples are not clamped: the file's number is the wave's number
        const auto w = fromJson (R"({"samples":[2.0,-3.0]})");
        check (w.samples.size() == 2
               && w.samples[0] == 2.0f && w.samples[1] == -3.0f,
               "a sample outside -1..1 is not pulled back into it");
    }
    {
        // and a sample that is not a number is not caught either
        juce::Array<juce::var> samples;
        samples.add (juce::var (std::numeric_limits<double>::quiet_NaN()));
        auto* o = new juce::DynamicObject();
        o->setProperty ("samples", samples);
        const auto w = decode (juce::var (o));
        check (w.samples.size() == 1 && std::isnan (w.samples[0]),
               "a sample that is not a number survives the read");
    }
}
