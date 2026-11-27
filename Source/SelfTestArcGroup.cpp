#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "ArcGroup.h"
#include "PieceLimits.h"

//==============================================================================
// R2.5: the arc group's data and its file format.
//
// A group is a named set of arcs on the page with shared, non-destructive
// attributes: mute, solo, gain, transpose and the timbre/channel overrides.
// The group's own data is the model; the file format of a group is a
// different thing - the edge where the JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what a group writes: the names of the keys and the order they are
//   written in;
// - what a group leaves out: every key that is only written when the group
//   is not the default;
// - the id: the fallback, the clamp, and the numbers that are not ids;
// - the name: the default, the empty name and the 64-character cut;
// - the colour: the default and the one the file carries;
// - the membership: the ids that are dropped, the duplicates and the cap;
// - the clamps: the gain, the transpose and the four bank ids;
// - the round trip: a group with everything comes back the way it went.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it. The name and the colour are read through two more helpers, so
// the suite does not care whether the domain holds a JUCE string and colour
// or standard ones either.

namespace
{
    // The group's codec, reached through one door.
    juce::var encode (const upic::ArcGroup& g) { return g.toVar(); }
    upic::ArcGroup decode (const juce::var& v, int fallbackId = 1) { return upic::ArcGroup::fromVar (v, fallbackId); }

    // The group's name as text, and its colour as a colour - whatever the
    // domain holds.
    juce::String textOf (const upic::ArcGroup& g) { return juce::String (g.name); }
    juce::uint32 argbOf (const upic::ArcGroup& g) { return juce::Colour (g.colour).getARGB(); }

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

    bool has (const juce::var& v, const char* key) { return v.hasProperty (key); }

    // The array a property holds. The property is held in a named variable
    // first: the array of a temporary dangles.
    juce::Array<juce::var>* arrayOf (const juce::var& v, const char* key)
    {
        const auto prop = v.getProperty (key, juce::var());
        return prop.getArray();
    }

    // A group read from a JSON text - the way a crafted file arrives.
    upic::ArcGroup fromJson (const juce::String& json, int fallbackId = 1)
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

void upic::runArcGroupCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default group -------------------------------------------------
    {
        const upic::ArcGroup g;
        check (g.id == 0, "a new group has no id yet");
        check (textOf (g) == "Group", "a new group is called Group");
        check (argbOf (g) == 0xff5ac8fa, "a new group has the group's colour");
        check (g.arcIds.empty(), "a new group owns no arcs");
        check (! g.muted && ! g.solo, "a new group is neither muted nor solo");
        check (g.gain == 1.0f, "a new group does not change the gain");
        check (g.transposeSemitones == 0.0, "a new group does not transpose");
        check (g.waveformId == -1 && g.envelopeId == -1
               && g.amplitudeTableId == -1 && g.channelId == -1,
               "a new group overrides nothing");
    }

    // --- what a group writes ------------------------------------------------
    {
        const auto v = encode (upic::ArcGroup());
        check (names (v).joinIntoString (",") == "id,name,colour,arcs",
               "a default group writes four keys",
               names (v).joinIntoString (","));
        check (! has (v, "mute") && ! has (v, "solo") && ! has (v, "gain")
               && ! has (v, "transpose") && ! has (v, "wave") && ! has (v, "env")
               && ! has (v, "ampTable") && ! has (v, "channel"),
               "a default group leaves out every optional key");
        const auto arcs = arrayOf (v, "arcs");
        check (arcs != nullptr && arcs->isEmpty(), "a default group writes an empty arc list");
    }
    {
        upic::ArcGroup g;
        g.id = 3;
        g.name = "Drums";
        g.arcIds = { 1, 2 };
        g.muted = true;
        g.solo = true;
        g.gain = 0.5f;
        g.transposeSemitones = -12.0;
        g.waveformId = 4;
        g.envelopeId = 5;
        g.amplitudeTableId = 6;
        g.channelId = 7;
        const auto v = encode (g);
        check (names (v).joinIntoString (",")
                 == "id,name,colour,arcs,mute,solo,gain,transpose,wave,env,ampTable,channel",
               "a group with everything writes every key in order",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("id", 0) == 3, "the id is in the file");
        check (v.getProperty ("name", juce::var()).toString() == "Drums", "the name is in the file");
        check ((int) v.getProperty ("colour", 0) == (int) argbOf (g),
               "the colour in the file is the group's ARGB");
        const auto arcs = arrayOf (v, "arcs");
        check (arcs != nullptr && arcs->size() == 2
               && (int) (*arcs)[0] == 1 && (int) (*arcs)[1] == 2,
               "the arcs are in the file, in order");
    }

    // --- the round trip ------------------------------------------------------
    {
        upic::ArcGroup g;
        g.id = 3;
        g.name = "Drums";
        g.arcIds = { 1, 2 };
        g.muted = true;
        g.solo = true;
        g.gain = 0.5f;
        g.transposeSemitones = -12.0;
        g.waveformId = 4;
        g.envelopeId = 5;
        g.amplitudeTableId = 6;
        g.channelId = 7;
        const auto back = decode (encode (g));
        check (back.id == g.id && textOf (back) == textOf (g) && back.arcIds == g.arcIds
               && back.muted == g.muted && back.solo == g.solo
               && back.gain == g.gain && back.transposeSemitones == g.transposeSemitones
               && back.waveformId == g.waveformId && back.envelopeId == g.envelopeId
               && back.amplitudeTableId == g.amplitudeTableId && back.channelId == g.channelId,
               "a group with everything comes back the way it went");
        check (argbOf (back) == argbOf (g), "the colour survives the round trip");
    }

    // --- the id ----------------------------------------------------------------
    {
        check (fromJson (R"({"arcs":[]})", 5).id == 5,
               "a group without an id takes the fallback");
        check (fromJson (R"({"arcs":[]})", 0).id == 1,
               "a fallback that is not an id becomes 1");
        check (fromJson (R"({"id":0,"arcs":[]})", 5).id == 1,
               "an id of zero is not an id");
        check (fromJson (R"({"id":-4,"arcs":[]})", 5).id == 1,
               "a negative id is not an id");
        check (fromJson (R"({"id":1000001,"arcs":[]})").id == 1000000,
               "an id above the cap is cut to the cap");
        check (fromJson (R"({"id":2.7,"arcs":[]})").id == 2,
               "a fractional id is cut to a whole one");
    }

    // --- the name ---------------------------------------------------------------
    {
        check (textOf (fromJson (R"({"name":"","arcs":[]})")) == "Group",
               "an empty name falls back to Group");
        const auto g = fromJson (juce::String (R"({"name":")")
                                 + juce::String::repeatedString ("x", 70)
                                 + R"(","arcs":[]})");
        check (textOf (g).length() == 64,
               "a name longer than 64 characters is cut",
               juce::String (textOf (g).length()));
        check (textOf (g) == juce::String::repeatedString ("x", 64),
               "the cut keeps the first 64 characters");
    }

    // --- the colour ---------------------------------------------------------------
    {
        check (argbOf (fromJson (R"({"arcs":[]})")) == 0xff5ac8fa,
               "a group without a colour takes the group's colour");
        check (argbOf (fromJson (R"({"colour":1122867,"arcs":[]})")) == 0x00112233u,
               "the colour in the file is the colour that is read");
        check (argbOf (fromJson (R"({"colour":-1,"arcs":[]})")) == 0xffffffffu,
               "a negative colour is read as the colour it stands for");
    }

    // --- the membership ---------------------------------------------------------------
    {
        const auto g = fromJson (R"({"arcs":[1,0,-3,2,2,1000001]})");
        check (g.arcIds.size() == 2 && g.arcIds[0] == 1 && g.arcIds[1] == 2,
               "the arcs that are not ids are dropped and a duplicate comes once",
               juce::String ((int) g.arcIds.size()));
    }
    {
        juce::Array<juce::var> ids;
        for (int i = 1; i <= upic::limits::kMaxArcsPerPage + 5; ++i) ids.add (i);
        auto* o = new juce::DynamicObject();
        o->setProperty ("arcs", ids);
        const auto g = decode (juce::var (o));
        check ((int) g.arcIds.size() == upic::limits::kMaxArcsPerPage,
               "the arc list is capped",
               juce::String ((int) g.arcIds.size()));
    }

    // --- the mute and the solo ------------------------------------------------------------
    {
        check (fromJson (R"({"mute":1,"arcs":[]})").muted,
               "a mute in the file is a mute");
        check (! fromJson (R"({"solo":false,"arcs":[]})").solo,
               "a solo that is false is not a solo");
    }

    // --- the clamps -----------------------------------------------------------------------
    {
        check (fromJson (R"({"gain":3.0,"arcs":[]})").gain == 2.0f,
               "a gain above 2 is cut to 2");
        check (fromJson (R"({"gain":-1.0,"arcs":[]})").gain == 0.0f,
               "a gain below 0 is cut to 0");
        check (decode (objectWith ("gain", juce::var (std::numeric_limits<double>::quiet_NaN()))).gain == 0.0f,
               "a gain that is not a number falls to 0");
        check (fromJson (R"({"transpose":100.0,"arcs":[]})").transposeSemitones == 48.0,
               "a transpose above 48 is cut to 48");
        check (fromJson (R"({"transpose":-100.0,"arcs":[]})").transposeSemitones == -48.0,
               "a transpose below -48 is cut to -48");
        check (decode (objectWith ("transpose", juce::var (std::numeric_limits<double>::quiet_NaN()))).transposeSemitones == -48.0,
               "a transpose that is not a number falls to -48");
    }

    // --- the four bank ids -------------------------------------------------------------------
    {
        const auto g = fromJson (R"({"wave":3,"env":4,"ampTable":5,"channel":6,"arcs":[]})");
        check (g.waveformId == 3 && g.envelopeId == 4
               && g.amplitudeTableId == 5 && g.channelId == 6,
               "the four bank ids are read");
        const auto d = fromJson (R"({"arcs":[]})");
        check (d.waveformId == -1 && d.envelopeId == -1
               && d.amplitudeTableId == -1 && d.channelId == -1,
               "a group without bank ids overrides nothing");
        const auto c = fromJson (R"({"wave":1000001,"env":-5,"arcs":[]})");
        check (c.waveformId == 1000000 && c.envelopeId == -1,
               "the bank ids are cut to their range");
    }
}
