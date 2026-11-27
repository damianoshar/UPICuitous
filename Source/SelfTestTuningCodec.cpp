#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Tuning.h"
#include "PieceLimits.h"

//==============================================================================
// R2.10: the tuning's file format.
//
// A tuning is the model's own type: which pitch a step of the scale is.
// What a piece file carries of it is a different thing - the edge, where the
// JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what a tuning writes: the names of the keys and the order they are
//   written in;
// - the default tuning: the name, the base note and the empty scale;
// - the round trip: a tuning with everything comes back the way it went;
// - the name and the base note: the defaults the file uses and the clamps;
// - the degrees: the base, the order they must rise in, the range, the cap,
//   and the rule that a broken tuning falls back to the default WHOLE.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it. The name is read through one more helper, so the suite does not
// care what the domain holds it in either.

namespace
{
    // The tuning's codec, reached through one door.
    juce::var encode (const upic::Tuning& t) { return t.toVar(); }
    upic::Tuning decode (const juce::var& v) { return upic::Tuning::fromVar (v); }

    // The tuning's name as text - whatever the domain holds.
    juce::String textOf (const upic::Tuning& t) { return juce::String (t.name); }

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

    // A tuning read from a JSON text - the way a crafted file arrives.
    upic::Tuning fromJson (const juce::String& json)
    {
        return decode (juce::JSON::parse (json));
    }
}

void upic::runTuningCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default tuning -------------------------------------------------------
    {
        const upic::Tuning t;
        check (textOf (t).isEmpty(), "a new tuning has no name");
        check (t.cents.empty(), "a new tuning has no scale");
        check (t.baseNote == 60, "a new tuning sits on middle C");
        check (! t.custom && t.isDefault(),
               "a new tuning is the default one");
        check (t.degrees() == 12,
               "the default tuning has twelve degrees");
        check (t.describe() == "12-tone equal temperament",
               "the default tuning says what it is");
    }

    // --- what a tuning writes ------------------------------------------------------
    {
        const auto v = encode (upic::Tuning());
        check (names (v).joinIntoString (",") == "name,base,cents",
               "a tuning writes three keys",
               names (v).joinIntoString (","));
        check (v.getProperty ("name", juce::var()).toString().isEmpty(),
               "the name is in the file");
        check ((int) v.getProperty ("base", -1) == 60,
               "the base note is in the file");
        const auto cents = arrayOf (v, "cents");
        check (cents != nullptr && cents->isEmpty(),
               "the scale is in the file, and it is empty",
               juce::String (cents != nullptr ? cents->size() : -1));
    }
    {
        // a custom tuning
        upic::Tuning t;
        t.name = "Just";
        t.baseNote = 48;
        t.cents = { 0.0, 100.0, 200.0 };
        t.custom = true;
        const auto v = encode (t);
        check (names (v).joinIntoString (",") == "name,base,cents",
               "a custom tuning writes the same three keys",
               names (v).joinIntoString (","));
        check (v.getProperty ("name", juce::var()).toString() == "Just",
               "the name is in the file");
        check ((int) v.getProperty ("base", -1) == 48,
               "the base note is in the file");
        const auto cents = arrayOf (v, "cents");
        check (cents != nullptr && cents->size() == 3,
               "the scale is in the file",
               juce::String (cents != nullptr ? cents->size() : -1));
        if (cents != nullptr && cents->size() == 3)
        {
            check ((*cents)[0].isDouble() && (double) (*cents)[0] == 0.0
                   && (double) (*cents)[1] == 100.0
                   && (double) (*cents)[2] == 200.0,
                   "a degree is a double in the file, and it keeps its value");
        }
    }

    // --- the round trip ----------------------------------------------------------------
    {
        upic::Tuning t;
        t.name = "Meanquart";
        t.baseNote = 57;
        t.cents = { 0.0, 100.0, 300.0, 600.0 };
        t.custom = true;
        const auto back = decode (encode (t));
        check (back.custom && textOf (back) == textOf (t)
               && back.baseNote == t.baseNote,
               "a tuning with everything comes back the way it went");
        check (back.cents.size() == 4 && back.cents[0] == 0.0
               && back.cents[1] == 100.0 && back.cents[2] == 300.0
               && back.cents[3] == 600.0,
               "the scale comes back the way it went",
               juce::String ((int) back.cents.size()));
    }

    // --- the name and the base note ------------------------------------------------------
    {
        check (fromJson (R"({"base":60,"cents":[0]})").name.isEmpty(),
               "a tuning without a name has none");
        check (fromJson (R"({"name":"Just","cents":[0]})").name == "Just",
               "a name comes back as it went");
        {
            // the tuning's name is not cut
            juce::String longName;
            for (int i = 0; i < 100; ++i) longName += 'x';
            juce::Array<juce::var> cents;
            cents.add (0.0);
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", longName);
            o->setProperty ("cents", cents);
            check (decode (juce::var (o)).name.length() == 100,
                   "a long name is not cut",
                   juce::String (decode (juce::var (o)).name.length()));
        }
        check (fromJson (R"({"cents":[0]})").baseNote == 60,
               "a tuning without a base note sits on middle C");
        check (fromJson (R"({"base":48,"cents":[0]})").baseNote == 48,
               "a base note comes back as it went");
        check (fromJson (R"({"base":-1,"cents":[0]})").baseNote == 0,
               "a base note below zero is pulled up to zero");
        check (fromJson (R"({"base":200,"cents":[0]})").baseNote == 127,
               "a base note over 127 is pulled down to it");
        check (fromJson (R"({"base":2.7,"cents":[0]})").baseNote == 2,
               "a fractional base note is cut to a whole one");
    }

    // --- the scale ---------------------------------------------------------------------------
    {
        // no scale at all is the default tuning
        check (fromJson (R"({"name":"X"})").isDefault(),
               "a tuning without a scale is the default one");
        check (fromJson (R"({"name":"X","cents":[]})").isDefault(),
               "a tuning with an empty scale is the default one");
        check (fromJson (R"({"name":"X","cents":[]})").name.isEmpty(),
               "the default tuning has no name of its own");
    }
    {
        // one degree is one degree
        const auto t = fromJson (R"({"cents":[0]})");
        check (t.custom && t.cents.size() == 1 && t.cents[0] == 0.0,
               "one degree is one degree",
               juce::String ((int) t.cents.size()));
    }
    {
        // the base degree must be 0
        check (fromJson (R"({"cents":[100]})").isDefault(),
               "a scale that does not start at the base is the default one");
        check (! fromJson (R"({"cents":[0.01]})").isDefault(),
               "a base degree a hundredth of a cent off is still the base");
        check (fromJson (R"({"cents":[0.02]})").isDefault(),
               "a base degree two hundredths of a cent off is not");
    }
    {
        // the degrees must rise
        check (fromJson (R"({"cents":[0,200,100]})").isDefault(),
               "a scale that does not rise is the default one");
        check (fromJson (R"({"cents":[0,100,100]})").isDefault(),
               "a scale with two degrees the same is the default one");
        check (! fromJson (R"({"cents":[0,100,200]})").isDefault(),
               "a scale that rises is kept");
    }
    {
        // the degrees are in one octave
        check (fromJson (R"({"cents":[-1]})").isDefault(),
               "a degree below zero is the default tuning");
        check (fromJson (R"({"cents":[0,1200]})").isDefault(),
               "a degree at the octave is the default tuning");
        check (! fromJson (R"({"cents":[0,1199.9]})").isDefault(),
               "a degree just under the octave is kept");
    }
    {
        // a degree that is not a number
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> cents;
        cents.add (0.0);
        cents.add (juce::var (std::numeric_limits<double>::quiet_NaN()));
        o->setProperty ("cents", cents);
        check (decode (juce::var (o)).isDefault(),
               "a degree that is not a number is the default tuning");
    }
    {
        // a degree that is not finite
        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> cents;
        cents.add (0.0);
        cents.add (juce::var (std::numeric_limits<double>::infinity()));
        o->setProperty ("cents", cents);
        check (decode (juce::var (o)).isDefault(),
               "a degree that is not finite is the default tuning");
    }
    {
        // the cap: 256 degrees
        juce::Array<juce::var> cents;
        for (int i = 0; i < upic::limits::kMaxTuningDegrees; ++i)
            cents.add ((double) i);
        auto* o = new juce::DynamicObject();
        o->setProperty ("cents", cents);
        const auto t = decode (juce::var (o));
        check (t.custom && (int) t.cents.size() == upic::limits::kMaxTuningDegrees,
               "a scale of the maximum size is kept",
               juce::String ((int) t.cents.size()));
    }
    {
        // one degree over the cap
        juce::Array<juce::var> cents;
        for (int i = 0; i < upic::limits::kMaxTuningDegrees + 1; ++i)
            cents.add ((double) i);
        auto* o = new juce::DynamicObject();
        o->setProperty ("cents", cents);
        check (decode (juce::var (o)).isDefault(),
               "a scale over the cap is the default tuning");
    }
    {
        // something that is not an object at all
        check (fromJson ("5").isDefault(),
               "a file that is not a tuning is the default tuning");
        check (fromJson (R"({"name":"X","base":48,"cents":[0,100,9999]})").isDefault(),
               "a broken scale discards the whole tuning");
        check (fromJson (R"({"name":"X","base":48,"cents":[0,100,9999]})").name.isEmpty(),
               "a broken scale discards the name too");
        check (fromJson (R"({"name":"X","base":48,"cents":[0,100,9999]})").baseNote == 60,
               "a broken scale discards the base note too");
    }
}
