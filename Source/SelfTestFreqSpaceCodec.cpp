#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "FreqSpace.h"
#include "PieceLimits.h"
#include "UpicConstants.h"

//==============================================================================
// R2.13: the frequency space definition's file format.
//
// A frequency space definition is the model's own type: the page's four
// frequency spaces - a preset, a drawn table, a staircase and an inversion.
// What a piece file carries of it is a different thing - the edge, where the
// JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what a definition writes: the keys it always has, the ones that come
//   only when they carry something, and the order they are written in;
// - what a point writes: its two keys, and the fact that the file carries
//   the frequency in Hz and not in log2;
// - the round trip: a definition with everything comes back the way it went;
// - the defaults the file uses;
// - the clamps a crafted file gets;
// - the rules a table has to follow: two points, and the points in order;
// - the rule that the space index is written but not read.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it.

namespace
{
    // The frequency space definition's codec, reached through one door.
    juce::var encode (const upic::FreqSpaceDef& d, int index) { return d.toVar (index); }
    upic::FreqSpaceDef decode (const juce::var& v) { return upic::FreqSpaceDef::fromVar (v); }

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

    // A definition read from a JSON text - the way a crafted file arrives.
    upic::FreqSpaceDef fromJson (const juce::String& json)
    {
        return decode (juce::JSON::parse (json));
    }
}

void upic::runFreqSpaceCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default definition ------------------------------------------------
    {
        const upic::FreqSpaceDef d;
        check (d.preset == 0, "a new space is the page's own range");
        check (! d.editable, "a new space is not editable");
        check (d.points.empty(), "a new space has no table");
        check (! d.stepped && d.stepDivisions == 0,
               "a new space is not a staircase");
        check (! d.inverted, "a new space is not inverted");
        check (d.isDefault(), "a new space is the default one");
    }

    // --- what a definition writes -------------------------------------------------
    {
        const auto v = encode (upic::FreqSpaceDef(), 2);
        check (names (v).joinIntoString (",") == "space,editable",
               "a default space writes two keys",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("space", -1) == 2,
               "the space index is in the file");
        check ((bool) v.getProperty ("editable", true) == false,
               "the editable flag is in the file - even when it is false");
    }
    {
        // everything
        upic::FreqSpaceDef d;
        d.preset = 3;
        d.editable = true;
        d.stepped = true;
        d.stepDivisions = 12;
        d.inverted = true;
        d.points = { { 0.0f, (float) std::log2 (220.0) },
                     { 1.0f, (float) std::log2 (880.0) } };
        const auto v = encode (d, 1);
        check (names (v).joinIntoString (",")
                   == "space,preset,editable,stepped,divisions,inverted,points",
               "a space with everything writes seven keys",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("preset", -1) == 3,
               "the preset is in the file");
        check ((int) v.getProperty ("divisions", -1) == 12,
               "the divisions are in the file");
        check ((bool) v.getProperty ("stepped", false) == true,
               "the staircase flag is in the file");
        check ((bool) v.getProperty ("inverted", false) == true,
               "the inversion flag is in the file");
        const auto pts = arrayOf (v, "points");
        check (pts != nullptr && pts->size() == 2,
               "the table is in the file",
               juce::String (pts != nullptr ? pts->size() : -1));
        if (pts != nullptr && pts->size() == 2)
        {
            const auto& p = (*pts)[0];
            check (names (p).joinIntoString (",") == "x,hz",
                   "a point writes two keys",
                   names (p).joinIntoString (","));
            check ((double) p.getProperty ("x", -1.0) == 0.0
                   && std::abs ((double) p.getProperty ("hz", -1.0) - 220.0) < 1e-3,
                   "the point carries its frequency in Hz",
                   juce::String ((double) p.getProperty ("hz", -1.0)));
        }
    }
    {
        // a preset of zero is the default, so it is not written
        upic::FreqSpaceDef d;
        d.preset = 0;
        const auto v = encode (d, 0);
        check (names (v).contains ("preset") == false,
               "the page's own range is not written as a preset");
    }

    // --- the round trip ------------------------------------------------------------------
    {
        upic::FreqSpaceDef d;
        d.preset = 2;
        d.editable = true;
        d.stepped = true;
        d.stepDivisions = 7;
        d.inverted = true;
        d.points = { { 0.0f, (float) std::log2 (110.0) },
                     { 0.5f, (float) std::log2 (440.0) },
                     { 1.0f, (float) std::log2 (1760.0) } };
        const auto back = decode (encode (d, 0));
        check (back.preset == 2, "the preset comes back as it went");
        check (back.editable, "the editable flag comes back as it went");
        check (back.stepped && back.stepDivisions == 7,
               "the staircase comes back as it went");
        check (back.inverted, "the inversion comes back as it went");
        check (back.points.size() == 3
               && std::abs (back.points[0].x - 0.0f) < 1e-6
               && std::abs (back.points[1].x - 0.5f) < 1e-6
               && std::abs (back.points[2].x - 1.0f) < 1e-6,
               "the table comes back as it went",
               juce::String ((int) back.points.size()));
        check (std::abs (std::exp2 ((double) back.points[1].log2Hz) - 440.0) < 1e-2,
               "the table's pitches come back as they went",
               juce::String (std::exp2 ((double) back.points[1].log2Hz), 3));
    }

    // --- the defaults the file uses ---------------------------------------------------
    {
        const auto d = fromJson ("{}");
        check (d.preset == 0, "a space without a preset is the page's own range");
        check (! d.editable, "a space without the editable flag is not editable");
        check (! d.stepped, "a space without the staircase flag is not a staircase");
        check (d.stepDivisions == 0, "a space without divisions has none");
        check (! d.inverted, "a space without the inversion flag is not inverted");
        check (d.points.empty(), "a space without a table has none");
        check (d.isDefault(), "a space with nothing in it is the default one");
    }

    // --- the clamps -------------------------------------------------------------------
    {
        check (fromJson (R"({"preset":-1})").preset == 0,
               "a negative preset is pulled up to the first one");
        check (fromJson (R"({"preset":99})").preset == 6,
               "a preset over the last one is pulled down to it",
               juce::String (fromJson (R"({"preset":99})").preset));
        check (fromJson (R"({"divisions":-5})").stepDivisions == 0,
               "negative divisions are pulled up to none");
        check (fromJson (R"({"divisions":1000})").stepDivisions
                   == upic::limits::kMaxStepDivisions,
               "divisions over 99 are pulled down to 99",
               juce::String (fromJson (R"({"divisions":1000})").stepDivisions));
    }
    {
        // the table's points
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("x", -1.0);
            po->setProperty ("hz", 440.0);
            juce::Array<juce::var> pts;
            pts.add (juce::var (po));
            auto* o = new juce::DynamicObject();
            o->setProperty ("editable", true);
            o->setProperty ("points", pts);
            const auto d = decode (juce::var (o));
            check (d.points.empty(),
                   "a table of one point is not a table",
                   juce::String ((int) d.points.size()));
            check (! d.editable,
                   "a table of one point is not editable");
        }
        {
            // a point with nothing in it: x 0, hz 440
            juce::Array<juce::var> pts;
            pts.add (juce::var (new juce::DynamicObject()));
            pts.add (juce::var (new juce::DynamicObject()));
            auto* o = new juce::DynamicObject();
            o->setProperty ("editable", true);
            o->setProperty ("points", pts);
            const auto d = decode (juce::var (o));
            check (d.points.size() == 2 && d.points[0].x == 0.0f
                   && std::abs (std::exp2 ((double) d.points[0].log2Hz) - 440.0) < 1e-2,
                   "a point with nothing in it is at the start, at 440 Hz",
                   juce::String ((int) d.points.size()));
        }
        {
            // the x and the frequency are clamped
            juce::Array<juce::var> pts;
            {
                auto* po = new juce::DynamicObject();
                po->setProperty ("x", 2.0);
                po->setProperty ("hz", 999999.0);
                pts.add (juce::var (po));
            }
            {
                auto* po = new juce::DynamicObject();
                po->setProperty ("x", -1.0);
                po->setProperty ("hz", 0.0);
                pts.add (juce::var (po));
            }
            auto* o = new juce::DynamicObject();
            o->setProperty ("editable", true);
            o->setProperty ("points", pts);
            const auto d = decode (juce::var (o));
            check (d.points.size() == 2, "the points come back",
                   juce::String ((int) d.points.size()));
            if (d.points.size() == 2)
            {
                // the points are sorted by x, so the one at x -1 is first
                check (std::abs (d.points[0].x - 0.0f) < 1e-6
                       && std::abs (d.points[1].x - 1.0f) < 1e-6,
                       "the x is clamped to 0..1 and the points are sorted by it");
                check (std::exp2 ((double) d.points[0].log2Hz) >= upic::kMinFreq - 1e-9
                       && std::exp2 ((double) d.points[1].log2Hz) <= upic::kMaxFreq + 1e-9,
                       "the frequency is clamped to the app's range");
            }
        }
        {
            // the values that are not numbers
            juce::Array<juce::var> pts;
            auto* po = new juce::DynamicObject();
            po->setProperty ("x", juce::var (std::numeric_limits<double>::quiet_NaN()));
            po->setProperty ("hz", juce::var (std::numeric_limits<double>::quiet_NaN()));
            pts.add (juce::var (po));
            auto* po2 = new juce::DynamicObject();
            po2->setProperty ("x", 0.5);
            po2->setProperty ("hz", 440.0);
            pts.add (juce::var (po2));
            auto* o = new juce::DynamicObject();
            o->setProperty ("editable", true);
            o->setProperty ("points", pts);
            const auto d = decode (juce::var (o));
            check (d.points.size() == 2
                   && std::abs (d.points[0].x - 0.0f) < 1e-6,
                   "a point that is not a number is at the start of the range",
                   juce::String ((int) d.points.size()));
        }
    }

    // --- the rules a table has to follow ---------------------------------------------------
    {
        // a table of one point is dropped
        juce::Array<juce::var> pts;
        auto* po = new juce::DynamicObject();
        po->setProperty ("x", 0.5);
        po->setProperty ("hz", 440.0);
        pts.add (juce::var (po));
        auto* o = new juce::DynamicObject();
        o->setProperty ("editable", true);
        o->setProperty ("points", pts);
        const auto d = decode (juce::var (o));
        check (! d.editable && d.points.empty(),
               "a table of one point is dropped whole");
    }
    {
        // a table that is not editable is dropped
        juce::Array<juce::var> pts;
        for (int i = 0; i < 3; ++i)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("x", (double) i * 0.5);
            po->setProperty ("hz", 440.0);
            pts.add (juce::var (po));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("editable", false);
        o->setProperty ("points", pts);
        const auto d = decode (juce::var (o));
        check (d.points.empty(),
               "a table of a space that is not editable is dropped",
               juce::String ((int) d.points.size()));
    }
    {
        // the cap: 16384 points
        juce::Array<juce::var> pts;
        for (int i = 0; i < upic::limits::kMaxTablePoints; ++i)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("x", (double) i / (double) upic::limits::kMaxTablePoints);
            po->setProperty ("hz", 440.0);
            pts.add (juce::var (po));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("editable", true);
        o->setProperty ("points", pts);
        check ((int) decode (juce::var (o)).points.size()
                   == upic::limits::kMaxTablePoints,
               "a table of the maximum size is kept",
               juce::String ((int) decode (juce::var (o)).points.size()));
    }
    {
        // one point over the cap
        juce::Array<juce::var> pts;
        for (int i = 0; i < upic::limits::kMaxTablePoints + 1; ++i)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("x", (double) i / (double) upic::limits::kMaxTablePoints);
            po->setProperty ("hz", 440.0);
            pts.add (juce::var (po));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("editable", true);
        o->setProperty ("points", pts);
        check ((int) decode (juce::var (o)).points.size()
                   == upic::limits::kMaxTablePoints,
               "a table over the cap is cut",
               juce::String ((int) decode (juce::var (o)).points.size()));
    }

    // --- the space index ---------------------------------------------------------------------
    {
        // the definition's reader does not read the space index: the PAGE's
        // codec is what puts a table in the right slot
        const auto d = fromJson (R"({"space":3,"editable":true})");
        check (! d.editable,
               "a space with no table is not editable even when it says it is");
    }
}
