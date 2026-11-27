#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "PieceLimits.h"
#include "Sieve.h"
#include "SieveCodec.h"
#include "UpicConstants.h"

//==============================================================================
// R2.11: the sieve zone's file format.
//
// A sieve zone is the model's own type: a translucent magnetic region on the
// page - where it is, how big it is, its pitch sieve, its time grid and its
// stickiness. What a piece file carries of it is a different thing - the
// edge, where the JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what a zone writes: the seven keys it always has, the order they are
//   written in, and the two that come only when they carry something;
// - the order independence: the times and the frequencies are written in
//   order, whichever way round the zone was drawn;
// - the round trip: a zone with everything comes back the way it went;
// - the defaults the file uses - which are not the defaults the data uses;
// - the clamps a crafted file gets;
// - the remainders: they are folded into the zone's modulus, and they keep
//   their order and their duplicates;
// - the id: the codec does not touch it. The page's loader is what makes
//   the ids unique.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it.

namespace
{
    // The sieve zone's codec, reached through one door.
    juce::var encode (const upic::SieveZone& z) { return upic::sieveZoneToVar (z); }
    upic::SieveZone decode (const juce::var& v) { return upic::sieveZoneFromVar (v); }

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

    // A zone read from a JSON text - the way a crafted file arrives.
    upic::SieveZone fromJson (const juce::String& json)
    {
        return decode (juce::JSON::parse (json));
    }

    // The remainders of a zone as a comma-separated list - for the detail
    // of a check.
    juce::String listOf (const std::vector<int>& rs)
    {
        juce::String s;
        for (size_t i = 0; i < rs.size(); ++i)
        {
            if (i > 0) s += ",";
            s += juce::String (rs[i]);
        }
        return s;
    }
}

void upic::runSieveCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default zone ---------------------------------------------------------
    {
        const upic::SieveZone z;
        check (z.id == 1, "a new zone has the id 1");
        check (z.t0 == 0.0 && z.t1 == 0.0, "a new zone has no time");
        check (z.f0 == 20.0 && z.f1 == 20.0,
               "a new zone sits at 20 Hz",
               juce::String (z.f0) + ".." + juce::String (z.f1));
        check (z.m == 12, "a new zone divides the octave into twelve");
        check (z.remainders.size() == 1 && z.remainders[0] == 0,
               "a new zone allows the C's",
               listOf (z.remainders));
        check (z.timeGrid == 0.0, "a new zone has no time grid");
        check (z.stickiness == -1.0, "a new zone follows the global stick");
    }

    // --- what a zone writes -------------------------------------------------------
    {
        const auto v = encode (upic::SieveZone());
        check (names (v).joinIntoString (",") == "id,t0,t1,f0,f1,m,r",
               "a zone writes seven keys",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("id", -1) == 1, "the id is in the file");
        check ((double) v.getProperty ("t0", -1.0) == 0.0
               && (double) v.getProperty ("t1", -1.0) == 0.0,
               "the times are in the file");
        check ((double) v.getProperty ("f0", -1.0) == 20.0
               && (double) v.getProperty ("f1", -1.0) == 20.0,
               "the frequencies are in the file");
        check ((int) v.getProperty ("m", -1) == 12, "the modulus is in the file");
        const auto rs = arrayOf (v, "r");
        check (rs != nullptr && rs->size() == 1
               && (int) (*rs)[0] == 0,
               "the remainders are in the file",
               juce::String (rs != nullptr ? rs->size() : -1));
        check (names (v).contains ("timeGrid") == false,
               "a zone with no time grid does not write one");
        check (names (v).contains ("stick") == false,
               "a zone that follows the global stick does not write one");
    }
    {
        // the two optional keys, and their order
        upic::SieveZone z;
        z.timeGrid = 4.0;
        z.stickiness = 0.5;
        const auto v = encode (z);
        check (names (v).joinIntoString (",") == "id,t0,t1,f0,f1,m,r,timeGrid,stick",
               "the optional keys come after the ones that are always there",
               names (v).joinIntoString (","));
        check ((double) v.getProperty ("timeGrid", -1.0) == 4.0,
               "the time grid is in the file");
        check ((double) v.getProperty ("stick", -1.0) == 0.5,
               "the stickiness is in the file");
    }
    {
        // a stick of zero is a stick: it is written, because it means "this
        // zone does not stick" - not "follow the global"
        upic::SieveZone z;
        z.stickiness = 0.0;
        const auto v = encode (z);
        check (names (v).contains ("stick"),
               "a zone with no stickiness of its own writes the zero");
        check ((double) v.getProperty ("stick", -1.0) == 0.0,
               "the zero is in the file");
    }

    // --- the order independence ------------------------------------------------------
    {
        upic::SieveZone z;
        z.t0 = 5.0; z.t1 = 2.0;
        z.f0 = 500.0; z.f1 = 100.0;
        const auto v = encode (z);
        check ((double) v.getProperty ("t0", -1.0) == 2.0
               && (double) v.getProperty ("t1", -1.0) == 5.0,
               "the times are written in order",
               juce::String ((double) v.getProperty ("t0", -1.0)) + ".."
                         + juce::String ((double) v.getProperty ("t1", -1.0)));
        check ((double) v.getProperty ("f0", -1.0) == 100.0
               && (double) v.getProperty ("f1", -1.0) == 500.0,
               "the frequencies are written in order",
               juce::String ((double) v.getProperty ("f0", -1.0)) + ".."
                         + juce::String ((double) v.getProperty ("f1", -1.0)));
    }

    // --- the round trip ------------------------------------------------------------------
    {
        upic::SieveZone z;
        z.id = 7;
        z.t0 = 1.0; z.t1 = 2.0;
        z.f0 = 100.0; z.f1 = 200.0;
        z.m = 7;
        z.remainders = { 0, 3, 5 };
        z.timeGrid = 4.0;
        z.stickiness = 0.25;
        const auto back = decode (encode (z));
        check (back.id == 7, "the id comes back as it went");
        check (back.t0 == 1.0 && back.t1 == 2.0, "the times come back as they went");
        check (back.f0 == 100.0 && back.f1 == 200.0,
               "the frequencies come back as they went");
        check (back.m == 7, "the modulus comes back as it went");
        check (listOf (back.remainders) == "0,3,5",
               "the remainders come back as they went",
               listOf (back.remainders));
        check (back.timeGrid == 4.0, "the time grid comes back as it went");
        check (back.stickiness == 0.25, "the stickiness comes back as it went");
    }

    // --- the defaults the file uses ---------------------------------------------------
    {
        // a zone with nothing in it: what the file says when it says nothing
        const auto z = fromJson ("{}");
        check (z.id == 1, "a zone without an id has the id 1");
        check (z.t0 == 0.0 && z.t1 == 0.0,
               "a zone without times has no time");
        check (z.f0 == upic::kMinFreq && z.f1 == upic::kMaxFreq,
               "a zone without frequencies covers the whole range",
               juce::String (z.f0) + ".." + juce::String (z.f1));
        check (z.m == 12, "a zone without a modulus divides the octave into twelve");
        check (z.remainders.empty(),
               "a zone without remainders allows nothing",
               listOf (z.remainders));
        check (z.timeGrid == 0.0, "a zone without a time grid has none");
        check (z.stickiness == -1.0,
               "a zone without a stickiness follows the global one");
    }
    {
        // something that is not an object at all
        const auto z = fromJson ("5");
        check (z.id == 1 && z.m == 12 && z.remainders.empty(),
               "a file that is not a zone is an empty zone");
    }

    // --- the clamps -------------------------------------------------------------------
    {
        check (fromJson (R"({"t0":-5})").t0 == 0.0,
               "a time below zero is pulled up to zero");
        check (fromJson (R"({"t1":99999})").t1 == upic::kMaxPageSeconds,
               "a time over the page's length is pulled down to it",
               juce::String (fromJson (R"({"t1":99999})").t1));
        check (fromJson (R"({"f0":0})").f0 == upic::kMinFreq,
               "a frequency below the range is pulled up to it");
        check (fromJson (R"({"f1":999999})").f1 == upic::kMaxFreq,
               "a frequency over the range is pulled down to it",
               juce::String (fromJson (R"({"f1":999999})").f1));
        check (fromJson (R"({"m":1})").m == 2,
               "a modulus under two is pulled up to two");
        check (fromJson (R"({"m":500})").m == 120,
               "a modulus over 120 is pulled down to it");
        check (fromJson (R"({"m":-3})").m == 2,
               "a negative modulus is pulled up to two");
        check (fromJson (R"({"timeGrid":-1})").timeGrid == 0.0,
               "a time grid below zero is pulled up to zero");
        check (fromJson (R"({"timeGrid":100})").timeGrid == 64.0,
               "a time grid over 64 is pulled down to it");
        check (fromJson (R"({"stick":-0.5})").stickiness == -1.0,
               "a negative stickiness means: follow the global one");
        check (fromJson (R"({"stick":1.5})").stickiness == 1.0,
               "a stickiness over one is pulled down to one");
        check (fromJson (R"({"stick":0})").stickiness == 0.0,
               "a stickiness of zero is a stickiness");
    }

    // --- the values that are not numbers -------------------------------------------------
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("t0", juce::var (std::numeric_limits<double>::quiet_NaN()));
        o->setProperty ("f0", juce::var (std::numeric_limits<double>::quiet_NaN()));
        o->setProperty ("timeGrid", juce::var (std::numeric_limits<double>::quiet_NaN()));
        o->setProperty ("stick", juce::var (std::numeric_limits<double>::quiet_NaN()));
        const auto z = decode (juce::var (o));
        check (z.t0 == 0.0, "a time that is not a number is zero");
        check (z.f0 == upic::kMinFreq, "a frequency that is not a number is the lowest");
        check (z.timeGrid == 0.0, "a time grid that is not a number is none");
        check (z.stickiness == -1.0,
               "a stickiness that is not a number follows the global one");
    }

    // --- the remainders --------------------------------------------------------------------
    {
        check (listOf (fromJson (R"({"m":12,"r":[13]})").remainders) == "1",
               "a remainder over the modulus is folded into it");
        check (listOf (fromJson (R"({"m":12,"r":[-1]})").remainders) == "11",
               "a negative remainder is folded into the modulus");
        check (listOf (fromJson (R"({"m":12,"r":[2.7]})").remainders) == "2",
               "a fractional remainder is cut to a whole one");
        check (listOf (fromJson (R"({"m":7,"r":[0,3,5]})").remainders) == "0,3,5",
               "the remainders keep their order");
        check (listOf (fromJson (R"({"m":7,"r":[3,1,3]})").remainders) == "3,1,3",
               "the remainders keep their duplicates");
        check (listOf (fromJson (R"({"m":1,"r":[5]})").remainders) == "1",
               "the remainders are folded into the modulus the zone ended up with");
        check (listOf (fromJson (R"({"r":[]})").remainders).isEmpty(),
               "an empty list of remainders is an empty list");
    }
    {
        // the cap: 5000 remainders
        juce::Array<juce::var> rs;
        for (int i = 0; i < upic::limits::kMaxSieveRemainders; ++i)
            rs.add (i % 12);
        auto* o = new juce::DynamicObject();
        o->setProperty ("m", 12);
        o->setProperty ("r", rs);
        check ((int) decode (juce::var (o)).remainders.size()
                   == upic::limits::kMaxSieveRemainders,
               "a list of remainders of the maximum size is kept",
               juce::String ((int) decode (juce::var (o)).remainders.size()));
    }
    {
        // one remainder over the cap
        juce::Array<juce::var> rs;
        for (int i = 0; i < upic::limits::kMaxSieveRemainders + 1; ++i)
            rs.add (i % 12);
        auto* o = new juce::DynamicObject();
        o->setProperty ("m", 12);
        o->setProperty ("r", rs);
        check ((int) decode (juce::var (o)).remainders.size()
                   == upic::limits::kMaxSieveRemainders,
               "a list of remainders over the cap is cut",
               juce::String ((int) decode (juce::var (o)).remainders.size()));
    }

    // --- the id --------------------------------------------------------------------------------
    {
        // the codec does not touch the id: the page's loader is what makes
        // the ids of a piece unique
        check (fromJson (R"({"id":0})").id == 0,
               "the codec keeps an id of zero as it is");
        check (fromJson (R"({"id":-5})").id == -5,
               "the codec keeps a negative id as it is");
        check (fromJson (R"({"id":99999999})").id == 99999999,
               "the codec keeps a huge id as it is");
    }
}
