#pragma once

//==============================================================================
// R2.2: the arc's codec edge.
//
// The arc is the model's central type: the drawn pitch curve. It is data
// and rules - its points, its material, its settings, the four classical
// transformations of a drawn curve and the interpolation the engine reads
// per sample. It carried one thing that was neither: the file format.
// `juce::var toVar()` and `static Arc fromVar()` put JUCE's value types
// inside the class, and with them the JUCE umbrella header.
//
// The codec moves out of the class and becomes two free functions. The
// class keeps the data and the rules; this file keeps the edge where the
// domain meets JUCE's value types.
//
// The plan keeps the JUCE value types at the codec edge (R2): the piece
// format is JSON, and `juce::var` is the value type that carries it. What
// changes is which side of the edge the JUCE types live on - not the
// format itself.
//
// The bodies are the old members, body for body: the property names, their
// order and the clamps are the file format, and the suite
// (Source/SelfTestArc.cpp) pins them.
#include <JuceHeader.h>

#include "Arc.h"

namespace upic
{
    // The arc as a piece file carries it: its identity, its material, its
    // settings and its points. The optional keys are left out when they
    // are the defaults, so a plain piece stays as small as it was.
    inline juce::var arcToVar (const Arc& a)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", a.id); o->setProperty("waveformId", a.waveformId);
        o->setProperty("envelopeId", a.envelopeId); o->setProperty("intensity", a.intensity);
        o->setProperty("muted", a.muted);
        o->setProperty("mode", a.mode);
        o->setProperty("pmRatio", a.pmRatio);
        o->setProperty("pmIndex", a.pmIndex);
        o->setProperty("pmIndexEnv", a.pmIndexEnv);
        o->setProperty("pmFeedback", a.pmFeedback);
        o->setProperty ("pmOps", a.pmOps); o->setProperty ("pmR2", a.pmR2); o->setProperty ("pmR3", a.pmR3); o->setProperty ("pmR4", a.pmR4);
        o->setProperty ("pmI2", a.pmI2); o->setProperty ("pmI3", a.pmI3); o->setProperty ("pmI4", a.pmI4);
        o->setProperty("sampleId", a.sampleId);
        o->setProperty("refFreq", a.refFreq);
        // v1.31 (U1): the polyagogy settings travel with the piece
        o->setProperty("waveUse", a.waveUse);
        o->setProperty("modRate", a.modRate);
        o->setProperty("modDepth", a.modDepth);
        // v1.32: the discrete frequency space travels with the piece
        if (a.stepDivisions > 0) o->setProperty("sd", a.stepDivisions);
        if (a.amplitudeTableId >= 0) o->setProperty ("ampTable", a.amplitudeTableId);
        if (a.modulatorArcId >= 0) o->setProperty ("mod", a.modulatorArcId);
        if (a.modTarget != 0) o->setProperty ("modTarget", a.modTarget);
        if (std::abs (a.modArcDepth - 0.5) > 1e-9) o->setProperty ("modArcDepth", a.modArcDepth);
        if (a.modAlign != 0) o->setProperty ("modAlign", a.modAlign);
        if (a.channelId > 0) o->setProperty ("ch", a.channelId);
        // v1.33: the frequency space the arc is played in
        if (a.space != 0) o->setProperty("sp", a.space);
        // v1.33.4 (H7): the per-arc overrides change the sound, so they
        // travel with the piece (and with the host state)
        if (a.pitchMode >= 0) o->setProperty("pitchMode", a.pitchMode);
        if (a.reversed) o->setProperty("reversed", true);
        juce::Array<juce::var> arr;
        for (auto& p : a.points)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty("t", p.t); po->setProperty("f", p.freq);
            if (p.i != 1.0f) po->setProperty("i", p.i); // omit default (keeps files compact)
            arr.add(juce::var(po));
        }
        o->setProperty("points", arr);
        return juce::var(o);
    }

    // The arc as a piece file carries it, read back. A file is untrusted
    // input: every number is clamped against the model's limits, and the
    // points come back in the order the engine reads them in.
    inline Arc arcFromVar (const juce::var& v)
    {
        Arc a;
        a.id = (int) v.getProperty("id", 0);
        a.waveformId = (int) v.getProperty("waveformId", 0);
        a.envelopeId = (int) v.getProperty("envelopeId", 0);
        a.intensity = limits::clampIntensity((float)(double) v.getProperty("intensity", 0.8));
        a.muted = (bool) v.getProperty("muted", false);
        // absent in old / Python-written files -> oscillator mode, as before
        a.mode = juce::jlimit(0, 3, (int) v.getProperty("mode", 0));
        a.pmRatio    = limits::clampFinite((double) v.getProperty("pmRatio", 2.0), 0.25, 16.0);
        a.pmIndex    = limits::clampFinite((double) v.getProperty("pmIndex", 1.5), 0.0, 100.0);
        a.pmIndexEnv = (int)    v.getProperty("pmIndexEnv", 0);
        a.pmFeedback  = limits::clampFinite((double) v.getProperty("pmFeedback", 0.0), -1.0, 1.0);
        a.pmOps = juce::jlimit (2, 4, (int) v.getProperty ("pmOps", 2));
        a.pmR2  = limits::clampFinite((double) v.getProperty ("pmR2", 2.0), 0.25, 16.0);  a.pmR3 = limits::clampFinite((double) v.getProperty ("pmR3", 3.0), 0.25, 16.0);  a.pmR4 = limits::clampFinite((double) v.getProperty ("pmR4", 5.0), 0.25, 16.0);
        a.pmI2  = limits::clampFinite((double) v.getProperty ("pmI2", 1.0), 0.0, 100.0);  a.pmI3 = limits::clampFinite((double) v.getProperty ("pmI3", 1.0), 0.0, 100.0);  a.pmI4 = limits::clampFinite((double) v.getProperty ("pmI4", 1.0), 0.0, 100.0);
        a.sampleId = juce::jlimit(1, kMaxSampleSlots, (int) v.getProperty("sampleId", 1));
        a.refFreq = limits::clampFinite((double) v.getProperty("refFreq", 261.63), 20.0, 4000.0);
        // v1.31 (U1): the polyagogy settings (absent in older pieces ->
        // the classic oscillator)
        a.waveUse = juce::jlimit (0, 2, (int) v.getProperty ("waveUse", 0));
        a.modRate  = limits::clampFinite ((double) v.getProperty ("modRate", 1.0), 0.01, 100.0);
        a.modDepth = limits::clampFinite ((double) v.getProperty ("modDepth", 0.0), 0.0, 1.0);
        // v1.32: the discrete frequency space (absent in older pieces ->
        // the continuum)
        a.stepDivisions = juce::jlimit (0, 99, (int) v.getProperty ("sd", 0));
        a.amplitudeTableId = juce::jlimit (-1, limits::kMaxItemId,
                                        (int) v.getProperty ("ampTable", -1));
        a.modulatorArcId = juce::jlimit (-1, limits::kMaxItemId,
                                       (int) v.getProperty ("mod", -1));
        a.modTarget = juce::jlimit (0, 1, (int) v.getProperty ("modTarget", 0));
        a.modArcDepth = limits::clampFinite ((double) v.getProperty ("modArcDepth", 0.5), 0.0, 1.0);
        a.modAlign = juce::jlimit (0, 1, (int) v.getProperty ("modAlign", 0));
        a.channelId = juce::jlimit (0, limits::kMaxItemId, (int) v.getProperty ("ch", 0));
        // v1.33: the frequency space (absent in older pieces -> the
        // page's own range, which is the identity)
        a.space = juce::jlimit (0, 3, (int) v.getProperty ("sp", 0));
        // v1.33.4 (H7): the per-arc overrides (absent in older pieces ->
        // follow the tab's setting, play forward)
        a.pitchMode = juce::jlimit (-1, 2, (int) v.getProperty ("pitchMode", -1));
        a.reversed = (bool) v.getProperty ("reversed", false);
        if (auto* arr = v.getProperty("points", juce::var()).getArray())
            for (auto& pv : *arr)
            {
                if ((int) a.points.size() >= limits::kMaxPointsPerArc) break;
                upic::ArcPoint pt;
                pt.t = limits::clampFinite((double) pv.getProperty("t", 0), 0.0, kMaxPageSeconds);
                pt.freq = limits::clampFinite((double) pv.getProperty("f", 440), kMinFreq, kMaxFreq);
                pt.i = limits::clampIntensity((float)(double) pv.getProperty("i", 1.0));
                a.points.push_back(pt);
            }
        // stable: a staircase's corners share a time, and their order is
        // the direction of the jump
        std::stable_sort(a.points.begin(), a.points.end(), [](auto& x, auto& y){ return x.t < y.t; });
        return a;
    }
}
