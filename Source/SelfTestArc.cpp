#include <JuceHeader.h>
#include "SelfTest.h"

#include <cmath>
#include <limits>

#include "Arc.h"
#include "PieceLimits.h"
#include "UpicConstants.h"

//==============================================================================
// R2.2: the arc - its transformations, its interpolation and its codec.
//
// Arc.h is the model's central type: the drawn pitch curve. It carries the
// arc's data (the points, the material, the settings), the four classical
// transformations of a drawn curve, the interpolation the engine reads per
// sample, and the `juce::var` serialization that writes it into a piece.
//
// R2.2 moves the serialization out of the class - the codec edge, where the
// plan keeps the JUCE value types - so that Arc.h itself can be free of the
// GUI. This suite is the contract that move has to keep: it pins what the
// arc does, so the move can be checked against something more precise than
// the golden render.
//
// The suite reaches the arc through the domain's own headers. It builds no
// window, no component tree and no engine: the arc's rules are the model's
// rules.

namespace
{
    // The suite talks to the codec through these two helpers, so the move
    // that renames the codec's entry points touches the suite in one place
    // only - the checks themselves stay exactly as they are.
    juce::var encode (const upic::Arc& a) { return a.toVar(); }
    upic::Arc decode (const juce::var& v) { return upic::Arc::fromVar (v); }
}

void upic::runArcChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the arc a model starts with --------------------------------------
    {
        const upic::Arc a;
        check (a.id == 0 && a.waveformId == 0 && a.envelopeId == 0,
               "a new arc has no id and no material");
        check (a.intensity == 0.8f,
               "a new arc is at the default intensity", juce::String (a.intensity, 2));
        check (! a.muted && a.mode == 0 && a.sampleId == 1,
               "a new arc is an oscillator on the first sample slot");
        check (a.refFreq == 261.63,
               "a new arc's unison is C4", juce::String (a.refFreq, 2));
        check (a.waveUse == upic::Arc::waveAsOscillator,
               "a new arc is only the timbre");
        check (a.modRate == 1.0 && a.modDepth == 0.0,
               "a new arc has no modulation");
        check (a.pmRatio == 2.0 && a.pmIndex == 1.5 && a.pmOps == 2,
               "a new arc is a 2-operator phase modulator");
        check (a.stepDivisions == 0,
               "a new arc is played in the continuum");
        check (a.amplitudeTableId == -1 && a.modulatorArcId == -1,
               "a new arc has no table and no modulator");
        check (a.channelId == 0 && a.space == 0,
               "a new arc is on the main channel, in the page's own space");
        check (a.pitchMode == -1 && ! a.reversed,
               "a new arc follows the tab and plays forward");
        check (a.points.empty() && a.startTime() == 0.0 && a.endTime() == 0.0,
               "a new arc has no points and no span");
    }

    // --- the span ----------------------------------------------------------
    {
        upic::Arc a;
        a.points = { { 0.0, 440.0, 1.0f }, { 2.0, 880.0, 0.5f } };
        check (a.startTime() == 0.0 && a.endTime() == 2.0,
               "the span is the first and the last point");
    }

    // --- the transposition (the pitch mirror's other half) -----------------
    {
        upic::Arc a;
        a.points = { { 0.0, 440.0, 1.0f }, { 1.0, 880.0, 1.0f } };
        a.refFreq = 261.63;
        a.transpose (12.0);
        check (std::abs (a.points[0].freq - 880.0) < 1e-9
               && std::abs (a.points[1].freq - 1760.0) < 1e-9,
               "a transposition moves every drawn pitch by the semitones",
               juce::String (a.points[0].freq, 1) + " / "
               + juce::String (a.points[1].freq, 1) + " Hz");
        check (a.refFreq == 261.63,
               "a transposition leaves the material's unison where it is");

        upic::Arc none;
        none.points = { { 0.0, 440.0, 1.0f } };
        none.transpose (0.0);
        check (none.points[0].freq == 440.0,
               "a transposition of zero semitones changes nothing");

        upic::Arc empty;
        empty.transpose (12.0);
        check (empty.points.empty(),
               "a transposition of an empty arc changes nothing");

        upic::Arc top;
        top.points = { { 0.0, upic::kMaxFreq, 1.0f } };
        top.transpose (12.0);
        check (top.points[0].freq == upic::kMaxFreq,
               "a transposed pitch stays inside the model's range");
    }

    // --- the inversion (the pitch mirror) ----------------------------------
    {
        upic::Arc a;
        a.points = { { 0.0, 440.0, 1.0f }, { 1.0, 880.0, 1.0f } };
        a.invertPitch();
        check (std::abs (a.points[0].freq - 880.0) < 1e-6
               && std::abs (a.points[1].freq - 440.0) < 1e-6,
               "an inversion mirrors the contour about the arc's own centre",
               juce::String (a.points[0].freq, 1) + " / "
               + juce::String (a.points[1].freq, 1) + " Hz");
        a.invertPitch();
        check (std::abs (a.points[0].freq - 440.0) < 1e-6
               && std::abs (a.points[1].freq - 880.0) < 1e-6,
               "an inversion is its own inverse");

        upic::Arc one;
        one.points = { { 0.0, 440.0, 1.0f } };
        one.invertPitch();
        check (one.points[0].freq == 440.0,
               "a single-point arc is not inverted");
    }

    // --- the retrograde (the time mirror) ----------------------------------
    {
        upic::Arc a;
        a.points = { { 0.0, 220.0, 1.0f }, { 1.0, 880.0, 1.0f }, { 2.0, 440.0, 1.0f } };
        a.retrograde();
        check (a.points.size() == 3
               && a.points[0].t == 0.0 && a.points[1].t == 1.0 && a.points[2].t == 2.0,
               "a retrograde turns the curve back inside its own span");
        check (a.points[0].freq == 440.0 && a.points[2].freq == 220.0,
               "a retrograde reads the contour backwards");
        a.retrograde();
        check (a.points[0].t == 0.0 && a.points[0].freq == 220.0,
               "a retrograde is its own inverse");

        upic::Arc one;
        one.points = { { 0.0, 440.0, 1.0f } };
        one.retrograde();
        check (one.points[0].t == 0.0,
               "a single-point arc is not retrograde");
    }

    // --- the time scale ----------------------------------------------------
    {
        upic::Arc stretch;
        stretch.points = { { 1.0, 440.0, 1.0f }, { 2.0, 880.0, 1.0f } };
        stretch.scaleTime (2.0);
        check (stretch.points[0].t == 1.0 && stretch.points[1].t == 3.0,
               "a time scale stretches the curve from its own start",
               juce::String (stretch.points[1].t, 2) + " s");

        upic::Arc squeeze;
        squeeze.points = { { 1.0, 440.0, 1.0f }, { 2.0, 880.0, 1.0f } };
        squeeze.scaleTime (0.5);
        check (squeeze.points[0].t == 1.0 && squeeze.points[1].t == 1.5,
               "a time scale compresses the curve from its own start",
               juce::String (squeeze.points[1].t, 2) + " s");

        upic::Arc none;
        none.points = { { 1.0, 440.0, 1.0f }, { 2.0, 880.0, 1.0f } };
        none.scaleTime (0.0);
        check (none.points[1].t == 2.0,
               "a time scale of zero changes nothing");
        none.scaleTime (std::numeric_limits<double>::quiet_NaN());
        check (none.points[1].t == 2.0,
               "a time scale that is not a number changes nothing");

        upic::Arc longArc;
        longArc.points = { { 0.0, 440.0, 1.0f }, { upic::kMaxPageSeconds, 880.0, 1.0f } };
        longArc.scaleTime (4.0);
        check (longArc.points[1].t == upic::kMaxPageSeconds,
               "a stretched curve stays inside the page");
    }

    // --- the interpolation the engine reads per sample ---------------------
    {
        upic::Arc a;
        a.points = { { 0.0, 440.0, 0.2f }, { 1.0, 880.0, 0.8f } };
        check (std::abs (a.freqAt (0.5) - std::sqrt (440.0 * 880.0)) < 1e-6,
               "the pitch is interpolated in the log domain",
               juce::String (a.freqAt (0.5), 4) + " Hz");
        check (a.freqAt (-1.0) == 440.0,
               "before the first point the pitch is the first point's");
        check (a.freqAt (2.0) == 880.0,
               "after the last point the pitch is the last point's");

        const upic::Arc empty;
        check (empty.freqAt (1.0) == 440.0,
               "an empty arc sounds the middle of the range");

        check (std::abs (a.lifeAt (0.0) - 0.0f) < 1e-6f
               && std::abs (a.lifeAt (0.5) - 0.5f) < 1e-6f
               && std::abs (a.lifeAt (1.0) - 1.0f) < 1e-6f,
               "the life runs from 0 to 1 over the arc's own span");
        check (a.lifeAt (-1.0) == 0.0f && a.lifeAt (9.0) == 1.0f,
               "the life is clamped outside the span");
        check (empty.lifeAt (1.0) == 0.0f,
               "an empty arc has no life");

        check (std::abs (a.intensityAt (0.5) - 0.5f) < 1e-6f,
               "the pressure is interpolated linearly");
        check (a.intensityAt (-1.0) == 0.2f && a.intensityAt (2.0) == 0.8f,
               "the pressure is held outside the span");
        check (empty.intensityAt (1.0) == 1.0f,
               "an empty arc has no pressure");
    }

    // --- the cursor the audio thread walks with ----------------------------
    {
        upic::Arc a;
        a.points = { { 0.0, 440.0, 1.0f }, { 1.0, 660.0, 1.0f },
                     { 2.0, 880.0, 1.0f }, { 3.0, 1320.0, 1.0f } };

        upic::Arc::PitchCursor c;
        check (std::abs (a.freqAt (2.5, c) - a.freqAt (2.5)) < 1e-12,
               "the cursor answers like the plain lookup");
        check (std::abs (a.freqAt (0.5, c) - a.freqAt (0.5)) < 1e-12,
               "the cursor follows the time backwards");
        check (std::abs (a.freqAt (1.5, c) - a.freqAt (1.5)) < 1e-12,
               "the cursor follows the time forwards");
        check (std::abs (a.intensityAt (1.5, c) - a.intensityAt (1.5)) < 1e-6f,
               "the cursor reads the pressure as well");
    }

    // --- the codec: the round trip ----------------------------------------
    {
        upic::Arc a;
        a.id = 7; a.waveformId = 3; a.envelopeId = 4;
        a.intensity = 0.35f; a.muted = true; a.mode = 2; a.sampleId = 5;
        a.refFreq = 123.45;
        a.waveUse = upic::Arc::waveAlsoFrequency; a.modRate = 3.5; a.modDepth = 0.25;
        a.pmRatio = 3.0; a.pmIndex = 4.5; a.pmIndexEnv = 2; a.pmFeedback = 0.5;
        a.pmOps = 4;
        a.pmR2 = 1.5; a.pmR3 = 2.5; a.pmR4 = 3.5;
        a.pmI2 = 4.5; a.pmI3 = 5.5; a.pmI4 = 6.5;
        a.stepDivisions = 12; a.amplitudeTableId = 2; a.modulatorArcId = 9;
        a.modTarget = 1; a.modArcDepth = 0.75; a.modAlign = 1;
        a.channelId = 3; a.space = 2;
        a.pitchMode = 1; a.reversed = true;
        a.points = { { 0.25, 440.0, 0.5f }, { 1.75, 880.0, 1.0f } };

        const upic::Arc back = decode (encode (a));
        check (back.id == 7 && back.waveformId == 3 && back.envelopeId == 4,
               "the arc's references travel with the piece");
        check (std::abs (back.intensity - 0.35f) < 1e-6f && back.muted,
               "the arc's level and its mute travel with the piece");
        check (back.mode == 2 && back.sampleId == 5
               && std::abs (back.refFreq - 123.45) < 1e-9,
               "the arc's material travels with the piece");
        check (back.waveUse == upic::Arc::waveAlsoFrequency
               && std::abs (back.modRate - 3.5) < 1e-9
               && std::abs (back.modDepth - 0.25) < 1e-9,
               "the polyagogy travels with the piece");
        check (std::abs (back.pmRatio - 3.0) < 1e-9
               && std::abs (back.pmIndex - 4.5) < 1e-9
               && back.pmIndexEnv == 2
               && std::abs (back.pmFeedback - 0.5) < 1e-9
               && back.pmOps == 4,
               "the phase modulator's settings travel with the piece");
        check (std::abs (back.pmR2 - 1.5) < 1e-9
               && std::abs (back.pmR3 - 2.5) < 1e-9
               && std::abs (back.pmR4 - 3.5) < 1e-9,
               "the phase modulator's ratios travel with the piece");
        check (std::abs (back.pmI2 - 4.5) < 1e-9
               && std::abs (back.pmI3 - 5.5) < 1e-9
               && std::abs (back.pmI4 - 6.5) < 1e-9,
               "the phase modulator's indices travel with the piece");
        check (back.stepDivisions == 12 && back.amplitudeTableId == 2,
               "the discrete space and the amplitude table travel with the piece");
        check (back.modulatorArcId == 9 && back.modTarget == 1
               && std::abs (back.modArcDepth - 0.75) < 1e-9 && back.modAlign == 1,
               "the modulator travels with the piece");
        check (back.channelId == 3 && back.space == 2,
               "the channel and the frequency space travel with the piece");
        check (back.pitchMode == 1 && back.reversed,
               "the per-arc overrides travel with the piece");
        check (back.points.size() == 2
               && std::abs (back.points[0].t - 0.25) < 1e-9
               && std::abs (back.points[1].freq - 880.0) < 1e-9,
               "the drawn points travel with the piece");
        check (std::abs (back.points[0].i - 0.5f) < 1e-6f,
               "the pressure travels with the piece");
    }

    // --- the codec: the defaults of an arc that is not there --------------
    {
        const upic::Arc a = decode (juce::JSON::parse ("{}"));
        check (a.id == 0 && a.points.empty(),
               "an arc that is not there is an empty arc");
        check (a.intensity == 0.8f && ! a.muted && a.mode == 0,
               "an arc that is not there is a plain oscillator");
        check (a.sampleId == 1 && std::abs (a.refFreq - 261.63) < 1e-9,
               "an arc that is not there is on the first slot, at C4");
        check (a.waveUse == upic::Arc::waveAsOscillator
               && a.modRate == 1.0 && a.modDepth == 0.0,
               "an arc that is not there has no modulation");
        check (a.pmRatio == 2.0 && a.pmIndex == 1.5 && a.pmOps == 2,
               "an arc that is not there is a 2-operator modulator");
        check (a.stepDivisions == 0 && a.amplitudeTableId == -1
               && a.modulatorArcId == -1,
               "an arc that is not there is in the continuum, with no table and no modulator");
        check (a.channelId == 0 && a.space == 0 && a.pitchMode == -1
               && ! a.reversed,
               "an arc that is not there is on the main channel, in the page's own space");
    }

    // --- the codec: a crafted file is clamped, not obeyed ------------------
    {
        const upic::Arc a = decode (juce::JSON::parse (
            "{\"id\":1,\"mode\":99,\"intensity\":5,\"sampleId\":0,\"pmOps\":9,"
            "\"sp\":9,\"pitchMode\":9,\"waveUse\":9,\"modTarget\":9,"
            "\"modAlign\":9,\"channelId\":-5,\"ampTable\":-9,\"mod\":-9,"
            "\"sd\":200,\"refFreq\":1e300,"
            "\"points\":[{\"t\":-5,\"f\":1e300,\"i\":5}]}"));
        check (a.mode == 3,
               "an impossible mode is clamped to the last one");
        check (a.intensity == 1.0f,
               "an out-of-range intensity is clamped");
        check (a.sampleId == 1,
               "a sample slot below the first is clamped");
        check (a.pmOps == 4,
               "an impossible operator count is clamped");
        check (a.space == 3 && a.pitchMode == 2,
               "an impossible space or override is clamped");
        check (a.waveUse == 2 && a.modTarget == 1 && a.modAlign == 1,
               "an impossible modulator setting is clamped");
        check (a.channelId == 0 && a.amplitudeTableId == -1
               && a.modulatorArcId == -1,
               "an impossible id is clamped to the range an id can have");
        check (a.stepDivisions == 99,
               "an impossible number of divisions is clamped");
        check (std::abs (a.refFreq - 4000.0) < 1e-9,
               "an out-of-range unison is clamped");
        check (a.points.size() == 1 && a.points[0].t == 0.0
               && a.points[0].freq == upic::kMaxFreq && a.points[0].i == 1.0f,
               "an out-of-range point is clamped");
    }

    // --- the codec: the points are read in time order ---------------------
    {
        const upic::Arc a = decode (juce::JSON::parse (
            "{\"points\":[{\"t\":2,\"f\":440},{\"t\":0,\"f\":880},{\"t\":1,\"f\":220}]}"));
        check (a.points.size() == 3 && a.points[0].t == 0.0
               && a.points[1].t == 1.0 && a.points[2].t == 2.0,
               "the points are sorted by the time they are read");
    }

    // --- the codec: the file's shape ---------------------------------------
    // The keys are part of the file format: an old piece, a Python-written
    // piece and the v0.29 fixture all use them. A rename would be a silent
    // loss of everything a piece carries.
    {
        const auto plain = encode (upic::Arc());
        check (plain.hasProperty ("id") && plain.hasProperty ("points"),
               "the arc's identity and its points are always written");
        check (! plain.hasProperty ("sd") && ! plain.hasProperty ("ampTable")
               && ! plain.hasProperty ("mod"),
               "a default arc does not write the optional keys");
        check (! plain.hasProperty ("modTarget")
               && ! plain.hasProperty ("modArcDepth")
               && ! plain.hasProperty ("modAlign"),
               "a default arc does not write the modulator keys");
        check (! plain.hasProperty ("ch") && ! plain.hasProperty ("sp")
               && ! plain.hasProperty ("pitchMode")
               && ! plain.hasProperty ("reversed"),
               "a default arc does not write the channel, the space or the overrides");

        upic::Arc named;
        named.stepDivisions = 12; named.amplitudeTableId = 3;
        named.modulatorArcId = 4; named.modTarget = 1;
        named.modArcDepth = 0.25; named.modAlign = 1;
        named.channelId = 2; named.space = 3;
        named.pitchMode = 1; named.reversed = true;
        const auto full = encode (named);
        check (full.hasProperty ("sd") && full.hasProperty ("ampTable")
               && full.hasProperty ("mod"),
               "the discrete space, the table and the modulator keep their short keys");
        check (full.hasProperty ("modTarget") && full.hasProperty ("modArcDepth")
               && full.hasProperty ("modAlign"),
               "the modulator keeps its keys");
        check (full.hasProperty ("ch") && full.hasProperty ("sp"),
               "the channel and the frequency space keep their short keys");
        check (full.hasProperty ("pitchMode") && full.hasProperty ("reversed"),
               "the per-arc overrides keep their keys");

        upic::Arc plainPoint;
        plainPoint.points = { { 0.0, 440.0, 1.0f } };
        const auto plainPointsVar = encode (plainPoint).getProperty ("points", juce::var());
        const auto* points = plainPointsVar.getArray();
        check (points != nullptr && points->size() == 1,
               "the points are written as an array");
        if (points != nullptr && points->size() == 1)
        {
            check ((*points)[0].hasProperty ("t") && (*points)[0].hasProperty ("f"),
                   "a point keeps its short keys");
            check (! (*points)[0].hasProperty ("i"),
                   "the default pressure is not written");
        }

        upic::Arc pressedPoint;
        pressedPoint.points = { { 0.0, 440.0, 0.4f } };
        const auto pressedPointsVar = encode (pressedPoint).getProperty ("points", juce::var());
        const auto* pressed = pressedPointsVar.getArray();
        check (pressed != nullptr && pressed->size() == 1
               && (*pressed)[0].hasProperty ("i"),
               "a pressure that is not the default is written");
    }
}
