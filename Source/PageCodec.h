#pragma once

//==============================================================================
// R2.4: the page's codec edge.
//
// The page is the model's surface: the arcs, the sieve zones, the four
// frequency spaces, the groups, the ambitus and the page's own time. It
// carried one thing that was neither: the file format. `juce::var toVar()`
// and `static Page fromVar()` put JUCE's value types inside the class,
// and with them the JUCE umbrella header.
//
// The codec moves out of the class and becomes two free functions. The
// class keeps the data and the rules; this file keeps the edge where the
// domain meets JUCE's value types - next to the arc's own edge
// (Source/ArcCodec.h).
//
// The plan keeps the JUCE value types at the codec edge (R2): the piece
// format is JSON, and `juce::var` is the value type that carries it. What
// changes is which side of the edge the JUCE types live on - not the
// format itself.
//
// The bodies are the old members, body for body: the property names, their
// order, the optional keys a default page does not write and the clamps a
// crafted file gets are the file format itself, and the suite
// (Source/SelfTestPageCodec.cpp) pins them.
#include <JuceHeader.h>

#include <algorithm>
#include <vector>

#include "ArcCodec.h"
#include "ArcGroup.h"
#include "FreqSpace.h"
#include "Ids.h"
#include "Page.h"
#include "Sieve.h"

namespace upic
{
    // The page as a piece file carries it: its time, its arcs, its zones,
    // its ambitus, its spaces and its groups. The optional keys are left
    // out when they are the defaults, so a plain page stays as small as it
    // was.
    inline juce::var pageToVar (const Page& p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("duration", p.durationSec);
        if (p.tempoBpm != 120.0) o->setProperty("tempo", p.tempoBpm);
        o->setProperty("nextArcId", p.nextArcId);
        juce::Array<juce::var> arr;
        for (auto& a : p.arcs) arr.add(arcToVar (a));
        o->setProperty("arcs", arr);
        // omitted when empty (old/Python readers never see the key)
        if (! p.zones.empty())
        {
            juce::Array<juce::var> zarr;
            for (auto& z : p.zones) zarr.add(z.toVar());
            o->setProperty("zones", zarr);
            o->setProperty("nextZoneId", p.nextZoneId);
        }
        // v1.33: the ambitus and the four spaces. Omitted when they are
        // the defaults, so an old piece's file is byte-for-byte what it
        // was.
        if (p.pitchLo != kDefaultPitchLo || p.pitchHi != kDefaultPitchHi)
        {
            o->setProperty("ambLo", p.pitchLo);
            o->setProperty("ambHi", p.pitchHi);
        }
        bool anySpace = false;
        for (auto s : p.spaces) if (s != 0) { anySpace = true; break; }
        if (anySpace)
        {
            juce::Array<juce::var> sarr;
            for (auto s : p.spaces) sarr.add((int) s);
            o->setProperty("spaces", sarr);
        }
        // WP1: the drawn tables. Omitted for every space that is still a
        // preset, so an old piece's file is byte-for-byte what it was.
        {
            juce::Array<juce::var> tarr;
            for (int i = 0; i < (int) p.spaceDefs.size(); ++i)
                if (! p.spaceDefs[(size_t) i].isDefault())
                    tarr.add (p.spaceDefs[(size_t) i].toVar (i));
            if (! tarr.isEmpty())
                o->setProperty ("spaceTables", tarr);
        }
        // v1.33: the page's acceleration (omitted when it is zero)
        if (p.warp != 0.0) o->setProperty("warp", p.warp);
        // WP3: the groups. Omitted when the page has none, so an old
        // piece's file is byte-for-byte what it was.
        if (! p.groups.empty())
        {
            juce::Array<juce::var> garr;
            for (auto& g : p.groups) garr.add (g.toVar());
            o->setProperty ("groups", garr);
            o->setProperty ("nextGroupId", p.nextGroupId);
        }
        return juce::var(o);
    }

    // The page as a piece file carries it, read back. A file is untrusted
    // input: every number is clamped against the model's limits, the ids
    // are made unique and the groups are cleaned of the arcs that are not
    // on the page.
    inline Page pageFromVar (const juce::var& v, int* renamedIds = nullptr)
    {
        Page p;
        p.durationSec = limits::clampFinite((double) v.getProperty("duration", 30.0),
                                          kMinPageSeconds, kMaxPageSeconds);
        p.tempoBpm    = limits::clampFinite((double) v.getProperty("tempo", 120.0), 20.0, 400.0);
        // v1.33.4 (L13): the file's own "next id" is only a suggestion -
        // it is checked against the arcs that are there (and against the
        // range an id can have) further down.
        const int fileNextArcId = (int) v.getProperty("nextArcId", 1);
        if (auto* arr = v.getProperty("arcs", juce::var()).getArray())
            for (auto& av : *arr)
            {
                if ((int) p.arcs.size() >= limits::kMaxArcsPerPage) break;
                p.arcs.push_back(arcFromVar (av));
            }
        // v1.33.4 (L13): the arc ids are the key of the per-arc maps (the
        // selection, the drag map, the arc list). A file with two arcs
        // carrying the same id made the selection and the delete hit the
        // wrong arc, so the ids are made unique here - the first arc of
        // an id keeps it, a duplicate or an impossible id gets a free
        // one.
        int renamed = uniquifyIds (p.arcs, 1);
        // The next id is derived from the arcs that are there: a file
        // could hand out an id that is already in use, or one so large
        // that the counter overflowed. The file's value is still used
        // when it is sane and higher (the ids it handed out earlier may
        // still be in the user's notes).
        p.nextArcId = 1;
        for (auto& a : p.arcs)
            p.nextArcId = juce::jmax(p.nextArcId, a.id + 1);
        if (fileNextArcId > p.nextArcId && fileNextArcId <= limits::kMaxItemId)
            p.nextArcId = fileNextArcId;
        // zones absent in old / Python-written files -> no zones, as before
        const int fileNextZoneId = (int) v.getProperty("nextZoneId", 1);
        if (auto* zarr = v.getProperty("zones", juce::var()).getArray())
            for (auto& zv : *zarr)
            {
                if ((int) p.zones.size() >= limits::kMaxSieveZones) break;
                p.zones.push_back(SieveZone::fromVar(zv));
            }
        renamed += uniquifyIds (p.zones, 1);
        p.nextZoneId = 1;
        for (auto& z : p.zones)
            p.nextZoneId = juce::jmax(p.nextZoneId, z.id + 1);
        if (fileNextZoneId > p.nextZoneId && fileNextZoneId <= limits::kMaxItemId)
            p.nextZoneId = fileNextZoneId;
        if (renamedIds != nullptr) *renamedIds += renamed;
        // v1.33: the ambitus and the four spaces (absent in old pieces ->
        // the whole canvas range, and the page's own space)
        p.pitchLo = limits::clampFinite((double) v.getProperty("ambLo", kDefaultPitchLo),
                                        kMinFreq, kMaxFreq);
        p.pitchHi = limits::clampFinite((double) v.getProperty("ambHi", kDefaultPitchHi),
                                        kMinFreq, kMaxFreq);
        if (p.pitchHi <= p.pitchLo * 1.0001)
        {
            p.pitchLo = kDefaultPitchLo;
            p.pitchHi = kDefaultPitchHi;
        }
        if (auto* sarr = v.getProperty("spaces", juce::var()).getArray())
            for (size_t i = 0; i < p.spaces.size() && i < (size_t) sarr->size(); ++i)
                p.spaces[i] = juce::jlimit (0, (int) freqSpacePresets().size() - 1,
                                           (int) (*sarr)[(int) i]);
        // WP1: the drawn tables (absent in old pieces -> presets only)
        if (auto* tarr = v.getProperty ("spaceTables", juce::var()).getArray())
            for (auto& tv : *tarr)
            {
                const int idx = juce::jlimit (0, 3, (int) tv.getProperty ("space", 0));
                p.spaceDefs[(size_t) idx] = FreqSpaceDef::fromVar (tv);
            }
        // v1.33: the page's acceleration (absent in old pieces -> a
        // straight line)
        p.warp = limits::clampFinite ((double) v.getProperty ("warp", 0.0), -0.9, 0.9);
        // WP3: the groups (absent in old pieces -> none). Membership is
        // cleaned here: only arcs that are on the page, no duplicates,
        // and one group per arc - the first group of an arc keeps it.
        const int fileNextGroupId = (int) v.getProperty ("nextGroupId", 1);
        if (auto* garr = v.getProperty ("groups", juce::var()).getArray())
            for (auto& gv : *garr)
            {
                if ((int) p.groups.size() >= limits::kMaxGroupsPerPage) break;
                p.groups.push_back (ArcGroup::fromVar (gv, (int) p.groups.size() + 1));
            }
        renamed += uniquifyIds (p.groups, 1);
        p.nextGroupId = 1;
        for (auto& g : p.groups)
            p.nextGroupId = juce::jmax(p.nextGroupId, g.id + 1);
        if (fileNextGroupId > p.nextGroupId && fileNextGroupId <= limits::kMaxItemId)
            p.nextGroupId = fileNextGroupId;
        {
            std::vector<int> claimed; // the arcs already in an earlier group
            for (auto& g : p.groups)
            {
                std::vector<int> kept;
                for (int aid : g.arcIds)
                {
                    const bool onPage = std::any_of (p.arcs.begin(), p.arcs.end(),
                                                     [aid] (const Arc& a) { return a.id == aid; });
                    if (! onPage) continue;
                    if (std::find (claimed.begin(), claimed.end(), aid) != claimed.end())
                        continue;
                    claimed.push_back (aid);
                    kept.push_back (aid);
                }
                g.arcIds = std::move (kept);
            }
        }
        return p;
    }
}
