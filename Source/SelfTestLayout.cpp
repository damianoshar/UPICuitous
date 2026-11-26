#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// R0 (refactoring wave R0, part 1): the layout characterization.
//
// The card layout engine is the first thing the refactoring moves out of
// MainComponent (R1.1). Before it moves, the layout's behaviour is written
// down here: which cards exist, where they are, in what order, and what the
// layout must NOT do.
//
// The checks are deliberately structural, not golden: a rectangle in pixels
// depends on the fonts the machine has, so the contract is expressed as
// relations (inside the window, in the rank order, not overlapping, the same
// twice in a row, the document untouched). A golden rectangle would fail on
// a machine with a different font and pass on a layout that broke.
//
// This is a characterization suite: it describes what the layout does today,
// so the extraction can be checked against it.

namespace
{
    // The card order the user chose (v1.34.2: the rank table in resized()).
    // It is the same table the M3 check uses - this suite repeats it at
    // every size, and only for the cards that are actually there.
    const char* const kCardOrder[] = {
        "transport", "seq", "pages", "file", "voice", "amp", "mod", "ch",
        "sieve", "tools", "tempo", "accel", "levels", "material", "magnets",
        "scrub", "drag", "pitch", "page", "clock", "status"
    };

    // The viewports the layout has to survive: a desktop, the app's own
    // default, a compact desktop, a small tablet and the app's minimum
    // window (the F5 checks use the same minimum).
    struct Viewport { int w, h; const char* label; };
    const Viewport kViewports[] = {
        { 1600, 1000, "desktop" },
        { 1280,  800, "default" },
        { 1024,  700, "compact" },
        {  800,  600, "tablet" },
        {  460,  360, "minimum" }
    };

    juce::Component* findByName (juce::Component& c, const juce::String& n)
    {
        if (c.getName() == n) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findByName (*c.getChildComponent (i), n)) return r;
        return nullptr;
    }

    // The cards that are there, in the order the toolbar holds them.
    std::vector<juce::String> presentCards (MainComponent& mc)
    {
        std::vector<juce::String> names;
        for (auto* name : kCardOrder)
            if (mc.toolbarCardIndexForGroupForTest (name) >= 0)
                names.push_back (name);
        return names;
    }
}

void upic::runLayoutCharacterizationChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // A piece with something in it: the layout must not depend on the
    // document, but the document must survive the layout.
    const juce::String piece =
        "{\"master\":1,\"pages\":[{\"duration\":30,\"nextArcId\":3,\"arcs\":["
        "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"intensity\":0.8,"
        "\"points\":[{\"t\":0,\"f\":220},{\"t\":15,\"f\":440}]},"
        "{\"id\":2,\"mode\":0,\"waveformId\":2,\"envelopeId\":2,\"intensity\":0.5,"
        "\"points\":[{\"t\":5,\"f\":110},{\"t\":25,\"f\":330}]}]}]}";

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);
    check (mc.loadFromVar (juce::JSON::parse (piece)),
           "R0 the layout's piece loads");

    // --- every viewport: the cards are in the rank order --------------------
    for (const auto& vp : kViewports)
    {
        mc.setSize (vp.w, vp.h);
        const auto at = juce::String (vp.label);

        // the cards that are there follow the rank table
        const auto names = presentCards (mc);
        bool orderOk = names.size() > 0;
        juce::String detail;
        int previousIndex = -1;
        for (const auto& name : names)
        {
            const int idx = mc.toolbarCardIndexForGroupForTest (name);
            if (idx <= previousIndex)
            {
                orderOk = false;
                detail << name << " is at " << idx << " after " << previousIndex << "  ";
            }
            previousIndex = idx;
        }
        check (orderOk,
               "R0 the cards keep the rank order at " + at + " size",
               detail.trim());

        // and they are placed in that order on the screen too: the cards
        // wrap onto more rows in a narrow window, but the reading order
        // (top row first, left to right) is still the rank order
        bool visualOk = true;
        detail.clear();
        for (int i = 0; i + 1 < (int) names.size(); ++i)
        {
            const auto a = mc.toolbarCardBoundsForGroupForTest (names[(size_t) i]);
            const auto b = mc.toolbarCardBoundsForGroupForTest (names[(size_t) i + 1]);
            const bool ok = ! a.isEmpty() && ! b.isEmpty()
                          && (a.getY() < b.getY()
                              || (a.getY() == b.getY() && a.getX() < b.getX()));
            if (! ok)
            {
                visualOk = false;
                detail << names[(size_t) i] << a.toString() << " before "
                       << names[(size_t) i + 1] << b.toString() << "  ";
            }
        }
        check (visualOk,
               "R0 the cards are placed in the rank order at " + at + " size",
               detail.trim());

        // --- the cards do not overlap ---------------------------------------
        bool noOverlap = true;
        detail.clear();
        const int cards = mc.toolbarCardCountForTest();
        for (int i = 0; i < cards && noOverlap; ++i)
        {
            const auto a = mc.toolbarCardBoundsForTest (i);
            if (a.isEmpty()) continue;
            for (int k = i + 1; k < cards; ++k)
            {
                const auto b = mc.toolbarCardBoundsForTest (k);
                if (b.isEmpty()) continue;
                if (a.intersects (b))
                {
                    noOverlap = false;
                    detail << a.toString() << " overlaps " << b.toString() << "  ";
                    break;
                }
            }
        }
        check (noOverlap,
               "R0 the cards do not overlap at " + at + " size",
               detail.trim());

        // --- the cards stay inside the window and inside the toolbar ---------
        const auto toolbar = mc.toolbarBoundsForTest();
        const juce::Rectangle<int> window (0, 0, mc.getWidth(), mc.getHeight());
        bool inside = true;
        detail.clear();
        for (int i = 0; i < cards; ++i)
        {
            const auto c = mc.toolbarCardBoundsForTest (i);
            if (c.isEmpty()) continue;
            if (! window.contains (c) || ! toolbar.contains (c))
            {
                inside = false;
                detail << c.toString() << "  ";
            }
        }
        check (inside,
               "R0 the cards stay inside the window at " + at + " size",
               detail.trim());

        // --- every toolbar control sits on a card ----------------------------
        // (the v1.33.3 check, repeated at every size: a group that wraps
        //  inside itself has to follow it line by line)
        {
            bool allOnCards = true;
            detail.clear();
            int toolbarControls = 0;
            for (int i = 0; i < mc.getNumChildComponents(); ++i)
            {
                auto* c = mc.getChildComponent (i);
                if (c == nullptr || ! c->isVisible() || c->getBounds().isEmpty()) continue;
                if (! toolbar.contains (c->getBounds())) continue;
                ++toolbarControls;
                bool on = false;
                for (int k = 0; k < cards && ! on; ++k)
                    if (mc.toolbarCardBoundsForTest (k).contains (c->getBounds()))
                        on = true;
                if (! on)
                {
                    allOnCards = false;
                    detail << c->getName() << c->getBounds().toString() << "  ";
                }
            }
            check (allOnCards && toolbarControls > 0,
                   "R0 every toolbar control sits on a card at " + at + " size",
                   detail.trim());
        }
    }

    // --- the layout is the same twice in a row ------------------------------
    {
        mc.setSize (1280, 800);
        std::vector<juce::Rectangle<int>> first;
        for (int i = 0; i < mc.toolbarCardCountForTest(); ++i)
            first.push_back (mc.toolbarCardBoundsForTest (i));

        mc.resized();
        bool same = true;
        juce::String detail;
        for (int i = 0; i < mc.toolbarCardCountForTest(); ++i)
        {
            const auto again = mc.toolbarCardBoundsForTest (i);
            if (i < (int) first.size() && again != first[(size_t) i])
            {
                same = false;
                detail << i << ": " << first[(size_t) i].toString() << " -> "
                       << again.toString() << "  ";
            }
        }
        check (same, "R0 the layout is the same when it is calculated again", detail.trim());

        // and the same after a round trip through another size
        mc.setSize (1024, 700);
        mc.setSize (1280, 800);
        bool sameAfter = true;
        detail.clear();
        for (int i = 0; i < mc.toolbarCardCountForTest(); ++i)
        {
            const auto again = mc.toolbarCardBoundsForTest (i);
            if (i < (int) first.size() && again != first[(size_t) i])
            {
                sameAfter = false;
                detail << i << ": " << first[(size_t) i].toString() << " -> "
                       << again.toString() << "  ";
            }
        }
        check (sameAfter,
               "R0 the layout is the same after a resize and back", detail.trim());
    }

    // --- the layout does not touch the document -----------------------------
    {
        mc.setSize (1280, 800);
        const double duration = mc.pageDurationForTest();
        const int arcs = mc.arcCountForTest();
        const int undo = mc.undoDepth();

        mc.setSize (460, 360);
        mc.setSize (1600, 1000);
        mc.setSize (1280, 800);

        check (std::abs (mc.pageDurationForTest() - duration) < 1e-9
               && mc.arcCountForTest() == arcs
               && mc.undoDepth() == undo,
               "R0 the layout does not change the document",
               juce::String (mc.pageDurationForTest(), 3) + " s, "
                 + juce::String (mc.arcCountForTest()) + " arc(s), "
                 + juce::String (mc.undoDepth()) + " undo step(s)");
    }

    // --- the permanent strip keeps its controls at every size ---------------
    {
        bool stripOk = true;
        juce::String detail;
        for (const auto& vp : kViewports)
        {
            mc.setSize (vp.w, vp.h);
            const auto strip = mc.permanentStripBoundsForTest();
            if (strip.isEmpty())
            {
                stripOk = false;
                detail << vp.label << ": no strip  ";
                continue;
            }
            for (const char* name : { "File", "Play", "Stop", "toolBox", "restoreGroupsBtn" })
            {
                auto* c = findByName (mc, name);
                if (c == nullptr || ! c->isVisible() || ! strip.contains (c->getBounds()))
                {
                    stripOk = false;
                    detail << vp.label << "/" << name << "="
                           << (c != nullptr ? c->getBounds().toString() : juce::String ("null"))
                           << "  ";
                }
            }
        }
        check (stripOk, "R0 the permanent strip keeps its controls at every size",
               detail.trim());
    }
}
