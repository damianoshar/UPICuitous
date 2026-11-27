#pragma once

//==============================================================================
// R2.13: the frequency space definition's file format - the edge.
//
// The frequency space definition is the model's data (Source/FreqSpace.h):
// the page's four frequency spaces - a preset, a drawn table, a staircase
// and an inversion. How a definition reaches a piece file, and how it comes
// back, is a different thing: this is the edge, and the JUCE value types
// live here.
//
// The bodies are the members the definition used to have, body for body.
// The property names, their order, the optional keys a default definition
// does not write, the Hz the points carry, the clamps a crafted file gets,
// the rules a table has to follow and the fact that the space index is
// written but not read are the file format itself, and they did not change
// by a character.
//
// The definition's behaviour stays in Source/FreqSpace.h: the log2
// frequency at a position, the presets, the range of a space and the
// render-time lookup table.
#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "FreqSpace.h"
#include "PieceLimits.h"
#include "UpicConstants.h"

namespace upic
{
    // The table's points as a file carries them: the position on the page's
    // vertical axis and the frequency in Hz.
    inline juce::var freqSpacePointsToVar (const std::vector<TablePoint>& pts)
    {
        juce::Array<juce::var> arr;
        for (const auto& p : pts)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("x", p.x);
            o->setProperty ("hz", std::exp2 ((double) p.log2Hz));
            arr.add (juce::var (o));
        }
        return juce::var (arr);
    }

    // The frequency space definition as a piece file carries it: its space
    // index, its preset, its table and the flags that make it a staircase
    // or an inversion. Only the non-default fields are written, so a piece
    // that never edited a table is byte-for-byte what it was.
    inline juce::var freqSpaceDefToVar (const FreqSpaceDef& d, int index)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("space", index);
        if (d.preset != 0) o->setProperty ("preset", d.preset);
        o->setProperty ("editable", d.editable);
        if (d.stepped) o->setProperty ("stepped", true);
        if (d.stepDivisions > 0) o->setProperty ("divisions", d.stepDivisions);
        if (d.inverted) o->setProperty ("inverted", true);
        if (! d.points.empty()) o->setProperty ("points", freqSpacePointsToVar (d.points));
        return juce::var (o);
    }

    // The frequency space definition as a piece file carries it, read back.
    // A file is untrusted input: every number is clamped against the
    // model's limits, the points come back in order, and a table that is
    // not a curve is dropped.
    inline FreqSpaceDef freqSpaceDefFromVar (const juce::var& v)
    {
        FreqSpaceDef d;
        d.preset = juce::jlimit (0, (int) freqSpacePresets().size() - 1,
                                 (int) v.getProperty ("preset", 0));
        d.editable = (bool) v.getProperty ("editable", false);
        d.stepped = (bool) v.getProperty ("stepped", false);
        d.stepDivisions = juce::jlimit (0, limits::kMaxStepDivisions,
                                        (int) v.getProperty ("divisions", 0));
        d.inverted = (bool) v.getProperty ("inverted", false);
        if (auto* arr = v.getProperty ("points", juce::var()).getArray())
        {
            const double loLog = std::log2 (kMinFreq);
            const double hiLog = std::log2 (kMaxFreq);
            for (auto& pv : *arr)
            {
                if ((int) d.points.size() >= limits::kMaxTablePoints) break;
                TablePoint p;
                p.x = (float) limits::clampFinite ((double) pv.getProperty ("x", 0.0), 0.0, 1.0);
                const double hz = limits::clampFinite ((double) pv.getProperty ("hz", 440.0),
                                                      kMinFreq, kMaxFreq);
                p.log2Hz = (float) limits::clampFinite (std::log2 (juce::jmax (1.0e-6, hz)),
                                                       loLog, hiLog);
                d.points.push_back (p);
            }
            std::sort (d.points.begin(), d.points.end(),
                       [] (const TablePoint& a, const TablePoint& b) { return a.x < b.x; });
        }
        // a table needs two points to be a curve; anything less falls
        // back to the preset rather than producing a constant pitch
        if (d.points.size() < 2) d.editable = false;
        if (! d.editable) d.points.clear();
        return d;
    }
}
