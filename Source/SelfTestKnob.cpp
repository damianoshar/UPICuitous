#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// F11 (2026-11-18): the knob row.
//
// The toolbar's continuous amounts are rotary knobs now. The checks pin
// down:
//   - every toolbar amount is a knob, and the settings overlay's OSC port
//     fields stay horizontal sliders;
//   - the knob is rotary in the wide layout and falls back to the
//     horizontal slider when the layout is too small to read it;
//   - the knob writes the same value to the same model field as the old
//     slider did, and a typed value goes through the same path;
//   - the value survives the piece's save and load;
//   - the layout still tiles: the knobs never overlap and never leave the
//     window.

namespace
{
    // the intensity of one arc in the piece
    double arcIntensity (const juce::var& piece, int arcId)
    {
        auto* pages = piece.getProperty ("pages", juce::var()).getArray();
        if (pages == nullptr || pages->isEmpty()) return -1.0;
        auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray();
        if (arcs == nullptr) return -1.0;
        for (const auto& a : *arcs)
            if ((int) a.getProperty ("id", -1) == arcId)
                return (double) a.getProperty ("intensity", -1.0);
        return -1.0;
    }
}

void upic::runKnobChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // the recursive name search (a control can live below the component)
    std::function<juce::Component*(juce::Component&, const juce::String&)> findByNameKnob =
        [&findByNameKnob] (juce::Component& c, const juce::String& n) -> juce::Component*
    {
        if (c.getName() == n) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findByNameKnob (*c.getChildComponent (i), n)) return r;
        return nullptr;
    };

    MainComponent mc (true);
    mc.setSize (1280, 800);

    auto find = [&] (const juce::String& n) { return findByNameKnob (mc, n); };

    // --- the toolbar's amounts are knobs --------------------------------
    {
        const char* names[] = { "tempoSlider", "intenSlider", "durSlider", "masterSlider",
                                "stickSlider", "stepDivSlider", "warpSlider",
                                "scrubLenSlider", "scrubCrossSlider", "scrubTailSlider",
                                "scrubGrainSlider", "dragSmoothSlider",
                                "refSlider", "levelSlider", "grainSlider", "inGainSlider",
                                "liveGrainSlider", "pmRatioSlider", "pmIndexSlider",
                                "pmFeedbackSlider", "modRateSlider", "modDepthSlider" };
        int knobs = 0, missing = 0;
        for (auto n : names)
        {
            if (dynamic_cast<upic::Knob*> (find (n)) != nullptr)
                ++knobs;
            else
                ++missing;
        }
        check (knobs == 22 && missing == 0,
               "F11 every toolbar amount is a knob",
               juce::String (knobs) + " knobs, " + juce::String (missing) + " missing");

        // the settings overlay's OSC port fields stay horizontal sliders -
        // they are not in the toolbar, and they are a port number, not an
        // amount
        auto* port = dynamic_cast<juce::Slider*> (find ("oscPortSlider"));
        auto* mcPort = dynamic_cast<juce::Slider*> (find ("oscMcPortSlider"));
        check (port != nullptr && mcPort != nullptr
               && dynamic_cast<upic::Knob*> (port) == nullptr
               && dynamic_cast<upic::Knob*> (mcPort) == nullptr
               && port->getSliderStyle() == juce::Slider::LinearHorizontal
               && mcPort->getSliderStyle() == juce::Slider::LinearHorizontal,
               "F11 the OSC port fields stay horizontal sliders");
    }

    // --- the wide layout is rotary, the narrow one falls back -----------
    {
        auto* knob = dynamic_cast<upic::Knob*> (find ("intenSlider"));
        check (knob != nullptr
               && knob->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
               && ! knob->isFallback(),
               "F11 the wide layout shows rotary knobs",
               knob != nullptr ? juce::String (knob->getSliderStyle()) : juce::String ("no knob"));

        // the rule itself: a knob below the readable size falls back
        check (! upic::Knob::shouldFallBack (1.0f)
               && upic::Knob::shouldFallBack (0.5f),
               "F11 the fallback rule follows the layout's scale");

        // the narrow layout: the knob is too small to read, so the
        // horizontal slider takes over (decision 14)
        mc.setSize (460, 360);
        check (knob != nullptr && knob->isFallback()
               && knob->getSliderStyle() == juce::Slider::LinearHorizontal,
               "F11 the narrow layout falls back to the horizontal slider",
               knob != nullptr ? (knob->isFallback() ? "fallback" : "rotary")
                                : juce::String ("no knob"));

        // the fallback keeps the value in its own box beside the track
        if (knob != nullptr)
        {
            const auto track = knob->fallbackTrackArea();
            const auto pill = knob->valuePillArea();
            check (pill.getX() >= track.getRight()
                   && pill.getRight() <= knob->getWidth()
                   && pill.getWidth() >= 20 && pill.getHeight() >= 8,
                   "F11 the fallback's value box sits beside the track",
                   pill.toString() + " vs track " + track.toString());
        }

        mc.setSize (1280, 800);
        check (knob != nullptr && ! knob->isFallback(),
               "F11 the wide layout comes back to the rotary knob");
    }

    // --- the knob writes the same value to the same field ---------------
    {
        auto* score = dynamic_cast<ScoreEditor*> (find ("score"));
        auto* inten = dynamic_cast<upic::Knob*> (find ("intenSlider"));
        if (score == nullptr || inten == nullptr)
        {
            check (false, "F11 the knob test setup", "no score or no knob");
            return;
        }

        score->setSelectedArcs ({ 1, 3 });
        inten->setValue (0.5, juce::sendNotificationSync);
        const auto piece = mc.pieceToVar();
        check (std::abs (arcIntensity (piece, 1) - 0.5) < 1e-6
               && std::abs (arcIntensity (piece, 3) - 0.5) < 1e-6,
               "F11 the knob writes the same value to the same field",
               juce::String (arcIntensity (piece, 1)) + " / "
                   + juce::String (arcIntensity (piece, 3)));

        // a typed value (the old sliders' value box) takes the same path
        score->setSelectedArcs ({ 1 });
        const bool typed = inten->applyTypedValue ("0.25");
        check (typed && std::abs (inten->getValue() - 0.25) < 1e-6
               && std::abs (arcIntensity (mc.pieceToVar(), 1) - 0.25) < 1e-6,
               "F11 a typed value goes through the knob's own path",
               juce::String (inten->getValue()));

        // a value outside the range is refused
        const double before = inten->getValue();
        check (! inten->applyTypedValue ("9.0") && inten->getValue() == before,
               "F11 a typed value outside the range is refused",
               juce::String (inten->getValue()));
    }

    // --- the value survives the piece's save and load -------------------
    {
        const auto saved = mc.pieceToVar();
        const double savedIntensity = arcIntensity (saved, 1);

        MainComponent mc2 (true);
        mc2.setSize (1280, 800);
        mc2.loadFromVar (juce::JSON::parse (juce::JSON::toString (saved)));
        const double loadedIntensity = arcIntensity (mc2.pieceToVar(), 1);

        check (savedIntensity >= 0.0 && std::abs (savedIntensity - loadedIntensity) < 1e-9,
               "F11 the knob's value survives the piece's save and load",
               juce::String (savedIntensity) + " -> " + juce::String (loadedIntensity));
    }

    // --- the knob's own readout -----------------------------------------
    {
        auto* knob = dynamic_cast<upic::Knob*> (find ("scrubLenSlider"));
        if (knob != nullptr)
        {
            knob->setValue (250.0, juce::sendNotificationSync);
            check (knob->valueText (true).contains ("250")
                   && knob->valueText (true).contains ("ms")
                   && ! knob->valueText (false).contains ("ms"),
                   "F11 the knob shows the value and the suffix",
                   knob->valueText (true));
            check (knob->getTooltip().contains ("250"),
                   "F11 the knob's tooltip carries the live value",
                   knob->getTooltip());
        }
        else
        {
            check (false, "F11 the knob's own readout", "no knob");
        }

        // the face's text is shortened until it fits: the old value box
        // showed "12.000 s", which does not fit on a knob's face
        auto* dur = dynamic_cast<upic::Knob*> (find ("durSlider"));
        if (dur != nullptr)
        {
            dur->setValue (12.0, juce::sendNotificationSync);
            const auto face = dur->faceText (31, 31);
            check (face.isNotEmpty() && face.contains ("12")
                   && face.length() < dur->valueText (true).length()
                   && dur->getTooltip().contains ("12"),
                   "F11 the knob's face shortens a long value",
                   face + " <- " + dur->valueText (true));

            // the value has its own box beside the knob (the reference's
            // pill): the box starts past the knob's square and stays inside
            const auto pill = dur->valuePillArea();
            const int side = juce::jmin (dur->getWidth(), dur->getHeight());
            check (pill.getX() >= side + 2 && pill.getRight() <= dur->getWidth()
                   && pill.getWidth() >= 32 && pill.getHeight() >= 10,
                   "F11 the value has its own box beside the knob",
                   pill.toString() + " in " + dur->getBounds().toString());
        }
        else
        {
            check (false, "F11 the knob's face shortens a long value", "no knob");
        }
    }

    // --- the layout still tiles ------------------------------------------
    {
        const char* knobNames[] = { "tempoSlider", "intenSlider", "durSlider", "masterSlider",
                                    "stickSlider", "scrubLenSlider", "dragSmoothSlider",
                                    "refSlider" };
        for (auto size : { juce::Point<int> (1280, 800), juce::Point<int> (460, 360) })
        {
            mc.setSize (size.x, size.y);

            juce::String detail;
            bool ok = true;
            std::vector<juce::Rectangle<int>> placed;
            for (auto n : knobNames)
            {
                auto* c = find (n);
                if (c == nullptr || ! c->isVisible()) continue;
                const auto b = c->getBounds();
                if (b.getWidth() < 8 || b.getHeight() < 8
                    || b.getX() < 0 || b.getY() < 0
                    || b.getRight() > size.x || b.getBottom() > size.y)
                {
                    ok = false;
                    detail << n << "=" << b.toString() << "  ";
                    continue;
                }
                for (const auto& other : placed)
                    if (other.intersects (b))
                    {
                        ok = false;
                        detail << "OVERLAP " << n << " " << b.toString() << " vs "
                               << other.toString() << "  ";
                    }
                placed.push_back (b);
            }
            check (ok,
                   "F11 the knobs tile the toolbar at "
                       + juce::String (size.x) + "x" + juce::String (size.y),
                   detail.trim());
        }
        mc.setSize (1280, 800);
    }
}
