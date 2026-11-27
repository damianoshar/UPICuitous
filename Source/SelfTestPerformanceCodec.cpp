#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Performance.h"
#include "PieceLimits.h"
#include "UpicConstants.h"

//==============================================================================
// R2.12: the performance sequence's file format.
//
// A performance sequence is the model's own type: the recorded movement
// through the score - a list of (performance time, page, position) points.
// What a piece file carries of it is a different thing - the edge, where the
// JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what a sequence writes: the four keys it has, the order they are
//   written in, and the one that comes only when it carries something;
// - what a point writes: its three keys and their order;
// - the round trip: a sequence with everything comes back the way it went;
// - the defaults the file uses;
// - the id: the fallback, and the clamps;
// - the name: the default, and the cut;
// - the loop flag;
// - the points: the clamps, the cap, and the order they come back in.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it. The name is read through one more helper, so the suite does not
// care what the domain holds it in either.

namespace
{
    // The performance sequence's codec, reached through one door.
    juce::var encode (const upic::PerformanceSequence& s) { return s.toVar(); }
    upic::PerformanceSequence decode (const juce::var& v, int fallbackId = 1)
    {
        return upic::PerformanceSequence::fromVar (v, fallbackId);
    }

    // The sequence's name as text - whatever the domain holds.
    juce::String textOf (const upic::PerformanceSequence& s) { return juce::String (s.name); }

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

    // A sequence read from a JSON text - the way a crafted file arrives.
    upic::PerformanceSequence fromJson (const juce::String& json, int fallbackId = 1)
    {
        return decode (juce::JSON::parse (json), fallbackId);
    }

    // A string of n characters - the name is cut, so it needs one.
    juce::String chars (int n)
    {
        juce::String s;
        for (int i = 0; i < n; ++i) s += 'x';
        return s;
    }
}

void upic::runPerformanceCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default sequence -----------------------------------------------------
    {
        const upic::PerformanceSequence s;
        check (s.id == 0, "a new sequence has no id of its own");
        check (textOf (s) == "Sequence", "a new sequence is called \"Sequence\"");
        check (s.points.empty(), "a new sequence has no points");
        check (s.loop, "a new sequence loops");
        check (s.lengthSec() == 0.0, "a new sequence is no time long");
    }

    // --- what a sequence writes -----------------------------------------------------
    {
        const auto v = encode (upic::PerformanceSequence());
        check (names (v).joinIntoString (",") == "id,name,points",
               "a sequence writes three keys",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("id", -1) == 0, "the id is in the file");
        check (v.getProperty ("name", juce::var()).toString() == "Sequence",
               "the name is in the file");
        check (names (v).contains ("loop") == false,
               "a sequence that loops does not write the flag");
        const auto pts = arrayOf (v, "points");
        check (pts != nullptr && pts->isEmpty(),
               "the points are in the file, and there are none",
               juce::String (pts != nullptr ? pts->size() : -1));
    }
    {
        // the loop flag, and its place in the order
        upic::PerformanceSequence s;
        s.loop = false;
        const auto v = encode (s);
        check (names (v).joinIntoString (",") == "id,name,loop,points",
               "the loop flag comes before the points",
               names (v).joinIntoString (","));
        check ((bool) v.getProperty ("loop", true) == false,
               "the loop flag is in the file");
    }
    {
        // what a point writes
        upic::PerformanceSequence s;
        s.points = { { 1.5, 2, 3.5 } };
        const auto v = encode (s);
        const auto pts = arrayOf (v, "points");
        check (pts != nullptr && pts->size() == 1,
               "the point is in the file",
               juce::String (pts != nullptr ? pts->size() : -1));
        if (pts != nullptr && pts->size() == 1)
        {
            const auto& p = (*pts)[0];
            check (names (p).joinIntoString (",") == "t,page,pos",
                   "a point writes three keys",
                   names (p).joinIntoString (","));
            check ((double) p.getProperty ("t", -1.0) == 1.5
                   && (int) p.getProperty ("page", -1) == 2
                   && (double) p.getProperty ("pos", -1.0) == 3.5,
                   "the point keeps its values");
        }
    }

    // --- the round trip ------------------------------------------------------------------
    {
        upic::PerformanceSequence s;
        s.id = 3;
        s.name = "Take";
        s.loop = false;
        s.points = { { 0.0, 0, 0.0 }, { 1.0, 0, 2.0 }, { 2.0, 1, 0.0 } };
        const auto back = decode (encode (s));
        check (back.id == 3, "the id comes back as it went");
        check (textOf (back) == "Take", "the name comes back as it went");
        check (! back.loop, "the loop flag comes back as it went");
        check (back.points.size() == 3
               && back.points[0].t == 0.0 && back.points[0].page == 0
               && back.points[0].pos == 0.0
               && back.points[1].t == 1.0 && back.points[1].page == 0
               && back.points[1].pos == 2.0
               && back.points[2].t == 2.0 && back.points[2].page == 1
               && back.points[2].pos == 0.0,
               "the points come back as they went",
               juce::String ((int) back.points.size()));
    }

    // --- the defaults the file uses ---------------------------------------------------
    {
        const auto s = fromJson ("{}");
        check (s.id == 1, "a sequence without an id takes the fallback");
        check (textOf (s) == "Sequence", "a sequence without a name is called \"Sequence\"");
        check (s.loop, "a sequence without a loop flag loops");
        check (s.points.empty(), "a sequence without points has none");
    }
    {
        // something that is not an object at all
        const auto s = fromJson ("5");
        check (s.id == 1 && textOf (s) == "Sequence" && s.points.empty(),
               "a file that is not a sequence is an empty sequence");
    }

    // --- the id -------------------------------------------------------------------
    {
        check (fromJson (R"({"id":7})", 3).id == 7, "an id comes back as it went");
        check (fromJson (R"({"id":0})", 3).id == 1,
               "an id of zero is pulled up to one");
        check (fromJson (R"({"id":-4})", 3).id == 1,
               "a negative id is pulled up to one");
        check (fromJson (R"({"id":99999999})", 3).id == upic::limits::kMaxItemId,
               "an id over the range is pulled down to it",
               juce::String (fromJson (R"({"id":99999999})", 3).id));
        check (fromJson ("{}", 5).id == 5,
               "a sequence without an id takes the fallback it was given");
        check (fromJson ("{}", 0).id == 1,
               "a fallback of zero is pulled up to one");
    }

    // --- the name ---------------------------------------------------------------------
    {
        check (fromJson (R"({"name":"Take"})").name == "Take",
               "a name comes back as it went");
        check (fromJson (R"({"name":""})").name == "Sequence",
               "an empty name becomes \"Sequence\"");
        {
            // the name is cut to 64 characters
            juce::Array<juce::var> none;
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", chars (64));
            check (decode (juce::var (o)).name.length() == 64,
                   "a name of exactly 64 characters is kept",
                   juce::String (decode (juce::var (o)).name.length()));
        }
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", chars (70));
            check (decode (juce::var (o)).name.length() == 64,
                   "a longer name is cut to 64 characters",
                   juce::String (decode (juce::var (o)).name.length()));
        }
    }

    // --- the loop flag ---------------------------------------------------------------------
    {
        check (fromJson (R"({"loop":false})").loop == false,
               "a sequence that does not loop does not loop");
        check (fromJson (R"({"loop":true})").loop == true,
               "a sequence that loops loops");
        check (fromJson (R"({"loop":false})").loop == false,
               "the flag is read, not guessed");
    }

    // --- the points ---------------------------------------------------------------------
    {
        // the time
        check (fromJson (R"({"points":[{"t":-1}]})").points[0].t == 0.0,
               "a point before zero is pulled up to zero");
        check (fromJson (R"({"points":[{"t":99999}]})").points[0].t
                   == (double) upic::limits::kMaxSequenceSeconds,
               "a point after the hour is pulled down to it",
               juce::String (fromJson (R"({"points":[{"t":99999}]})").points[0].t));
        // the page
        check (fromJson (R"({"points":[{"page":-3}]})").points[0].page == 0,
               "a point on a negative page is pulled up to the first page");
        check (fromJson (R"({"points":[{"page":999}]})").points[0].page
                   == upic::limits::kMaxPages - 1,
               "a point beyond the last page is pulled down to it",
               juce::String (fromJson (R"({"points":[{"page":999}]})").points[0].page));
        // the position
        check (fromJson (R"({"points":[{"pos":-1}]})").points[0].pos == 0.0,
               "a position before zero is pulled up to zero");
        check (fromJson (R"({"points":[{"pos":99999}]})").points[0].pos
                   == upic::kMaxPageSeconds,
               "a position beyond the page is pulled down to it",
               juce::String (fromJson (R"({"points":[{"pos":99999}]})").points[0].pos));
        // the defaults
        {
            const auto s = fromJson (R"({"points":[{}]})");
            check (s.points.size() == 1 && s.points[0].t == 0.0
                   && s.points[0].page == 0 && s.points[0].pos == 0.0,
                   "a point with nothing in it is at the start of the first page");
        }
    }
    {
        // the values that are not numbers
        auto* po = new juce::DynamicObject();
        po->setProperty ("t", juce::var (std::numeric_limits<double>::quiet_NaN()));
        po->setProperty ("pos", juce::var (std::numeric_limits<double>::quiet_NaN()));
        juce::Array<juce::var> pts;
        pts.add (juce::var (po));
        auto* o = new juce::DynamicObject();
        o->setProperty ("points", pts);
        const auto s = decode (juce::var (o));
        check (s.points.size() == 1 && s.points[0].t == 0.0 && s.points[0].pos == 0.0,
               "a point that is not a number is at the start");
    }
    {
        // the points come back in time order
        const auto s = fromJson (R"({"points":[{"t":2},{"t":0},{"t":1}]})");
        check (s.points.size() == 3 && s.points[0].t == 0.0
               && s.points[1].t == 1.0 && s.points[2].t == 2.0,
               "the points come back in time order",
               juce::String ((int) s.points.size()));
    }
    {
        // the cap: 20000 points
        juce::Array<juce::var> pts;
        for (int i = 0; i < upic::limits::kMaxSequencePoints; ++i)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("t", (double) i);
            pts.add (juce::var (po));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("points", pts);
        check ((int) decode (juce::var (o)).points.size()
                   == upic::limits::kMaxSequencePoints,
               "a list of points of the maximum size is kept",
               juce::String ((int) decode (juce::var (o)).points.size()));
    }
    {
        // one point over the cap
        juce::Array<juce::var> pts;
        for (int i = 0; i < upic::limits::kMaxSequencePoints + 1; ++i)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("t", (double) i);
            pts.add (juce::var (po));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("points", pts);
        check ((int) decode (juce::var (o)).points.size()
                   == upic::limits::kMaxSequencePoints,
               "a list of points over the cap is cut",
               juce::String ((int) decode (juce::var (o)).points.size()));
    }
}
