#include <JuceHeader.h>
#include "SelfTest.h"
#include "ScoreEditor.h"

#include <cmath>

//==============================================================================
// R5.2 (the review's R044): the coordinate mapper - the contract.
//
// The score canvas maps the page's time and pitch onto the screen: X is the
// time, Y is the pitch (logarithmic, over the page's ambitus). The mapping
// is what the drawing, the hit testing and the cursor's readout all use, so
// it has to be one mapping and not three that happen to agree.
//
// The review gives it a name and a home - "Introduce ScoreCoordinateMapper"
// (R044) - and asks for consistent mappings between the musical and the
// screen coordinates (§24).
//
// This suite is the mapper's contract, and it comes before the move, as
// every phase in this wave does. It pins what the mapping does today:
//
// - where time 0 and the page's end are on the screen;
// - the round trip: a time on the screen is the time that was mapped, and
//   the same for a pitch;
// - the ambitus: its low end is the plot's bottom and its high end the
//   plot's top;
// - a pitch outside the ambitus is NOT clamped (it is drawn off the edge);
// - a zone's rectangle does not depend on the order of its times and its
//   frequencies;
// - the zoom keeps the view's centre where it was;
// - the mapping clamps a cursor outside the page to the page's ends.
//
// The suite reaches the mapping through the editor's own doors
// (timeToX(), xToTime(), freqToY(), yToFreq(), zoneRectForTest()), which
// is where the app reaches it too. The move gives the mapping a header of
// its own; the checks themselves do not change.

namespace
{
    // The plot's own margins (the same four numbers ScoreEditor.cpp uses).
    constexpr float kPlotLeft = 6.0f, kPlotTop = 4.0f;
    constexpr float kPlotRight = 794.0f, kPlotBottom = 394.0f;

    struct Rig
    {
        std::vector<upic::Waveform> waves;
        upic::Page page;
        double playhead = 0.0;
        juce::CriticalSection lock;
        ScoreEditor ed;

        Rig()
        {
            waves.emplace_back (7, "Red", juce::Colours::red);
            page.durationSec = 8.0;

            ed.setModelLock (&lock);
            ed.bind (&page, &waves, &playhead, nullptr);
            ed.setSize (800, 400);
        }
    };
}

void upic::runCoordinateMapperChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the time axis -------------------------------------------------------
    {
        Rig rig;

        check (std::abs (rig.ed.timeToX (0.0) - kPlotLeft) < 1e-4f,
               "R5.2 the time 0 is the plot's left edge",
               juce::String (rig.ed.timeToX (0.0), 4));

        check (std::abs (rig.ed.timeToX (8.0) - kPlotRight) < 1e-4f,
               "R5.2 the page's end is the plot's right edge",
               juce::String (rig.ed.timeToX (8.0), 4));

        check (std::abs (rig.ed.xToTime (kPlotLeft)) < 1e-9
               && std::abs (rig.ed.xToTime (kPlotRight) - 8.0) < 1e-9,
               "R5.2 the plot's edges are the page's ends",
               juce::String (rig.ed.xToTime (kPlotLeft), 4) + " / "
                   + juce::String (rig.ed.xToTime (kPlotRight), 4));

        // The screen coordinates are floats, so the round trip is exact to
        // the float's precision, not to the last bit.
        bool roundTrip = true;
        juce::String detail;
        for (const double time : { 0.0, 1.0, 3.7, 8.0 })
        {
            const double back = rig.ed.xToTime (rig.ed.timeToX (time));
            if (std::abs (back - time) > 1e-5)
            {
                roundTrip = false;
                detail = juce::String (time, 3) + " -> " + juce::String (back, 8);
                break;
            }
        }
        check (roundTrip, "R5.2 a time survives the round trip", detail);

        check (std::abs (rig.ed.xToTime (-500.0f)) < 1e-9
               && std::abs (rig.ed.xToTime (5000.0f) - 8.0) < 1e-9,
               "R5.2 a cursor outside the page is clamped to its ends",
               juce::String (rig.ed.xToTime (-500.0f), 4) + " / "
                   + juce::String (rig.ed.xToTime (5000.0f), 4));
    }

    // --- the pitch axis ------------------------------------------------------
    {
        Rig rig;

        check (std::abs (rig.ed.freqToY (rig.ed.ambitusLo()) - kPlotBottom) < 1e-3f,
               "R5.2 the ambitus's low end is the plot's bottom",
               juce::String (rig.ed.freqToY (rig.ed.ambitusLo()), 4));

        check (std::abs (rig.ed.freqToY (rig.ed.ambitusHi()) - kPlotTop) < 1e-3f,
               "R5.2 the ambitus's high end is the plot's top",
               juce::String (rig.ed.freqToY (rig.ed.ambitusHi()), 4));

        bool roundTrip = true;
        juce::String detail;
        for (const double freq : { 100.0, 440.0, 3000.0 })
        {
            const double back = rig.ed.yToFreq (rig.ed.freqToY (freq));
            if (std::abs (back - freq) > juce::jmax (1e-3, freq * 1e-6))
            {
                roundTrip = false;
                detail = juce::String (freq, 1) + " -> " + juce::String (back, 6);
                break;
            }
        }
        check (roundTrip, "R5.2 a pitch survives the round trip", detail);

        check (rig.ed.freqToY (rig.ed.ambitusHi() * 2.0) < kPlotTop,
               "R5.2 a pitch above the ambitus is not clamped",
               juce::String (rig.ed.freqToY (rig.ed.ambitusHi() * 2.0), 4));

        check (rig.ed.freqToY (rig.ed.ambitusLo() / 2.0) > kPlotBottom,
               "R5.2 a pitch below the ambitus is not clamped",
               juce::String (rig.ed.freqToY (rig.ed.ambitusLo() / 2.0), 4));
    }

    // --- a zone's rectangle --------------------------------------------------
    {
        Rig rig;
        const int id = rig.ed.addZone (5.0, 3.0, 800.0, 200.0);
        const auto rect = rig.ed.zoneRectForTest (id);

        const float x0 = rig.ed.timeToX (3.0);
        const float x1 = rig.ed.timeToX (5.0);
        const float yTop = rig.ed.freqToY (800.0);
        const float yBot = rig.ed.freqToY (200.0);

        check (id > 0 && std::abs (rect.getX() - x0) < 1e-3f
               && std::abs (rect.getRight() - x1) < 1e-3f
               && std::abs (rect.getY() - yTop) < 1e-3f
               && std::abs (rect.getBottom() - yBot) < 1e-3f,
               "R5.2 a zone's rectangle sorts its times and its frequencies",
               rect.toString());
    }

    // --- the zoom ------------------------------------------------------------
    {
        Rig rig;
        const double centreBefore = rig.ed.xToTime (400.0f);
        const double timeBefore = rig.ed.xToTime (rig.ed.timeToX (3.7));

        rig.ed.zoomBy (2.0);

        check (std::abs (rig.ed.getTimeZoom() - 2.0) < 1e-9,
               "R5.2 the zoom changes the time zoom",
               juce::String (rig.ed.getTimeZoom(), 3));

        check (std::abs (rig.ed.xToTime (400.0f) - centreBefore) < 1e-9,
               "R5.2 the zoom keeps the view's centre where it was",
               juce::String (centreBefore, 4) + " -> "
                   + juce::String (rig.ed.xToTime (400.0f), 4));

        check (std::abs (rig.ed.xToTime (rig.ed.timeToX (3.7)) - timeBefore) < 1e-5,
               "R5.2 a time survives the round trip through the zoom",
               juce::String (timeBefore, 4) + " -> "
                   + juce::String (rig.ed.xToTime (rig.ed.timeToX (3.7)), 4));
    }
}
