#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Channel.h"
#include "PieceLimits.h"

//==============================================================================
// R2.8: the channel's file format.
//
// A channel is the model's own type: a named mixer channel with a gain, a
// mute, a solo and a page-long gain curve. What a piece file carries of it
// is a different thing - the edge, where the JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what a channel writes: the names of the keys and the order they are
//   written in, and the keys it leaves out when they are at their default;
// - the default channel: the name, the colour, the gain and the empty
//   curve;
// - the id: the fallback, and the clamps - the channel's id is clamped,
//   unlike the envelope's and the wave's;
// - the name and the colour: the defaults the file uses and the name's
//   length;
// - the gain, the mute and the solo: their defaults and their clamps;
// - the curve: the defaults of a point, the clamps, the cap, the order the
//   points come back in, and the round trip.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it. The name and the colour are read through two more helpers, so
// the suite does not care what the domain holds them in either.

namespace
{
    // The channel's codec, reached through one door.
    juce::var encode (const upic::Channel& c) { return c.toVar(); }
    upic::Channel decode (const juce::var& v, int fallbackId = 0) { return upic::Channel::fromVar (v, fallbackId); }

    // The channel's name as text, and its colour as a colour - whatever the
    // domain holds.
    juce::String textOf (const upic::Channel& c) { return juce::String (c.name); }
    juce::uint32 argbOf (const upic::Channel& c) { return juce::Colour (c.colour).getARGB(); }

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

    // A channel read from a JSON text - the way a crafted file arrives.
    upic::Channel fromJson (const juce::String& json, int fallbackId = 0)
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

void upic::runChannelCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default channel --------------------------------------------------
    {
        const upic::Channel c;
        check (c.id == 0, "a new channel has no id yet");
        check (textOf (c) == "Channel", "a new channel is called Channel");
        check (argbOf (c) == 0xff5ac8fau,
               "a new channel has the blue of the palette");
        check (c.gain == 1.0f, "a new channel is at unity");
        check (! c.muted && ! c.solo, "a new channel is neither muted nor solo");
        check (c.envelope.empty(), "a new channel has no curve");
    }

    // --- what a channel writes --------------------------------------------------
    {
        // only the three keys that are always there
        const auto v = encode (upic::Channel());
        check (names (v).joinIntoString (",") == "id,name,colour",
               "a channel writes the three keys it always has",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("id", -1) == 0, "the id is in the file");
        check (v.getProperty ("name", juce::var()).toString() == "Channel",
               "the name is in the file");
        check ((int) v.getProperty ("colour", 0) == (int) 0xff5ac8fa,
               "the colour in the file is the channel's ARGB");
        check (v.getProperty ("gain", juce::var()).isVoid()
               && v.getProperty ("mute", juce::var()).isVoid()
               && v.getProperty ("solo", juce::var()).isVoid()
               && v.getProperty ("env", juce::var()).isVoid(),
               "the keys that are at their default are left out");
    }
    {
        // everything: the keys and the order they are written in
        upic::Channel c;
        c.gain = 1.5f;
        c.muted = true;
        c.solo = true;
        c.envelope.push_back (upic::EnvPoint { 0.25f, 0.75f });
        const auto v = encode (c);
        check (names (v).joinIntoString (",") == "id,name,colour,gain,mute,solo,env",
               "a channel with everything writes seven keys",
               names (v).joinIntoString (","));
        check (v.getProperty ("gain", juce::var()).isDouble(),
               "the gain is a double in the file");
        check ((bool) v.getProperty ("mute", false)
               && (bool) v.getProperty ("solo", false),
               "the mute and the solo are in the file");
        const auto env = arrayOf (v, "env");
        check (env != nullptr && env->size() == 1,
               "the curve is in the file",
               juce::String (env != nullptr ? env->size() : -1));
        if (env != nullptr && env->size() == 1)
        {
            const auto point = (*env)[0];
            check (names (point).joinIntoString (",") == "x,y",
                   "a point writes its x and its y",
                   names (point).joinIntoString (","));
            check ((float) point.getProperty ("x", -1.0) == 0.25f
                   && (float) point.getProperty ("y", -1.0) == 0.75f,
                   "the point writes its own values");
        }
    }

    // --- the round trip ------------------------------------------------------------
    {
        upic::Channel c;
        c.id = 7;
        c.name = "Strings";
        c.colour = juce::Colour (0xff123456);
        c.gain = 1.5f;
        c.muted = true;
        c.solo = true;
        c.envelope = { { 0.0f, 0.0f }, { 0.25f, 1.0f }, { 1.0f, 0.0f } };
        const auto back = decode (encode (c));
        check (back.id == c.id && textOf (back) == textOf (c)
               && argbOf (back) == argbOf (c),
               "a channel with everything comes back the way it went");
        check (back.gain == c.gain && back.muted == c.muted && back.solo == c.solo,
               "the gain, the mute and the solo come back the way they went",
               juce::String ((double) back.gain));
        check (back.envelope.size() == 3
               && back.envelope[0].x == 0.0f && back.envelope[0].y == 0.0f
               && back.envelope[1].x == 0.25f && back.envelope[1].y == 1.0f
               && back.envelope[2].x == 1.0f && back.envelope[2].y == 0.0f,
               "the curve comes back the way it went",
               juce::String ((int) back.envelope.size()));
    }

    // --- the id -----------------------------------------------------------------------
    {
        check (fromJson (R"({})", 5).id == 5,
               "a channel without an id takes the fallback");
        check (fromJson (R"({})", 0).id == 1,
               "a fallback of zero is pulled up to one");
        check (fromJson (R"({})", -3).id == 1,
               "a negative fallback is pulled up to one");
        check (fromJson (R"({})", 999999999).id == upic::limits::kMaxItemId,
               "a huge fallback is pulled down to the last id");
        check (fromJson (R"({"id":0})", 5).id == 1,
               "an id of zero becomes one - it does not take the fallback");
        check (fromJson (R"({"id":-4})", 5).id == 1,
               "a negative id is pulled up to one");
        check (fromJson (R"({"id":999999999})").id == upic::limits::kMaxItemId,
               "a huge id is pulled down to the last id");
        check (fromJson (R"({"id":2.7})").id == 2,
               "a fractional id is cut to a whole one");
    }

    // --- the name and the colour --------------------------------------------------------
    {
        check (fromJson (R"({})").name == "Channel",
               "a channel without a name is called Channel");
        check (fromJson (R"({"name":""})").name == "Channel",
               "an empty name falls back to Channel");
        check (fromJson (R"({"name":"Bus"})").name == "Bus",
               "a name comes back as it went");
        {
            // the name is cut to 64 characters
            juce::String longName;
            for (int i = 0; i < 70; ++i) longName += 'x';
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", longName);
            const auto c = decode (juce::var (o));
            check (c.name.length() == 64,
                   "a name longer than 64 characters is cut",
                   juce::String (c.name.length()));
            check (c.name == longName.substring (0, 64),
                   "the cut keeps the first 64 characters");
        }
        check (argbOf (fromJson (R"({})")) == 0xff5ac8fau,
               "a channel without a colour takes the blue");
        check (argbOf (fromJson (R"({"colour":-1})")) == 0xffffffffu,
               "a negative colour is read as the colour it stands for");
    }

    // --- the gain, the mute and the solo --------------------------------------------------
    {
        check (fromJson (R"({})").gain == 1.0f,
               "a channel without a gain is at unity");
        check (fromJson (R"({"gain":1.5})").gain == 1.5f,
               "a gain comes back as it went");
        check (fromJson (R"({"gain":-1})").gain == 0.0f,
               "a gain below zero is pulled up to zero");
        check (fromJson (R"({"gain":5})").gain == 2.0f,
               "a gain above two is pulled down to two");
        {
            // a value that is not a number falls to the lower bound
            const auto c = decode (objectWith ("gain",
                                                juce::var (std::numeric_limits<double>::quiet_NaN())));
            check (c.gain == 0.0f,
                   "a gain that is not a number falls to zero",
                   juce::String ((double) c.gain));
        }
        check (! fromJson (R"({})").muted && ! fromJson (R"({})").solo,
               "a channel without a mute or a solo has neither");
        check (fromJson (R"({"mute":true})").muted,
               "a mute comes back as it went");
        check (fromJson (R"({"solo":true})").solo,
               "a solo comes back as it went");
        check (! fromJson (R"({"mute":false})").muted,
               "a mute of false stays false");
        check (fromJson (R"({"mute":1})").muted,
               "a mute of one is a mute");
        check (! fromJson (R"({"solo":0})").solo,
               "a solo of zero is not a solo");
    }

    // --- the curve -----------------------------------------------------------------------
    {
        check (fromJson (R"({})").envelope.empty(),
               "a channel without a curve has none");
    }
    {
        // a point without its x or its y takes the defaults: 0 and 1
        const auto c = fromJson (R"({"env":[{}]})");
        check (c.envelope.size() == 1
               && c.envelope[0].x == 0.0f && c.envelope[0].y == 1.0f,
               "a point without a position takes the start of the flat curve",
               juce::String ((int) c.envelope.size()));
    }
    {
        // the values are clamped to the curve's own range
        const auto c = fromJson (R"({"env":[{"x":-1,"y":2}]})");
        check (c.envelope.size() == 1
               && c.envelope[0].x == 0.0f && c.envelope[0].y == 1.0f,
               "a point outside the curve is pulled back into it");
    }
    {
        // a value that is not a number falls to the lower bound
        auto* p = new juce::DynamicObject();
        p->setProperty ("x", juce::var (std::numeric_limits<double>::quiet_NaN()));
        p->setProperty ("y", juce::var (std::numeric_limits<double>::quiet_NaN()));
        juce::Array<juce::var> points;
        points.add (juce::var (p));
        auto* o = new juce::DynamicObject();
        o->setProperty ("env", points);
        const auto c = decode (juce::var (o));
        check (c.envelope.size() == 1
               && c.envelope[0].x == 0.0f && c.envelope[0].y == 0.0f,
               "a point that is not a number falls to 0",
               juce::String ((int) c.envelope.size()));
    }
    {
        // the points come back in order, whatever the file said
        const auto c = fromJson (R"({"env":[{"x":0.8,"y":0.8},{"x":0.2,"y":0.2},{"x":0.5,"y":0.5}]})");
        check (c.envelope.size() == 3
               && c.envelope[0].x == 0.2f && c.envelope[1].x == 0.5f
               && c.envelope[2].x == 0.8f,
               "the points come back in order",
               juce::String ((int) c.envelope.size()));
    }
    {
        // the cap: the file's own order decides what is dropped, and the sort
        // comes after - so the file's first point survives even when the
        // file's points run backwards
        juce::Array<juce::var> points;
        const int n = upic::limits::kMaxPointsPerEnvelope + 5;
        for (int i = 0; i < n; ++i)
        {
            auto* p = new juce::DynamicObject();
            p->setProperty ("x", 1.0 - (double) i / (double) n);
            p->setProperty ("y", 1.0);
            points.add (juce::var (p));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("env", points);
        const auto c = decode (juce::var (o));
        check ((int) c.envelope.size() == upic::limits::kMaxPointsPerEnvelope,
               "the curve is capped",
               juce::String ((int) c.envelope.size()));
        check (c.envelope.back().x == 1.0f,
               "the cap drops the last points of the file, before the sort",
               juce::String ((double) c.envelope.back().x));
        check (c.envelope.front().x > 0.0f,
               "the points the file had last are the ones that are gone",
               juce::String ((double) c.envelope.front().x));
    }
}
