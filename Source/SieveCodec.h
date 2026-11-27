#pragma once

//==============================================================================
// R2.11: the sieve zone's file format - the edge.
//
// The sieve zone is the model's data (Source/Sieve.h): a translucent
// magnetic region on the page - where it is, how big it is, its pitch
// sieve, its time grid and its stickiness. How a zone reaches a piece file,
// and how it comes back, is a different thing: this is the edge, and the
// JUCE value types live here.
//
// The bodies are the members the zone used to have, body for body. The
// property names, their order, the optional keys a default zone does not
// write, the clamps a crafted file gets, the order independence of the
// times and the frequencies and the folding of the remainders into the
// modulus are the file format itself, and they did not change by a
// character.
//
// The zone's behaviour stays in Source/Sieve.h: the containment test, the
// effective stickiness and the quantisation that uses the zones.
#include <JuceHeader.h>

#include <vector>

#include "PieceLimits.h"
#include "Sieve.h"
#include "UpicConstants.h"

namespace upic
{
    // The sieve zone as a piece file carries it: its id, its time, its
    // frequency range, its modulus, its remainders and - when they carry
    // something - its time grid and its stickiness.
    inline juce::var sieveZoneToVar (const SieveZone& z)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", z.id);
        o->setProperty("t0", juce::jmin(z.t0, z.t1));
        o->setProperty("t1", juce::jmax(z.t0, z.t1));
        o->setProperty("f0", juce::jmin(z.f0, z.f1));
        o->setProperty("f1", juce::jmax(z.f0, z.f1));
        o->setProperty("m", z.m);
        juce::Array<juce::var> ra;
        for (int r : z.remainders) ra.add(r);
        o->setProperty("r", ra);
        if (z.timeGrid > 0) o->setProperty("timeGrid", z.timeGrid);
        if (z.stickiness >= 0.0) o->setProperty("stick", z.stickiness);
        return juce::var(o);
    }

    // The sieve zone as a piece file carries it, read back. A file is
    // untrusted input: every number is clamped against the model's limits,
    // and the remainders are folded into the modulus the zone ended up
    // with.
    inline SieveZone sieveZoneFromVar (const juce::var& v)
    {
        SieveZone z;
        z.id = (int) v.getProperty("id", 1);
        z.t0 = limits::clampFinite((double) v.getProperty("t0", 0.0), 0.0, kMaxPageSeconds);
        z.t1 = limits::clampFinite((double) v.getProperty("t1", 0.0), 0.0, kMaxPageSeconds);
        z.f0 = limits::clampFinite((double) v.getProperty("f0", kMinFreq), kMinFreq, kMaxFreq);
        z.f1 = limits::clampFinite((double) v.getProperty("f1", kMaxFreq), kMinFreq, kMaxFreq);
        z.m = juce::jlimit(2, 120, (int) v.getProperty("m", 12));
        z.timeGrid = limits::clampFinite((double) v.getProperty("timeGrid", 0.0), 0.0, 64.0);
        {
            const double st = (double) v.getProperty("stick", -1.0);
            z.stickiness = (st >= 0.0) ? limits::clampFinite(st, 0.0, 1.0) : -1.0;
        }
        std::vector<int> rs;
        if (auto* arr = v.getProperty("r", juce::var()).getArray())
            for (auto& rv : *arr)
            {
                if ((int) rs.size() >= limits::kMaxSieveRemainders) break;
                rs.push_back((((int) rv % z.m) + z.m) % z.m);
            }
        z.remainders = std::move(rs);
        return z;
    }
}
