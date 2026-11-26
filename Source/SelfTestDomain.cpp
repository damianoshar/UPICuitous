#include <JuceHeader.h>
#include "SelfTest.h"

#include "Arc.h"
#include "Ids.h"
#include "PieceLimits.h"
#include "UpicConstants.h"
#include "UpicMath.h"

//==============================================================================
// R2.1: the domain's numeric core, before it loses JUCE.
//
// The domain's numbers - the units (MIDI to frequency, the page's pitch
// space), the validation (the clamps every untrusted value goes through) and
// the lookups (the segment search the audio thread runs per sample) - are
// spread over four small headers. They include the JUCE umbrella header
// today, for three numeric helpers, and they are the foundation the rest of
// the domain wave stands on.
//
// This suite writes down what those headers do, in the same style as the
// other characterization suites: the relations and the edge cases, not
// golden values. The checks are deliberately about the *rules* (a clamp that
// rejects NaN, a lookup that finds the first point at or after the key, an
// id that is replaced when it cannot be an id), so they stay true when the
// headers lose JUCE and gain their own helpers.
//
// The suite only needs the domain's own headers - no MainComponent, no
// window, no GUI. That is the point of it: the domain's behaviour has to be
// checkable without the app around it.

namespace
{
    /** The id helpers are templates over "something with an id". */
    struct IdItem { int id = 0; };

    double keyOfT (const upic::ArcPoint& p) { return p.t; }
}

void upic::runDomainChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the units: the constants the whole domain agrees on -------------
    check (upic::kMinFreq > 0.0 && upic::kMaxFreq > upic::kMinFreq,
           "the frequency range is a range");
    check (upic::kMaxPageSeconds > 0.0,
           "the page has a positive length");
    check (upic::kDefaultPitchLo > 0.0 && upic::kDefaultPitchHi > upic::kDefaultPitchLo,
           "the default ambitus is a range");

    // --- the units: MIDI and frequency ------------------------------------
    check (std::abs (upic::midiToFreq (69.0) - 440.0) < 1e-9,
           "A4 is 440 Hz", juce::String (upic::midiToFreq (69.0), 4));
    check (std::abs (upic::midiToFreq (81.0) - 880.0) < 1e-9,
           "A5 is 880 Hz", juce::String (upic::midiToFreq (81.0), 4));
    check (std::abs (upic::midiToFreq (57.0) - 220.0) < 1e-9,
           "A3 is 220 Hz", juce::String (upic::midiToFreq (57.0), 4));
    check (std::abs (upic::freqToMidi (440.0) - 69.0) < 1e-9,
           "440 Hz is A4", juce::String (upic::freqToMidi (440.0), 6));
    check (std::abs (upic::freqToMidi (upic::midiToFreq (64.0)) - 64.0) < 1e-9,
           "MIDI to frequency and back is the identity");
    check (upic::freqToMidi (1e12) == upic::freqToMidi (upic::kMaxFreq),
           "a frequency above the range is clamped to the top");
    check (upic::freqToMidi (1e-6) == upic::freqToMidi (upic::kMinFreq),
           "a frequency below the range is clamped to the bottom");

    // --- the units: the pitch space ---------------------------------------
    check (std::abs (upic::freqToNormY (upic::kMinFreq) - 0.0f) < 1e-6f,
           "the lowest frequency is the bottom of the space");
    check (std::abs (upic::freqToNormY (upic::kMaxFreq) - 1.0f) < 1e-6f,
           "the highest frequency is the top of the space");
    check (upic::freqToNormY (440.0f) > upic::freqToNormY (220.0f),
           "the space grows with the pitch");
    check (std::abs (upic::freqToNormY (std::sqrt (upic::kMinFreq * upic::kMaxFreq)) - 0.5f) < 1e-4f,
           "the geometric mean of the range is halfway up the space",
           juce::String (upic::freqToNormY (std::sqrt (upic::kMinFreq * upic::kMaxFreq)), 6));
    check (std::abs (upic::normYToFreq (0.0f) - upic::kMinFreq) < 1e-6,
           "the bottom of the space is the lowest frequency");
    check (std::abs (upic::normYToFreq (1.0f) - upic::kMaxFreq) < 1e-6,
           "the top of the space is the highest frequency");
    check (upic::normYToFreq (-1.0f) == upic::normYToFreq (0.0f),
           "a position below the space is clamped to the bottom");
    check (upic::normYToFreq (2.0f) == upic::normYToFreq (1.0f),
           "a position above the space is clamped to the top");
    check (std::abs (upic::normYToFreq (upic::freqToNormY (440.0f)) - 440.0) < 1e-3,
           "the space and its inverse agree");

    // --- the units: the page's own interval -------------------------------
    {
        const double lo = 200.0, hi = 2000.0;
        check (std::abs (upic::freqToNormYIn (lo, lo, hi) - 0.0f) < 1e-6f
               && std::abs (upic::freqToNormYIn (hi, lo, hi) - 1.0f) < 1e-6f,
               "the page's interval maps its own ends to the ends of the space");
        check (upic::freqToNormYIn (upic::kMinFreq, lo, hi) < 0.0f
               && upic::freqToNormYIn (upic::kMaxFreq, lo, hi) > 1.0f,
               "a pitch outside the page's interval is off the page, not clamped");
        check (std::abs (upic::normYToFreqIn (0.0f, lo, hi) - lo) < 1e-6
               && std::abs (upic::normYToFreqIn (1.0f, lo, hi) - hi) < 1e-6,
               "the page's interval maps the ends of the space back");
        check (upic::normYToFreqIn (-0.5f, lo, hi) == upic::normYToFreqIn (0.0f, lo, hi),
               "the page holds only what is inside it");
        check (upic::normYToFreqIn (1.5f, lo, hi) == upic::normYToFreqIn (1.0f, lo, hi),
               "the page's top is its top");
    }

    // --- the validation: the clamps every untrusted value goes through ----
    check (upic::limits::clampFinite (0.5, 0.0, 1.0) == 0.5,
           "a value inside the range is kept");
    check (upic::limits::clampFinite (-1.0, 0.0, 1.0) == 0.0,
           "a value below the range is clamped up");
    check (upic::limits::clampFinite (2.0, 0.0, 1.0) == 1.0,
           "a value above the range is clamped down");
    check (upic::limits::clampFinite (std::numeric_limits<double>::quiet_NaN (), 0.0, 1.0) == 0.0,
           "a NaN never survives the clamp");
    check (upic::limits::clampFinite (std::numeric_limits<double>::infinity (), 0.0, 1.0) == 0.0,
           "an infinity does not survive the clamp either");
    check (upic::limits::clampFinite (-std::numeric_limits<double>::infinity (), 0.0, 1.0) == 0.0,
           "a negative infinity does not survive the clamp either");
    check (! upic::limits::isFinite (std::numeric_limits<double>::quiet_NaN ())
           && upic::limits::isFinite (1.0),
           "the finite check knows a NaN from a number");
    check (upic::limits::clampIntensity (0.5f) == 0.5f,
           "an intensity inside 0..1 is kept");
    check (upic::limits::clampIntensity (-0.2f) == 0.0f,
           "an intensity below 0 is clamped");
    check (upic::limits::clampIntensity (1.7f) == 1.0f,
           "an intensity above 1 is clamped");
    check (upic::limits::clampIntensity (std::numeric_limits<float>::quiet_NaN ()) == 0.0f,
           "a NaN intensity becomes 0");
    check (upic::limits::clampGain (1.0f) == 1.0f,
           "a gain inside the slider's range is kept");
    check (upic::limits::clampGain (-0.5f) == upic::limits::kMinMasterGain,
           "a gain below the slider is clamped");
    check (upic::limits::clampGain (9.0f) == upic::limits::kMaxMasterGain,
           "a gain above the slider is clamped");
    check (upic::limits::kMaxItemId > 0 && upic::limits::kMaxPages > 0
           && upic::limits::kMaxPointsPerArc > 0,
           "the piece limits are positive");

    // --- the lookups: the segment search the audio thread runs -----------
    {
        std::vector<upic::ArcPoint> points;
        for (int i = 0; i < 4; ++i)
            points.push_back ({ (double) i, 440.0, 1.0f });

        check (upic::findSegment (points, 0.5, keyOfT) == 1,
               "the search finds the first point at or after the key");
        check (upic::findSegment (points, 1.0, keyOfT) == 1,
               "the search finds the point that sits on the key");
        check (upic::findSegment (points, -1.0, keyOfT) == 0,
               "a key before the first point finds the first point");
        check (upic::findSegment (points, 99.0, keyOfT) == 4,
               "a key after the last point finds the end");
        check (upic::findSegment (points, 2.5, keyOfT) == 3,
               "the search lands on the segment that holds the key");

        const std::vector<upic::ArcPoint> empty;
        check (upic::findSegment (empty, 1.0, keyOfT) == 0,
               "the search on an empty list finds nothing");

        upic::SegmentCursor cursor;
        check (upic::followSegment (points, 0.5, keyOfT, cursor) == 1,
               "the cursor finds the same segment as the search");
        check (upic::followSegment (points, 2.5, keyOfT, cursor) == 3,
               "the cursor follows the key forwards");
        check (upic::followSegment (points, 0.5, keyOfT, cursor) == 1,
               "the cursor follows the key backwards");
        check (upic::followSegment (points, 2.5, keyOfT, cursor) == 3,
               "the cursor finds the same segment after going back");
        check (upic::followSegment (empty, 1.0, keyOfT, cursor) == 0,
               "the cursor on an empty list finds nothing");
    }

    // --- the ids: what can be an id, and what replaces one that cannot ----
    {
        std::vector<IdItem> items { { 5 }, { 5 }, { 2 } };
        const int changed = upic::uniquifyIds (items, 0);
        check (changed == 1,
               "one duplicate id is replaced", juce::String (changed));
        check (items[0].id == 5 && items[2].id == 2,
               "the first item of an id keeps it");
        check (items[1].id != 5 && items[1].id != 2,
               "the duplicate gets an id of its own", juce::String (items[1].id));

        std::vector<IdItem> bad { { -1 }, { upic::limits::kMaxItemId + 1 } };
        const int changedBad = upic::uniquifyIds (bad, 0);
        check (changedBad == 2,
               "an impossible id is replaced", juce::String (changedBad));
        check (bad[0].id >= 0 && bad[0].id <= upic::limits::kMaxItemId
               && bad[1].id >= 0 && bad[1].id <= upic::limits::kMaxItemId,
               "the replacement is an id that could be one");

        std::vector<IdItem> good { { 1 }, { 2 } };
        check (upic::uniquifyIds (good, 0) == 0,
               "ids that are already good are left alone");
        check (upic::hasId (good, 2) && ! upic::hasId (good, 3),
               "the id lookup finds an id that is there");

        std::vector<IdItem> bank { { 0 }, { 2 } };
        check (upic::nextBankId (bank, 0) == 1,
               "the next free bank id is the first hole",
               juce::String (upic::nextBankId (bank, 0)));
        check (upic::nextBankId (bank, 1) == 1,
               "the bank id search starts where it is told");
        check (upic::nextBankId (bank, -5) == 1,
               "a negative start is lifted to zero");
        check (upic::nextBankId (std::vector<IdItem> { { 0 } }, 0) == 1,
               "an empty bank starts at the first id");
    }
}
