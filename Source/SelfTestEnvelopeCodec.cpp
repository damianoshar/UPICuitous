#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Envelope.h"
#include "EnvelopeCodec.h"
#include "PieceLimits.h"

//==============================================================================
// R2.6: the envelope's file format.
//
// An envelope is the model's own type: a named set of points over the arc's
// life, with a gain at each point. What a piece file carries of it is a
// different thing - the edge, where the JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what an envelope writes: the names of the keys and the order they are
//   written in;
// - the default envelope: the name, the colour and the flat curve;
// - the id: the fallback, and the fact that it is not clamped;
// - the name and the colour: the defaults the file uses, which are not the
//   defaults the data uses;
// - the points: the defaults, the clamps, the cap, the order they come
//   back in, and the curve an envelope with no points falls back to;
// - the round trip: an envelope with everything comes back the way it went.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class or
// beside it. The name and the colour are read through two more helpers, so
// the suite does not care what the domain holds them in either.

namespace
{
    // The envelope's codec, reached through one door.
    juce::var encode (const upic::Envelope& e) { return upic::envelopeToVar (e); }
    upic::Envelope decode (const juce::var& v, int fallbackId = 0) { return upic::envelopeFromVar (v, fallbackId); }

    // The envelope's name as text, and its colour as a colour - whatever the
    // domain holds.
    juce::String textOf (const upic::Envelope& e) { return juce::String (e.name); }
    juce::uint32 argbOf (const upic::Envelope& e) { return juce::Colour (e.colour).getARGB(); }

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

    // An envelope read from a JSON text - the way a crafted file arrives.
    upic::Envelope fromJson (const juce::String& json, int fallbackId = 0)
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

void upic::runEnvelopeCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default envelope -----------------------------------------------
    {
        const upic::Envelope e;
        check (e.id == 0, "a new envelope has no id yet");
        check (textOf (e) == "Flat", "a new envelope is called Flat");
        check (argbOf (e) == 0xffffa500u,
               "a new envelope has the orange of the palette");
        check (e.points.size() == 2
               && e.points[0].x == 0.0f && e.points[0].y == 1.0f
               && e.points[1].x == 1.0f && e.points[1].y == 1.0f,
               "a new envelope is the flat curve",
               juce::String ((int) e.points.size()));
    }

    // --- what an envelope writes ---------------------------------------------
    {
        const auto v = encode (upic::Envelope());
        check (names (v).joinIntoString (",") == "id,name,colour,points",
               "an envelope writes four keys",
               names (v).joinIntoString (","));
        check ((int) v.getProperty ("id", -1) == 0, "the id is in the file");
        check (v.getProperty ("name", juce::var()).toString() == "Flat",
               "the name is in the file");
        check ((int) v.getProperty ("colour", 0) == (int) 0xffffa500,
               "the colour in the file is the envelope's ARGB");
        const auto points = arrayOf (v, "points");
        check (points != nullptr && points->size() == 2,
               "the flat curve is two points",
               juce::String (points != nullptr ? points->size() : -1));
        if (points != nullptr && points->size() == 2)
        {
            const auto first = (*points)[0];
            check (names (first).joinIntoString (",") == "x,y",
                   "a point writes its x and its y",
                   names (first).joinIntoString (","));
            check ((float) first.getProperty ("x", -1.0) == 0.0f
                   && (float) first.getProperty ("y", -1.0) == 1.0f,
                   "the flat curve's first point is at 0, 1");
        }
    }

    // --- the round trip --------------------------------------------------------
    {
        upic::Envelope e;
        e.id = 7;
        e.name = "Strings";
        e.colour = juce::Colour (0xff123456);
        e.points = { { 0.0f, 0.0f }, { 0.25f, 1.0f }, { 1.0f, 0.0f } };
        const auto back = decode (encode (e));
        check (back.id == e.id && textOf (back) == textOf (e)
               && argbOf (back) == argbOf (e),
               "an envelope with everything comes back the way it went");
        check (back.points.size() == 3
               && back.points[0].x == 0.0f && back.points[0].y == 0.0f
               && back.points[1].x == 0.25f && back.points[1].y == 1.0f
               && back.points[2].x == 1.0f && back.points[2].y == 0.0f,
               "the curve comes back the way it went",
               juce::String ((int) back.points.size()));
    }

    // --- the id -------------------------------------------------------------------
    {
        check (fromJson (R"({"points":[]})", 5).id == 5,
               "an envelope without an id takes the fallback");
        check (fromJson (R"({"id":-4,"points":[]})").id == -4,
               "a negative id is not clamped");
        check (fromJson (R"({"id":999999999,"points":[]})").id == 999999999,
               "a huge id is not clamped");
        check (fromJson (R"({"id":2.7,"points":[]})").id == 2,
               "a fractional id is cut to a whole one");
        check (fromJson (R"({"id":-2.7,"points":[]})").id == -2,
               "a negative fractional id is cut towards zero");
    }

    // --- the name and the colour --------------------------------------------------
    {
        check (fromJson (R"({"points":[]})").name == "Env",
               "an envelope without a name is called Env");
        check (fromJson (R"({"name":"","points":[]})").name == "",
               "an empty name stays empty");
        check (argbOf (fromJson (R"({"points":[]})")) == 0xffff9f0au,
               "an envelope without a colour takes the file's orange");
        check (argbOf (fromJson (R"({"colour":-1,"points":[]})")) == 0xffffffffu,
               "a negative colour is read as the colour it stands for");
    }

    // --- the points ------------------------------------------------------------------
    {
        const auto e = fromJson (R"({"points":[{"x":0.5,"y":0.25}]})");
        check (e.points.size() == 1 && e.points[0].x == 0.5f && e.points[0].y == 0.25f,
               "one point is one point",
               juce::String ((int) e.points.size()));
    }
    {
        // a point without its x or its y takes the defaults: 0 and 1
        const auto e = fromJson (R"({"points":[{}]})");
        check (e.points.size() == 1 && e.points[0].x == 0.0f && e.points[0].y == 1.0f,
               "a point without a position takes the start of the flat curve");
    }
    {
        // the values are clamped to the curve's own range
        const auto e = fromJson (R"({"points":[{"x":-1,"y":2}]})");
        check (e.points.size() == 1 && e.points[0].x == 0.0f && e.points[0].y == 1.0f,
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
        o->setProperty ("points", points);
        const auto e = decode (juce::var (o));
        check (e.points.size() == 1 && e.points[0].x == 0.0f && e.points[0].y == 0.0f,
               "a point that is not a number falls to 0",
               juce::String ((int) e.points.size()));
    }
    {
        // the points come back in time order, whatever the file said
        const auto e = fromJson (R"({"points":[{"x":0.8,"y":0.8},{"x":0.2,"y":0.2},{"x":0.5,"y":0.5}]})");
        check (e.points.size() == 3
               && e.points[0].x == 0.2f && e.points[1].x == 0.5f && e.points[2].x == 0.8f,
               "the points come back in order",
               juce::String ((int) e.points.size()));
    }
    {
        // no points at all is the flat curve
        const auto e = fromJson (R"({"points":[]})");
        check (e.points.size() == 2
               && e.points[0].x == 0.0f && e.points[0].y == 1.0f
               && e.points[1].x == 1.0f && e.points[1].y == 1.0f,
               "an envelope with no points is the flat curve",
               juce::String ((int) e.points.size()));
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
        o->setProperty ("points", points);
        const auto e = decode (juce::var (o));
        check ((int) e.points.size() == upic::limits::kMaxPointsPerEnvelope,
               "the curve is capped",
               juce::String ((int) e.points.size()));
        check (e.points.back().x == 1.0f,
               "the cap drops the last points of the file, before the sort",
               juce::String ((double) e.points.back().x));
        check (e.points.front().x > 0.0f,
               "the points the file had last are the ones that are gone",
               juce::String ((double) e.points.front().x));
    }
}
