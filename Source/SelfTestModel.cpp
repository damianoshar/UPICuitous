#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "AllocCounter.h"

//==============================================================================
// The model: the pitch transformations, the frequency spaces, the staircase,
// the acceleration, the digitised wave page, the polyagogy, the sieve, the
// generators and the curve simplification.

void upic::runTransformChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6i. v1.31 (U3): the UPIC's pitch transformations - transpose and
    //     invert. The reverse already exists; these are the pitch ones.
    {
        upic::Arc a;
        a.points = { { 0.0, 220.0 }, { 1.0, 440.0 }, { 2.0, 880.0 } };

        // an octave up doubles every pitch
        a.transpose (12.0);
        check (std::abs (a.points[0].freq - 440.0) < 1e-6
            && std::abs (a.points[1].freq - 880.0) < 1e-6
            && std::abs (a.points[2].freq - 1760.0) < 1e-6,
               "transpose up an octave doubles the drawn pitches",
               juce::String (a.points[0].freq, 1) + " / "
               + juce::String (a.points[1].freq, 1) + " / "
               + juce::String (a.points[2].freq, 1));

        // and back down again
        a.transpose (-12.0);
        check (std::abs (a.points[0].freq - 220.0) < 1e-6
            && std::abs (a.points[2].freq - 880.0) < 1e-6,
               "transpose down an octave is the inverse");

        // the inversion mirrors the contour: a rising line becomes a
        // falling one of the same size, around the same centre
        upic::Arc b;
        b.points = { { 0.0, 220.0 }, { 1.0, 440.0 }, { 2.0, 880.0 } };
        const double centre = std::sqrt (220.0 * 880.0); // the geometric mean
        b.invertPitch();
        check (std::abs (b.points[0].freq - centre * centre / 220.0) < 1e-6
            && std::abs (b.points[2].freq - centre * centre / 880.0) < 1e-6
            && b.points[0].freq > b.points[1].freq
            && b.points[1].freq > b.points[2].freq,
               "invert mirrors the pitch contour",
               juce::String (b.points[0].freq, 1) + " / "
               + juce::String (b.points[1].freq, 1) + " / "
               + juce::String (b.points[2].freq, 1));

        // and it is its own inverse
        b.invertPitch();
        check (std::abs (b.points[0].freq - 220.0) < 1e-6
            && std::abs (b.points[2].freq - 880.0) < 1e-6,
               "inverting twice gives the arc back");
    }

    // 6i-e. v1.31: the time-axis half of the classical transformation
    //      family - retrograde (mirror in time) and augmentation /
    //      diminution (scale in time).
    {
        upic::Arc a;
        a.points = { { 0.0, 220.0 }, { 1.0, 440.0 }, { 2.0, 880.0 } };

        // the retrograde reads backwards inside the arc's own span
        upic::Arc r = a;
        r.retrograde();
        check (std::abs (r.points[0].t - 0.0) < 1e-9
            && std::abs (r.points[2].t - 2.0) < 1e-9,
               "the retrograde keeps the arc's time span",
               juce::String (r.points[0].t, 3) + " .. "
               + juce::String (r.points[2].t, 3));
        check (std::abs (r.points[0].freq - 880.0) < 1e-6
            && std::abs (r.points[2].freq - 220.0) < 1e-6,
               "the retrograde plays the contour backwards",
               juce::String (r.points[0].freq, 1) + " -> "
               + juce::String (r.points[2].freq, 1));
        bool sorted = true;
        for (size_t i = 1; i < r.points.size(); ++i)
            if (r.points[i].t < r.points[i-1].t) sorted = false;
        check (sorted, "the retrograde keeps the points sorted in time");

        // and it is its own inverse
        r.retrograde();
        check (std::abs (r.points[0].freq - 220.0) < 1e-6
            && std::abs (r.points[2].freq - 880.0) < 1e-6,
               "retrograding twice gives the arc back");

        // the augmentation stretches the contour in time
        upic::Arc g = a;
        g.scaleTime (2.0);
        check (std::abs ((g.points[2].t - g.points[0].t) - 4.0) < 1e-6,
               "the augmentation doubles the arc's time span",
               juce::String (g.points[2].t - g.points[0].t, 3) + " s");
        check (std::abs (g.points[1].freq - 440.0) < 1e-6,
               "the augmentation keeps the pitch");

        // and the diminution halves it
        upic::Arc d = a;
        d.scaleTime (0.5);
        check (std::abs ((d.points[2].t - d.points[0].t) - 1.0) < 1e-6,
               "the diminution halves the arc's time span",
               juce::String (d.points[2].t - d.points[0].t, 3) + " s");
    }

    // 6i-f. v1.32: the discrete frequency space - the UPIC's
    //      "discrete/non-discrete" function. The pitch that plays is
    //      moved through a temperament of n equal divisions per octave,
    //      so a glissando is heard as a staircase of discrete steps.
    {
        const double ref = upic::kDefaultRefFreq; // C4

        // the continuum leaves the pitch alone
        check (std::abs (upic::applyTemperament (445.0, 0, ref) - 445.0) < 1e-9,
               "no temperament leaves the pitch alone");

        // a pitch on the chromatic scale stays where it is
        const double a4 = upic::applyTemperament (440.0, 12, ref);
        check (std::abs (a4 - 440.0) < 0.02,
               "the chromatic scale leaves A4 where it is",
               juce::String (a4, 3) + " Hz");

        // a pitch between two steps snaps to the nearest one
        const double snapped = upic::applyTemperament (445.0, 12, ref);
        check (std::abs (snapped - a4) < 0.02,
               "a pitch between two steps snaps to the nearer one",
               juce::String (snapped, 2) + " Hz");

        // a different temperament is a different scale: the diatonic
        // one (7 divisions) puts A4 between two of its steps
        const double seven = upic::applyTemperament (440.0, 7, ref);
        check (std::abs (seven - a4) > 1.0,
               "the diatonic scale moves A4 to another step",
               juce::String (seven, 2) + " Hz");
        check (std::abs (std::log2 (seven / ref) * 7.0
                         - std::round (std::log2 (seven / ref) * 7.0)) < 1e-9,
               "the diatonic scale lands exactly on one of its steps");

        // the glissando becomes a staircase: every step of the sweep
        // lands on the scale, and the sweep visits several steps
        upic::Page plainPage;   // the page's own space: the identity
        upic::Arc g;
        g.points = { { 0.0, 400.0 }, { 1.0, 800.0 } };
        g.stepDivisions = 12;
        const auto plainMap = plainPage.spaceMap (0);
        bool allOnScale = true;
        std::set<double> visited;
        for (int i = 0; i <= 100; ++i)
        {
            const double f = UpicEngine::arcPitch (g, (double) i / 100.0, plainMap);
            const double l = std::log2 (f / ref) * 12.0;
            if (std::abs (l - std::round (l)) > 1e-9) allOnScale = false;
            visited.insert (std::round (l));
        }
        check (allOnScale, "the stepped glissando stays on the scale");
        check (visited.size() > 3,
               "the stepped glissando is heard as several discrete steps",
               juce::String ((int) visited.size()) + " steps");

        // and the continuum version of the same arc is not a staircase
        upic::Arc c = g;
        c.stepDivisions = 0;
        bool anyOffScale = false;
        for (int i = 0; i <= 100; ++i)
        {
            const double f = UpicEngine::arcPitch (c, (double) i / 100.0, plainMap);
            const double l = std::log2 (f / ref) * 12.0;
            if (std::abs (l - std::round (l)) > 1e-9) anyOffScale = true;
        }
        check (anyOffScale, "the continuum glissando is not stepped");

        // the drawing is untouched: the stepping is a property of the
        // sound, not of the drawn curve
        check (std::abs (g.points[0].freq - 400.0) < 1e-9
            && std::abs (g.points[1].freq - 800.0) < 1e-9,
               "the stepping leaves the drawn curve alone");

        // it travels with the piece
        upic::Arc saved = upic::Arc::fromVar (g.toVar());
        check (saved.stepDivisions == 12,
               "the discrete frequency space travels with the piece",
               juce::String (saved.stepDivisions) + " divisions");
        upic::Arc plain = upic::Arc::fromVar (upic::Arc().toVar());
        check (plain.stepDivisions == 0,
               "a piece without it is the continuum");
    }

    // 6i-f2. v1.33: the frequency spaces - the UPIC's four frequency
    //      tables. The page owns four spaces; an arc names the one it
    //      is played in, the way it names its wave or its envelope.
    {
        upic::Page pg;   // the ambitus: 20 Hz - 12 kHz
        const double mid = 2000.0;

        // space 0 IS the page's range: the pitch passes through
        // unchanged, bit for bit (the phase vocoder's bin index is a
        // truncated k*ratio, so an identity mapping must not round-trip
        // through log2/exp2)
        check (pg.placeInSpace (mid, 0) == mid,
               "the page's own space is the identity",
               juce::String (pg.placeInSpace (mid, 0), 6) + " Hz");

        // an octave up: the same drawing, one octave higher
        pg.spaces[1] = 3;   // "An octave up"
        const double up = pg.placeInSpace (mid, 1);
        check (std::abs (up - mid * 2.0) < 1e-6,
               "a space an octave up sounds an octave up",
               juce::String (up, 1) + " Hz");

        // half the range: the intervals are scaled with it
        pg.spaces[2] = 1;   // "Half the range"
        const double half = pg.placeInSpace (mid, 2);
        check (half < mid && half > mid * 0.4 && half < mid * 0.6,
               "half the range compresses the drawing's intervals",
               juce::String (half, 1) + " Hz");
        // the edges of the drawing land on the edges of the space
        double sLo = 0.0, sHi = 0.0;
        pg.spaceRange (2, sLo, sHi);
        check (std::abs (pg.placeInSpace (upic::kMinFreq, 2) - sLo) < 1e-6
            && std::abs (pg.placeInSpace (upic::kMaxFreq, 2) - sHi) < 1e-6,
               "the space's edges are where the drawing's edges land",
               juce::String (sLo, 1) + " - " + juce::String (sHi, 1) + " Hz");

        // the arcs are independent: each names its own space
        upic::Arc a1, a2;
        a1.points = { { 0.0, mid }, { 1.0, mid } };
        a2.points = { { 0.0, mid }, { 1.0, mid } };
        a1.space = 0;
        a2.space = 1;
        const auto m0 = pg.spaceMap (a1.space);
        const auto m1 = pg.spaceMap (a2.space);
        check (std::abs (UpicEngine::arcPitch (a1, 0.5, m0) - mid) < 1e-9
            && std::abs (UpicEngine::arcPitch (a2, 0.5, m1) - mid * 2.0) < 1e-3,
               "two arcs can sit in two spaces at the same time",
               juce::String (UpicEngine::arcPitch (a1, 0.5, m0), 1) + " / "
               + juce::String (UpicEngine::arcPitch (a2, 0.5, m1), 1) + " Hz");

        // the space travels with the piece
        upic::Arc saved = upic::Arc::fromVar (a2.toVar());
        check (saved.space == 1, "the frequency space travels with the piece");
        check (upic::Arc::fromVar (a1.toVar()).space == 0,
               "an arc without a space is in the page's own");
        const auto pageBack = upic::Page::fromVar (pg.toVar());
        check (pageBack.spaces[1] == 3 && pageBack.spaces[2] == 1,
               "the page's four spaces travel with the piece");
    }

    // 6i-f3. v1.33: the ambitus - the interval the page's vertical axis
    //      covers. "The scale of the millimetric surface could be
    //      selected, so that the vertical axis could correspond to any
    //      interval."
    {
        // the mapping is the page's interval, not the whole world
        const double lo = 261.63 / 4.0, hi = 261.63 * 4.0;   // three octaves
        check (std::abs (upic::normYToFreqIn (0.0f, lo, hi) - lo) < 1e-6
            && std::abs (upic::normYToFreqIn (1.0f, lo, hi) - hi) < 1e-6,
               "the page's interval spans its axis");
        check (std::abs (upic::freqToNormYIn (lo, lo, hi) - 0.0f) < 1e-6
            && std::abs (upic::freqToNormYIn (hi, lo, hi) - 1.0f) < 1e-6,
               "the page's interval maps back to its axis");
        // the middle of the axis is the middle of the interval, in log
        // pitch: the geometric mean, not the arithmetic one
        check (std::abs (upic::normYToFreqIn (0.5f, lo, hi)
                         - std::sqrt (lo * hi)) < 1e-6,
               "the middle of the page is the geometric middle",
               juce::String (upic::normYToFreqIn (0.5f, lo, hi), 1) + " Hz");

        // a pitch outside the page is off the page (not clamped onto it)
        check (upic::freqToNormYIn (upic::kMinFreq, lo, hi) < 0.0f
            && upic::freqToNormYIn (upic::kMaxFreq, lo, hi) > 1.0f,
               "a pitch outside the page is off the page");

        // the ambitus travels with the piece
        upic::Page pg;
        pg.pitchLo = lo;
        pg.pitchHi = hi;
        const auto back = upic::Page::fromVar (pg.toVar());
        check (std::abs (back.pitchLo - lo) < 1e-9
            && std::abs (back.pitchHi - hi) < 1e-9,
               "the ambitus travels with the piece",
               juce::String (back.pitchLo, 1) + " - "
               + juce::String (back.pitchHi, 1) + " Hz");
        // and a piece without one is the default interval
        const auto plain = upic::Page::fromVar (upic::Page().toVar());
        check (plain.pitchLo == upic::kDefaultPitchLo
            && plain.pitchHi == upic::kDefaultPitchHi,
               "a piece without an ambitus is the default interval");
    }

    // 6i-f5. v1.33: the range reaches infrasonic and ultrasonic. "The
    //      frequency range of the 1990s UPIC was extended to
    //      infrasonics, starting at 0.01 Hz" (Robindoré), and the
    //      frequency table could span "as large a range as
    //      0.01-22,050 Hz".
    {
        check (upic::kMinFreq <= 0.01 + 1e-9 && upic::kMaxFreq >= 22050.0 - 1e-9,
               "the pitch range reaches infrasonic and ultrasonic",
               juce::String (upic::kMinFreq, 3) + " - "
               + juce::String (upic::kMaxFreq, 0) + " Hz");

        auto renderAt = [&](double f, juce::AudioBuffer<float>& buf) {
            upic::Page pg; pg.durationSec = 4.0;
            upic::Arc a; a.id = 1; a.mode = 0; a.intensity = 1.0f;
            a.points = { { 0.0, f }, { 4.0, f } };
            pg.arcs.push_back (a);
            auto waves = upic::defaultWaveforms();
            std::vector<upic::Envelope> envs;
            upic::Envelope flat; flat.presetFlat(); envs.push_back (flat);
            std::vector<upic::SampleSlot> noSlots;
            buf.setSize (2, 44100 * 4); buf.clear();
            UpicEngine e; e.prepare (44100.0, 8192);
            e.renderPageOffline (pg, waves, envs, noSlots, buf, 44100.0, 1.0f);
        };
        auto crossings = [](const juce::AudioBuffer<float>& buf) {
            int n = 0; bool high = false; bool finite = true;
            for (int i = 44100; i < 88200; ++i)
            {
                const float v = buf.getSample (0, i);
                if (! std::isfinite (v)) { finite = false; continue; }
                if (! high && v > 0.0f) { high = true; ++n; }
                else if (high && v < 0.0f) high = false;
            }
            return std::make_pair (n, finite);
        };

        // an infrasonic arc: one slow swell in a second, not a buzz
        juce::AudioBuffer<float> lo;
        renderAt (0.01, lo);
        const auto loX = crossings (lo);
        check (loX.second && loX.first <= 1,
               "an infrasonic arc is a slow swell",
               juce::String (loX.first) + " crossings in a second");

        // and an ultrasonic one
        juce::AudioBuffer<float> hi;
        renderAt (20000.0, hi);
        const auto hiX = crossings (hi);
        check (hiX.second && hiX.first > 15000,
               "an ultrasonic arc is still a tone",
               juce::String (hiX.first) + " crossings in a second");

        // the middle of the range is untouched
        juce::AudioBuffer<float> mid;
        renderAt (440.0, mid);
        const auto midX = crossings (mid);
        check (midX.second && std::abs (midX.first - 440) < 20,
               "the middle of the range sounds as it always did",
               juce::String (midX.first) + " crossings in a second");
    }

    // 6i-f7. v1.33: the staircase. The sieve's points are the step
    //      corners - the pitch holds until the next step and then
    //      jumps - so the sound is a staircase too.
    {
        upic::Arc a;
        // (0, 261) - (1, 277) - (1, 294) - (2, 294): a step at t = 1
        a.points = { { 0.0, 261.0 }, { 1.0, 277.0 }, { 1.0, 294.0 }, { 2.0, 294.0 } };
        check (std::abs (a.freqAt (0.5) - std::sqrt (261.0 * 277.0)) < 1.0,
               "the staircase holds the pitch between the corners",
               juce::String (a.freqAt (0.5), 1) + " Hz");
        check (std::abs (a.freqAt (1.0) - 277.0) < 1e-6,
               "the staircase is at the old step on the corner",
               juce::String (a.freqAt (1.0), 1) + " Hz");
        check (std::abs (a.freqAt (1.0001) - 294.0) < 1e-6,
               "the staircase jumps to the new step right after it",
               juce::String (a.freqAt (1.0001), 1) + " Hz");
        check (std::abs (a.freqAt (1.5) - 294.0) < 1e-6,
               "the staircase holds the new step");
    }

    // 6i-f6. v1.33: the page's acceleration. The UPIC's page could be
    //      expanded and contracted in time "with the additional option
    //      of acceleration and deceleration" (Robindoré): the page's
    //      own time ran at a varying rate.
    {
        upic::Page pg;
        pg.durationSec = 4.0;
        check (pg.rateAt (0.0) == 1.0 && pg.rateAt (2.0) == 1.0,
               "a straight page runs at its own rate");
        pg.warp = 0.6;
        check (std::abs (pg.rateAt (0.0) - 0.4) < 1e-9
            && std::abs (pg.rateAt (4.0) - 1.6) < 1e-9,
               "an accelerating page starts slow and speeds up",
               juce::String (pg.rateAt (0.0), 2) + " -> "
               + juce::String (pg.rateAt (4.0), 2) + "x");
        pg.warp = -0.6;
        check (std::abs (pg.rateAt (0.0) - 1.6) < 1e-9,
               "a decelerating page starts fast and slows down");

        // it travels with the piece
        upic::Page warped; warped.warp = 0.3;
        check (std::abs (upic::Page::fromVar (warped.toVar()).warp - 0.3) < 1e-9,
               "the acceleration travels with the piece");
        check (upic::Page::fromVar (upic::Page().toVar()).warp == 0.0,
               "a piece without one is a straight line");

        // and the render follows the sweep: a short arc at page time
        // 1 s arrives later when the page starts slowly
        auto clickAt = [&](double warp) {
            upic::Page p; p.durationSec = 4.0; p.warp = warp;
            upic::Arc a; a.id = 1; a.mode = 0; a.intensity = 1.0f;
            a.points = { { 0.9, 1000.0 }, { 1.1, 1000.0 } };
            p.arcs.push_back (a);
            auto waves = upic::defaultWaveforms();
            std::vector<upic::Envelope> envs;
            upic::Envelope flat; flat.presetFlat(); envs.push_back (flat);
            std::vector<upic::SampleSlot> noSlots;
            juce::AudioBuffer<float> buf (2, 44100 * 6);
            UpicEngine e; e.prepare (44100.0, 8192);
            e.renderPageOffline (p, waves, envs, noSlots, buf, 44100.0, 1.0f);
            int peak = -1; float best = 0.0f;
            for (int i = 0; i < buf.getNumSamples(); ++i)
                if (std::abs (buf.getSample (0, i)) > best)
                { best = std::abs (buf.getSample (0, i)); peak = i; }
            return (double) peak / 44100.0;
        };
        const double straight = clickAt (0.0);
        const double swept = clickAt (0.6);
        check (std::abs (straight - 1.0) < 0.15,
               "a straight page plays the arc at its own time",
               juce::String (straight, 2) + " s");
        check (swept > straight + 0.4,
               "an accelerating page delays the arc",
               juce::String (straight, 2) + " s -> " + juce::String (swept, 2) + " s");
    }

    // 6i-b. v1.31 (U4): the digitised wave page - a take becomes one
    //      loop of the oscillator.
    {
        // a sine over four cycles, resampled into one cycle of 1024
        juce::AudioBuffer<float> take (1, 4096);
        for (int i = 0; i < 4096; ++i)
            take.setSample (0, i, 0.5f * (float) std::sin (
                2.0 * juce::MathConstants<double>::pi * 4.0 * i / 4096.0));

        const auto w = upic::waveFromTake (take, 42, "take");
        check (w.id == 42 && w.name == "take",
               "the digitised wave keeps its id and name");
        check ((int) w.samples.size () == upic::kWaveSize,
               "the digitised wave is one cycle of the wavetable",
               juce::String ((int) w.samples.size ()));
        float peak = 0.0f;
        for (auto s : w.samples) peak = juce::jmax (peak, std::abs (s));
        check (peak > 0.9f && peak <= 1.0f,
               "the digitised wave is normalised",
               juce::String (peak, 3));

        // the loop is seamless: the last sample meets the first
        const float first = w.samples.front();
        const float last  = w.samples.back();
        check (std::abs (first - last) < 0.05f,
               "the digitised wave loops without a step",
               juce::String (first, 3) + " -> " + juce::String (last, 3));
    }

    // 6i-c. v1.31 (U2): the micro-time end of the duration range. The
    //      real UPIC's page ran from a few milliseconds to 12 minutes.
    {
        check (upic::kMinPageSeconds <= 0.01
            && upic::kMaxPageSeconds >= 700.0,
               "the page duration range reaches micro-time and macro-time",
               juce::String (upic::kMinPageSeconds, 3) + " s .. "
               + juce::String (upic::kMaxPageSeconds, 0) + " s");

        // a millisecond-scale page survives the parser
        const auto pg = upic::Page::fromVar (juce::JSON::parse (
            "{\"duration\":0.02,\"arcs\":[{\"id\":1,\"points\":["
            "{\"t\":0,\"f\":220},{\"t\":0.015,\"f\":440}]}]}"));
        check (std::abs (pg.durationSec - 0.02) < 1e-9,
               "a millisecond-scale page loads",
               juce::String (pg.durationSec, 4) + " s");
        check (pg.arcs.size () == 1 && pg.arcs[0].points.size () == 2
            && pg.arcs[0].points[1].t <= 0.02,
               "the arcs of a millisecond-scale page keep their times",
               juce::String (pg.arcs[0].points[1].t, 4));

        // and a 12-minute page too
        const auto longPg = upic::Page::fromVar (juce::JSON::parse (
            "{\"duration\":900,\"arcs\":[{\"id\":1,\"points\":["
            "{\"t\":0,\"f\":220},{\"t\":700,\"f\":440}]}]}"));
        check (std::abs (longPg.durationSec - 720.0) < 1e-9,
               "a 12-minute page is clamped to the top of the range",
               juce::String (longPg.durationSec, 1) + " s");
        check (longPg.arcs[0].points[1].t <= 720.0,
               "an arc beyond the top of the range is clamped",
               juce::String (longPg.arcs[0].points[1].t, 1));
    }

    // 6i-d. v1.31 (U1): the polyagogy. The same drawn curve can be the
    //      timbre AND a control signal - an amplitude LFO or a frequency
    //      LFO. The sound has to change when the arc's wave is put to
    //      that second use.
    {
        upic::Page pg; pg.durationSec = 4.0;
        upic::Arc a; a.id = 1; a.mode = 0; a.intensity = 1.0f;
        a.refFreq = 220.0;
        a.points = { { 0.0, 220.0 }, { 4.0, 220.0 } };
        pg.arcs.push_back (a);
        upic::Envelope flat; flat.presetFlat();
        std::vector<upic::Envelope> envs { flat };
        auto waves = upic::defaultWaveforms();

        // render the arc plain, then with its own wave as an amplitude
        // LFO, and compare the two
        auto renderArc = [&] (const upic::Arc& arc, juce::AudioBuffer<float>& buf)
        {
            upic::Page p2 = pg; p2.arcs[0] = arc;
            UpicEngine e; e.prepare (44100.0, 8192);
            buf.setSize (1, 44100);
            buf.clear();
            e.renderPageOffline (p2, waves, envs, {}, buf, 44100.0, 1.0f);
        };

        juce::AudioBuffer<float> plainBuf (1, 44100);
        renderArc (a, plainBuf);
        auto peakOf = [] (const juce::AudioBuffer<float>& b)
        {
            float pk = 0.0f;
            for (int i = 0; i < b.getNumSamples(); ++i)
                pk = juce::jmax (pk, std::abs (b.getSample (0, i)));
            return pk;
        };
        const float plain = peakOf (plainBuf);
        check (plain > 0.01f, "the plain oscillator arc sounds",
               juce::String (plain, 4));

        // the amplitude LFO: the same wave now shapes the loudness, so
        // the signal is no longer the same - and it tremolos (the
        // loudness in one eighth of a second differs from the next)
        juce::AudioBuffer<float> ampBuf (1, 44100);
        upic::Arc modAmp = a;
        modAmp.waveUse = upic::Arc::waveAlsoAmplitude;
        modAmp.modRate = 4.0;   // 4 sweeps a second
        modAmp.modDepth = 1.0;
        renderArc (modAmp, ampBuf);

        double maxDiff = 0.0;
        for (int i = 0; i < plainBuf.getNumSamples(); ++i)
            maxDiff = juce::jmax (maxDiff, (double) std::abs (
                plainBuf.getSample (0, i) - ampBuf.getSample (0, i)));
        check (maxDiff > 0.01,
               "the wave as an amplitude LFO changes the sound",
               juce::String (maxDiff, 4));

        auto slicePeak = [&] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            float pk = 0.0f;
            for (int i = from; i < to; ++i) pk = juce::jmax (pk, std::abs (b.getSample (0, i)));
            return pk;
        };
        // the loudest and the quietest 10 ms window of the first second:
        // a full-depth tremolo swings the gain from 0 to 2
        float loudest = 0.0f, quietest = 1e9f;
        for (int from = 0; from + 441 <= ampBuf.getNumSamples(); from += 441)
        {
            const float pk = slicePeak (ampBuf, from, from + 441);
            loudest = juce::jmax (loudest, pk);
            quietest = juce::jmin (quietest, pk);
        }
        check (loudest > quietest * 1.5f,
               "the amplitude LFO tremolos the arc",
               juce::String (quietest, 4) + " .. " + juce::String (loudest, 4));

        // the frequency LFO: the same wave now moves the pitch, so the
        // waveform itself changes (the peak stays the same - it is a
        // vibrato)
        juce::AudioBuffer<float> freqBuf (1, 44100);
        upic::Arc modFreq = a;
        modFreq.waveUse = upic::Arc::waveAlsoFrequency;
        modFreq.modRate = 4.0;
        modFreq.modDepth = 1.0;
        renderArc (modFreq, freqBuf);

        double maxDiffF = 0.0;
        for (int i = 0; i < plainBuf.getNumSamples(); ++i)
            maxDiffF = juce::jmax (maxDiffF, (double) std::abs (
                plainBuf.getSample (0, i) - freqBuf.getSample (0, i)));
        check (maxDiffF > 0.01,
               "the wave as a frequency LFO changes the sound",
               juce::String (maxDiffF, 4));

        // and the settings travel with the piece
        upic::Arc saved = upic::Arc::fromVar (modAmp.toVar());
        check (saved.waveUse == upic::Arc::waveAlsoAmplitude
            && std::abs (saved.modRate - 4.0) < 1e-6
            && std::abs (saved.modDepth - 1.0) < 1e-6,
               "the polyagogy settings survive a piece round trip",
               juce::String (saved.waveUse) + " / "
               + juce::String (saved.modRate, 2) + " / "
               + juce::String (saved.modDepth, 2));
    }

}

void upic::runModelMathChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 7. pen pressure -> per-point intensity curve
    {
        upic::Arc a;
        a.points = { {0.0, 440.0, 1.0f}, {1.0, 440.0, 0.0f}, {2.0, 440.0, 1.0f} };
        const float i05 = a.intensityAt(0.5);
        const float i10 = a.intensityAt(1.0);
        const float ib  = a.intensityAt(-1.0); // before start -> front value
        const float ia  = a.intensityAt(5.0);  // after end -> back value
        check(std::abs(i05 - 0.5f) < 1e-5f && std::abs(i10 - 0.0f) < 1e-5f,
              "pressure intensityAt linear interp",
              juce::String(i05, 3) + "/" + juce::String(i10, 3));
        check(std::abs(ib - 1.0f) < 1e-6f && std::abs(ia - 1.0f) < 1e-6f,
              "pressure intensityAt clamps outside arc");

        // pressure 0 everywhere => silence
        upic::Page pg; pg.durationSec = 1.0;
        upic::Arc z; z.id = 1; z.intensity = 1.0f;
        z.points = { {0.0, 440.0, 0.0f}, {1.0, 440.0, 0.0f} };
        pg.arcs.push_back(z);
        juce::AudioBuffer<float> zb(2, 44100); zb.clear();
        UpicEngine e1; e1.prepare(44100.0, 8192);
        auto waves = upic::defaultWaveforms();
        auto envs = upic::defaultEnvelopes();
        std::vector<upic::SampleSlot> noSlots;
        e1.renderPageOffline(pg, waves, envs, noSlots, zb, 44100.0, 1.0f);
        double zpeak = 0.0;
        for (int i = 0; i < zb.getNumSamples(); ++i)
            zpeak = juce::jmax(zpeak, (double) std::abs(zb.getSample(0, i)));
        check(zpeak < 1e-6, "pressure 0 => silence", juce::String(zpeak, 8));

        // JSON round-trip of point pressure (old files: absent -> 1.0)
        auto back = upic::Page::fromVar(juce::JSON::parse(juce::JSON::toString(pg.toVar(), false)));
        check(! back.arcs.empty() && ! back.arcs[0].points.empty()
              && std::abs(back.arcs[0].points[0].i - 0.0f) < 1e-6f,
              "json round-trip point pressure (i)");
        auto legacy = upic::Page::fromVar(juce::JSON::parse(
            "{\"duration\":10,\"arcs\":[{\"id\":1,\"points\":[{\"t\":0,\"f\":440}]}]}"));
        check(! legacy.arcs.empty() && legacy.arcs[0].points[0].i == 1.0f,
              "legacy point without i defaults to 1");
    }

    // 8. sieve quantizer (Xenakis) on the MIDI-note axis
    {
        auto closeTo = [](double a, double b){ return std::abs(a - b) < 0.5; };
        check(closeTo(upic::sieveQuantizeHz(440.0, 12, {9}), 440.0),
              "sieve on-grid pitch unchanged (m12 r9 = A)");
        const double off = upic::sieveQuantizeHz(450.0, 12, {9});
        check(closeTo(off, 440.0), "sieve snaps to nearest allowed pitch",
              juce::String(off, 2) + " Hz (expect 440)");
        const double major = upic::sieveQuantizeHz(262.0, 12, {0,2,4,5,7,9,11});
        check(closeTo(major, 261.63), "sieve major scale -> C4", juce::String(major, 3) + " Hz");
        check(upic::sieveQuantizeHz(300.0, 12, {}) == 300.0,
              "sieve empty remainders = identity");
        check(upic::sieveQuantizeHz(300.0, 0, {1}) == 300.0,
              "sieve m < 2 = identity");
    }

    // 9. page sequencer order
    {
        check(upic::seqNextPage(0, 3, true) == 1, "seq advances page 0 -> 1");
        check(upic::seqNextPage(2, 3, true) == 0, "seq wraps last -> first when looping");
        check(upic::seqNextPage(2, 3, false) == -1, "seq stops after last page");
        check(upic::seqNextPage(0, 1, true) == 0, "seq single page wraps to itself");
    }

}

void upic::runSieveChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 11. magnetic snap helpers: stickiness, time grid, click-free stop ramp
    {
        check(upic::stickify(450.0, 440.0, 0.0) == 450.0, "stickify 0 = free");
        check(upic::stickify(450.0, 440.0, 1.0) == 440.0, "stickify 1 = magnetic");
        check(std::abs(upic::stickify(450.0, 440.0, 0.5) - 445.0) < 1e-9, "stickify 0.5 = midpoint");
        check(std::abs(upic::timeGridSnap(1.13, 4.0) - 1.25) < 1e-9, "time grid 1.13 @4/s -> 1.25",
              juce::String(upic::timeGridSnap(1.13, 4.0), 4));
        check(upic::timeGridSnap(2.0, 0.0) == 2.0, "time grid off = identity");
        check(std::abs(upic::timeGridSnap(1.0, 8.0) - 1.0) < 1e-9, "time grid on-step unchanged");

        check(upic::stopFadeGain(0.05, 0.03) == 1.0, "stop fade before window = 1");
        check(std::abs(upic::stopFadeGain(0.015, 0.03) - 0.5) < 1e-9, "stop fade midpoint = 0.5");
        check(upic::stopFadeGain(0.0, 0.03) == 0.0, "stop fade at end = 0");
        check(upic::stopFadeGain(-1.0, 0.03) == 0.0, "stop fade past end = 0");
        bool mono = true;
        double prev = 1.1;
        for (int i = 0; i <= 100; ++i)
        {
            const double g = upic::stopFadeGain(0.03 * (1.0 - i / 100.0), 0.03);
            mono = mono && g <= prev + 1e-12;
            prev = g;
        }
        check(mono, "stop fade ramp monotonic");
    }

    // 12. sieve preset library (UPISketch-style examples)
    {
        const auto ps = upic::sievePresets();
        check(ps.size() >= 8, "sieve presets >= 8", juce::String((int) ps.size()));
        bool valid = ! ps.empty();
        bool unique = true;
        for (size_t i = 0; i < ps.size(); ++i)
        {
            valid = valid && ps[i].m >= 2;
            for (size_t j = i + 1; j < ps.size(); ++j)
                unique = unique && ps[i].name != ps[j].name;
            for (int r : ps[i].remainders) valid = valid && r >= 0 && r < ps[i].m;
        }
        check(valid, "sieve presets well-formed (m>=2, 0<=r<m)");
        check(unique, "sieve preset names unique");
        bool wt = false;
        for (auto& p : ps)
            wt = wt || (p.m == 12 && p.remainders == std::vector<int>({ 0,2,4,6,8,10 }));
        check(wt, "preset whole-tone = m12 {0,2,4,6,8,10}");
        bool chrom = false;
        for (auto& p : ps)
            chrom = chrom || (p.name == "Chromatic" && p.m == 12
                              && (int) p.remainders.size() == 12);
        check(chrom, "preset Chromatic allows all twelve semitones");
    }

    // 13. sieve zones: containment, JSON, unified quantizePoint precedence
    {
        upic::SieveZone z;
        z.id = 7; z.t0 = 5; z.t1 = 10; z.f0 = 400; z.f1 = 800; // reversed corners allowed
        z.m = 12; z.remainders = { 0,1,2,3,4,5,6,7,8,9,10,11 }; z.timeGrid = 8;
        check(z.contains(7.5, 500.0), "zone contains interior point");
        check(z.contains(5.0, 400.0), "zone contains corners (inclusive)");
        check(! z.contains(4.999, 500.0), "zone excludes outside point");
        std::vector<upic::SieveZone> zones { z };
        check(upic::zoneAt(zones, 6.0, 500.0) != nullptr, "zoneAt finds zone");
        check(upic::zoneAt(zones, 1.0, 500.0) == nullptr, "zoneAt misses outside");

        z.stickiness = 0.4;
        auto zj = upic::SieveZone::fromVar(juce::JSON::parse(juce::JSON::toString(z.toVar(), false)));
        check(zj.id == 7 && std::abs(zj.t0 - 5.0) < 1e-9 && std::abs(zj.f1 - 800.0) < 1e-9
              && zj.m == 12 && zj.remainders == z.remainders
              && std::abs(zj.timeGrid - 8.0) < 1e-9
              && std::abs(zj.stickiness - 0.4) < 1e-9, "zone JSON round-trip");
        {
            // a zone written before per-zone stickiness follows the global
            auto legacy = upic::SieveZone::fromVar(juce::JSON::parse(
                "{\"id\":9,\"t0\":0,\"t1\":1,\"f0\":100,\"f1\":200,\"m\":12,\"r\":[0]}"));
            check(std::abs(legacy.stickiness + 1.0) < 1e-9,
                  "a legacy zone follows the global stick");
        }

        upic::Page pz; pz.durationSec = 20; pz.zones.push_back(z); pz.nextZoneId = 8;
        auto pb = upic::Page::fromVar(juce::JSON::parse(juce::JSON::toString(pz.toVar(), false)));
        check(pb.zones.size() == 1 && pb.nextZoneId == 8, "page zones JSON round-trip");
        auto oldPg = upic::Page::fromVar(juce::JSON::parse("{\"duration\":10,\"arcs\":[]}"));
        check(oldPg.zones.empty() && oldPg.nextZoneId == 1, "legacy page has no zones");

        // zone sieve beats global sieve; global grid applies outside the zone
        const std::vector<int> globalR { 0, 7 };
        double tSec = 6.0, f = 450.0; // inside the chromatic zone
        upic::quantizePoint(true, true, 12, globalR, 4.0, 1.0, zones, upic::defaultTuning(), tSec, f);
        check(std::abs(f - 440.0) < 0.5, "zone sieve overrides global (450 -> 440)",
              juce::String(f, 2));
        check(std::abs(tSec - 6.0) < 1e-9, "zone time grid 8/s keeps 6.0");
        tSec = 2.13; f = 450.0;       // outside -> global sieve {0,7} + global grid 4/s
        upic::quantizePoint(true, true, 12, globalR, 4.0, 1.0, zones, upic::defaultTuning(), tSec, f);
        check(std::abs(f - 392.0) < 0.6, "global sieve outside zone (450 -> 392)",
              juce::String(f, 2));
        check(std::abs(tSec - 2.25) < 1e-9, "global time grid snaps 2.13 -> 2.25",
              juce::String(tSec, 4));
        tSec = 0.0; f = 450.0;        // chromatic snap softened by stickiness 0.5
        upic::quantizePoint(true, false, 12, { 0 }, 0.0, 0.5, {}, upic::defaultTuning(), tSec, f);
        check(std::abs(f - 445.0) < 0.6, "stickiness 0.5 half-snaps (450 -> ~445)",
              juce::String(f, 2));

        // v1.33.2: per-zone stickiness. The zone's own value wins inside
        // it; -1 means "follow the global stick" (old pieces).
        {
            upic::SieveZone zs;
            zs.t0 = 0; zs.t1 = 10; zs.f0 = 100; zs.f1 = 1000;
            zs.m = 12; zs.remainders = { 0 }; zs.timeGrid = 0; zs.stickiness = 0.0;
            std::vector<upic::SieveZone> zz { zs };
            tSec = 5.0; f = 450.0;
            upic::quantizePoint(true, true, 12, { 0, 7 }, 0.0, 1.0, zz, upic::defaultTuning(), tSec, f);
            check(std::abs(f - 450.0) < 1e-9,
                  "zone stick 0 leaves the pitch free inside the zone",
                  juce::String(f, 2));
            zz[0].stickiness = -1.0;
            tSec = 5.0; f = 450.0;
            upic::quantizePoint(true, true, 12, { 0, 7 }, 0.0, 1.0, zz, upic::defaultTuning(), tSec, f);
            check(std::abs(upic::freqToMidi(f) - 72.0) < 0.05,
                  "zone stick -1 follows the global stick (450 -> C5)",
                  juce::String(f, 2));
        }
    }

    // 14. freehand curve simplification (RDP): fewer points, shape kept
    {
        std::vector<upic::ArcPoint> pts;
        juce::Random jr(77);
        const int N = 300;
        for (int i = 0; i <= N; ++i)
        {
            const double tSec = (double) i / N * 10.0;
            const double f = 440.0 * std::pow(2.0, 0.8 * std::sin(tSec))
                           * (1.0 + (jr.nextFloat() - 0.5f) * 0.01);
            pts.push_back({ tSec, f, 1.0f });
        }
        const auto orig = pts;
        upic::simplifyArcPoints(pts, 0.01);
        check(pts.size() < orig.size() / 2, "simplify halves dense stroke",
              juce::String((int) pts.size()) + " of " + juce::String((int) orig.size()));
        check(! pts.empty() && pts.front().t == orig.front().t
              && pts.front().freq == orig.front().freq
              && pts.back().t == orig.back().t
              && pts.back().freq == orig.back().freq,
              "simplify preserves endpoints");

        const double t0 = orig.front().t, tSpan = juce::jmax(1e-9, orig.back().t - t0);
        double lmin = 1e18, lmax = -1e18;
        for (auto& q : orig) { const double l = std::log2(q.freq); lmin = std::min(lmin, l); lmax = std::max(lmax, l); }
        const double lSpan = juce::jmax(1e-9, lmax - lmin);
        auto segDist = [](double px, double py, double ax, double ay, double bx, double by){
            const double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
            if (len2 < 1e-18) return std::hypot(px - ax, py - ay);
            const double u = juce::jlimit(0.0, 1.0, ((px - ax) * dx + (py - ay) * dy) / len2);
            return std::hypot(px - (ax + u * dx), py - (ay + u * dy));
        };
        double maxDev = 0.0;
        for (auto& q : orig)
        {
            const double px = (q.t - t0) / tSpan, py = (std::log2(q.freq) - lmin) / lSpan;
            double best = 1e18;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                const double ax = (pts[i-1].t - t0) / tSpan, ay = (std::log2(pts[i-1].freq) - lmin) / lSpan;
                const double bx = (pts[i].t - t0) / tSpan,   by = (std::log2(pts[i].freq) - lmin) / lSpan;
                best = std::min(best, segDist(px, py, ax, ay, bx, by));
            }
            maxDev = std::max(maxDev, best);
        }
        check(maxDev <= 0.021, "simplify deviation <= 2*eps", juce::String(maxDev, 4));
    }

    // 15. procedural arc generation (nUPIC-style generators)
    {
        upic::GenParams gp; gp.t0 = 0.0; gp.t1 = 8.0; gp.f0 = 90.0; gp.f1 = 3000.0;
        auto structural = [&](const std::vector<upic::Arc>& arcs, int minArcs){
            bool ok = (int) arcs.size() >= minArcs;
            for (auto& a : arcs)
            {
                ok = ok && a.points.size() >= 2;
                double lastT = -1.0;
                for (auto& p : a.points)
                {
                    ok = ok && p.t >= gp.t0 - 1e-9 && p.t <= gp.t1 + 1e-9;
                    ok = ok && p.t > lastT; // strictly increasing
                    ok = ok && p.freq >= upic::kMinFreq - 1e-6 && p.freq <= upic::kMaxFreq + 1e-6;
                    ok = ok && p.freq == p.freq; // no NaN
                    lastT = p.t;
                }
            }
            return ok;
        };
        juce::Random r1(4242);
        auto stoch = upic::genStochasticArborescence(gp, r1);
        check(structural(stoch, 3), "gen stochastic arborescence (>=3 arcs)",
              juce::String((int) stoch.size()) + " arcs");

        juce::Random r2(4242);
        auto conv = upic::genConvergentArborescence(gp, r2);
        check(structural(conv, 4), "gen convergent arborescence (>=4 arcs)",
              juce::String((int) conv.size()) + " arcs");
        const double target = 0.5 * (std::log2(gp.f0) + std::log2(gp.f1));
        bool convOk = ! conv.empty();
        for (auto& a : conv)
            convOk = convOk && a.points.size() >= 2
                  && std::abs(std::log2(a.points.back().freq) - target) < 1e-6;
        check(convOk, "convergent arcs end on the shared target pitch");

        juce::Random r3(4242);
        auto ech = upic::genEchelon(gp, r3);
        check(structural(ech, 4), "gen echelon construction (>=4 ranks)",
              juce::String((int) ech.size()) + " arcs");

        juce::Random r4(4242);
        auto spine = upic::genOrdinalSpine(gp, r4);
        check(structural(spine, 4), "gen ordinal spine + branches (>=4 arcs)",
              juce::String((int) spine.size()) + " arcs");

        juce::Random d1(99), d2(99);
        auto A = upic::genStochasticArborescence(gp, d1);
        auto B = upic::genStochasticArborescence(gp, d2);
        bool same = A.size() == B.size() && ! A.empty();
        for (size_t i = 0; same && i < A.size(); ++i)
        {
            same = A[i].points.size() == B[i].points.size();
            for (size_t j = 0; same && j < A[i].points.size(); ++j)
                same = A[i].points[j].t == B[i].points[j].t
                    && A[i].points[j].freq == B[i].points[j].freq;
        }
        check(same, "generators deterministic for a fixed seed");
    }

    // 16. per-arc entry/exit edge fade: square wave (worst case) starts/ends silent
    {
        upic::Waveform sq(99, "Square", juce::Colours::white);
        sq.makeSquare();
        std::vector<upic::Waveform> waves { sq };
        auto envs = upic::defaultEnvelopes();
        upic::Page pg; pg.durationSec = 1.0;
        upic::Arc a; a.id = 1; a.waveformId = 99; a.intensity = 1.0f;
        a.points = { { 0.2, 440.0 }, { 0.8, 440.0 } };
        pg.arcs.push_back(a);
        juce::AudioBuffer<float> buf(2, 44100); buf.clear();
        UpicEngine eng; eng.prepare(44100.0, 8192);
        std::vector<upic::SampleSlot> noSlots;
        eng.renderPageOffline(pg, waves, envs, noSlots, buf, 44100.0, 1.0f);
        const int i0 = (int) (0.2 * 44100.0), i1 = (int) (0.8 * 44100.0);
        const float first = std::abs(buf.getSample(0, i0));
        const float last  = std::abs(buf.getSample(0, i1));
        const float mid   = std::abs(buf.getSample(0, (i0 + i1) / 2));
        check(first < 0.01, "arc entry fade starts silent", juce::String(first, 4));
        check(last < 0.01, "arc exit fade ends silent", juce::String(last, 4));
        check(mid > 0.15, "arc mid-level audible", juce::String(mid, 4));
    }

}

//==============================================================================
// M4: the tuning.
//
// The Scala parser, the scale's degrees and the sieve on the scale's own
// axis. The arcs hold frequencies, so the tuning does not change what is
// played - it changes what the drawing snaps to.
void upic::runTuningChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the official 12-tone file: the 1/1 is implicit, the octave is 2/1 ---
    const juce::String twelveTone =
        "! 12-tone equal temperament.scl\n"
        "!\n"
        "12-tone equal temperament\n"
        " 12\n"
        "!\n"
        " 100.00000\n"
        " 200.00000\n"
        " 300.00000\n"
        " 400.00000\n"
        " 500.00000\n"
        " 600.00000\n"
        " 700.00000\n"
        " 800.00000\n"
        " 900.00000\n"
        " 1000.00000\n"
        " 1100.00000\n"
        " 2/1\n";
    upic::Tuning tun;
    juce::String err;
    const bool parsed = upic::parseScalaTuning (twelveTone, tun, err);
    check (parsed, "scl: a 12-tone file parses", err);
    check (parsed && tun.custom && tun.degrees() == 12,
           "scl: the 12-tone file has twelve degrees", parsed ? juce::String (tun.degrees()) : err);
    check (parsed && tun.name == "12-tone equal temperament",
           "scl: the description is the name", parsed ? tun.name : err);
    check (parsed && tun.cents.size() == 12 && std::abs (tun.cents[0]) < 1e-9
           && std::abs (tun.cents[1] - 100.0) < 1e-6
           && std::abs (tun.cents[11] - 1100.0) < 1e-6,
           "scl: the degrees are the twelve semitones");

    // the same file with the 1/1 written out (the other convention)
    {
        const juce::String withBase =
            "12-tone equal temperament\n"
            " 12\n"
            " 0.0\n"
            " 100.0\n 200.0\n 300.0\n 400.0\n 500.0\n 600.0\n"
            " 700.0\n 800.0\n 900.0\n 1000.0\n 1100.0\n"
            " 2/1\n";
        upic::Tuning t2; juce::String e2;
        const bool ok = upic::parseScalaTuning (withBase, t2, e2);
        check (ok && t2.degrees() == 12, "scl: a file that lists the 1/1 parses too",
               ok ? juce::String (t2.degrees()) : e2);
    }

    // the official example: cents and ratios mixed, the octave as 2/1
    {
        const juce::String meanquar =
            "! meanquar.scl\n"
            "!\n"
            "1/4-comma meantone scale. Pietro Aaron's temperament (1523)\n"
            " 12\n"
            "!\n"
            " 76.04900\n"
            " 193.15686\n"
            " 310.26471\n"
            " 5/4\n"
            " 503.42157\n"
            " 579.47057\n"
            " 696.57843\n"
            " 25/16\n"
            " 889.73529\n"
            " 1006.84314\n"
            " 1082.89214\n"
            " 2/1\n";
        upic::Tuning t3; juce::String e3;
        const bool ok = upic::parseScalaTuning (meanquar, t3, e3);
        check (ok && t3.degrees() == 12, "scl: the meantone example parses",
               ok ? juce::String (t3.degrees()) : e3);
        // 5/4 is 386.31 cents, 25/16 is 772.63 cents
        check (ok && std::abs (t3.cents[4] - 386.3137) < 0.01
               && std::abs (t3.cents[8] - 772.6273) < 0.01,
               "scl: a ratio is read as a ratio",
               ok ? juce::String (t3.cents[4], 2) : e3);
    }

    // the details of the format: a plain integer is a ratio (2 -> 2/1), a
    // name after the value is ignored, and a value that starts with a
    // period is relative to the pitch before it
    {
        upic::Tuning t4; juce::String e4;
        const bool ok = upic::parseScalaTuning (
            "a test scale\n"
            " 4\n"
            " 100.0 C#\n"
            " 9/8\n"
            " .5\n"
            " 2\n", t4, e4);
        check (ok && t4.degrees() == 4, "scl: the format's details parse",
               ok ? juce::String (t4.degrees()) : e4);
        check (ok && std::abs (t4.cents[1] - 100.0) < 1e-6,
               "scl: a name after the value is ignored");
        check (ok && std::abs (t4.cents[2] - 203.9099) < 0.01,
               "scl: 9/8 is 203.91 cents");
        check (ok && std::abs (t4.cents[3] - (203.9099 + 0.5)) < 0.01,
               "scl: a value that starts with a period is relative",
               ok ? juce::String (t4.cents[3], 3) : e4);
    }

    // --- the refusals ---
    {
        auto refuses = [&check] (const juce::String& text, const juce::String& what)
        {
            upic::Tuning tt; juce::String ee;
            const bool ok = upic::parseScalaTuning (text, tt, ee);
            check (! ok && ee.isNotEmpty(), "scl: " + what + " is refused", ee);
        };
        refuses ("a scale\n 5\n 100.0\n 200.0\n 2/1\n", "a wrong note count");
        refuses ("a scale\n 2\n 200.0\n 100.0\n 2/1\n", "a scale that does not rise");
        refuses ("a scale\n 2\n 100.0\n 200.0\n", "a scale without the octave");
        refuses ("a scale\n 2\n 100.0\n hello\n 2/1\n", "a line that is not a pitch");
        refuses ("a scale\n 2\n -5.0\n 2/1\n", "a pitch below the base");
        refuses ("a scale\n", "a file that is too short");
    }

    // --- the degrees ---
    {
        // the degrees of the 12-tone scale: the base is middle C, the
        // octave is an octave up, and the pitch and the degree are
        // each other's inverse
        check (std::abs (tun.degreeToCents (0.0)) < 1e-9,
               "tuning: the base is 0 cents");
        check (std::abs (tun.degreeToCents (12.0) - 1200.0) < 1e-6,
               "tuning: the twelfth degree is the octave");
        check (std::abs (tun.stepToFreq (0.0) - upic::kDefaultRefFreq) < 0.01,
               "tuning: the base is middle C",
               juce::String (tun.stepToFreq (0.0), 2) + " Hz");
        bool inverse = true;
        for (double s = -6.0; s <= 18.0; s += 0.5)
            inverse = inverse && std::abs (tun.freqToStep (tun.stepToFreq (s)) - s) < 1e-6;
        check (inverse, "tuning: a degree and its pitch are each other's inverse");
        // the chromatic snap lands on a degree of the scale
        const double snapped = tun.snapToStep (300.0);
        check (std::abs (tun.freqToStep (snapped) - std::round (tun.freqToStep (snapped))) < 1e-6,
               "tuning: the chromatic snap lands on a degree",
               juce::String (snapped, 2) + " Hz");
    }

    // --- the sieve on the scale's own axis ---
    {
        // a seven-note scale (just intonation): the degrees are the
        // seventh of the octave, so the sieve's m = 7 is the octave
        const juce::String justIntonation =
            "! Just_Intonation.scl\n"
            "!\n"
            "Just Intonation\n"
            "8\n"
            "!\n"
            "1/1\n"
            "9/8\n"
            "5/4\n"
            "4/3\n"
            "3/2\n"
            "5/3\n"
            "15/8\n"
            "2/1\n";
        upic::Tuning ji; juce::String e5;
        const bool ok = upic::parseScalaTuning (justIntonation, ji, e5);
        check (ok && ji.degrees() == 7, "scl: a seven-note scale parses",
               ok ? juce::String (ji.degrees()) : e5);
        // the sieve's m = 7, R = {0} keeps the base degrees
        const double base = upic::sieveQuantizeHz (300.0, 7, { 0 }, ji);
        check (ok && std::abs (base - upic::kDefaultRefFreq) < 0.5,
               "tuning: the sieve m=7 counts the scale's degrees",
               juce::String (base, 2) + " Hz");
        // ... and R = {1} keeps the second degree (9/8)
        const double second = upic::sieveQuantizeHz (300.0, 7, { 1 }, ji);
        check (ok && std::abs (second - upic::kDefaultRefFreq * 9.0 / 8.0) < 0.5,
               "tuning: the sieve R=1 is the scale's second degree",
               juce::String (second, 2) + " Hz");
        // the 12-tone sieve is unchanged (the numbers do not move)
        check (std::abs (upic::sieveQuantizeHz (450.0, 12, { 9 }) - 440.0) < 0.5,
               "tuning: the 12-tone sieve is unchanged");
    }
}

//==============================================================================
// F2/F3 (2026-11-17): the wavetable and envelope libraries. The bank
// commands, the use counts (every page, both envelope slots), the reference
// rewrite on delete, the caps, the fork-on-edit, the undo transaction and
// the JSON round trip.
void upic::runLibraryModelChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // A small piece with a known bank: waves 10/11/12, envelopes 20/21/22.
    // Page 0's arc 1 uses env 20 and PM env 21; the other arcs use env 20.
    // Two pages make the "every page" half of the use count testable.
    auto makePiece = [](int waveCount, int envCount, int pageCount, bool withArcs) -> juce::var
    {
        upic::PieceModel m;
        for (int i = 0; i < waveCount; ++i)
        {
            upic::Waveform w (10 + i, "Wave " + juce::String (10 + i),
                              upic::paletteColour (i));
            w.makeSine();
            m.waveforms.push_back (w);
        }
        for (int i = 0; i < envCount; ++i)
        {
            upic::Envelope e (20 + i, "Env " + juce::String (20 + i),
                              upic::paletteColour (i + 1));
            e.presetFlat();
            m.envelopes.push_back (e);
        }
        for (int p = 0; p < pageCount; ++p)
        {
            upic::Page pg;
            pg.durationSec = 4.0;
            pg.nextArcId = 3;
            if (withArcs)
            {
                upic::Arc a;
                a.id = 1; a.mode = 0; a.waveformId = 10; a.envelopeId = 20;
                a.pmIndexEnv = (p == 0 ? 21 : 0); a.intensity = 0.8f;
                a.points = { { 0.0, 220.0 }, { 2.0, 440.0 } };
                pg.arcs.push_back (a);

                upic::Arc b;
                b.id = 2; b.mode = 0; b.waveformId = 10; b.envelopeId = 20;
                b.intensity = 0.8f;
                b.points = { { 0.0, 330.0 }, { 2.0, 660.0 } };
                pg.arcs.push_back (b);
            }
            m.pages.push_back (pg);
        }
        return upic::pieceToVar (m);
    };

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;

    auto load = [&mc, &check, &makePiece] (int w, int e, int p, bool arcs)
    {
        const bool ok = mc.loadFromVar (makePiece (w, e, p, arcs));
        check (ok, "library test piece loads",
               juce::String (w) + "/" + juce::String (e) + "/" + juce::String (p));
    };
    auto edit = [&mc] (const juce::String& label, const std::function<bool()>& fn)
    {
        mc.beginEdit (label);
        const bool ok = fn();
        mc.endEdit();
        return ok;
    };
    auto waveIndex = [&mc] (int id)
    {
        for (int i = 0; i < mc.waveformCountForTest(); ++i)
            if (mc.waveformIdForTest (i) == id) return i;
        return -1;
    };
    auto envIndex = [&mc] (int id)
    {
        for (int i = 0; i < mc.envelopeCountForTest(); ++i)
            if (mc.envelopeIdForTest (i) == id) return i;
        return -1;
    };

    // --- the bank and its use counts -------------------------------------
    {
        load (3, 3, 2, true);
        check (mc.waveformCountForTest() == 3 && mc.envelopeCountForTest() == 3,
               "library: the loaded bank has the piece's assets",
               juce::String (mc.waveformCountForTest()) + "/"
               + juce::String (mc.envelopeCountForTest()));
        check (mc.libraryWaveItemCountForTest() == 3
               && mc.libraryEnvelopeItemCountForTest() == 3,
               "library: the managers list every asset",
               juce::String (mc.libraryWaveItemCountForTest()) + "/"
               + juce::String (mc.libraryEnvelopeItemCountForTest()));
        check (mc.waveformUseCountForTest (10) == 4
               && mc.waveformUseCountForTest (11) == 0
               && mc.waveformUseCountForTest (12) == 0,
               "library: wave use counts span every page",
               juce::String (mc.waveformUseCountForTest (10)));
        check (mc.envelopeUseCountForTest (20) == 4
               && mc.envelopeUseCountForTest (21) == 1
               && mc.envelopeUseCountForTest (22) == 0,
               "library: envelope use counts include the PM index slot",
               juce::String (mc.envelopeUseCountForTest (20)) + "/"
               + juce::String (mc.envelopeUseCountForTest (21)));
        check (mc.selectedWaveformIdForTest() == 10
               && mc.selectedLibraryWaveForTest() == 10
               && mc.selectedEnvelopeIdForTest() == 20
               && mc.selectedLibraryEnvelopeForTest() == 20,
               "library: the managers follow the model's selection");
        check (mc.libraryWaveUsesForTest (0) == 4
               && mc.libraryEnvelopeUsesForTest (0) == 4,
               "library: the manager rows show the use counts");
    }

    // --- Make unique: the fork is one undo step --------------------------
    {
        load (3, 3, 2, true);
        mc.selectWaveformForTest (10);
        mc.selectArcsForTest ({ 1 });
        const int before = mc.waveformCountForTest();
        const int depth0 = mc.undoDepth();
        const bool ok = edit ("Make the wavetable unique",
                              [&mc]{ return mc.libraryMakeWaveUnique(); });
        const int fresh = mc.selectedWaveformIdForTest();
        check (ok && mc.waveformCountForTest() == before + 1 && fresh != 10,
               "library: Make unique gives the selection a private wavetable",
               juce::String (fresh));
        check (mc.waveformUseCountForTest (fresh) == 1
               && mc.waveformUseCountForTest (10) == 3,
               "library: only the selected arcs move to the fork",
               juce::String (mc.waveformUseCountForTest (fresh)) + "/"
               + juce::String (mc.waveformUseCountForTest (10)));
        check (mc.arcWaveformIdForTest (1) == fresh
               && mc.arcWaveformIdForTest (2) == 10
               && mc.pageArcWaveformIdForTest (1, 1) == 10,
               "library: the fork rewrites only the current page's selection");
        check (mc.undoDepth() == depth0 + 1,
               "library: Make unique is one undo step",
               juce::String (mc.undoDepth()) + " vs " + juce::String (depth0 + 1));
        mc.undo();
        check (mc.waveformCountForTest() == before
               && mc.arcWaveformIdForTest (1) == 10
               && mc.waveformUseCountForTest (10) == 4,
               "library: undo restores the shared wavetable");
        mc.redo();
        check (mc.waveformCountForTest() == before + 1
               && mc.arcWaveformIdForTest (1) == fresh,
               "library: redo restores the fork");
    }

    // --- Duplicate --------------------------------------------------------
    {
        load (3, 3, 2, true);
        mc.selectWaveformForTest (11);
        const float src = mc.waveformSampleForTest (11, 256);
        const int depth0 = mc.undoDepth();
        const bool ok = edit ("Duplicate the wavetable",
                              [&mc]{ return mc.libraryDuplicateWave(); });
        const int dup = mc.selectedWaveformIdForTest();
        const int idx = waveIndex (dup);
        check (ok && mc.waveformCountForTest() == 4 && dup != 11,
               "library: Duplicate adds a wavetable and selects it",
               juce::String (dup));
        check (idx >= 0 && mc.waveformNameForTest (idx) == "Wave 11 copy",
               "library: the duplicate is named after its source",
               idx >= 0 ? mc.waveformNameForTest (idx) : juce::String ("missing"));
        check (std::abs (mc.waveformSampleForTest (dup, 256) - src) < 1e-6f,
               "library: the duplicate copies the shape");
        check (mc.waveformUseCountForTest (dup) == 0,
               "library: the duplicate starts unused");
        check (mc.undoDepth() == depth0 + 1,
               "library: Duplicate is one undo step");
    }

    // --- Rename: trimmed, unique, case-insensitive -----------------------
    {
        load (3, 3, 2, true);
        mc.selectWaveformForTest (10);
        const int depth0 = mc.undoDepth();
        const bool ok = edit ("Rename the wavetable",
                              [&mc]{ return mc.libraryRenameWave ("  Lead  "); });
        check (ok && mc.waveformNameForTest (waveIndex (10)) == "Lead",
               "library: Rename trims the name",
               mc.waveformNameForTest (waveIndex (10)));
        check (mc.undoDepth() == depth0 + 1,
               "library: Rename is one undo step");

        const int depth1 = mc.undoDepth();
        const bool empty = edit ("Rename the wavetable",
                                 [&mc]{ return mc.libraryRenameWave ("   "); });
        check (! empty && mc.undoDepth() == depth1
               && mc.waveformNameForTest (waveIndex (10)) == "Lead"
               && mc.flashMessageForTest().isNotEmpty(),
               "library: an empty name is refused and explained",
               mc.flashMessageForTest());

        mc.selectWaveformForTest (11);
        const bool dup = edit ("Rename the wavetable",
                               [&mc]{ return mc.libraryRenameWave ("lead"); });
        check (! dup && mc.undoDepth() == depth1
               && mc.waveformNameForTest (waveIndex (11)) == "Wave 11",
               "library: a duplicate name is refused (case-insensitive)",
               mc.flashMessageForTest());
        const bool unique = edit ("Rename the wavetable",
                                  [&mc]{ return mc.libraryRenameWave ("Bass"); });
        check (unique && mc.waveformNameForTest (waveIndex (11)) == "Bass"
               && mc.undoDepth() == depth1 + 1,
               "library: a unique name is accepted");
    }

    // --- Delete a used wavetable: the replacement rewrites every page ----
    {
        load (3, 3, 2, true);
        mc.selectWaveformForTest (10);
        const int depth0 = mc.undoDepth();
        const bool ok = edit ("Delete the wavetable",
                              [&mc]{ return mc.libraryDeleteWave (11); });
        check (ok && mc.waveformCountForTest() == 2
               && waveIndex (10) < 0 && mc.selectedWaveformIdForTest() == 11,
               "library: Delete removes the wavetable and selects the replacement",
               juce::String (mc.waveformCountForTest()) + "/"
               + juce::String (mc.selectedWaveformIdForTest()));
        check (mc.arcWaveformIdForTest (1) == 11
               && mc.arcWaveformIdForTest (2) == 11
               && mc.pageArcWaveformIdForTest (1, 1) == 11
               && mc.pageArcWaveformIdForTest (1, 2) == 11,
               "library: the replacement is rewritten on every page");
        check (mc.waveformUseCountForTest (11) == 4
               && mc.waveformUseCountForTest (12) == 0,
               "library: the replacement inherits the uses",
               juce::String (mc.waveformUseCountForTest (11)));
        check (mc.undoDepth() == depth0 + 1,
               "library: Delete is one undo step");
        mc.undo();
        check (mc.waveformCountForTest() == 3
               && mc.arcWaveformIdForTest (1) == 10
               && mc.pageArcWaveformIdForTest (1, 1) == 10,
               "library: undo restores the deleted wavetable and its references");
        mc.redo();
        check (mc.waveformCountForTest() == 2
               && mc.arcWaveformIdForTest (1) == 11,
               "library: redo restores the replacement");
    }

    // --- Delete refusals -------------------------------------------------
    {
        load (3, 3, 2, true);
        mc.selectWaveformForTest (10);
        const bool bad = edit ("Delete the wavetable",
                               [&mc]{ return mc.libraryDeleteWave (999); });
        check (! bad && mc.waveformCountForTest() == 3
               && mc.flashMessageForTest().isNotEmpty(),
               "library: a missing replacement is refused and explained",
               mc.flashMessageForTest());
        const bool self = edit ("Delete the wavetable",
                                [&mc]{ return mc.libraryDeleteWave (10); });
        check (! self && mc.waveformCountForTest() == 3,
               "library: a wavetable cannot replace itself");

        mc.selectWaveformForTest (12);
        const bool unused = edit ("Delete the wavetable",
                                  [&mc]{ return mc.libraryDeleteWave (-1); });
        check (unused && mc.waveformCountForTest() == 2 && waveIndex (12) < 0,
               "library: an unused wavetable deletes without a replacement");
        check (mc.selectedWaveformIdForTest() == 10,
               "library: the selection falls back after an unused delete",
               juce::String (mc.selectedWaveformIdForTest()));
    }

    {
        load (1, 1, 1, true);
        mc.selectWaveformForTest (10);
        const bool last = edit ("Delete the wavetable",
                                [&mc]{ return mc.libraryDeleteWave (-1); });
        check (! last && mc.waveformCountForTest() == 1
               && mc.flashMessageForTest().containsIgnoreCase ("last"),
               "library: the last wavetable cannot be deleted",
               mc.flashMessageForTest());
    }

    // --- Envelope delete: the amp slot and the PM slot -------------------
    {
        load (3, 3, 2, true);
        mc.selectEnvelopeForTest (20);
        const int depth0 = mc.undoDepth();
        const bool ok = edit ("Delete the envelope",
                              [&mc]{ return mc.libraryDeleteEnvelope (22); });
        check (ok && mc.envelopeCountForTest() == 2
               && envIndex (20) < 0 && mc.selectedEnvelopeIdForTest() == 22,
               "library: Delete removes the envelope and selects the replacement",
               juce::String (mc.envelopeCountForTest()) + "/"
               + juce::String (mc.selectedEnvelopeIdForTest()));
        check (mc.arcEnvelopeIdForTest (1) == 22
               && mc.arcEnvelopeIdForTest (2) == 22
               && mc.pageArcEnvelopeIdForTest (1, 1) == 22
               && mc.pageArcEnvelopeIdForTest (1, 2) == 22,
               "library: the envelope replacement is rewritten on every page");
        check (mc.arcPmIndexEnvForTest (1) == 21,
               "library: deleting the amp envelope leaves the PM slot alone",
               juce::String (mc.arcPmIndexEnvForTest (1)));
        check (mc.envelopeUseCountForTest (22) == 4
               && mc.envelopeUseCountForTest (21) == 1,
               "library: the envelope replacement inherits the amp uses");
        check (mc.undoDepth() == depth0 + 1,
               "library: envelope Delete is one undo step");
        mc.undo();
        check (mc.envelopeCountForTest() == 3
               && mc.arcEnvelopeIdForTest (1) == 20
               && mc.pageArcEnvelopeIdForTest (1, 1) == 20,
               "library: undo restores the deleted envelope");
    }

    {
        load (3, 3, 2, true);
        mc.selectEnvelopeForTest (21);
        const bool ok = edit ("Delete the envelope",
                              [&mc]{ return mc.libraryDeleteEnvelope (22); });
        check (ok && mc.envelopeCountForTest() == 2
               && mc.arcPmIndexEnvForTest (1) == 22
               && mc.pageArcPmIndexEnvForTest (0, 1) == 22
               && mc.pageArcPmIndexEnvForTest (1, 1) == 0,
               "library: deleting a PM envelope rewrites the PM slot on every page",
               juce::String (mc.arcPmIndexEnvForTest (1)));
        check (mc.arcEnvelopeIdForTest (1) == 20,
               "library: deleting the PM envelope leaves the amp slot alone");
    }

    {
        load (1, 1, 1, true);
        mc.selectEnvelopeForTest (20);
        const bool last = edit ("Delete the envelope",
                                [&mc]{ return mc.libraryDeleteEnvelope (-1); });
        check (! last && mc.envelopeCountForTest() == 1
               && mc.flashMessageForTest().containsIgnoreCase ("last"),
               "library: the last envelope cannot be deleted",
               mc.flashMessageForTest());
    }

    // --- the caps ---------------------------------------------------------
    {
        load (upic::limits::kMaxWaveforms, upic::limits::kMaxEnvelopes, 1, true);
        check (mc.waveformCountForTest() == upic::limits::kMaxWaveforms
               && mc.envelopeCountForTest() == upic::limits::kMaxEnvelopes,
               "library: a piece at the caps loads",
               juce::String (mc.waveformCountForTest()) + "/"
               + juce::String (mc.envelopeCountForTest()));

        const int depth0 = mc.undoDepth();
        const bool newWave = edit ("New wavetable",
                                   [&mc]{ return mc.libraryNewWave(); });
        check (! newWave && mc.waveformCountForTest() == upic::limits::kMaxWaveforms
               && mc.undoDepth() == depth0
               && mc.flashMessageForTest().contains ("200"),
               "library: New refuses at the wavetable cap and explains",
               mc.flashMessageForTest());
        const bool dupWave = edit ("Duplicate the wavetable",
                                   [&mc]{ return mc.libraryDuplicateWave(); });
        check (! dupWave && mc.waveformCountForTest() == upic::limits::kMaxWaveforms,
               "library: Duplicate refuses at the wavetable cap");

        const bool newEnv = edit ("New envelope",
                                  [&mc]{ return mc.libraryNewEnvelope(); });
        check (! newEnv && mc.envelopeCountForTest() == upic::limits::kMaxEnvelopes
               && mc.flashMessageForTest().contains ("50"),
               "library: New refuses at the envelope cap and explains",
               mc.flashMessageForTest());
        const bool dupEnv = edit ("Duplicate the envelope",
                                  [&mc]{ return mc.libraryDuplicateEnvelope(); });
        check (! dupEnv && mc.envelopeCountForTest() == upic::limits::kMaxEnvelopes,
               "library: Duplicate refuses at the envelope cap");

        // The fork is a creation too: Make unique must obey the cap.
        mc.selectArcsForTest ({ 1 });
        mc.selectWaveformForTest (10);
        const bool uniqueWave = edit ("Make the wavetable unique",
                                      [&mc]{ return mc.libraryMakeWaveUnique(); });
        check (! uniqueWave && mc.waveformCountForTest() == upic::limits::kMaxWaveforms
               && mc.flashMessageForTest().contains ("200"),
               "library: Make unique refuses at the wavetable cap",
               mc.flashMessageForTest());

        mc.selectEnvelopeForTest (20);
        const bool uniqueEnv = edit ("Make the envelope unique",
                                     [&mc]{ return mc.libraryMakeEnvelopeUnique(); });
        check (! uniqueEnv && mc.envelopeCountForTest() == upic::limits::kMaxEnvelopes
               && mc.flashMessageForTest().contains ("50"),
               "library: Make unique refuses at the envelope cap",
               mc.flashMessageForTest());

        // The editors use the same fork: a shared edit is cancelled, and
        // the cancelled transaction leaves no undo step.
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName (*c.getChildComponent (i), n)) return r;
                return nullptr;
            };
        auto* waveEd = dynamic_cast<WaveformEditor*> (findByName (mc, "waveEd"));
        auto* envEd = dynamic_cast<EnvelopeEditor*> (findByName (mc, "envEd"));
        const int depth1 = mc.undoDepth();
        check (waveEd != nullptr && ! waveEd->beginEdit(),
               "library: the wavetable editor cancels a fork at the cap");
        check (envEd != nullptr && ! envEd->beginEdit(),
               "library: the envelope editor cancels a fork at the cap",
               juce::String (mc.envelopeCountForTest()) + "/"
               + juce::String (mc.selectedEnvelopeIdForTest()) + "/"
               + juce::String (mc.envelopeUseCountForTest (20)) + "/"
               + juce::String (mc.selectedArcCountForTest()));
        check (mc.undoDepth() == depth1,
               "library: a cancelled fork leaves no undo step");
    }

    // --- the envelope editor forks a shared envelope before an edit ------
    {
        load (3, 3, 2, true);
        mc.selectEnvelopeForTest (20);
        mc.selectArcsForTest ({ 1 });

        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName (*c.getChildComponent (i), n)) return r;
                return nullptr;
            };
        auto* envEd = dynamic_cast<EnvelopeEditor*> (findByName (mc, "envEd"));
        const int before = mc.envelopeCountForTest();
        const int depth0 = mc.undoDepth();
        const bool started = envEd != nullptr && envEd->beginEdit();
        const int fresh = mc.selectedEnvelopeIdForTest();
        check (started && mc.envelopeCountForTest() == before + 1 && fresh != 20,
               "library: editing a shared envelope forks it",
               juce::String (mc.envelopeCountForTest()) + "/" + juce::String (fresh));
        check (mc.arcEnvelopeIdForTest (1) == fresh
               && mc.arcEnvelopeIdForTest (2) == 20
               && mc.pageArcEnvelopeIdForTest (1, 1) == 20,
               "library: only the selected arc moves to the envelope fork");
        const int points0 = envEd != nullptr ? envEd->pointCount() : -1;
        const bool added = envEd != nullptr && envEd->addPoint();
        if (envEd != nullptr) envEd->endEdit();
        check (added && envEd->pointCount() == points0 + 1,
               "library: the fork is the envelope the edit changes",
               juce::String (points0) + " -> "
               + juce::String (envEd != nullptr ? envEd->pointCount() : -1));
        check (mc.undoDepth() == depth0 + 1,
               "library: the envelope fork and its edit are one undo step",
               juce::String (mc.undoDepth()) + " vs " + juce::String (depth0 + 1));
        mc.undo();
        check (mc.envelopeCountForTest() == before
               && mc.arcEnvelopeIdForTest (1) == 20
               && mc.selectedEnvelopeIdForTest() == 20,
               "library: undo removes the fork and its edit");
    }

    // --- one JSON round trip with a renamed and a new asset ---------------
    {
        load (3, 3, 2, true);
        mc.selectWaveformForTest (10);
        const bool renamed = edit ("Rename the wavetable",
                                   [&mc]{ return mc.libraryRenameWave ("Lead"); });
        const bool addedWave = edit ("New wavetable",
                                     [&mc]{ return mc.libraryNewWave(); });
        const int freshWave = mc.selectedWaveformIdForTest();
        mc.selectEnvelopeForTest (20);
        const bool renamedEnv = edit ("Rename the envelope",
                                      [&mc]{ return mc.libraryRenameEnvelope ("Shape"); });
        const bool addedEnv = edit ("New envelope",
                                    [&mc]{ return mc.libraryNewEnvelope(); });
        const int freshEnv = mc.selectedEnvelopeIdForTest();
        check (renamed && addedWave && renamedEnv && addedEnv,
               "library: the round-trip setup commands succeed");

        const auto saved = mc.pieceToVar();
        auto mc2Holder = std::make_unique<MainComponent> (true);
        auto& mc2 = *mc2Holder;
        const bool loaded = mc2.loadFromVar (saved);
        auto waveIndex2 = [&mc2] (int id)
        {
            for (int i = 0; i < mc2.waveformCountForTest(); ++i)
                if (mc2.waveformIdForTest (i) == id) return i;
            return -1;
        };
        auto envIndex2 = [&mc2] (int id)
        {
            for (int i = 0; i < mc2.envelopeCountForTest(); ++i)
                if (mc2.envelopeIdForTest (i) == id) return i;
            return -1;
        };
        check (loaded && mc2.waveformCountForTest() == 4
               && mc2.envelopeCountForTest() == 4,
               "library: the bank survives a JSON round trip",
               juce::String (mc2.waveformCountForTest()) + "/"
               + juce::String (mc2.envelopeCountForTest()));
        check (mc2.waveformNameForTest (waveIndex2 (10)) == "Lead"
               && mc2.waveformNameForTest (waveIndex2 (freshWave)).startsWith ("Wave "),
               "library: the renamed and new wavetables survive the round trip",
               mc2.waveformNameForTest (waveIndex2 (10)));
        check (mc2.envelopeNameForTest (envIndex2 (20)) == "Shape"
               && mc2.envelopeIdForTest (envIndex2 (freshEnv)) == freshEnv,
               "library: the renamed and new envelopes survive the round trip",
               mc2.envelopeNameForTest (envIndex2 (20)));
        check (mc2.waveformUseCountForTest (10) == 4
               && mc2.waveformUseCountForTest (freshWave) == 0
               && mc2.envelopeUseCountForTest (20) == 4
               && mc2.envelopeUseCountForTest (freshEnv) == 0,
               "library: the use counts are rebuilt after a load");
    }
}

//==============================================================================
// WP1: the drawn frequency tables (the New UPIC's editable conversion
// tables). A space can be a drawn curve instead of a preset; the audio
// thread reads a compiled LUT, and the piece format stays additive.
void upic::runFreqTableChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };
    auto log2Hz = [] (double hz) { return (float) std::log2 (hz); };

    // WP1-a: a page that draws no table is unchanged and writes no key.
    {
        upic::Page pg;
        bool allDefault = true;
        for (auto& d : pg.spaceDefs) allDefault = allDefault && d.isDefault();
        check (allDefault, "freq tables: a page's four spaces start as presets");
        const auto v = pg.toVar();
        check (! v.hasProperty ("spaceTables"),
               "freq tables: a preset-only page writes no frequency tables");
    }

    // WP1-b: a drawn table maps the page's vertical axis.
    {
        upic::Page pg;   // the default ambitus: 20 Hz - 12 kHz
        auto& d = pg.spaceDefs[1];
        d.editable = true;
        d.points = { { 0.0f, log2Hz (110.0) },
                     { 0.5f, log2Hz (440.0) },
                     { 1.0f, log2Hz (1760.0) } };

        check (std::abs (pg.placeInSpace (pg.pitchLo, 1) - 110.0) < 1e-3
            && std::abs (pg.placeInSpace (pg.pitchHi, 1) - 1760.0) < 1e-3,
               "freq tables: the table maps the bottom and the top of the page",
               juce::String (pg.placeInSpace (pg.pitchLo, 1), 2) + " - "
               + juce::String (pg.placeInSpace (pg.pitchHi, 1), 2) + " Hz");

        const double midPos = std::sqrt (pg.pitchLo * pg.pitchHi);
        check (std::abs (pg.placeInSpace (midPos, 1) - 440.0) < 1e-3,
               "freq tables: the middle of the page lands on the table's middle point",
               juce::String (pg.placeInSpace (midPos, 1), 2) + " Hz");
        check (pg.placeInSpace (midPos, 0) == midPos,
               "freq tables: a space without a table is still the identity");

        // WP1-c: the LUT the audio thread reads agrees with the curve.
        upic::FreqLut lut = upic::buildFreqLut (d, pg.pitchLo, pg.pitchHi);
        check (lut.valid, "freq tables: a drawn table compiles to a LUT");
        upic::PageLutSet set;
        set.lut[1] = &lut;
        double worst = 0.0;
        for (int i = 0; i < 65; ++i)
        {
            const float n = (float) i / 64.0f;
            const double hz = upic::normYToFreqIn (n, pg.pitchLo, pg.pitchHi);
            const double direct = pg.spaceMap (1).apply (hz);
            const double fast = pg.spaceMap (1, &lut).apply (hz);
            worst = juce::jmax (worst, std::abs (fast - direct) / juce::jmax (1.0, direct));
        }
        check (worst < 1e-3, "freq tables: the LUT matches the drawn curve",
               juce::String (worst * 100.0, 4) + " % worst error");

        // WP1-d: the table travels with the piece.
        const auto back = upic::Page::fromVar (pg.toVar());
        check (back.spaceDefs[1].editable && back.spaceDefs[1].points.size() == 3
               && std::abs (std::exp2 (back.spaceDefs[1].points[1].log2Hz) - 440.0) < 1e-2,
               "freq tables: the drawn table travels with the piece");
        check (back.spaceDefs[2].isDefault(),
               "freq tables: the untouched spaces stay presets");
    }

    // WP1-e: a stepped space lands on its grid; an inverted one mirrors.
    {
        upic::Page pg;
        auto& d = pg.spaceDefs[2];
        d.editable = true;
        d.points = { { 0.0f, log2Hz (100.0) }, { 1.0f, log2Hz (3200.0) } };
        d.stepped = true;
        d.stepDivisions = 12;

        upic::Arc a;
        a.space = 2;
        const double mid = std::sqrt (pg.pitchLo * pg.pitchHi) * 1.37; // off any grid point
        a.points = { { 0.0, mid }, { 1.0, mid } };
        const double stepped = UpicEngine::arcPitch (a, 0.5, pg.spaceMap (2));
        const double steps = std::log2 (stepped / upic::kDefaultRefFreq) * 12.0;
        check (std::abs (steps - std::round (steps)) < 1e-6,
               "freq tables: a stepped space lands on its grid",
               juce::String (steps, 4) + " steps");

        d.stepped = false;
        d.inverted = true;
        check (std::abs (pg.placeInSpace (pg.pitchLo, 2) - 3200.0) < 1e-2
            && std::abs (pg.placeInSpace (pg.pitchHi, 2) - 100.0) < 1e-2,
               "freq tables: an inverted table mirrors the drawing",
               juce::String (pg.placeInSpace (pg.pitchLo, 2), 1) + " - "
               + juce::String (pg.placeInSpace (pg.pitchHi, 2), 1) + " Hz");
    }

    // WP1-f: a hostile table is capped at load, not trusted.
    {
        juce::Array<juce::var> pts;
        for (int i = 0; i < upic::limits::kMaxTablePoints + 500; ++i)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("x", (double) i / (double) (upic::limits::kMaxTablePoints + 500));
            o->setProperty ("hz", 440.0);
            pts.add (juce::var (o));
        }
        auto* tv = new juce::DynamicObject();
        tv->setProperty ("space", 0);
        tv->setProperty ("editable", true);
        tv->setProperty ("points", pts);
        juce::Array<juce::var> tables;
        tables.add (juce::var (tv));
        auto* pv = new juce::DynamicObject();
        pv->setProperty ("duration", 10.0);
        pv->setProperty ("spaceTables", tables);
        const upic::Page loaded = upic::Page::fromVar (juce::var (pv));
        check ((int) loaded.spaceDefs[0].points.size() == upic::limits::kMaxTablePoints,
               "freq tables: an oversized table is truncated to the cap",
               juce::String ((int) loaded.spaceDefs[0].points.size()) + " points");
    }

    // WP1-g: an old piece (no new keys) loads and renders bit for bit.
    {
        const juce::String oldJson = R"({"duration":4.0,"nextArcId":2,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"intensity":0.8,"points":[{"t":0.0,"f":220.0},{"t":4.0,"f":440.0}]}]})";
        const juce::var v = juce::JSON::parse (oldJson);
        const upic::Page loaded = upic::Page::fromVar (v);

        upic::Page built;
        built.durationSec = 4.0;
        upic::Arc a;
        a.id = 1;
        a.intensity = 0.8f;
        a.points = { { 0.0, 220.0 }, { 4.0, 440.0 } };
        built.arcs.push_back (a);

        const auto waves = upic::defaultWaveforms();
        const auto envs = upic::defaultEnvelopes();
        const std::vector<upic::SampleSlot> slots;
        UpicEngine e1, e2;
        e1.prepare (48000.0, 512);
        e2.prepare (48000.0, 512);
        std::vector<float> l1 (512, 0.0f), l2 (512, 0.0f);
        e1.render (loaded, 0.0, waves, envs, slots, l1.data(), nullptr, 512, 0.8f);
        e2.render (built,  0.0, waves, envs, slots, l2.data(), nullptr, 512, 0.8f);
        bool same = true;
        for (size_t i = 0; i < l1.size(); ++i)
            if (l1[i] != l2[i]) { same = false; break; }
        check (same, "freq tables: an old piece renders bit for bit");
        check (! loaded.spaceDefs[0].editable && loaded.spaceDefs[0].isDefault(),
               "freq tables: an old piece gets the preset spaces");
    }

    // WP1-h: the Table... button path - open, draw, undo.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        mc.openFreqTableForTest (2);
        check (mc.freqTableOpenForTest(), "freq tables: the editor opens");

        const std::vector<upic::TablePoint> pts = { { 0.0f, log2Hz (200.0) },
                                                    { 0.5f, log2Hz (400.0) },
                                                    { 1.0f, log2Hz (800.0) } };
        mc.commitFreqTableCurveForTest (pts);
        check (mc.spaceDefEditableForTest (2) && mc.spaceDefPointCountForTest (2) == 3,
               "freq tables: the drawn curve reaches the page");
        check (mc.canUndo(), "freq tables: the draw is one undo transaction");
        mc.undo();
        check (! mc.spaceDefEditableForTest (2) && mc.spaceDefPointCountForTest (2) == 0,
               "freq tables: undo restores the preset space");

        mc.openFreqTableForTest (1);
        mc.commitFreqTableFlagsForTest();
        check (mc.pieceToVar().hasProperty ("pages"),
               "freq tables: the piece still serializes after a table edit");
    }
}

//==============================================================================
// WP2: the amplitude conversion tables. The envelope keeps its shape; the
// table maps its output to gain. The bank is separate, additive in the piece
// format, and the engine applies the table in every arc mode.
void upic::runAmpTableChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // WP2-a: the default bank is one identity table.
    {
        const auto bank = upic::defaultAmplitudeTables();
        check (bank.size() == 1, "amp tables: a new piece has one identity table");
        const auto& pts = bank.front().points;
        check (pts.size() == 2 && pts.front().y == 0.0f && pts.back().y == 1.0f,
               "amp tables: the default table is the identity");
    }

    // WP2-b: the presets are the historical transfer curves.
    {
        auto makePreset = [] (int p)
        {
            upic::Envelope e (1, "Preset", juce::Colours::white);
            upic::ampPresetPoints (p, e.points);
            return e;
        };
        check (makePreset (upic::ampSquare).points[2].y == 0.25f,
               "amp tables: square maps 0.5 to 0.25");
        check (makePreset (upic::ampCompress).points[2].y > 0.6f,
               "amp tables: compress lifts the middle");
        check (makePreset (upic::ampExpand).points[2].y < 0.4f,
               "amp tables: expand pushes the middle down");
    }

    // WP2-c: the engine applies the table in the oscillator and the PM paths.
    {
        upic::Page base;
        base.durationSec = 1.0;
        upic::Arc a;
        a.id = 1;
        a.waveformId = 0;
        a.envelopeId = 0;
        a.intensity = 1.0f;
        a.points = { { 0.0, 440.0 }, { 1.0, 440.0 } };
        base.arcs.push_back (a);

        auto waves = upic::defaultWaveforms();
        auto envs = upic::defaultEnvelopes();
        envs[0].points = { { 0.0f, 0.5f }, { 1.0f, 0.5f } };
        const std::vector<upic::SampleSlot> slots;

        std::vector<upic::Envelope> tabs;
        tabs.push_back (upic::makeAmplitudeTable (7, "Square", juce::Colours::white,
                                                  upic::ampSquare));

        auto pageFor = [&] (int mode, int tableId)
        {
            upic::Page p = base;
            p.arcs[0].mode = mode;
            p.arcs[0].amplitudeTableId = tableId;
            p.arcs[0].pmIndex = 0.0;
            return p;
        };
        auto rms = [&] (const upic::Page& p, const std::vector<upic::Envelope>* bank)
        {
            UpicEngine e;
            e.prepare (48000.0, 512);
            std::vector<float> l (512, 0.0f);
            e.render (p, 0.0, waves, envs, slots, l.data(), nullptr, 512, 0.8f,
                      1.0, false, nullptr, bank);
            double sum = 0.0;
            for (float v : l) sum += (double) v * v;
            return std::sqrt (sum / 512.0);
        };

        const double noTable = rms (pageFor (0, -1), nullptr);
        check (std::abs (noTable - rms (pageFor (0, -1), &tabs)) < 1e-12,
               "amp tables: a table the arc does not name changes nothing");
        const double withTable = rms (pageFor (0, 7), &tabs);
        check (std::abs (withTable / noTable - 0.5) < 0.02,
               "amp tables: square halves a half-level envelope",
               juce::String (withTable / noTable, 4));
        const double pmNoTable = rms (pageFor (3, -1), nullptr);
        const double pmTable = rms (pageFor (3, 7), &tabs);
        check (std::abs (pmTable / pmNoTable - 0.5) < 0.03,
               "amp tables: the PM path plays through the table too",
               juce::String (pmTable / pmNoTable, 4));
    }

    // WP2-d: assignment, the drawn curve, undo and the round trip.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"ampTables":[{"id":1,"name":"Table 1","colour":-1,"points":[{"x":0,"y":0},{"x":1,"y":1}]}],"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":440},{"t":2,"f":440}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json)), "amp tables: the test piece loads");

        mc.selectArcsForTest ({ 1 });
        mc.ampTableAssignForTest (1);
        check (mc.arcAmpTableIdForTest (1) == 1,
               "amp tables: the assignment reaches the arc");

        mc.openAmpTableEditorForTest();
        check (mc.ampTableEditorOpenForTest(), "amp tables: the bank editor opens");

        mc.ampTableDrawForTest ({ { 0.0f, 0.0f }, { 0.5f, 0.25f }, { 1.0f, 1.0f } });
        check (std::abs (mc.ampTableValueForTest (0, 0.5f) - 0.25f) < 1e-5f,
               "amp tables: the drawn curve reaches the bank",
               juce::String (mc.ampTableValueForTest (0, 0.5f), 4));
        check (mc.canUndo(), "amp tables: the draw is one undo step");
        mc.undo();
        check (std::abs (mc.ampTableValueForTest (0, 0.5f) - 0.5f) < 1e-5f,
               "amp tables: undo restores the identity");

        const int beforeDelete = mc.ampTableCountForTest();
        mc.ampTableDeleteForTest();
        check (mc.ampTableCountForTest() == beforeDelete,
               "amp tables: a table in use cannot be deleted");

        mc.ampTableDrawForTest ({ { 0.0f, 0.0f }, { 0.5f, 0.25f }, { 1.0f, 1.0f } });
        const auto saved = mc.pieceToVar();
        check (saved.hasProperty ("ampTables"), "amp tables: the bank travels with the piece");
        auto mc2Holder = std::make_unique<MainComponent> (true);
        auto& mc2 = *mc2Holder;
        check (mc2.loadFromVar (saved) && mc2.ampTableCountForTest() == 1,
               "amp tables: the piece loads with its bank");
        check (std::abs (mc2.ampTableValueForTest (0, 0.5f) - 0.25f) < 1e-5f
               && mc2.arcAmpTableIdForTest (1) == 1,
               "amp tables: the table and the arc's reference survive a round trip");

        mc.ampTableNewForTest();
        check (mc.ampTableCountForTest() == 2, "amp tables: a new table joins the bank");
        mc.ampTableDuplicateForTest();
        check (mc.ampTableCountForTest() == 3, "amp tables: a duplicate joins the bank");
    }

    // WP2-e: an old piece (no new keys) loads with no table and no reference.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String oldJson = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":220},{"t":2,"f":440}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (oldJson)), "amp tables: an old piece loads");
        check (mc.ampTableCountForTest() == 0 && mc.arcAmpTableIdForTest (1) == -1,
               "amp tables: an old piece has no tables and no references");
    }
}
//==============================================================================
// WP3: the arc groups. A group is a named set of arcs with shared,
// non-destructive attributes. The model keeps the arcs untouched; the
// snapshot resolves one override per arc, and the engine reads that.
void upic::runGroupChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    auto arcWith = [] (int id, double freq)
    {
        upic::Arc a;
        a.id = id;
        a.waveformId = 0;
        a.envelopeId = 0;
        // quiet, so the output's soft clip stays in its linear region and a
        // level ratio is measurable
        a.intensity = 0.05f;
        a.points = { { 0.0, freq }, { 1.0, freq } };
        return a;
    };

    // WP3-a: the resolution truth table (solo, mute, gain, overrides).
    // WP3-a: the resolution truth table (solo, mute, gain, overrides).
    {
        upic::Page pg;
        pg.arcs = { arcWith (1, 220.0), arcWith (2, 330.0), arcWith (3, 440.0) };
        upic::ArcGroup g1;
        g1.id = 1; g1.name = "A"; g1.arcIds = { 1, 2 };
        g1.gain = 0.5f; g1.transposeSemitones = 7.0;
        g1.waveformId = 3; g1.envelopeId = 2;
        pg.groups = { g1 };
        auto ov = pg.resolveArcOverrides();
        check (ov.size() == 3, "groups: one override per arc");
        check (std::abs (ov[0].gain - 0.5f) < 1e-6f && std::abs (ov[1].gain - 0.5f) < 1e-6f
               && std::abs (ov[2].gain - 1.0f) < 1e-6f,
               "groups: the gain reaches the members only");
        check (ov[0].transposeSemitones == 7.0 && ov[2].transposeSemitones == 0.0,
               "groups: the transpose reaches the members only");
        check (ov[0].waveformId == 3 && ov[0].envelopeId == 2 && ov[2].waveformId == -1,
               "groups: the timbre overrides reach the members only");
        check (! ov[0].muted && ! ov[2].muted, "groups: nothing is muted by default");

        // solo silences the groups that are not solo
        upic::ArcGroup g2;
        g2.id = 2; g2.name = "B"; g2.arcIds = { 3 }; g2.solo = true;
        pg.groups.push_back (g2);
        ov = pg.resolveArcOverrides();
        check (ov[0].muted && ov[1].muted && ! ov[2].muted,
               "groups: a solo silences the other groups");

        // a solo also silences arcs that have no group at all
        {
            upic::Page soloPg;
            soloPg.arcs = { arcWith (1, 220.0), arcWith (2, 330.0), arcWith (3, 440.0) };
            upic::ArcGroup soloG; soloG.id = 1; soloG.arcIds = { 2 }; soloG.solo = true;
            upic::ArcGroup otherG; otherG.id = 2; otherG.arcIds = { 3 };
            soloPg.groups = { soloG, otherG };
            const auto sov = soloPg.resolveArcOverrides();
            check (sov[0].muted && ! sov[1].muted && sov[2].muted,
                   "groups: a solo silences ungrouped arcs too");
        }
        g2.solo = false;
        g1.muted = true;
        pg.groups[0] = g1;
        pg.groups[1] = g2;
        ov = pg.resolveArcOverrides();
        check (ov[0].muted && ov[1].muted && ! ov[2].muted,
               "groups: a group mute silences its members");

        // one group per arc: an arc listed twice belongs to the first group
        g2.arcIds.push_back (1);
        pg.groups[1] = g2;
        ov = pg.resolveArcOverrides();
        check (std::abs (ov[0].gain - 0.5f) < 1e-6f,
               "groups: an arc listed twice keeps its first group");

        // the arcs' own data is untouched
        check (pg.arcs[0].points.front().freq == 220.0 && pg.arcs[0].intensity == 0.05f,
               "groups: the arcs' own data is never changed");
    }

    // WP3-b: the space map applies the group transpose after the mapping.
    // WP3-b: the space map applies the group transpose after the mapping.
    {
        upic::Page pg;
        pg.arcs = { arcWith (1, 440.0) };
        upic::Page::SpaceMap m = pg.spaceMap (0);
        m.transposeSemis = 7.0;
        const double up = UpicEngine::arcPitch (pg.arcs[0], 0.5, m);
        check (std::abs (up - 440.0 * std::pow (2.0, 7.0 / 12.0)) < 1e-6,
               "groups: +7 st transposes the mapped pitch",
               juce::String (up, 3) + " Hz");
        upic::Page::SpaceMap m0 = pg.spaceMap (0);
        check (m0.identity && m0.apply (440.0) == 440.0,
               "groups: no transpose leaves the space map identity");
        check (std::abs (UpicEngine::arcPitch (pg.arcs[0], 0.5, m0) - 440.0) < 1e-6,
               "groups: no transpose leaves the pitch alone");
        // WP3 fix: the transpose is the LAST pitch operation, after the
        // space's temperament, so a tempered space is transposed whole.
        {
            upic::Page tp;
            tp.arcs = { arcWith (1, 440.0) };
            upic::Page::SpaceMap tm = tp.spaceMap (0);
            tm.stepDivisions = 7;
            tm.transposeSemis = 0.25;
            const double got = UpicEngine::arcPitch (tp.arcs[0], 0.5, tm);
            const double want = upic::applyTemperament (440.0, 7, upic::kDefaultRefFreq)
                                * std::pow (2.0, 0.25 / 12.0);
            check (std::abs (got - want) < 1e-9,
                   "groups: the transpose follows the temperament",
                   juce::String (got, 6) + " vs " + juce::String (want, 6));
        }
    }

    // WP3-c: the engine plays the group's gain, mute and overrides.
    // WP3-c: the engine plays the group's gain, mute and overrides.
    {
        upic::Page pg;
        pg.arcs = { arcWith (1, 440.0) };

        auto envs = upic::defaultEnvelopes();
        // a flat half envelope for the override test (id 9)
        upic::Envelope half (9, "Half", juce::Colours::white);
        half.points = { { 0.0f, 0.5f }, { 1.0f, 0.5f } };
        envs.push_back (half);

        auto waves = upic::defaultWaveforms();
        const std::vector<upic::SampleSlot> slots;
        auto rms = [&] (bool withGroup)
        {
            upic::Page p = pg;
            std::vector<upic::ArcRenderOverride> ov;
            if (withGroup)
            {
                ov = p.resolveArcOverrides();
                if (ov.empty()) ov.resize (p.arcs.size());
            }
            UpicEngine e;
            e.prepare (48000.0, 512);
            std::vector<float> l (512, 0.0f);
            e.render (p, 0.0, waves, envs, slots, l.data(), nullptr, 512, 0.8f,
                      1.0, false, nullptr, nullptr,
                      withGroup && ! ov.empty() ? &ov : nullptr);
            double sum = 0.0;
            for (float v : l) sum += (double) v * v;
            return std::sqrt (sum / 512.0);
        };

        const double base = rms (false);
        check (base > 0.0, "groups: the plain arc sounds");

        upic::ArcGroup g;
        g.id = 1; g.name = "G"; g.arcIds = { 1 };
        pg.groups = { g };
        check (std::abs (rms (true) / base - 1.0) < 1e-9,
               "groups: an empty group changes nothing");

        pg.groups[0].gain = 0.5f;
        check (std::abs (rms (true) / base - 0.5) < 1e-3,
               "groups: the gain scales the level");
        pg.groups[0].gain = 1.0f;

        pg.groups[0].envelopeId = 9;
        check (std::abs (rms (true) / base - 0.5) < 1e-3,
               "groups: the envelope override reaches the sound");

        pg.groups[0].envelopeId = -1;
        pg.groups[0].muted = true;
        check (rms (true) == 0.0, "groups: a muted group is silent");
        pg.groups[0].muted = false;

        // a wave override changes the timbre (the buffers differ)
        auto renderTo = [&] ()
        {
            UpicEngine e;
            e.prepare (48000.0, 512);
            std::vector<float> l (512, 0.0f);
            auto p = pg;
            auto ov = p.resolveArcOverrides();
            e.render (p, 0.0, waves, envs, slots, l.data(), nullptr, 512, 0.8f,
                      1.0, false, nullptr, nullptr, &ov);
            return l;
        };
        const auto l0 = renderTo();
        pg.groups[0].waveformId = 2;   // square
        const auto l1 = renderTo();
        bool differs = false;
        for (size_t i = 0; i < l0.size(); ++i)
            if (l0[i] != l1[i]) { differs = true; break; }
        check (differs, "groups: the wave override changes the timbre");
    }

    // WP3-d: the groups travel with the piece; hostile members are dropped.
    // WP3-d: the groups travel with the piece; hostile members are dropped.
    {
        upic::Page pg;
        pg.arcs = { arcWith (1, 220.0), arcWith (2, 330.0) };
        const auto plain = pg.toVar();
        check (! plain.hasProperty ("groups"),
               "groups: a page without groups writes no key");

        upic::ArcGroup g;
        g.id = 1; g.name = "A"; g.arcIds = { 1, 999, 1 };
        g.gain = 0.5f; g.transposeSemitones = 7.0; g.waveformId = 2;
        pg.groups = { g };
        const auto saved = pg.toVar();
        check (saved.hasProperty ("groups"), "groups: the groups travel with the page");
        const upic::Page back = upic::Page::fromVar (saved);
        check (back.groups.size() == 1 && back.groups[0].arcIds.size() == 1
               && back.groups[0].arcIds[0] == 1,
               "groups: an absent arc is dropped and a duplicate is folded",
               juce::String ((int) back.groups[0].arcIds.size()) + " member(s)");
        check (std::abs (back.groups[0].gain - 0.5f) < 1e-6f
               && back.groups[0].transposeSemitones == 7.0
               && back.groups[0].waveformId == 2,
               "groups: the attributes survive the round trip");
    }

    // WP3-e: the panel's commands through MainComponent.
    // WP3-e: the panel's commands through MainComponent.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":440},{"t":2,"f":440}]},{"id":2,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":330},{"t":2,"f":330}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json)), "groups: the test piece loads");

        mc.openGroupPanelForTest();
        check (mc.groupPanelOpenForTest(), "groups: the panel opens");

        mc.groupNewForTest();
        check (mc.groupCountForTest() == 1, "groups: New adds a group");
        check (mc.selectedGroupIdForTest() == mc.groupIdAtForTest (0),
               "groups: the new group is selected");

        mc.selectArcsForTest ({ 1, 2 });
        mc.groupAddSelForTest();
        check (mc.groupMemberCountAtForTest (0) == 2,
               "groups: the selection joins the group");
        check (mc.arcGroupIdForTest (1) == mc.groupIdAtForTest (0),
               "groups: membership is visible from the arc");

        mc.groupRenameForTest ("Wood");
        mc.groupGainForTest (0.5f);
        check (std::abs (mc.groupGainAtForTest (0) - 0.5f) < 1e-6f,
               "groups: the gain control reaches the model");
        mc.undo();
        check (std::abs (mc.groupGainAtForTest (0) - 1.0f) < 1e-6f,
               "groups: a level edit is one undo step");

        mc.groupTransposeForTest (7.0);
        mc.groupMuteForTest (true);
        mc.groupOverrideForTest (0, 2);
        check (std::abs (mc.groupTransposeAtForTest (0) - 7.0) < 1e-9
               && mc.groupMutedAtForTest (0)
               && mc.groupOverrideAtForTest (0, 0) == 2,
               "groups: the panel's controls reach the model");

        mc.selectArcsForTest ({ 1 });
        mc.groupRemoveSelForTest();
        check (mc.groupMemberCountAtForTest (0) == 1,
               "groups: Remove takes the selection out");
        mc.groupSelectMembersForTest();
        check (mc.selectedArcCountForTest() == 1,
               "groups: Select selects the members");

        // one group per arc: a second group takes the arc, the first keeps
        // the arc that stayed with it
        mc.groupNewForTest();
        mc.selectArcsForTest ({ 1 });
        mc.groupAddSelForTest();
        check (mc.groupMemberCountAtForTest (0) == 1 && mc.groupMemberCountAtForTest (1) == 1
               && mc.arcGroupIdForTest (1) == mc.groupIdAtForTest (1)
               && mc.arcGroupIdForTest (2) == mc.groupIdAtForTest (0),
               "groups: an arc belongs to one group at a time",
               "g0=" + juce::String (mc.groupMemberCountAtForTest (0))
                 + " g1=" + juce::String (mc.groupMemberCountAtForTest (1)));

        // the piece round trip keeps the groups
        const auto saved = mc.pieceToVar();
        check (saved.hasProperty ("pages") && mc.groupCountForTest() == 2,
               "groups: the piece still saves");
        auto mc2Holder = std::make_unique<MainComponent> (true);
        auto& mc2 = *mc2Holder;
        check (mc2.loadFromVar (saved) && mc2.groupCountForTest() == 2,
               "groups: the groups survive a piece round trip");

        // deleting a group leaves the arcs alone
        const int arcsBefore = mc.arcCountForTest();
        mc.groupDeleteForTest();
        check (mc.groupCountForTest() == 1, "groups: Delete removes the group");
        check (mc.arcCountForTest() == arcsBefore,
               "groups: deleting a group leaves the arcs alone");
    }

    // WP3-f: an old piece has no groups.
    // WP3-f: an old piece has no groups.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String oldJson = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":220},{"t":2,"f":440}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (oldJson)) && mc.groupCountForTest() == 0,
               "groups: an old piece has no groups");
    }
}

//==============================================================================
// WP4: the performance sequences. The position function is pure (the audio
// thread calls it with its own clock), the bank is piece-level and additive,
// and the recorder/transport commands work through the panel's callbacks.
void upic::runSequenceChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // WP4-a: the position function (forward, hold, reverse, page jump).
    // WP4-a: the position function (forward, hold, reverse, page jump).
    // WP4-a: the position function (forward, hold, reverse, page jump).
    {
        std::vector<upic::PerformancePoint> pts = {
            { 0.0, 0, 0.0 }, { 1.0, 0, 5.0 }, { 2.0, 0, 5.0 },
            { 2.5, 0, 3.0 }, { 3.0, 0, 0.0 }, { 3.5, 1, 0.0 },
            { 4.0, 1, 4.0 }
        };
        upic::SequencePosition p;
        check (upic::sequencePositionAt (pts, 0.5, p) && p.page == 0
               && std::abs (p.pos - 2.5) < 1e-9 && std::abs (p.rate - 5.0) < 1e-9,
               "sequences: a forward segment interpolates position and rate",
               juce::String (p.pos, 3) + " @ " + juce::String (p.rate, 3));
        check (upic::sequencePositionAt (pts, 1.5, p) && std::abs (p.pos - 5.0) < 1e-9
               && std::abs (p.rate) < 1e-9,
               "sequences: a hold keeps the position");
        check (upic::sequencePositionAt (pts, 2.75, p) && std::abs (p.pos - 1.5) < 1e-9
               && p.rate < 0.0,
               "sequences: a reverse segment reports a negative rate");
        check (upic::sequencePositionAt (pts, 3.4, p) && p.page == 0
               && upic::sequencePositionAt (pts, 3.6, p) && p.page == 1,
               "sequences: the page switches at the point's time");
        check (upic::sequencePositionAt (pts, -5.0, p) && std::abs (p.pos) < 1e-9,
               "sequences: before the start the position holds");
        check (upic::sequencePositionAt (pts, 99.0, p) && std::abs (p.pos - 4.0) < 1e-9,
               "sequences: after the end the position holds");
        check (std::abs (upic::sequenceLoopTime (4.25, 4.0) - 0.25) < 1e-9
               && std::abs (upic::sequenceLoopTime (-0.25, 4.0) - 3.75) < 1e-9,
               "sequences: the loop wraps in both directions");
    }

    // WP4-b: the recorder bank through MainComponent.
    // WP4-b: the recorder bank through MainComponent.
    // WP4-b: the recorder bank through MainComponent.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[]},{"duration":3.0,"arcs":[]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json)), "sequences: the two-page piece loads");
        check (mc.seqCountForTest() == 0, "sequences: a new piece has no sequence");
        check (! mc.pieceToVar().hasProperty ("sequences"),
               "sequences: a piece without sequences writes no key");

        mc.openSeqPanelForTest();
        check (mc.seqPanelOpenForTest(), "sequences: the panel opens");

        mc.seqRecordToggleForTest();
        check (mc.seqRecordingForTest() && mc.seqCountForTest() == 1,
               "sequences: Record starts a sequence");
        check (mc.seqPointCountAtForTest (0) == 1,
               "sequences: recording starts with the current position");

        mc.seqAppendForTest (0.5, 0, 0.5);
        mc.seqAppendForTest (1.0, 0, 1.0);
        mc.seqAppendForTest (1.0, 0, 1.0);   // a duplicate in time is harmless
        mc.seqAppendForTest (1.5, 1, 0.2);
        mc.seqRecordToggleForTest();
        check (! mc.seqRecordingForTest(), "sequences: Stop ends the recording");
        check (mc.seqPointCountAtForTest (0) == 4,
               "sequences: the recorded points are kept",
               juce::String (mc.seqPointCountAtForTest (0)) + " points");
        check (mc.seqPointTAtForTest (0, 1) <= mc.seqPointTAtForTest (0, 2)
               && mc.seqPointPageAtForTest (0, 3) == 1,
               "sequences: the points stay ordered and keep their pages");

        check (mc.seqLoopAtForTest (0), "sequences: a new sequence loops");
        mc.seqLoopToggleForTest();
        check (! mc.seqLoopAtForTest (0), "sequences: the loop toggle reaches the model");

        mc.seqPlayToggleForTest();
        check (mc.seqPlayActiveForTest(), "sequences: Play arms the sequence transport");
        check (mc.transportStateForTest() != MainComponent::TransportState::stopped,
               "sequences: the transport starts with the sequence");
        mc.seqPlayToggleForTest();
        check (! mc.seqPlayActiveForTest(), "sequences: Stop ends the sequence transport");

        // the piece round trip keeps the sequence
        const auto saved = mc.pieceToVar();
        check (saved.hasProperty ("sequences"), "sequences: the bank travels with the piece");
        auto mc2Holder = std::make_unique<MainComponent> (true);
        auto& mc2 = *mc2Holder;
        check (mc2.loadFromVar (saved) && mc2.seqCountForTest() == 1
               && mc2.seqPointCountAtForTest (0) == 4,
               "sequences: the points survive a piece round trip");

        mc.seqClearForTest();
        check (mc.seqPointCountAtForTest (0) == 0, "sequences: Clear erases the points");
    }

    // WP4-c2: the panel edits a recording (drag, Trim, Replace-tail) and the
    // WP4-c2: the panel edits a recording (drag, Trim, Replace-tail) and the
    // WP4-c2: the panel edits a recording (drag, Trim, Replace-tail) and the
    // ordinary Stop releases the sequence source.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json)), "sequences: the edit piece loads");
        mc.openSeqPanelForTest();
        mc.seqRecordToggleForTest();
        mc.seqAppendForTest (0.5, 0, 0.5);
        mc.seqAppendForTest (1.0, 0, 1.0);
        mc.seqAppendForTest (1.5, 0, 1.5);
        mc.seqRecordToggleForTest();
        check (mc.seqPointCountAtForTest (0) == 4, "sequences: the take is recorded");

        // the whole take is one undo step
        mc.undo();
        check (mc.seqPointCountAtForTest (0) == 0,
               "sequences: the take is one undo transaction",
               juce::String (mc.seqPointCountAtForTest (0)) + " points");
        mc.redo();

        // drag point 1 (t = 0.5) to 0.25
        mc.seqDragPointForTest (1, 0.25, 0, 0.25);
        check (std::abs (mc.seqPointTAtForTest (0, 1) - 0.25) < 1e-9,
               "sequences: a dragged point reaches the model",
               juce::String (mc.seqPointTAtForTest (0, 1), 3));
        mc.undo();
        check (std::abs (mc.seqPointTAtForTest (0, 1) - 0.5) < 1e-9,
               "sequences: the drag is one undo step");

        // Trim keeps the points up to the selected one
        mc.seqSelectPointForTest (2);
        mc.seqTrimForTest();
        check (mc.seqPointCountAtForTest (0) == 3,
               "sequences: Trim drops the points after the selection",
               juce::String (mc.seqPointCountAtForTest (0)) + " points");
        mc.undo();
        check (mc.seqPointCountAtForTest (0) == 4, "sequences: Trim is one undo step");

        // Replace re-records from the selected point, keeping what came before
        mc.seqSelectPointForTest (2);   // t = 1.0
        mc.seqReplaceForTest();
        check (mc.seqRecordingForTest(), "sequences: Replace arms the recorder");
        mc.seqAppendForTest (1.2, 0, 1.2);
        mc.seqRecordToggleForTest();
        check (mc.seqPointCountAtForTest (0) == 4
               && std::abs (mc.seqPointTAtForTest (0, 3) - 1.2) < 1e-9,
               "sequences: Replace keeps the head and re-records the tail",
               juce::String (mc.seqPointCountAtForTest (0)) + " points: "
               + juce::String (mc.seqPointTAtForTest (0, 0)) + ","
               + juce::String (mc.seqPointTAtForTest (0, 1)) + ","
               + juce::String (mc.seqPointTAtForTest (0, 2)) + ","
               + juce::String (mc.seqPointTAtForTest (0, 3)));

        // overwriting an existing take asks twice
        const int before = mc.seqPointCountAtForTest (0);
        mc.seqRecordToggleForTest();
        check (! mc.seqRecordingForTest() && mc.seqPointCountAtForTest (0) == before,
               "sequences: the first Record press only asks");
        mc.seqRecordToggleForTest();
        check (mc.seqRecordingForTest(), "sequences: the second press overwrites");
        mc.seqAppendForTest (0.3, 0, 0.3);
        mc.seqRecordToggleForTest();

        // the ordinary transport Stop releases the sequence source
        mc.seqPlayToggleForTest();
        check (mc.seqPlayActiveForTest(), "sequences: sequence playback is armed");
        mc.stopPlaybackForTest();
        check (! mc.seqPlayActiveForTest(),
               "sequences: the ordinary Stop returns to the internal transport");
    }
    // WP4-c: a hostile file is clamped to the pages and the limits.
    // WP4-c: a hostile file is clamped to the pages and the limits.
    // WP4-c: a hostile file is clamped to the pages and the limits.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[]}],"sequences":[{"id":1,"name":"Hostile","points":[{"t":1.0,"page":9,"pos":99.0},{"t":0.5,"page":0,"pos":0.5},{"t":0.5,"page":0,"pos":0.6}]},{"id":2,"name":"Second","points":[{"t":0,"page":0,"pos":0},{"t":1,"page":0,"pos":1}]},{"id":3,"name":"Third","points":[{"t":0,"page":0,"pos":0}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json)), "sequences: the hostile piece loads");
        check (mc.seqCountForTest() == 2, "sequences: the sequence bank is capped",
               juce::String (mc.seqCountForTest()) + " sequences");
        check (mc.seqPointPageAtForTest (0, 1) == 0
               && mc.seqPointPosAtForTest (0, 1) <= 2.0,
               "sequences: the pages and positions are clamped to the piece");
        check (mc.seqPointTAtForTest (0, 0) <= mc.seqPointTAtForTest (0, 1),
               "sequences: the points are sorted and deduplicated in time");
    }

    // WP4-d: an old piece has no sequences.
    // WP4-d: an old piece has no sequences.
    // WP4-d: an old piece has no sequences.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String oldJson = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[]}]})";
        check (mc.loadFromVar (juce::JSON::parse (oldJson)) && mc.seqCountForTest() == 0,
               "sequences: an old piece has no sequences");
    }
}

//==============================================================================
// WP5: the independent modulating arcs. A drawn arc on the same page bends a
// carrier's pitch or its level; the relationship is resolved once per
// snapshot, cycles are refused, and the arcs themselves are untouched.
void upic::runModulationChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };
    auto l2 = [] (double hz) { return std::log2 (hz); };

    // the factor: +/- depth octaves between the contour's bounds
    {
        double n01 = -1.0;
        const double top = upic::modulationFactor (880.0, l2 (220.0), l2 (880.0), 0.5, &n01);
        check (std::abs (top - std::pow (2.0, 0.5)) < 1e-9 && std::abs (n01 - 1.0) < 1e-9,
               "modulation: the contour's top is +depth octaves",
               juce::String (top, 6));
        const double mid = upic::modulationFactor (std::sqrt (220.0 * 880.0),
                                                   l2 (220.0), l2 (880.0), 0.5);
        check (std::abs (mid - 1.0) < 1e-9, "modulation: the middle is no bend");
        const double low = upic::modulationFactor (220.0, l2 (220.0), l2 (880.0), 0.5, &n01);
        check (std::abs (low - std::pow (2.0, -0.5)) < 1e-9 && std::abs (n01) < 1e-9,
               "modulation: the contour's bottom is -depth octaves",
               juce::String (low, 6));
    }

    // the resolver points at the modulator and refuses cycles
    {
        upic::Page pg;
        upic::Arc car;
        car.id = 1; car.waveformId = 0; car.envelopeId = 0; car.intensity = 0.05f;
        car.points = { { 0.0, 440.0 }, { 2.0, 440.0 } };
        upic::Arc m;
        m.id = 2; m.waveformId = 0; m.envelopeId = 0; m.intensity = 0.05f;
        m.muted = true;   // heard only through the carrier in this test
        m.points = { { 0.0, 220.0 }, { 2.0, 880.0 } };
        pg.arcs = { car, m };
        pg.arcs[0].modulatorArcId = 2;
        pg.arcs[0].modArcDepth = 0.5;
        auto ov = pg.resolveArcOverrides();
        check (ov[0].modIndex == 1 && ov[0].modTarget == 0
               && std::abs (ov[0].modDepth - 0.5) < 1e-9
               && std::abs (ov[0].modLoLog2 - l2 (220.0)) < 1e-9
               && std::abs (ov[0].modHiLog2 - l2 (880.0)) < 1e-9,
               "modulation: the resolver points at the modulator");
        check (ov[1].modIndex == -1, "modulation: an arc with no modulator has none");

        pg.arcs[0].modulatorArcId = 1;
        check (pg.resolveArcOverrides()[0].modIndex == -1,
               "modulation: self-modulation is refused");

        pg.arcs[0].modulatorArcId = 2;
        pg.arcs[1].modulatorArcId = 1;
        const auto cyc = pg.resolveArcOverrides();
        check (cyc[0].modIndex == -1 && cyc[1].modIndex == -1,
               "modulation: a cycle is refused");
    }

    // the engine: the pitch target bends, the amplitude target trembles
    {
        upic::Page pg;
        pg.durationSec = 2.0;
        upic::Arc car;
        car.id = 1; car.waveformId = 0; car.envelopeId = 0; car.intensity = 0.05f;
        car.points = { { 0.0, 440.0 }, { 2.0, 440.0 } };
        upic::Arc m;
        m.id = 2; m.waveformId = 0; m.envelopeId = 0; m.intensity = 0.05f;
        m.muted = true;   // heard only through the carrier in this test
        m.points = { { 0.0, 220.0 }, { 2.0, 880.0 } };
        pg.arcs = { car, m };

        const auto waves = upic::defaultWaveforms();
        const auto envs = upic::defaultEnvelopes();
        const std::vector<upic::SampleSlot> slots;
        auto rmsAt = [&] (const upic::Page& p, double t)
        {
            const auto ov = p.resolveArcOverrides();
            UpicEngine e;
            e.prepare (48000.0, 512);
            std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
            e.render (p, t, waves, envs, slots, l.data(), r.data(), 1024, 0.8f,
                      1.0, false, nullptr, nullptr, &ov);
            double s = 0.0;
            for (float v : l) s += (double) v * v;
            return std::sqrt (s / 1024.0);
        };

        const double base = rmsAt (pg, 0.0);
        pg.arcs[0].modulatorArcId = 2;
        pg.arcs[0].modArcDepth = 0.5;
        const double bent = rmsAt (pg, 0.0);
        check (std::abs (bent - base) > 1e-6,
               "modulation: the pitch target changes the render",
               juce::String (base, 5) + " -> " + juce::String (bent, 5));

        pg.arcs[0].modTarget = 1;
        pg.arcs[0].modArcDepth = 1.0;
        const double sil = rmsAt (pg, 0.0);      // the contour's bottom
        const double loud = rmsAt (pg, 1.9);     // the contour's top
        check (sil < base * 0.05 && loud > base * 0.5,
               "modulation: the amplitude target trembles with the contour",
               juce::String (sil, 5) + " vs " + juce::String (loud, 5));
    }

    // the keys travel; an old arc has no relationship
    {
        upic::Page pg;
        upic::Arc a;
        a.id = 1; a.points = { { 0.0, 220.0 }, { 1.0, 220.0 } };
        pg.arcs = { a };
        const auto plain = pg.toVar();
        check (! plain.hasProperty ("mod"), "modulation: an arc without one writes no key");

        pg.arcs[0].modulatorArcId = 7;
        pg.arcs[0].modTarget = 1;
        pg.arcs[0].modArcDepth = 0.25;
        pg.arcs[0].modAlign = 1;
        const auto back = upic::Page::fromVar (pg.toVar());
        check (back.arcs[0].modulatorArcId == 7 && back.arcs[0].modTarget == 1
               && std::abs (back.arcs[0].modArcDepth - 0.25) < 1e-9
               && back.arcs[0].modAlign == 1,
               "modulation: the relationship travels with the arc");
    }

    // load sanitisation and the UI command
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"mod":2,"modArcDepth":0.25,"points":[{"t":0,"f":440},{"t":2,"f":440}]},{"id":2,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":220},{"t":2,"f":880}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json)), "modulation: the test piece loads");
        check (mc.arcModulatorForTest (1) == 2 && std::abs (mc.arcModDepthForTest (1) - 0.25) < 1e-9,
               "modulation: the relationship survives the load");

        // the UI path: assign, undo, refuse a cycle
        mc.selectArcsForTest ({ 1 });
        mc.modApplyForTest (2, 0, 0.5);
        check (mc.arcModulatorForTest (1) == 2 && mc.arcModTargetForTest (1) == 0
               && std::abs (mc.arcModDepthForTest (1) - 0.5) < 1e-9,
               "modulation: the panel assigns the modulator");
        mc.undo();
        check (mc.arcModulatorForTest (1) == 2,
               "modulation: the assignment is one undo step (back to the loaded value)");

        mc.selectArcsForTest ({ 2 });
        mc.modApplyForTest (1, 0, 0.5);   // 1 -> 2 -> 1 would be a cycle
        check (mc.arcModulatorForTest (2) == -1,
               "modulation: the panel refuses a cycle");

        mc.selectArcsForTest ({ 1 });
        mc.modApplyForTest (1, 0, 0.5);   // self
        check (mc.arcModulatorForTest (1) == 2,
               "modulation: the panel refuses self-modulation");

        const juce::String cycJson = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"mod":2,"points":[{"t":0,"f":440},{"t":2,"f":440}]},{"id":2,"waveformId":0,"envelopeId":0,"mod":1,"points":[{"t":0,"f":220},{"t":2,"f":880}]}]}]})";
        auto mc2Holder = std::make_unique<MainComponent> (true);
        auto& mc2 = *mc2Holder;
        check (mc2.loadFromVar (juce::JSON::parse (cycJson))
               && mc2.arcModulatorForTest (1) == -1 && mc2.arcModulatorForTest (2) == -1,
               "modulation: a cyclic file is repaired on load");

        const juce::String missJson = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"mod":99,"points":[{"t":0,"f":440},{"t":2,"f":440}]}]}]})";
        auto mc3Holder = std::make_unique<MainComponent> (true);
        auto& mc3 = *mc3Holder;
        check (mc3.loadFromVar (juce::JSON::parse (missJson))
               && mc3.arcModulatorForTest (1) == -1,
               "modulation: a missing modulator is dropped on load");
    }
}

//==============================================================================
// WP5 verification targets (review 2026-11-19): the modulation's meaning in
// nonlinear/stepped spaces, the stability of the arc references, and the
// absence of non-finite output at sharp contours and reverse playback.
void upic::runModulationSemanticsChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 1. the factor scales the DRAWN frequency, before the space map
    {
        upic::Page pg;
        pg.spaceDefs[0].editable = true;
        pg.spaceDefs[0].points = { { 0.0f, (float) std::log2 (110.0) },
                                   { 1.0f, (float) std::log2 (1760.0) } };
        upic::Page::SpaceMap m = pg.spaceMap (0);
        upic::Arc a;
        a.points = { { 0.0, 440.0 }, { 1.0, 440.0 } };

        const double plain = UpicEngine::arcPitch (a, 0.5, m);
        const double bent = UpicEngine::arcPitch (a, 0.5, m, 2.0);
        // the factor is applied in the drawn domain: the table maps 880, it
        // is not an octave added after the table
        const double expected = std::exp2 (m.def->log2At (
            (float) ((std::log2 (880.0) - std::log2 (pg.pitchLo))
                     / (std::log2 (pg.pitchHi) - std::log2 (pg.pitchLo))),
            std::log2 (pg.pitchLo), std::log2 (pg.pitchHi)));
        check (std::abs (bent - expected) < 1e-6 && std::abs (bent - plain * 2.0) > 1e-6,
               "modulation: the factor bends the drawn pitch inside the table",
               juce::String (bent, 3) + " vs " + juce::String (expected, 3));

        // and a stepped space quantises the result, not the bend
        upic::Page sp;
        upic::Arc q;
        q.stepDivisions = 12;
        q.points = { { 0.0, 440.0 }, { 1.0, 440.0 } };
        const double stepped = UpicEngine::arcPitch (q, 0.5, sp.spaceMap (0), 1.5);
        const double steps = std::log2 (stepped / upic::kDefaultRefFreq) * 12.0;
        check (std::abs (steps - std::round (steps)) < 1e-6,
               "modulation: a stepped carrier quantises the modulated pitch",
               juce::String (steps, 4) + " steps");
    }

    // 2. the reference is the arc id; a deleted modulator is simply absent
    {
        upic::Page pg;
        upic::Arc car; car.id = 1; car.points = { { 0.0, 440.0 }, { 1.0, 440.0 } };
        upic::Arc mod; mod.id = 2; mod.points = { { 0.0, 220.0 }, { 1.0, 880.0 } };
        pg.arcs = { car, mod };
        pg.arcs[0].modulatorArcId = 2;
        check (pg.resolveArcOverrides()[0].modIndex == 1,
               "modulation: the relationship follows the arc id");

        // reordering the arcs keeps the id-based reference
        std::swap (pg.arcs[0], pg.arcs[1]);
        pg.arcs[1].modulatorArcId = 2;   // the carrier is now at index 1
        auto reordered = pg.resolveArcOverrides();
        check (reordered[1].modIndex == 0 && reordered[0].modIndex == -1,
               "modulation: reordering the page keeps the reference");

        // deleting the modulator drops the relationship, never indexes out
        pg.arcs.erase (pg.arcs.begin());
        const auto gone = pg.resolveArcOverrides();
        check (gone.size() == 1 && gone[0].modIndex == -1,
               "modulation: a deleted modulator leaves the carrier alone");

        // a duplicated carrier keeps its own relationship, a duplicate id is
        // repaired on load (the loader''s uniquify runs before this)
        upic::Page dup = pg;
        dup.arcs.push_back (dup.arcs[0]);
        const auto dupOv = dup.resolveArcOverrides();
        check (dupOv.size() == 2 && dupOv[0].modIndex == -1 && dupOv[1].modIndex == -1,
               "modulation: a duplicated carrier has no stale modulator");
    }

    // 3. sharp contours, reverse playback and extreme depths stay finite
    {
        upic::Page pg;
        pg.durationSec = 2.0;
        upic::Arc car; car.id = 1; car.waveformId = 0; car.envelopeId = 0;
        car.intensity = 0.2f; car.points = { { 0.0, 440.0 }, { 2.0, 440.0 } };
        upic::Arc mod; mod.id = 2; mod.waveformId = 0; mod.envelopeId = 0;
        mod.muted = true;
        // a sharp jump: 110 -> 1760 half way, twice
        mod.points = { { 0.0, 110.0 }, { 0.99, 110.0 }, { 1.0, 1760.0 },
                       { 1.01, 110.0 }, { 2.0, 1760.0 } };
        pg.arcs = { car, mod };
        pg.arcs[0].modulatorArcId = 2;
        pg.arcs[0].modArcDepth = 1.0;

        const auto waves = upic::defaultWaveforms();
        const auto envs = upic::defaultEnvelopes();
        const std::vector<upic::SampleSlot> slots;
        auto finite = [&] (double rate)
        {
            const auto ov = pg.resolveArcOverrides();
            UpicEngine e;
            e.prepare (48000.0, 512);
            std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
            e.render (pg, 0.98, waves, envs, slots, l.data(), r.data(), 2048, 0.8f,
                      rate, false, nullptr, nullptr, &ov);
            for (float v : l) if (! std::isfinite (v)) return false;
            for (float v : r) if (! std::isfinite (v)) return false;
            return true;
        };
        check (finite (1.0), "modulation: a sharp contour stays finite forward");
        check (finite (-1.0), "modulation: a sharp contour stays finite in reverse");

        pg.arcs[0].modTarget = 1;
        check (finite (1.0), "modulation: the amplitude target stays finite");
    }
}
//==============================================================================
// WP6.1: the mixer channel MODEL (no engine routing yet). The channel bank,
// the arc routing and the gain curve travel with the piece; a piece without
// channels is unchanged.
void upic::runChannelModelChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // the curve is a pure, allocation-free segment walk
    {
        upic::Channel c;
        c.envelope = { { 0.0f, 0.5f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } };
        upic::SegmentCursor cur;
        check (std::abs (c.envelopeAt (0.0f, cur) - 0.5f) < 1e-6f,
               "channels: the curve starts at its first point");
        upic::SegmentCursor cur2;
        check (std::abs (c.envelopeAt (0.25f, cur2) - 0.75f) < 1e-6f,
               "channels: the curve interpolates inside a segment",
               juce::String (c.envelopeAt (0.25f, cur2), 3));
        upic::SegmentCursor cur3;
        check (std::abs (c.envelopeAt (1.0f, cur3) - 0.0f) < 1e-6f,
               "channels: the curve ends at its last point");
        upic::Channel flat;
        upic::SegmentCursor cur4;
        check (flat.envelopeAt (0.3f, cur4) == 1.0f,
               "channels: an empty curve is unity");
    }

    // the bank travels with the piece
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"channels":[{"id":1,"name":"A","gain":0.5,"env":[{"x":0,"y":0},{"x":1,"y":1}]},{"id":2,"name":"B","mute":true}],"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"ch":2,"points":[{"t":0,"f":440},{"t":2,"f":440}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json)), "channels: the test piece loads");
        check (mc.channelCountForTest() == 2 && mc.channelIdAtForTest (0) == 1
               && std::abs (mc.channelGainAtForTest (0) - 0.5f) < 1e-6f
               && mc.channelEnvelopePointsAtForTest (0) == 2,
               "channels: the bank and its curve survive the load");
        check (mc.arcChannelForTest (1) == 2, "channels: the arc keeps its channel");

        const auto saved = mc.pieceToVar();
        check (saved.hasProperty ("channels"), "channels: the bank travels with the piece");
        auto mc2Holder = std::make_unique<MainComponent> (true);
        auto& mc2 = *mc2Holder;
        check (mc2.loadFromVar (saved) && mc2.channelCountForTest() == 2
               && mc2.arcChannelForTest (1) == 2,
               "channels: the bank survives a round trip");

        check (! mc.pieceToVar().hasProperty ("channels") == false,
               "channels: a piece with channels writes the key");
    }

    // WP6.4: the mixer panel's commands
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":440},{"t":2,"f":440}]},{"id":2,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":330},{"t":2,"f":330}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json)), "channels: the mixer piece loads");
        mc.openChannelPanelForTest();
        check (mc.channelPanelOpenForTest(), "channels: the mixer opens");
        mc.channelNewForTest();
        check (mc.channelCountForTest() == 1, "channels: New adds a channel");
        const int id = mc.channelIdAtForTest (0);
        mc.channelRenameForTest ("Wood");
        mc.channelGainForTest (0.6f);
        mc.channelMuteForTest (true);
        check (std::abs (mc.channelGainAtForTest (0) - 0.6f) < 1e-6f
               && mc.channelMutedAtForTest (0),
               "channels: the panel's gain and mute reach the model");
        mc.channelMuteForTest (false);
        mc.selectArcsForTest ({ 1, 2 });
        mc.channelAssignForTest (id);
        check (mc.arcChannelForTest (1) == id && mc.arcChannelForTest (2) == id,
               "channels: the panel routes the selection");
        mc.channelDeleteForTest();
        check (mc.channelCountForTest() == 0 && mc.arcChannelForTest (1) == 0,
               "channels: deleting the channel returns its arcs to Main");
        mc.undo();
        check (mc.channelCountForTest() == 1 && mc.arcChannelForTest (1) == id,
               "channels: the delete is one undo step");
        mc.closeChannelPanelForTest();
        check (! mc.channelPanelOpenForTest(), "channels: the mixer closes");
    }
    // an old piece has no bank and no routing
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String oldJson = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":220},{"t":2,"f":440}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (oldJson))
               && mc.channelCountForTest() == 0 && mc.arcChannelForTest (1) == 0,
               "channels: an old piece has no channels");
        check (! mc.pieceToVar().hasProperty ("channels"),
               "channels: a piece without channels writes no key");
    }

    // hostile references are repaired deterministically
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const juce::String json = R"({"currentPage":0,"channels":[{"id":1,"name":"A"}],"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"ch":99,"points":[{"t":0,"f":440},{"t":2,"f":440}]}]}]})";
        check (mc.loadFromVar (juce::JSON::parse (json))
               && mc.arcChannelForTest (1) == 0,
               "channels: a missing channel returns the arc to Main");

        juce::String big = "{\"currentPage\":0,\"channels\":[";
        for (int i = 1; i <= 40; ++i)
            big += juce::String (i > 1 ? "," : "") + "{\"id\":" + juce::String (i) + ",\"name\":\"C" + juce::String (i) + "\"}";
        big += "],\"pages\":[{\"duration\":1.0,\"arcs\":[]}]}";
        auto mc2Holder = std::make_unique<MainComponent> (true);
        auto& mc2 = *mc2Holder;
        check (mc2.loadFromVar (juce::JSON::parse (big))
               && mc2.channelCountForTest() == upic::limits::kMaxChannels,
               "channels: the bank is capped",
               juce::String (mc2.channelCountForTest()) + " channels");
    }
}

//==============================================================================
// WP6.4: the mixer panel's commands (open, channels, routing, curve, undo).
void upic::runChannelUiChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    MainComponent mc (true);
    const juce::String json = R"({"currentPage":0,"pages":[{"duration":2.0,"arcs":[{"id":1,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":440},{"t":2,"f":440}]},{"id":2,"waveformId":0,"envelopeId":0,"points":[{"t":0,"f":330},{"t":2,"f":330}]}]}]})";
    check (mc.loadFromVar (juce::JSON::parse (json)), "mixer ui: the piece loads");
    check (! mc.channelPanelOpenForTest(), "mixer ui: the panel starts closed");
    mc.openChannelPanelForTest();
    check (mc.channelPanelOpenForTest(), "mixer ui: the panel opens");

    mc.channelNewForTest();
    check (mc.channelCountForTest() == 1, "mixer ui: New adds a channel");
    const int id = mc.channelIdAtForTest (0);

    mc.channelRenameForTest ("Wood");
    mc.channelGainForTest (0.6f);
    mc.channelMuteForTest (true);
    check (std::abs (mc.channelGainAtForTest (0) - 0.6f) < 1e-6f
           && mc.channelMutedAtForTest (0),
           "mixer ui: gain and mute reach the model");
    mc.channelMuteForTest (false);

    mc.channelCurveForTest ({ { 0.0f, 0.25f }, { 1.0f, 0.75f } });
    check (mc.channelEnvelopePointsAtForTest (0) == 2,
           "mixer ui: a drawn curve reaches the bank");

    // WP6.4 (audit H3): a gain DRAG is one command - one undo step, not one
    // per mouse-move tick.
    {
        const int depthBefore = mc.undoDepth();
        mc.channelGainDragStartForTest();
        for (int i = 1; i <= 10; ++i)
            mc.channelGainForTest (0.05f * (float) i);
        mc.channelGainDragEndForTest();
        const int depthAfter = mc.undoDepth();
        check (depthAfter == depthBefore + 1,
               "mixer ui: a gain drag is ONE undo step",
               juce::String (depthBefore) + " -> " + juce::String (depthAfter));
        check (std::abs (mc.channelGainAtForTest (0) - 0.5f) < 1e-6f,
               "mixer ui: the drag's last value is the model's",
               juce::String (mc.channelGainAtForTest (0), 4));
        mc.undo();
        check (std::abs (mc.channelGainAtForTest (0) - 0.6f) < 1e-6f,
               "mixer ui: one undo restores the gain from before the drag",
               juce::String (mc.channelGainAtForTest (0), 4));
    }

    // WP6.4 (audit H4): the curve obeys the piece format's point cap, so a
    // drawn curve cannot change when the piece is reloaded.
    {
        std::vector<upic::EnvPoint> many;
        for (int i = 0; i < upic::limits::kMaxPointsPerEnvelope + 500; ++i)
            many.push_back ({ (float) i / (float) (upic::limits::kMaxPointsPerEnvelope + 500),
                              0.5f });
        mc.channelCurveForTest (many);
        check (mc.channelEnvelopePointsAtForTest (0) == upic::limits::kMaxPointsPerEnvelope,
               "mixer ui: the curve is capped at the piece format's limit",
               juce::String (mc.channelEnvelopePointsAtForTest (0)) + " points");
        const auto saved = mc.pieceToVar();
        auto mc2Holder = std::make_unique<MainComponent> (true);
        auto& mc2 = *mc2Holder;
        const bool loaded = mc2.loadFromVar (saved);
        const int points = mc2.channelEnvelopePointsAtForTest (0);
        check (loaded && points == upic::limits::kMaxPointsPerEnvelope,
               "mixer ui: the capped curve survives a round trip unchanged",
               juce::String (points) + " points");
    }

    mc.selectArcsForTest ({ 1, 2 });
    mc.channelAssignForTest (id);
    check (mc.arcChannelForTest (1) == id && mc.arcChannelForTest (2) == id,
           "mixer ui: the panel routes the selection");

    mc.channelDeleteForTest();
    check (mc.channelCountForTest() == 0 && mc.arcChannelForTest (1) == 0,
           "mixer ui: deleting the channel returns its arcs to Main");
    mc.undo();
    check (mc.channelCountForTest() == 1 && mc.arcChannelForTest (1) == id,
           "mixer ui: the delete is one undo step");

    mc.closeChannelPanelForTest();
    check (! mc.channelPanelOpenForTest(), "mixer ui: the panel closes");
}
