#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>
#include <vector>

#include "Arc.h"
#include "ArcCodec.h"
#include "ArcGroup.h"
#include "FreqSpace.h"
#include "Page.h"
#include "PieceLimits.h"
#include "Sieve.h"
#include "UpicConstants.h"

//==============================================================================
// R2.4: the page's codec - the file format of a page.
//
// A page is the model's surface: the arcs, the sieve zones, the four
// frequency spaces, the groups, the ambitus and the page's own time. How
// all of that reaches a file is a different thing from what a page is -
// the file format is the edge where the JUCE value types live.
//
// This suite is that edge's contract. It pins
//
// - what a page writes: the names of the keys and the order they are
//   written in;
// - what a page leaves out: every key that is only written when the page
//   is not the default, so an old piece's file is byte for byte what it
//   was;
// - the clamps a crafted file gets: the duration, the tempo, the warp,
//   the ambitus, the spaces and the drawn tables;
// - the ids: the duplicates that are made unique, the counter that is
//   derived from the arcs that are there, and the renames that are
//   reported;
// - the caps: the arcs, the zones and the groups;
// - the group's membership: only the arcs that are on the page, no
//   duplicates, one group per arc;
// - the round trip: a page with everything comes back the way it went.
//
// The suite reaches the codec through one door (the encode/decode helpers
// at the top), so it does not care whether the format lives in the class
// or beside it.

namespace
{
    // The page's codec, reached through one door.
    juce::var encode (const upic::Page& p) { return p.toVar(); }
    upic::Page decode (const juce::var& v, int* renamed = nullptr) { return upic::Page::fromVar (v, renamed); }

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

    // An arc to put on a page.
    upic::Arc anArc (int id, double t, double f)
    {
        upic::Arc a;
        a.id = id;
        a.points = { { t, f, 1.0f } };
        return a;
    }

    // A group that names the arcs it owns.
    upic::ArcGroup aGroup (int id, std::vector<int> arcIds)
    {
        upic::ArcGroup g;
        g.id = id;
        g.arcIds = std::move (arcIds);
        return g;
    }

    // A page read from a JSON text - the way a crafted file arrives.
    upic::Page fromJson (const juce::String& json)
    {
        return decode (juce::JSON::parse (json));
    }
}

void upic::runPageCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the default page: what it writes, and what it leaves out --------
    {
        const upic::Page p;
        const auto v = encode (p);
        check (names (v).joinIntoString (",") == "duration,nextArcId,arcs",
               "a default page writes its duration, its next arc id and its arcs",
               names (v).joinIntoString (","));
        check (! has (v, "tempo"), "a default page writes no tempo");
        check (! has (v, "zones") && ! has (v, "nextZoneId"),
               "a default page writes no zones");
        check (! has (v, "ambLo") && ! has (v, "ambHi"),
               "a default page writes no ambitus");
        check (! has (v, "spaces"), "a default page writes no spaces");
        check (! has (v, "spaceTables"), "a default page writes no space tables");
        check (! has (v, "warp"), "a default page writes no warp");
        check (! has (v, "groups") && ! has (v, "nextGroupId"),
               "a default page writes no groups");
        check ((double) v.getProperty ("duration", 0.0) == 30.0,
               "the default page lasts 30 s");
        check ((int) v.getProperty ("nextArcId", 0) == 1,
               "the default page hands out arc id 1");
        auto* arcs = arrayOf (v, "arcs");
        check (arcs != nullptr && arcs->isEmpty(), "the default page has no arcs");
    }

    // --- the order the keys are written in --------------------------------
    {
        upic::Page p;
        p.tempoBpm = 90.0;
        p.arcs.push_back (anArc (1, 1.0, 220.0));
        upic::SieveZone z;
        z.id = 1; z.t0 = 0.0; z.t1 = 2.0; z.f0 = 100.0; z.f1 = 200.0;
        p.zones.push_back (z);
        p.pitchLo = 40.0; p.pitchHi = 8000.0;
        p.spaces = { 1, 0, 0, 0 };
        p.spaceDefs[0].editable = true;
        p.spaceDefs[0].points = { { 0.0f, 8.0f }, { 1.0f, 10.0f } };
        p.warp = 0.25;
        p.groups.push_back (aGroup (1, { 1 }));

        const auto v = encode (p);
        check (names (v).joinIntoString (",")
                 == "duration,tempo,nextArcId,arcs,zones,nextZoneId,ambLo,ambHi,"
                    "spaces,spaceTables,warp,groups,nextGroupId",
               "the page's keys are written in the order the format has always used",
               names (v).joinIntoString (","));
    }

    // --- the keys that only a page that is not the default writes --------
    {
        upic::Page p;
        p.tempoBpm = 90.0;
        const auto v = encode (p);
        check (has (v, "tempo") && (double) v.getProperty ("tempo", 0.0) == 90.0,
               "a page with a tempo writes it");
    }
    {
        upic::Page p;
        upic::SieveZone z;
        z.id = 3; z.t0 = 0.0; z.t1 = 1.0; z.f0 = 100.0; z.f1 = 200.0;
        p.zones.push_back (z);
        const auto v = encode (p);
        check (has (v, "zones") && has (v, "nextZoneId"),
               "a page with zones writes them and their next id");
        auto* zones = arrayOf (v, "zones");
        check (zones != nullptr && zones->size() == 1,
               "the page writes one zone");
        if (zones != nullptr && zones->size() == 1)
        {
            const auto zone = (*zones)[0];
            check ((int) zone.getProperty ("id", 0) == 3,
                   "the zone keeps its own id");
        }
    }
    {
        upic::Page p;
        p.pitchLo = 40.0;
        p.pitchHi = 8000.0;
        const auto v = encode (p);
        check (has (v, "ambLo") && has (v, "ambHi"),
               "a page with another ambitus writes it");
        check ((double) v.getProperty ("ambLo", 0.0) == 40.0
               && (double) v.getProperty ("ambHi", 0.0) == 8000.0,
               "the ambitus is written as it is");
    }
    {
        upic::Page p;
        p.spaces = { 2, 0, 3, 0 };
        const auto v = encode (p);
        check (has (v, "spaces"), "a page with a changed space writes its spaces");
        auto* spaces = arrayOf (v, "spaces");
        check (spaces != nullptr && spaces->size() == 4,
               "the page writes all four spaces");
        if (spaces != nullptr && spaces->size() == 4)
            check ((int) (*spaces)[0] == 2 && (int) (*spaces)[2] == 3,
                   "the spaces are written in the page's own order");
    }
    {
        upic::Page p;
        p.spaceDefs[0].editable = true;
        p.spaceDefs[0].points = { { 0.0f, 8.0f }, { 1.0f, 10.0f } };
        const auto v = encode (p);
        check (has (v, "spaceTables"), "a page with a drawn table writes it");
        auto* tables = arrayOf (v, "spaceTables");
        check (tables != nullptr && tables->size() == 1,
               "the page writes only the tables that are not presets");
        if (tables != nullptr && tables->size() == 1)
            check ((int) (*tables)[0].getProperty ("space", -1) == 0,
                   "the drawn table names the space it belongs to");
    }
    {
        upic::Page p;
        p.warp = 0.25;
        const auto v = encode (p);
        check (has (v, "warp") && (double) v.getProperty ("warp", 0.0) == 0.25,
               "a warped page writes its warp");
    }
    {
        upic::Page p;
        p.groups.push_back (aGroup (2, { 1 }));
        const auto v = encode (p);
        check (has (v, "groups") && has (v, "nextGroupId"),
               "a page with groups writes them and their next id");
        auto* groups = arrayOf (v, "groups");
        check (groups != nullptr && groups->size() == 1,
               "the page writes its groups");
    }

    // --- the clamps a crafted file gets -----------------------------------
    {
        check (std::abs (fromJson (R"({"duration":0,"arcs":[]})").durationSec
                         - upic::kMinPageSeconds) < 1e-12,
               "a page that is too short is pulled back to the shortest one");
        check (std::abs (fromJson (R"({"duration":1e9,"arcs":[]})").durationSec
                         - upic::kMaxPageSeconds) < 1e-9,
               "a page that is too long is pulled back to the longest one");
        check (std::abs (fromJson (R"({"duration":12.5,"arcs":[]})").durationSec
                         - 12.5) < 1e-12,
               "a page's own duration is kept");
        {
            // The object is on the heap: a var takes it over, and a stack
            // object would be deleted when the var lets go of it.
            auto* o = new juce::DynamicObject();
            o->setProperty ("duration", std::numeric_limits<double>::quiet_NaN());
            const auto p = decode (juce::var (o));
            check (std::abs (p.durationSec - upic::kMinPageSeconds) < 1e-12,
                   "a duration that is not a number becomes the shortest page");
        }
    }
    {
        check (std::abs (fromJson (R"({"tempo":0,"arcs":[]})").tempoBpm - 20.0) < 1e-12,
               "a tempo that is too slow is pulled back to the slowest one");
        check (std::abs (fromJson (R"({"tempo":10000,"arcs":[]})").tempoBpm - 400.0) < 1e-12,
               "a tempo that is too fast is pulled back to the fastest one");
        check (std::abs (fromJson (R"({"tempo":90,"arcs":[]})").tempoBpm - 90.0) < 1e-12,
               "the page's own tempo is kept");
    }
    {
        check (std::abs (fromJson (R"({"warp":5,"arcs":[]})").warp - 0.9) < 1e-12,
               "a warp that is too strong is pulled back to the strongest one");
        check (std::abs (fromJson (R"({"warp":-5,"arcs":[]})").warp + 0.9) < 1e-12,
               "a warp that runs backwards too fast is pulled back too");
        check (fromJson (R"({"warp":0.3,"arcs":[]})").warp == 0.3,
               "the page's own warp is kept");
    }
    {
        const auto p = fromJson (R"({"ambLo":-100,"ambHi":1e9,"arcs":[]})");
        check (std::abs (p.pitchLo - upic::kMinFreq) < 1e-12
               && std::abs (p.pitchHi - upic::kMaxFreq) < 1e-6,
               "an ambitus outside the model's range is pulled back into it");
    }
    {
        const auto p = fromJson (R"({"ambLo":100,"ambHi":100,"arcs":[]})");
        check (std::abs (p.pitchLo - upic::kDefaultPitchLo) < 1e-12
               && std::abs (p.pitchHi - upic::kDefaultPitchHi) < 1e-6,
               "an ambitus that is too narrow falls back to the default one");
    }
    {
        const auto p = fromJson (R"({"spaces":[99,-5,3,0],"arcs":[]})");
        check (p.spaces[0] == 6 && p.spaces[1] == 0 && p.spaces[2] == 3,
               "a space outside the presets is pulled back into them");
    }
    {
        const auto p = fromJson (R"({"spaceTables":[{"space":99,"editable":true,"points":[{"x":0,"hz":256},{"x":1,"hz":1024}]}],"arcs":[]})");
        check (p.spaceDefs[3].editable && p.spaceDefs[0].points.empty(),
               "a drawn table outside the four spaces is put into the last one");
    }

    // --- the ids ----------------------------------------------------------
    {
        const auto p = fromJson (R"({"arcs":[{"id":5,"points":[]},{"id":5,"points":[]}]})");
        check (p.arcs.size() == 2, "both arcs are there");
        if (p.arcs.size() == 2)
        {
            check (p.arcs[0].id == 5, "the first arc of an id keeps it");
            check (p.arcs[1].id != 5 && p.arcs[1].id > 0,
                   "the second arc of an id gets a free one",
                   juce::String (p.arcs[1].id));
        }
        check (p.nextArcId == 6, "the next arc id is derived from the arcs that are there",
               juce::String (p.nextArcId));
    }
    {
        const auto p = fromJson (R"({"nextArcId":10,"arcs":[{"id":5,"points":[]}]})");
        check (p.nextArcId == 10, "the file's next arc id is kept when it is higher");
    }
    {
        const auto p = fromJson (R"({"nextArcId":2,"arcs":[{"id":5,"points":[]}]})");
        check (p.nextArcId == 6, "the file's next arc id is ignored when it is lower");
    }
    {
        const auto p = fromJson (R"({"nextArcId":2000000,"arcs":[{"id":5,"points":[]}]})");
        check (p.nextArcId == 6, "the file's next arc id is ignored when it is out of range");
    }
    {
        int renamed = 0;
        const auto p = decode (juce::JSON::parse (R"({"arcs":[{"id":5,"points":[]},)"
                                                    R"({"id":5,"points":[]}]})"), &renamed);
        check (renamed == 1, "a renamed arc is reported", juce::String (renamed));
        check (p.arcs.size() == 2, "the page still has both arcs");
    }
    {
        int renamed = 0;
        const auto p = decode (juce::JSON::parse (R"({"zones":[{"id":2},{"id":2}]})"), &renamed);
        check (renamed == 1, "a renamed zone is reported", juce::String (renamed));
        check (p.zones.size() == 2 && p.zones[0].id == 2 && p.zones[1].id != 2,
               "the first zone of an id keeps it");
        check (p.nextZoneId == 3, "the next zone id is derived from the zones that are there",
               juce::String (p.nextZoneId));
    }
    {
        int renamed = 0;
        const auto p = decode (juce::JSON::parse (R"({"groups":[{"id":2},{"id":2}]})"), &renamed);
        check (renamed == 0, "a renamed group is not reported - the counter is read before it",
               juce::String (renamed));
        check (p.groups.size() == 2 && p.groups[0].id == 2 && p.groups[1].id != 2,
               "the first group of an id keeps it");
        check (p.nextGroupId == 3, "the next group id is derived from the groups that are there",
               juce::String (p.nextGroupId));
    }

    // --- the caps ---------------------------------------------------------
    {
        juce::String json = R"({"arcs":[)";
        for (int i = 1; i <= upic::limits::kMaxArcsPerPage + 1; ++i)
        {
            if (i > 1) json += ",";
            json += R"({"id":)" + juce::String (i) + R"(,"points":[]})";
        }
        json += "]}";
        const auto p = fromJson (json);
        check ((int) p.arcs.size() == upic::limits::kMaxArcsPerPage,
               "a page cannot carry more arcs than the model's ceiling",
               juce::String (p.arcs.size()));
    }
    {
        juce::String json = R"({"zones":[)";
        for (int i = 1; i <= upic::limits::kMaxSieveZones + 1; ++i)
        {
            if (i > 1) json += ",";
            json += R"({"id":)" + juce::String (i) + "}";
        }
        json += "]}";
        const auto p = fromJson (json);
        check ((int) p.zones.size() == upic::limits::kMaxSieveZones,
               "a page cannot carry more zones than the model's ceiling",
               juce::String (p.zones.size()));
    }
    {
        juce::String json = R"({"groups":[)";
        for (int i = 1; i <= upic::limits::kMaxGroupsPerPage + 1; ++i)
        {
            if (i > 1) json += ",";
            json += R"({"id":)" + juce::String (i) + "}";
        }
        json += "]}";
        const auto p = fromJson (json);
        check ((int) p.groups.size() == upic::limits::kMaxGroupsPerPage,
               "a page cannot carry more groups than the model's ceiling",
               juce::String (p.groups.size()));
    }

    // --- the group's membership -------------------------------------------
    {
        const auto p = fromJson (R"({"arcs":[{"id":1,"points":[]},{"id":2,"points":[]}],)"
                                 R"("groups":[{"id":1,"arcs":[1,2,99]}]})");
        check (p.groups.size() == 1, "the group is there");
        if (p.groups.size() == 1)
            check (p.groups[0].arcIds.size() == 2
                   && p.groups[0].arcIds[0] == 1 && p.groups[0].arcIds[1] == 2,
                   "a group keeps only the arcs that are on the page");
    }
    {
        const auto p = fromJson (R"({"arcs":[{"id":1,"points":[]}],)"
                                 R"("groups":[{"id":1,"arcs":[1,1,1]}]})");
        check (p.groups.size() == 1 && p.groups[0].arcIds.size() == 1,
               "a group names an arc only once");
    }
    {
        const auto p = fromJson (R"({"arcs":[{"id":1,"points":[]}],)"
                                 R"("groups":[{"id":1,"arcs":[1]},{"id":2,"arcs":[1]}]})");
        check (p.groups.size() == 2, "both groups are there");
        if (p.groups.size() == 2)
            check (p.groups[0].arcIds.size() == 1 && p.groups[1].arcIds.empty(),
                   "an arc belongs to the first group that names it");
    }

    // --- the round trip ---------------------------------------------------
    {
        upic::Page p;
        p.durationSec = 12.5;
        p.tempoBpm = 96.0;
        p.pitchLo = 40.0;
        p.pitchHi = 8000.0;
        p.spaces = { 2, 0, 0, 0 };
        p.warp = 0.25;
        p.arcs.push_back (anArc (1, 1.0, 220.0));
        p.arcs.push_back (anArc (2, 2.0, 440.0));
        p.nextArcId = 3;
        upic::SieveZone z;
        z.id = 1; z.t0 = 0.0; z.t1 = 2.0; z.f0 = 100.0; z.f1 = 200.0;
        p.zones.push_back (z);
        p.nextZoneId = 2;
        p.groups.push_back (aGroup (1, { 1, 2 }));
        p.nextGroupId = 2;

        const auto back = decode (encode (p));
        check (std::abs (back.durationSec - 12.5) < 1e-9
               && std::abs (back.tempoBpm - 96.0) < 1e-9
               && std::abs (back.warp - 0.25) < 1e-9,
               "the page's own numbers come back");
        check (std::abs (back.pitchLo - 40.0) < 1e-9
               && std::abs (back.pitchHi - 8000.0) < 1e-6,
               "the page's ambitus comes back");
        check (back.spaces[0] == 2 && back.spaces[1] == 0,
               "the page's spaces come back");
        check (back.arcs.size() == 2
               && back.arcs[0].id == 1 && back.arcs[1].id == 2
               && std::abs (back.arcs[0].points[0].t - 1.0) < 1e-9
               && std::abs (back.arcs[0].points[0].freq - 220.0) < 1e-6,
               "the page's arcs come back");
        check (back.nextArcId == 3, "the page's next arc id comes back");
        check (back.zones.size() == 1 && back.zones[0].id == 1
               && std::abs (back.zones[0].t1 - 2.0) < 1e-9
               && std::abs (back.zones[0].f1 - 200.0) < 1e-6,
               "the page's zones come back");
        check (back.groups.size() == 1 && back.groups[0].id == 1
               && back.groups[0].arcIds.size() == 2,
               "the page's groups come back");
        check (back.nextGroupId == 2, "the page's next group id comes back");
    }
    {
        // A page that is written and read twice is the same page: the
        // format is a fixed point, not a one-way trip.
        upic::Page p;
        p.arcs.push_back (anArc (1, 1.0, 220.0));
        p.nextArcId = 2;
        p.groups.push_back (aGroup (1, { 1 }));
        const auto once = encode (p);
        const auto twice = encode (decode (once));
        check (names (once).joinIntoString (",") == names (twice).joinIntoString (","),
               "a page written twice has the same keys",
               names (once).joinIntoString (",") + " / " + names (twice).joinIntoString (","));
    }
}
