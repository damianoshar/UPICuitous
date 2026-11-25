#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "AllocCounter.h"

//==============================================================================
// The interface: the drawing, the editors, the arc list, the toolbar, the
// PM arc, the widgets and the envelope points.

void upic::runDrawingChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6a-12. v1.33.4 (L12): drawing on a micro-time page. The review:
    //     "the stroke sampler and the quantiser dedup enforce a 15 ms
    //     minimum point spacing, so a 5 ms page keeps one point and the
    //     arc is dropped".
    {
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByNameL12 =
            [&findByNameL12](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByNameL12(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };

        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);
        // the shortest page the app allows: 5 ms
        mc.loadFromVar (juce::JSON::parse ("{\"pages\":[{\"duration\":0.005,\"arcs\":[]}]}"));

        auto* sc = dynamic_cast<ScoreEditor*> (findByNameL12 (mc, "score"));
        check (sc != nullptr, "the canvas is there for the micro-time test");
        if (sc != nullptr)
        {
            sc->setTool (ScoreEditor::Draw);
            const auto b = sc->getLocalBounds().toFloat();
            // the stroke stays above the pitch-centre line (that band is
            // the playhead's scrub target)
            const auto p0 = juce::Point<float> (b.getX() + b.getWidth() * 0.25f,
                                                b.getY() + b.getHeight() * 0.22f);
            const auto p1 = juce::Point<float> (b.getX() + b.getWidth() * 0.60f,
                                                b.getY() + b.getHeight() * 0.30f);
            const auto now = juce::Time::getCurrentTime();
            const auto src = juce::Desktop::getInstance().getMainMouseSource();
            juce::MouseEvent down (src, p0, juce::ModifierKeys(), 1.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f, sc, sc, now, p0, now, 1, false);
            sc->mouseDown (down);
            juce::MouseEvent drag (src, p1, juce::ModifierKeys(), 1.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f, sc, sc, now, p0, now, 1, true);
            sc->mouseDrag (drag);
            sc->mouseUp (drag);

            const auto piece = mc.pieceToVar();
            auto* pages = piece.getProperty ("pages", juce::var()).getArray();
            auto* arcs = pages != nullptr && pages->size() > 0
                            ? pages->getReference (0).getProperty ("arcs", juce::var()).getArray()
                            : nullptr;
            check (arcs != nullptr && arcs->size() == 1,
                   "a stroke on a 5 ms page still makes an arc",
                   arcs != nullptr ? juce::String (arcs->size()) : juce::String ("no arcs"));
            if (arcs != nullptr && arcs->size() == 1)
            {
                auto* pts = arcs->getReference (0).getProperty ("points", juce::var()).getArray();
                check (pts != nullptr && pts->size() >= 2,
                       "the micro-time arc keeps its points",
                       pts != nullptr ? juce::String (pts->size()) : juce::String ("none"));
                if (pts != nullptr && pts->size() >= 2)
                {
                    const double t0 = (double) pts->getReference (0).getProperty ("t", 0.0);
                    const double t1 = (double) pts->getReference (pts->size() - 1)
                                            .getProperty ("t", 0.0);
                    check (t1 > t0, "the points of the micro-time arc are spread out",
                           juce::String (t0, 5) + " .. " + juce::String (t1, 5));
                }
            }
        }
    }

}

void upic::runEditorChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 17. ScoreEditor interactions (in-process, no window): selection,
    //     shortcuts, arrow nudge, sieve-aware steps, zones, time-strip scrub
    {
        auto waves = upic::defaultWaveforms();
        double ph = 0.0;
        juce::CriticalSection lock;
        upic::Page pg;
        pg.durationSec = 10.0;
        for (int i = 0; i < 3; ++i)
        {
            upic::Arc a; a.id = i + 1;
            a.points = { { 1.0 + i, 220.0 * (i + 1) }, { 3.0 + i, 440.0 * (i + 1) } };
            pg.arcs.push_back(a);
        }
        ScoreEditor ed;
        ed.setModelLock(&lock);
        ed.bind(&pg, &waves, &ph, nullptr);
        ed.setSize(1000, 400);

        // v1.33.2: the global sieve cannot become empty (the checkbox is
        // the off switch); an empty list keeps the last valid set.
        ed.setSieve (true, 12, { 0 });
        ed.setSieve (true, 12, {});
        check (ed.getSieveR() == std::vector<int>({ 0 }),
               "setSieve(empty) keeps the last remainders");
        ed.setSieve (false, 12, { 0 });

        ed.keyPressed(juce::KeyPress('A', juce::ModifierKeys::commandModifier, 0));
        check(ed.getSelectedArcs().size() == 3, "ctrl+A selects all arcs",
              juce::String((int) ed.getSelectedArcs().size()));

        const double t0 = pg.arcs[0].points[0].t;
        ed.keyPressed(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(), 0));
        check(std::abs(pg.arcs[0].points[0].t - (t0 + 0.02)) < 1e-9,
              "right arrow nudges selection +0.02s", juce::String(pg.arcs[0].points[0].t, 4));

        ed.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys(), 0));
        check(pg.arcs.empty(), "Del deletes the whole selection");

        upic::Arc one; one.id = pg.nextArcId++;
        one.points = { { 2.0, 300.0 }, { 5.0, 600.0 } };
        pg.arcs.push_back(one);
        ed.selectAll();
        ed.keyPressed(juce::KeyPress('D', juce::ModifierKeys::commandModifier, 0));
        check(pg.arcs.size() == 2, "ctrl+D duplicates", juce::String((int) pg.arcs.size()));
        check(pg.arcs.size() == 2 && pg.arcs[1].id != pg.arcs[0].id, "duplicate gets a fresh id");
        check(pg.arcs.size() == 2
              && std::abs(pg.arcs[1].points[0].t - pg.arcs[0].points[0].t) > 0.05,
              "duplicate is time-offset");

        // sieve-aware up/down: A440 with sieve {9} (A's only) -> A5 -> back
        upic::Page ap; ap.durationSec = 10.0;
        upic::Arc a1; a1.id = 1; a1.points = { { 1.0, 440.0 }, { 3.0, 440.0 } };
        ap.arcs.push_back(a1);
        ed.bind(&ap, &waves, &ph, nullptr);
        ed.setSieve(true, 12, { 9 });
        ed.setStickiness(1.0);
        ed.setTimeGrid(0.0);
        ed.setSelectedArc(1);
        ed.keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys(), 0));
        check(std::abs(ap.arcs[0].points[0].freq - 880.0) < 0.5,
              "sieve up-arrow -> next allowed pitch A5",
              juce::String(ap.arcs[0].points[0].freq, 1));
        ed.keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(), 0));
        check(std::abs(ap.arcs[0].points[0].freq - 440.0) < 0.5,
              "sieve down-arrow -> back to A4",
              juce::String(ap.arcs[0].points[0].freq, 1));

        // zone-aware quantization through the editor
        upic::Page zp; zp.durationSec = 10.0;
        ed.bind(&zp, &waves, &ph, nullptr);
        ed.setSieve(true, 12, { 0, 7 }); // global: C/G only
        double tSec = 1.0, f = 450.0;
        ed.quantizeXY(tSec, f);
        check(std::abs(f - 392.0) < 0.6, "editor global sieve 450 -> 392",
              juce::String(f, 2));

        const int zid = ed.addZone(4.0, 6.0, 300.0, 600.0);
        check(zid > 0 && zp.zones.size() == 1, "editor addZone inserts a zone");
        zp.zones[0].m = 12;
        zp.zones[0].remainders = { 0,1,2,3,4,5,6,7,8,9,10,11 }; // chromatic zone
        tSec = 5.0; f = 450.0;
        ed.quantizeXY(tSec, f);
        check(std::abs(f - 440.0) < 0.5, "editor zone sieve overrides global",
              juce::String(f, 2));
        tSec = 1.0; f = 450.0;
        ed.quantizeXY(tSec, f);
        check(std::abs(f - 392.0) < 0.6, "editor outside zone keeps global sieve",
              juce::String(f, 2));

        ed.setTimeGrid(4.0);
        tSec = 5.13; f = 500.0; // inside zone (zone grid = 0 -> no time snap)
        ed.quantizeXY(tSec, f);
        check(std::abs(tSec - 5.13) < 1e-9, "editor zone timeGrid 0 overrides global grid");
        tSec = 2.13; f = 500.0; // outside -> global grid snaps
        ed.quantizeXY(tSec, f);
        check(std::abs(tSec - 2.25) < 1e-9, "editor outside zone global grid snaps 2.13 -> 2.25",
              juce::String(tSec, 4));

        ed.deleteZone(zid);
        check(zp.zones.empty(), "editor deleteZone removes");

        // time-strip scrub moves the playhead
        ed.scrubTo(ed.timeToX(7.0));
        check(std::abs(ph - 7.0) < 1e-6, "centre line scrub sets playhead",
              juce::String(ph, 3));

        // v1.25: the playhead triangle at the top is a drag handle
        const auto phHandle = ed.playheadHandleRect();
        check(phHandle.getWidth() >= 20.0f && phHandle.getHeight() >= 14.0f
              && std::abs(phHandle.getCentreX() - ed.timeToX(7.0)) < 0.01f,
              "playhead triangle handle sits on the playhead",
              phHandle.toString());

        // v1.19: the scrub line runs across the middle of the canvas, so
        // the target is a narrow band at its TOP - a drawing stroke that
        // starts in the middle must not move the playhead.
        {
            const auto strip = ed.timeStripBounds();
            const auto target = ed.scrubTargetRect();
            check(target.getHeight() <= 6.0f,
                  "the scrub target is a narrow band",
                  juce::String(target.getHeight(), 1) + " dp");
            check(strip.contains(target),
                  "the scrub target stays inside the scrub line");
            check(! target.contains(strip.getCentre()),
                  "the middle of the scrub line is free for drawing");
        }
    }

}

void upic::runPanelChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 21. v1.21 (wave 19): the arc list - the inventory of the arcs, with
    //     the per-arc controls (the Rev box and the mode dropdown)
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);
        const juce::String json =
            "{\"pages\":["
            "{\"duration\":4,\"nextArcId\":9,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":880}]}]},"
            "{\"duration\":4,\"nextArcId\":9,\"arcs\":["
            "{\"id\":2,\"mode\":2,\"refFreq\":220,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":3,\"f\":440}]},"
            "{\"id\":3,\"mode\":2,\"refFreq\":330,\"intensity\":0.7,"
            "\"points\":[{\"t\":0,\"f\":330},{\"t\":1,\"f\":660}]}]},"
            "{\"duration\":4,\"nextArcId\":9,\"arcs\":["
            "{\"id\":4,\"mode\":3,\"pmRatio\":3.5,\"pmIndex\":4.5,\"pmIndexEnv\":1,"
            "\"pmFeedback\":0.2,\"pmOps\":4,\"intensity\":0.6,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":1,\"f\":220}]}]}]}";
        mc.loadFromVar(juce::JSON::parse(json));

        // open the panel
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };
        auto* ab = dynamic_cast<juce::TextButton*>(findByName(mc, "arcListBtn"));
        check(ab != nullptr, "the arc list toggle is present");
        if (ab != nullptr)
        {
            ab->setToggleState(true, juce::dontSendNotification);
            ab->onClick();
        }
        // the panel is a child of the component
        bool found = false;
        for (int i = 0; i < mc.getNumChildComponents() && ! found; ++i)
            found = dynamic_cast<ArcListPanel*>(mc.getChildComponent(i)) != nullptr;
        check(found, "the arc list panel is in the component");

        // a snapshot of the panel (for eyeballing)
        {
            ArcListPanel* panel = nullptr;
            for (int i = 0; i < mc.getNumChildComponents() && panel == nullptr; ++i)
                panel = dynamic_cast<ArcListPanel*>(mc.getChildComponent(i));
            if (panel != nullptr)
            {
                mc.timerCallback();          // build the rows
                const juce::File png = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                           .getChildFile("upic_snapshot_arclist.png");
                juce::Image hi = panel->createComponentSnapshot (panel->getLocalBounds(), false);
                if (hi.isValid())
                {
                    if (auto os = png.createOutputStream())
                    {
                        juce::PNGImageFormat fmt;
                        fmt.writeImageToStream (hi, *os);
                        os->flush();
                    }
                }
                // and the whole window with the panel open
                {
                    const juce::File png2 = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                .getChildFile("upic_snapshot_arclist_window.png");
                    mc.setSize (1280, 800);
                    mc.resized();
                    juce::Image hi2 = mc.createComponentSnapshot (mc.getLocalBounds(), false);
                    if (hi2.isValid())
                        if (auto os = png2.createOutputStream())
                        {
                            juce::PNGImageFormat fmt;
                            fmt.writeImageToStream (hi2, *os);
                            os->flush();
                        }
                }
            }
        }

        // v1.26: the arc list selects across layers and supports a
        // Ctrl/Cmd multi-selection. Selecting a PM arc loads its PM
        // controls; PM rows no longer show the sample/live mode menu.
        {
            ArcListPanel* panel = nullptr;
            for (int i = 0; i < mc.getNumChildComponents() && panel == nullptr; ++i)
                panel = dynamic_cast<ArcListPanel*>(mc.getChildComponent(i));
            auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
            check(panel != nullptr && sc != nullptr, "arc list selection test setup");
            if (panel != nullptr && sc != nullptr)
            {
                mc.timerCallback();
                bool pmRow = false, pmMode = true;
                for (const auto& r : panel->getRows())
                    if (r.kind == ArcListPanel::Row::arc && r.arcId == 4)
                    { pmRow = true; pmMode = r.showMode; }
                check(pmRow && ! pmMode, "PM arcs have no sample/live mode menu");

                panel->onArcClicked(2, 1, false);
                check(sc->getLayer() == 2, "arc list selection switches the canvas layer");
                check(sc->getSelectedArc() == 2, "arc list selection selects the arc");
                auto* liveTab = dynamic_cast<juce::Button*>(findByName(mc, "Live"));
                check(liveTab != nullptr && liveTab->getToggleState(),
                      "arc list selection checks the Live tab");

                panel->onArcClicked(3, 1, true);
                check(sc->getSelectedArcs().size() == 2
                      && panel->getSelectedArcs().size() == 2,
                      "arc list ctrl-click multi-selects");

                panel->onArcClicked(4, 2, false);
                auto* pr = dynamic_cast<juce::Slider*>(findByName(mc, "pmRatioSlider"));
                auto* pi = dynamic_cast<juce::Slider*>(findByName(mc, "pmIndexSlider"));
                check(pr != nullptr && pi != nullptr
                      && std::abs(pr->getValue() - 3.5) < 1e-6
                      && std::abs(pi->getValue() - 4.5) < 1e-6,
                      "arc list selection loads the PM ratio/index",
                      pr != nullptr && pi != nullptr
                          ? juce::String(pr->getValue()) + "/" + juce::String(pi->getValue())
                          : juce::String("missing"));
                auto* pmTab = dynamic_cast<juce::Button*>(findByName(mc, "PM"));
                check(pmTab != nullptr && pmTab->getToggleState(),
                      "arc list selection checks the PM tab");
            }
        }
    }

    // 21b. v1.26: the toolbar is a live editor for the selected arcs.
    // Wave / intensity changes apply immediately, Ref retunes the selected
    // arc, and the Ref control is also present on the Live tab.
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);
        const juce::String json =
            "{\"pages\":[{\"duration\":4,\"nextArcId\":9,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":880}]},"
            "{\"id\":2,\"mode\":1,\"sampleId\":1,\"refFreq\":220,\"intensity\":0.7,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]},"
            "{\"id\":3,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]},"
            "{\"id\":4,\"mode\":1,\"sampleId\":1,\"refFreq\":220,\"intensity\":0.7,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]}]}]}";
        mc.loadFromVar(juce::JSON::parse(json));

        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };
        auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
        auto* wave = dynamic_cast<juce::ComboBox*>(findByName(mc, "waveBox"));
        auto* inten = dynamic_cast<juce::Slider*>(findByName(mc, "intenSlider"));
        auto* ref = dynamic_cast<juce::Slider*>(findByName(mc, "refSlider"));
        auto* sample = dynamic_cast<juce::ComboBox*>(findByName(mc, "sampleBox"));
        check(sc != nullptr && wave != nullptr && inten != nullptr && ref != nullptr
              && sample != nullptr,
              "toolbar live-edit test setup");
        if (sc != nullptr && wave != nullptr && inten != nullptr && ref != nullptr
            && sample != nullptr)
        {
            auto clickTab = [&](const char* n)
            {
                if (auto* t = dynamic_cast<juce::Button*>(findByName(mc, n)))
                {
                    t->setToggleState(true, juce::dontSendNotification);
                    t->onClick();
                }
            };
            clickTab("Osc");
            sc->setSelectedArcs({1, 3});
            check(sc->getLayer() == 0 && sc->getSelectedArcs().size() == 2,
                  "toolbar live-edit setup selects the osc arcs",
                  "layer " + juce::String(sc->getLayer()) + " sel "
                      + juce::String((int) sc->getSelectedArcs().size()));
            wave->setSelectedId(3, juce::sendNotificationSync); // waveform id 2
            inten->setValue(0.5, juce::sendNotificationSync);
            auto out = mc.pieceToVar();
            auto* pagesArr = out.getProperty("pages", juce::var()).getArray();
            auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                : nullptr;
            double wid1 = -1.0, wid3 = -1.0, amp1 = -1.0, amp3 = -1.0;
            if (arcsArr != nullptr)
                for (const auto& av : *arcsArr)
                {
                    const int id = (int) av.getProperty("id", -1);
                    if (id == 1)
                    {
                        wid1 = (double) av.getProperty("waveformId", -1.0);
                        amp1 = (double) av.getProperty("intensity", -1.0);
                    }
                    else if (id == 3)
                    {
                        wid3 = (double) av.getProperty("waveformId", -1.0);
                        amp3 = (double) av.getProperty("intensity", -1.0);
                    }
                }
            check(std::abs(wid1 - 2.0) < 1e-6 && std::abs(amp1 - 0.5) < 1e-6
                  && std::abs(wid3 - 2.0) < 1e-6 && std::abs(amp3 - 0.5) < 1e-6,
                  "toolbar wave/intensity edit all selected arcs",
                  juce::String(wid1) + "/" + juce::String(amp1) + " "
                      + juce::String(wid3) + "/" + juce::String(amp3));

            clickTab("Sample");
            check(ref->isVisible() && ref->getWidth() > 0,
                  "the Sample tab has the Ref control");
            sc->setSelectedArcs({2, 4});
            ref->setValue(330.0, juce::sendNotificationSync);
            out = mc.pieceToVar();
            pagesArr = out.getProperty("pages", juce::var()).getArray();
            arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                          ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                          : nullptr;
            double rf2 = -1.0, rf4 = -1.0;
            if (arcsArr != nullptr)
                for (const auto& av : *arcsArr)
                {
                    const int id = (int) av.getProperty("id", -1);
                    if (id == 2) rf2 = (double) av.getProperty("refFreq", -1.0);
                    else if (id == 4) rf4 = (double) av.getProperty("refFreq", -1.0);
                }
            check(std::abs(rf2 - 330.0) < 1e-6 && std::abs(rf4 - 330.0) < 1e-6,
                  "Ref retunes all selected sample arcs",
                  juce::String(rf2) + "/" + juce::String(rf4));

            sample->setSelectedId(2, juce::sendNotificationSync); // slot id 2
            out = mc.pieceToVar();
            pagesArr = out.getProperty("pages", juce::var()).getArray();
            arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                          ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                          : nullptr;
            double sid2 = -1.0, sid4 = -1.0;
            if (arcsArr != nullptr)
                for (const auto& av : *arcsArr)
                {
                    const int id = (int) av.getProperty("id", -1);
                    if (id == 2) sid2 = (double) av.getProperty("sampleId", -1.0);
                    else if (id == 4) sid4 = (double) av.getProperty("sampleId", -1.0);
                }
            check(std::abs(sid2 - 2.0) < 1e-6 && std::abs(sid4 - 2.0) < 1e-6,
                  "the sample slot applies to all selected sample arcs",
                  juce::String(sid2) + "/" + juce::String(sid4));

            clickTab("Live");
            check(ref->isVisible() && ref->getWidth() > 0,
                  "the Live tab has the Ref control");

            // arc ids repeat across pages: a list click must use the
            // row's page, not the first arc with that id
            {
                const juce::String twoPages =
                    "{\"pages\":["
                    "{\"duration\":4,\"nextArcId\":2,\"arcs\":["
                    "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                    "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":880}]}]},"
                    "{\"duration\":4,\"nextArcId\":2,\"arcs\":["
                    "{\"id\":1,\"mode\":3,\"pmRatio\":3.5,\"pmIndex\":4.5,"
                    "\"pmFeedback\":0.25,\"pmOps\":4,\"intensity\":0.8,"
                    "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]}]}]}";
                mc.loadFromVar(juce::JSON::parse(twoPages));
                mc.timerCallback();
                ArcListPanel* panel = nullptr;
                for (int i = 0; i < mc.getNumChildComponents() && panel == nullptr; ++i)
                    panel = dynamic_cast<ArcListPanel*>(mc.getChildComponent(i));
                auto* pr = dynamic_cast<juce::Slider*>(findByName(mc, "pmRatioSlider"));
                auto* pmTab = dynamic_cast<juce::Button*>(findByName(mc, "PM"));
                check(panel != nullptr && sc != nullptr && pr != nullptr && pmTab != nullptr,
                      "page-specific arc list test setup");
                if (panel != nullptr && sc != nullptr && pr != nullptr && pmTab != nullptr)
                {
                    panel->onArcClicked(1, 1, false);
                    check(sc->getLayer() == 3 && pmTab->getToggleState(),
                          "arc list uses the row's page, not the first matching id");
                    check(std::abs(pr->getValue() - 3.5) < 1e-6,
                          "arc list loads the page's own arc",
                          juce::String(pr->getValue()));
                    check(! panel->isArcSelected(0, 1) && panel->isArcSelected(1, 1),
                          "arc list highlights only the selected page's row");
                }
            }
        }
    }

    // 21c. v1.26: a generated PM arc is born with the current PM toolbar
    // values (the generator used to leave the per-arc defaults).
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };
        auto* pmTab = dynamic_cast<juce::Button*>(findByName(mc, "PM"));
        auto* pr = dynamic_cast<juce::Slider*>(findByName(mc, "pmRatioSlider"));
        auto* pi = dynamic_cast<juce::Slider*>(findByName(mc, "pmIndexSlider"));
        auto* fb = dynamic_cast<juce::Slider*>(findByName(mc, "pmFeedbackSlider"));
        auto* ops = dynamic_cast<juce::ComboBox*>(findByName(mc, "pmOpsBox"));
        auto* gen = dynamic_cast<juce::ComboBox*>(findByName(mc, "genBox"));
        check(pmTab != nullptr && pr != nullptr && pi != nullptr && fb != nullptr
              && ops != nullptr && gen != nullptr,
              "PM current-voice test setup");
        if (pmTab != nullptr && pr != nullptr && pi != nullptr && fb != nullptr
            && ops != nullptr && gen != nullptr)
        {
            pmTab->setToggleState(true, juce::dontSendNotification);
            pmTab->onClick();
            pr->setValue(3.5, juce::sendNotificationSync);
            pi->setValue(4.5, juce::sendNotificationSync);
            fb->setValue(0.25, juce::sendNotificationSync);
            ops->setSelectedId(2, juce::sendNotificationSync); // 4-op
            gen->setSelectedId(1, juce::sendNotificationSync); // stochastic arborescence

            auto out = mc.pieceToVar();
            auto* pagesArr = out.getProperty("pages", juce::var()).getArray();
            auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                : nullptr;
            double ratio = -1.0, index = -1.0, feedback = -1.0;
            int pmOps = -1;
            if (arcsArr != nullptr)
                for (const auto& av : *arcsArr)
                    if ((int) av.getProperty("mode", -1) == 3)
                    {
                        ratio = (double) av.getProperty("pmRatio", -1.0);
                        index = (double) av.getProperty("pmIndex", -1.0);
                        feedback = (double) av.getProperty("pmFeedback", -1.0);
                        pmOps = (int) av.getProperty("pmOps", -1);
                        break;
                    }
            check(std::abs(ratio - 3.5) < 1e-6 && std::abs(index - 4.5) < 1e-6
                  && std::abs(feedback - 0.25) < 1e-6 && pmOps == 4,
                  "generated PM arcs use the current PM controls",
                  juce::String(ratio) + "/" + juce::String(index) + "/"
                      + juce::String(feedback) + "/" + juce::String(pmOps));
        }
    }

}

void upic::runWidgetChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 21d. v1.27: the bottom wavetable editor edits a shared wave. With a
    // selection it must fork a private copy instead of changing every arc
    // that names the same wave (the top dropdown still assigns normally).
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);
        const juce::String json =
            "{\"pages\":[{\"duration\":4,\"nextArcId\":3,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]},"
            "{\"id\":2,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]}]}]}";
        mc.loadFromVar(juce::JSON::parse(json));
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };
        auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
        auto* saw = dynamic_cast<juce::Button*>(findByName(mc, "Saw"));
        auto* we = dynamic_cast<WaveformEditor*>(findByName(mc, "waveEd"));
        check(sc != nullptr && saw != nullptr && we != nullptr,
              "wavetable edit test setup");
        if (sc != nullptr && saw != nullptr && we != nullptr)
        {
            auto arcWids = [&](int& wid1, int& wid2)
            {
                wid1 = wid2 = -1;
                auto out = mc.pieceToVar();
                auto* pagesArr = out.getProperty("pages", juce::var()).getArray();
                auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                    ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                    : nullptr;
                if (arcsArr != nullptr)
                    for (const auto& av : *arcsArr)
                    {
                        const int id = (int) av.getProperty("id", -1);
                        if (id == 1) wid1 = (int) av.getProperty("waveformId", -1);
                        else if (id == 2) wid2 = (int) av.getProperty("waveformId", -1);
                    }
            };

            sc->setSelectedArcs({1});
            saw->onClick();
            int wid1 = -1, wid2 = -1;
            arcWids(wid1, wid2);
            auto out = mc.pieceToVar();
            auto* wavesArr = out.getProperty("waveforms", juce::var()).getArray();
            double oldAt256 = 0.0, newAt256 = 0.0;
            if (wavesArr != nullptr)
                for (const auto& wv : *wavesArr)
                {
                    const int id = (int) wv.getProperty("id", -1);
                    if (id != 0 && id != wid1) continue;
                    auto* samples = wv.getProperty("samples", juce::var()).getArray();
                    if (samples != nullptr && samples->size() > 256)
                    {
                        const double v = (double) samples->getReference(256);
                        if (id == 0) oldAt256 = v;
                        else newAt256 = v;
                    }
                }
            check(wid1 != 0 && wid2 == 0,
                  "wavetable shape edit gives the selection a private copy",
                  juce::String(wid1) + "/" + juce::String(wid2));
            check(std::abs(oldAt256 - 1.0) < 0.01 && newAt256 < 0.2,
                  "the old wavetable is untouched and the selection gets the saw",
                  juce::String(oldAt256, 3) + "/" + juce::String(newAt256, 3));

            // Drawing in the same editor uses the same fork path.
            mc.loadFromVar(juce::JSON::parse(json));
            sc->setSelectedArcs({1});
            const auto now = juce::Time::getCurrentTime();
            const auto src = juce::Desktop::getInstance().getMainMouseSource();
            auto plot = we->getLocalBounds().toFloat().reduced (6);
            plot.removeFromTop (18);
            const auto p = plot.getCentre();
            juce::MouseEvent down(src, p, juce::ModifierKeys(), 1.0f,
                                  0.0f, 0.0f, 0.0f, 0.0f, we, we, now, p, now, 1, false);
            we->mouseDown(down);
            we->mouseUp(down);
            arcWids(wid1, wid2);
            check(wid1 != 0 && wid2 == 0,
                  "drawing a wavetable also forks only the selection",
                  juce::String(wid1) + "/" + juce::String(wid2));
        }
    }

    // 21e. v1.27: double-click Stop rewinds; Clear warns and only deletes
    // after the red confirm button is pressed.
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);
        const juce::String json =
            "{\"pages\":[{\"duration\":4,\"nextArcId\":3,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]},"
            "{\"id\":2,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]}]}]}";
        mc.loadFromVar(juce::JSON::parse(json));
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };
        auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
        auto* stop = dynamic_cast<juce::Button*>(findByName(mc, "Stop"));
        auto* time = dynamic_cast<juce::Label*>(findByName(mc, "timeLab"));
        auto* clear = dynamic_cast<juce::Button*>(findByName(mc, "Clear"));
        auto* overlay = dynamic_cast<juce::Component*>(findByName(mc, "clearOverlay"));
        auto* confirm = dynamic_cast<juce::Button*>(findByName(mc, "clearConfirmBtn"));
        auto* cancel = dynamic_cast<juce::Button*>(findByName(mc, "clearCancelBtn"));
        check(sc != nullptr && stop != nullptr && time != nullptr && clear != nullptr
              && overlay != nullptr && confirm != nullptr && cancel != nullptr,
              "transport safety test setup");
        if (sc != nullptr && stop != nullptr && time != nullptr && clear != nullptr
            && overlay != nullptr && confirm != nullptr && cancel != nullptr)
        {
            sc->scrubTo(sc->getLocalBounds().toFloat().getCentreX());
            mc.timerCallback();
            const auto before = time->getText();
            stop->onClick();
            stop->onClick();
            mc.timerCallback();
            const auto after = time->getText();
            check(after.startsWith("0.0") && ! before.startsWith("0.0"),
                  "double-click Stop returns to the start",
                  before + " -> " + after);

            check(clear->findColour(juce::TextButton::buttonColourId)
                      == juce::Colour(0xff7a2a2a),
                  "Clear is coloured as a destructive action");
            clear->onClick();
            check(overlay->isVisible(),
                  "Clear opens a warning before deleting");
            auto arcCount = [](const juce::var& v)
            {
                auto* pagesArr = v.getProperty("pages", juce::var()).getArray();
                auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                    ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                    : nullptr;
                return arcsArr != nullptr ? arcsArr->size() : -1;
            };
            check(arcCount(mc.pieceToVar()) == 2,
                  "the warning leaves the arcs intact until confirmation");
            cancel->onClick();
            check(! overlay->isVisible() && arcCount(mc.pieceToVar()) == 2,
                  "Cancel closes the warning and leaves the arcs intact");
            clear->onClick();
            confirm->onClick();
            check(arcCount(mc.pieceToVar()) == 0 && ! overlay->isVisible(),
                  "confirming Clear deletes the page and closes the warning");
        }
    }

    // 21f. v1.33.5 (L7): the button pictograms and the unified sliders.
    //     - every icon the button map names has to exist, and every
    //       embedded icon has to parse (a broken SVG would quietly leave
    //       a button without its pictogram)
    //     - every numeric slider wears the same colours now (the Master
    //       Volume treatment), so the rows read as one family
    //     - the captions stay: an icon is added, never a replacement
    {
        int missing = 0;
        for (const auto& mapping : upic::buttonIcons::buttonMappings)
            if (upic::buttonIcons::find (mapping.icon) == nullptr) ++missing;
        check (missing == 0, "every button mapping has an icon",
               juce::String (missing) + " missing");

        int broken = 0;
        for (const auto& definition : upic::buttonIcons::definitions)
            if (upic::buttonIcons::find (definition.name) == nullptr) ++broken;
        check (broken == 0, "every embedded icon parses",
               juce::String (broken) + " broken");

        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);

        std::function<void (juce::Component&, std::vector<juce::Slider*>&)> collectSliders =
            [&collectSliders] (juce::Component& c, std::vector<juce::Slider*>& out)
            {
                if (auto* s = dynamic_cast<juce::Slider*> (&c)) out.push_back (s);
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    collectSliders (*c.getChildComponent (i), out);
            };
        std::vector<juce::Slider*> sliders;
        collectSliders (mc, sliders);

        int offTheme = 0;
        for (auto* s : sliders)
            if (s->findColour (juce::Slider::backgroundColourId)  != juce::Colour (0xff4a4a5c)
             || s->findColour (juce::Slider::trackColourId)       != juce::Colour (0xff5ac8fa)
             || s->findColour (juce::Slider::thumbColourId)       != juce::Colour (0xffe6f4ff)
             || s->findColour (juce::Slider::textBoxTextColourId) != juce::Colour (0xfff2f2f7))
                ++offTheme;
        check (sliders.size() >= 24 && offTheme == 0,
               "all the numeric sliders wear the same colours",
               juce::String (sliders.size()) + " sliders, "
                   + juce::String (offTheme) + " off theme");

        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };
        auto* play = dynamic_cast<juce::Button*> (findByName (mc, "Play"));
        auto* sine = dynamic_cast<juce::TextButton*> (findByName (mc, "wSine"));
        check (play != nullptr && play->getButtonText() == "Play"
               && upic::buttonIcons::drawableFor (*play) != nullptr,
               "a button keeps its caption and gains an icon");
        check (sine != nullptr && sine->getButtonText() == "Sine"
               && upic::buttonIcons::drawableFor (*sine) != nullptr
               && upic::buttonIcons::compactShapeButton (*sine),
               "a narrow preset keeps its caption and gains its shape");
    }

}

void upic::runEnvelopeChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 23. v1.24 (wave 21): the envelope points - + adds, - removes, the
    //     ends are free and the extra points survive the piece format
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);
        mc.loadFromVar(juce::JSON::parse(
            "{\"envelopes\":[{\"id\":1,\"name\":\"Test\",\"points\":["
            "{\"x\":0,\"y\":0},{\"x\":0.25,\"y\":1},{\"x\":0.75,\"y\":0.2},{\"x\":1,\"y\":0}]}],"
            "\"pages\":[{\"duration\":30,\"nextArcId\":1,\"arcs\":[]}]}"));

        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };
        std::function<EnvelopeEditor*(juce::Component&)> findEnvEd =
            [&findEnvEd](juce::Component& c) -> EnvelopeEditor* {
                if (auto* ed = dynamic_cast<EnvelopeEditor*>(&c)) return ed;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findEnvEd(*c.getChildComponent(i))) return r;
                return nullptr;
            };

        auto* ed = findEnvEd(mc);
        check(ed != nullptr, "the envelope editor is present");
        if (ed != nullptr)
        {
            check(ed->pointCount() == 4, "the envelope keeps its points",
                  juce::String(ed->pointCount()));

            // select the attack point, then + splits the segment after it
            ed->selectPoint(1);
            const bool added = ed->addPoint();
            const auto mid = ed->pointAt(2);
            check(added && ed->pointCount() == 5, "the + adds a point",
                  juce::String(ed->pointCount()));
            check(std::abs(mid.x - 0.5f) < 0.005f && std::abs(mid.y - 0.6f) < 0.005f,
                  "the new point sits midway on the curve",
                  juce::String(mid.x, 3) + ", " + juce::String(mid.y, 3));
            check(ed->selectedPoint() == 2, "the new point becomes the selection");

            // a snapshot of the editor with a point selected (for eyeballing)
            {
                const juce::File png = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                           .getChildFile("upic_snapshot_env.png");
                juce::Image hi = ed->createComponentSnapshot(ed->getLocalBounds(), false);
                if (hi.isValid())
                    if (auto os = png.createOutputStream())
                    {
                        juce::PNGImageFormat fmt;
                        fmt.writeImageToStream(hi, *os);
                        os->flush();
                    }
            }

            // the extra points survive a save/load round trip
            {
                auto mc2Holder = std::make_unique<MainComponent>();
                auto& mc2 = *mc2Holder;
                mc2.setSize(1280, 800);
                mc2.loadFromVar(mc.pieceToVar());
                auto* ed2 = findEnvEd(mc2);
                const auto p2 = ed2 != nullptr ? ed2->pointAt(2) : upic::EnvPoint{ -1.0f, -1.0f };
                check(ed2 != nullptr && ed2->pointCount() == 5
                      && std::abs(p2.x - 0.5f) < 0.005f && std::abs(p2.y - 0.6f) < 0.005f,
                      "the extra envelope points survive the piece format");
            }

            // - removes the selected point; two must remain
            check(ed->removePoint() && ed->pointCount() == 4 && ed->selectedPoint() == -1,
                  "the - removes the selected point");
            check(!ed->removePoint(), "the - needs a selected point");
            ed->selectPoint(0);
            check(ed->removePoint() && ed->pointCount() == 3, "an end point can be removed");

            // a curve with no point at x=0 holds its first value (a late onset)
            upic::Envelope lateOnset;
            lateOnset.points = { { 0.25f, 1.0f }, { 0.75f, 0.2f }, { 1.0f, 0.0f } };
            check(std::abs(lateOnset.valueAt(0.0f) - 1.0f) < 1.0e-6f,
                  "a curve without a 0 point holds its first value");

            // tap a spot on the curve, then + inserts exactly there
            ed->placePointAt(0.9f);
            check(ed->addPoint() && ed->pointCount() == 4, "the + uses the tapped spot");
            {
                const auto p = ed->pointAt(2);
                check(std::abs(p.x - 0.9f) < 0.005f && std::abs(p.y - 0.08f) < 0.01f,
                      "the tapped point lands on the curve",
                      juce::String(p.x, 3) + ", " + juce::String(p.y, 3));
            }

            // v1.26: Ctrl/Cmd/Shift-click adds envelope points to the
            // selection; - removes the whole selected group.
            ed->selectPoint(0);
            ed->selectPoint(2, true);
            check(ed->selectedPoints().size() == 2 && ed->selectedPoint() == 0,
                  "ctrl-click adds envelope points to the selection",
                  juce::String((int) ed->selectedPoints().size()));

            // dragging one selected point moves the whole group together
            {
                ed->selectPoint(1);
                ed->selectPoint(2, true);
                const auto p1 = ed->pointAt(1);
                const auto p2 = ed->pointAt(2);
                auto plot = ed->getLocalBounds().toFloat().reduced(6.0f);
                plot.removeFromTop(26.0f);
                auto px = [&](upic::EnvPoint p) {
                    return juce::Point<float>(plot.getX() + p.x * plot.getWidth(),
                                              plot.getBottom() - p.y * plot.getHeight());
                };
                const auto start = px(p1);
                const auto end = start + juce::Point<float>(12.0f, 0.0f);
                const auto now = juce::Time::getCurrentTime();
                const auto src = juce::Desktop::getInstance().getMainMouseSource();
                juce::MouseEvent down(src, start, juce::ModifierKeys(), 1.0f,
                                      0.0f, 0.0f, 0.0f, 0.0f, ed, ed, now, start, now, 1, false);
                ed->mouseDown(down);
                juce::MouseEvent drag(src, end, juce::ModifierKeys(), 1.0f,
                                      0.0f, 0.0f, 0.0f, 0.0f, ed, ed, now, start, now, 1, true);
                ed->mouseDrag(drag);
                ed->mouseUp(drag);
                const float dx = 12.0f / plot.getWidth();
                const auto q1 = ed->pointAt(1);
                const auto q2 = ed->pointAt(2);
                check(std::abs((q1.x - p1.x) - dx) < 0.002f
                      && std::abs((q2.x - p2.x) - dx) < 0.002f
                      && std::abs(q1.y - p1.y) < 1.0e-4f
                      && ed->selectedPoints().size() == 2,
                      "dragging a selected envelope point moves the group",
                      juce::String(q1.x - p1.x, 3) + "/" + juce::String(q2.x - p2.x, 3));
            }

            const int beforeMultiRemove = ed->pointCount();
            check(ed->removePoint() && ed->pointCount() == beforeMultiRemove - 2,
                  "the - removes all selected envelope points",
                  juce::String(ed->pointCount()));

            // the cap keeps the per-sample scan cheap
            for (int i = 0; i < 40; ++i) ed->addPoint();
            check(ed->pointCount() == 32 && !ed->addPoint(), "the point cap is 32",
                  juce::String(ed->pointCount()));

            // the two buttons are in the header and follow the selection
            auto* addB = dynamic_cast<juce::Button*>(findByName(mc, "envPointAddBtn"));
            auto* delB = dynamic_cast<juce::Button*>(findByName(mc, "envPointDelBtn"));
            check(addB != nullptr && delB != nullptr && addB->isVisible() && delB->isVisible()
                  && ed->getLocalBounds().contains(addB->getBounds())
                  && ed->getLocalBounds().contains(delB->getBounds()),
                  "the + and - buttons sit in the editor header");
            check(addB != nullptr && !addB->isEnabled(), "the + is disabled at the cap");
            ed->selectPoint(1);
            check(delB != nullptr && delB->isEnabled() && delB->getParentComponent() == ed,
                  "the - follows the selection");
        }
    }

}

//==============================================================================
// F2/F3 (2026-11-17): the two library manager rows. The buttons, the
// selector text (names and use counts), the rename and replacement modes,
// the enabled states at the caps and the undo step per command.
void upic::runLibraryUiChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
        [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
            if (c.getName() == n) return &c;
            for (int i = 0; i < c.getNumChildComponents(); ++i)
                if (auto* r = findByName (*c.getChildComponent (i), n)) return r;
            return nullptr;
        };
    auto button = [&findByName] (juce::Component& c, const juce::String& n)
    {
        return dynamic_cast<juce::Button*> (findByName (c, n));
    };
    auto combo = [&findByName] (juce::Component& c, const juce::String& n)
    {
        return dynamic_cast<juce::ComboBox*> (findByName (c, n));
    };
    auto editor = [&findByName] (juce::Component& c, const juce::String& n)
    {
        return dynamic_cast<juce::TextEditor*> (findByName (c, n));
    };
    auto selectItem = [] (juce::ComboBox& box, const juce::String& prefix) -> bool
    {
        for (int i = 0; i < box.getNumItems(); ++i)
            if (box.getItemText (i).startsWith (prefix))
            {
                box.setSelectedId (box.getItemId (i), juce::sendNotificationSync);
                return true;
            }
        return false;
    };

    auto makePiece = [](int waveCount, int envCount) -> juce::var
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
        upic::Page pg;
        pg.durationSec = 4.0;
        pg.nextArcId = 3;
        upic::Arc a;
        a.id = 1; a.mode = 0; a.waveformId = 10; a.envelopeId = 20;
        a.pmIndexEnv = 21; a.intensity = 0.8f;
        a.points = { { 0.0, 220.0 }, { 2.0, 440.0 } };
        pg.arcs.push_back (a);
        upic::Arc b;
        b.id = 2; b.mode = 0; b.waveformId = 10; b.envelopeId = 20;
        b.intensity = 0.8f;
        b.points = { { 0.0, 330.0 }, { 2.0, 660.0 } };
        pg.arcs.push_back (b);
        m.pages.push_back (pg);
        return upic::pieceToVar (m);
    };

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    auto* waveBox      = combo (mc, "waveEditBox");
    auto* waveNew      = button (mc, "waveNewBtn");
    auto* waveDup      = button (mc, "waveDupBtn");
    auto* waveRename   = button (mc, "waveRenameBtn");
    auto* waveDel      = button (mc, "waveDelBtn");
    auto* waveUnique   = button (mc, "waveUniqueBtn");
    auto* waveNameEd   = editor (mc, "waveRenameEdit");
    auto* waveNameOk   = button (mc, "waveRenameOk");
    auto* waveNameNo   = button (mc, "waveRenameCancel");
    auto* waveReplace  = combo (mc, "waveReplaceBox");
    auto* waveDelOk    = button (mc, "waveDeleteOk");
    auto* waveDelNo    = button (mc, "waveDeleteCancel");

    auto* envBox       = combo (mc, "envEditBox");
    auto* envNew       = button (mc, "envNewBtn");
    auto* envDup       = button (mc, "envDupBtn");
    auto* envRename    = button (mc, "envRenameBtn");
    auto* envDel       = button (mc, "envDelBtn");
    auto* envUnique    = button (mc, "envUniqueBtn");
    auto* envNameEd    = editor (mc, "envRenameEdit");
    auto* envNameOk    = button (mc, "envRenameOk");
    auto* envNameNo    = button (mc, "envRenameCancel");
    auto* envReplace   = combo (mc, "envReplaceBox");
    auto* envDelOk     = button (mc, "envDeleteOk");
    auto* envDelNo     = button (mc, "envDeleteCancel");

    auto* waveLibRow = findByName (mc, "waveLib");
    auto* envLibRow  = findByName (mc, "envLib");

    check (waveBox != nullptr && envBox != nullptr
           && waveNew != nullptr && waveDup != nullptr && waveRename != nullptr
           && waveDel != nullptr && waveUnique != nullptr
           && waveNameEd != nullptr && waveNameOk != nullptr && waveNameNo != nullptr
           && waveReplace != nullptr && waveDelOk != nullptr && waveDelNo != nullptr,
           "library UI: the wavetable manager controls exist");
    check (envNew != nullptr && envDup != nullptr && envRename != nullptr
           && envDel != nullptr && envUnique != nullptr
           && envNameEd != nullptr && envNameOk != nullptr && envNameNo != nullptr
           && envReplace != nullptr && envDelOk != nullptr && envDelNo != nullptr,
           "library UI: the envelope manager controls exist");
    check (waveLibRow != nullptr && envLibRow != nullptr,
           "library UI: the manager rows are in the component tree");
    if (waveBox == nullptr || envBox == nullptr || waveNew == nullptr
        || waveDel == nullptr || waveUnique == nullptr || waveLibRow == nullptr)
        return;

    check (waveLibRow->getBounds().getHeight() > 10
           && waveLibRow->getLocalBounds().contains (waveBox->getBounds()),
           "library UI: the selector sits in the manager row",
           waveLibRow->getBounds().toString() + " / " + waveBox->getBounds().toString());

    const bool loaded = mc.loadFromVar (makePiece (3, 3));
    check (loaded, "library UI: the test piece loads");

    // --- the initial rows -------------------------------------------------
    {
        check (waveBox->getNumItems() == 3 && envBox->getNumItems() == 3,
               "library UI: the selectors list the bank",
               juce::String (waveBox->getNumItems()) + "/"
               + juce::String (envBox->getNumItems()));
        check (waveBox->getText().startsWith ("10: Wave 10")
               && waveBox->getText().contains ("(2 uses)"),
               "library UI: the selector shows the name and the use count",
               waveBox->getText());
        check (waveDel->isEnabled() && waveNew->isEnabled()
               && waveUnique->isEnabled(),
               "library UI: Delete and Make unique follow the selection");
        check (mc.selectedLibraryWaveForTest() == 10
               && mc.selectedLibraryEnvelopeForTest() == 20,
               "library UI: the managers start on the model's selection");
    }

    // --- New and Duplicate ------------------------------------------------
    {
        const int depth0 = mc.undoDepth();
        waveNew->onClick();
        const int fresh = mc.selectedWaveformIdForTest();
        check (mc.waveformCountForTest() == 4 && fresh == 0
               && mc.waveformIdForTest (3) == 0,
               "library UI: New adds a wavetable and selects it",
               juce::String (mc.waveformCountForTest()) + "/" + juce::String (fresh));
        check (mc.undoDepth() == depth0 + 1,
               "library UI: New is one undo step");
        check (waveBox->getNumItems() == 4
               && waveBox->getText().startsWith ("0:")
               && waveBox->getText().contains ("(0 uses)"),
               "library UI: the new wavetable is in the selector, unused",
               waveBox->getText());
        check (! waveUnique->isEnabled(),
               "library UI: Make unique is disabled for an unused wavetable");

        const int depth1 = mc.undoDepth();
        waveDup->onClick();
        const int dup = mc.selectedWaveformIdForTest();
        check (mc.waveformCountForTest() == 5 && dup != fresh
               && mc.undoDepth() == depth1 + 1,
               "library UI: Duplicate adds one wavetable in one undo step",
               juce::String (mc.waveformCountForTest()) + "/" + juce::String (dup));
        check (waveBox->getText().contains ("copy"),
               "library UI: the duplicate's name says it is a copy",
               waveBox->getText());
    }

    // --- Rename -----------------------------------------------------------
    {
        waveRename->onClick();
        check (mc.waveLibraryRenameModeForTest()
               && waveNameEd->isVisible() && waveNameOk->isVisible()
               && ! waveBox->isVisible() && ! waveNew->isVisible(),
               "library UI: Rename switches the row to the name editor");
        const int depth0 = mc.undoDepth();
        const int count0 = mc.waveformCountForTest();

        waveNameEd->setText ("   ", false);
        waveNameOk->onClick();
        check (mc.waveLibraryRenameModeForTest()
               && mc.waveformCountForTest() == count0
               && mc.undoDepth() == depth0
               && mc.flashMessageForTest().isNotEmpty(),
               "library UI: an empty name keeps the editor open and explains",
               mc.flashMessageForTest());

        waveNameEd->setText ("Wave 11", false);
        waveNameOk->onClick();
        check (mc.waveLibraryRenameModeForTest()
               && mc.undoDepth() == depth0
               && mc.flashMessageForTest().containsIgnoreCase ("already"),
               "library UI: a duplicate name keeps the editor open",
               mc.flashMessageForTest());

        waveNameEd->setText ("  Lead  ", false);
        waveNameOk->onClick();
        check (! mc.waveLibraryRenameModeForTest()
               && waveBox->isVisible() && ! waveNameEd->isVisible()
               && mc.undoDepth() == depth0 + 1,
               "library UI: a valid rename ends the mode in one undo step");
        bool namedLead = false;
        for (int i = 0; i < mc.waveformCountForTest(); ++i)
            if (mc.waveformNameForTest (i) == "Lead") namedLead = true;
        check (namedLead, "library UI: the new name reaches the model");

        waveRename->onClick();
        waveNameEd->setText ("Nope", false);
        waveNameNo->onClick();
        check (! mc.waveLibraryRenameModeForTest()
               && waveBox->isVisible()
               && mc.undoDepth() == depth0 + 1,
               "library UI: Cancel leaves the name and the undo stack alone");
    }

    // --- Delete an unused wavetable: no replacement asked -----------------
    {
        const int count0 = mc.waveformCountForTest();
        const int depth0 = mc.undoDepth();
        waveDel->onClick();
        check (! mc.waveLibraryDeleteModeForTest()
               && mc.waveformCountForTest() == count0 - 1
               && mc.undoDepth() == depth0 + 1,
               "library UI: Delete of an unused wavetable needs no replacement",
               juce::String (mc.waveformCountForTest()) + "/"
               + juce::String (mc.undoDepth()));
    }

    // --- Delete a used wavetable: the replacement picker ------------------
    {
        check (selectItem (*waveBox, "10:"),
               "library UI: the used wavetable is selectable");
        check (mc.selectedWaveformIdForTest() == 10
               && mc.selectedLibraryWaveForTest() == 10,
               "library UI: the selector reaches the model and the manager",
               juce::String (mc.selectedWaveformIdForTest()) + "/"
               + juce::String (mc.selectedLibraryWaveForTest()));

        const int depth0 = mc.undoDepth();
        const int count0 = mc.waveformCountForTest();
        waveDel->onClick();
        check (mc.waveLibraryDeleteModeForTest()
               && waveReplace->isVisible() && waveDelOk->isVisible()
               && ! waveBox->isVisible() && ! waveDel->isVisible(),
               "library UI: Delete of a used wavetable asks for a replacement");
        check (waveReplace->getNumItems() == count0 - 1,
               "library UI: the picker excludes the wavetable being deleted",
               juce::String (waveReplace->getNumItems()) + " of "
               + juce::String (count0));
        check (selectItem (*waveReplace, "11:"),
               "library UI: a replacement can be chosen");

        waveDelOk->onClick();
        check (! mc.waveLibraryDeleteModeForTest()
               && mc.waveformCountForTest() == count0 - 1
               && mc.undoDepth() == depth0 + 1,
               "library UI: the replacement delete is one undo step");
        check (mc.arcWaveformIdForTest (1) == 11
               && mc.arcWaveformIdForTest (2) == 11
               && mc.selectedWaveformIdForTest() == 11,
               "library UI: the arcs and the selection follow the replacement",
               juce::String (mc.arcWaveformIdForTest (1)) + "/"
               + juce::String (mc.selectedWaveformIdForTest()));
        check (waveBox->getText().startsWith ("11:")
               && waveBox->getText().contains ("(2 uses)"),
               "library UI: the selector follows the replacement and its uses",
               waveBox->getText());
    }

    // --- Cancel a delete --------------------------------------------------
    {
        const int count0 = mc.waveformCountForTest();
        const int depth0 = mc.undoDepth();
        waveDel->onClick();
        check (mc.waveLibraryDeleteModeForTest(),
               "library UI: the delete mode is open before the cancel");
        waveDelNo->onClick();
        check (! mc.waveLibraryDeleteModeForTest()
               && mc.waveformCountForTest() == count0
               && mc.undoDepth() == depth0,
               "library UI: Cancel leaves the bank and the undo stack alone");
    }

    // --- the envelope manager: New, Rename, Delete -----------------------
    {
        const int depth0 = mc.undoDepth();
        envNew->onClick();
        const int fresh = mc.selectedEnvelopeIdForTest();
        check (mc.envelopeCountForTest() == 4 && fresh == 0
               && mc.undoDepth() == depth0 + 1,
               "library UI: New adds an envelope and selects it",
               juce::String (mc.envelopeCountForTest()) + "/" + juce::String (fresh));
        check (envBox->getText().startsWith ("0:")
               && envBox->getText().contains ("(0 uses)")
               && ! envUnique->isEnabled(),
               "library UI: the new envelope is unused",
               envBox->getText());

        envRename->onClick();
        envNameEd->setText ("Shape", false);
        envNameOk->onClick();
        bool named = false;
        for (int i = 0; i < mc.envelopeCountForTest(); ++i)
            if (mc.envelopeNameForTest (i) == "Shape") named = true;
        check (! mc.envLibraryRenameModeForTest() && named,
               "library UI: the envelope rename reaches the model");
        check (envNameNo->isVisible() == false,
               "library UI: the envelope rename row is back to the selector");

        check (selectItem (*envBox, "20:"),
               "library UI: the used envelope is selectable");
        const int depth1 = mc.undoDepth();
        const int count1 = mc.envelopeCountForTest();
        envDel->onClick();
        check (mc.envLibraryDeleteModeForTest() && envReplace->isVisible(),
               "library UI: Delete of a used envelope asks for a replacement");
        check (selectItem (*envReplace, "22:"),
               "library UI: an envelope replacement can be chosen");
        envDelOk->onClick();
        check (! mc.envLibraryDeleteModeForTest()
               && mc.envelopeCountForTest() == count1 - 1
               && mc.undoDepth() == depth1 + 1,
               "library UI: the envelope replacement delete is one undo step");
        check (mc.arcEnvelopeIdForTest (1) == 22
               && mc.arcEnvelopeIdForTest (2) == 22
               && mc.arcPmIndexEnvForTest (1) == 21,
               "library UI: the amp references move and the PM reference stays");
        check (mc.selectedEnvelopeIdForTest() == 22
               && envBox->getText().startsWith ("22:"),
               "library UI: the envelope selector follows the replacement",
               envBox->getText());
    }

    // --- the last asset: Delete is disabled ------------------------------
    {
        const bool oneLoaded = mc.loadFromVar (makePiece (1, 1));
        check (oneLoaded && mc.waveformCountForTest() == 1
               && mc.envelopeCountForTest() == 1,
               "library UI: the one-asset piece loads");
        check (! waveDel->isEnabled() && ! envDel->isEnabled(),
               "library UI: the last wavetable and envelope cannot be deleted");
        const int depth0 = mc.undoDepth();
        waveDel->onClick();
        envDel->onClick();
        check (mc.waveformCountForTest() == 1 && mc.envelopeCountForTest() == 1
               && mc.undoDepth() == depth0,
               "library UI: the disabled delete buttons do nothing");
    }

    // --- undo restores the manager row -----------------------------------
    {
        const bool threeLoaded = mc.loadFromVar (makePiece (3, 3));
        check (threeLoaded && waveBox->getNumItems() == 3,
               "library UI: the three-asset piece reloads");
        const int depth0 = mc.undoDepth();
        waveNew->onClick();
        check (mc.waveformCountForTest() == 4
               && waveBox->getNumItems() == 4,
               "library UI: New grows the row");
        mc.undo();
        check (mc.waveformCountForTest() == 3
               && waveBox->getNumItems() == 3
               && mc.selectedWaveformIdForTest() == 10,
               "library UI: undo rebuilds the row and its selection",
               juce::String (waveBox->getNumItems()) + "/"
               + juce::String (mc.selectedWaveformIdForTest()));
        check (mc.undoDepth() == depth0,
               "library UI: undo removes the step");
    }
}

//==============================================================================
// F5 (2026-11-17): the collapsible toolbar groups, the permanent strip and
// the session-settings round trip.
void upic::runInterfaceChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
        [&findByName] (juce::Component& c, const juce::String& n) -> juce::Component* {
            if (c.getName() == n) return &c;
            for (int i = 0; i < c.getNumChildComponents(); ++i)
                if (auto* r = findByName (*c.getChildComponent (i), n)) return r;
            return nullptr;
        };

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    auto find = [&] (const juce::String& n) { return findByName (mc, n); };
    auto button = [&] (const juce::String& n)
    {
        return dynamic_cast<juce::Button*> (find (n));
    };

    // --- the disclosure toggle changes the layout ------------------------
    {
        auto* inten = find ("intenSlider");       // the levels group's first
        auto* dur = find ("durSlider");           // a secondary control
        auto* fold = button ("levelsFoldBtn");
        check (inten != nullptr && dur != nullptr && fold != nullptr,
               "F5 the levels group has its controls and its disclosure toggle",
               juce::String (inten != nullptr ? "inten " : "inten=null ")
               + (dur != nullptr ? "dur " : "dur=null ")
               + (fold != nullptr ? "fold " : "fold=null "));

        // the disclosure is a labelled toggle (the reference archive's
        // "Scrub settings" pattern): the caption is the group's name
        auto* foldToggle = dynamic_cast<juce::ToggleButton*> (fold);
        check (foldToggle != nullptr
               && foldToggle->getButtonText() == MainComponent::groupCaption ("levels"),
               "F5 the disclosure toggle is labelled with the group's name",
               foldToggle != nullptr ? foldToggle->getButtonText() : juce::String ("not a toggle"));

        const int rowsExpanded = mc.toolbarRowCountForTest();
        check (inten != nullptr && dur != nullptr && fold != nullptr
               && inten->isVisible() && dur->isVisible() && fold->isVisible(),
               "F5 expanded shows the whole group");

        // collapsed: the group's controls go, the labelled toggle stays
        if (fold != nullptr) fold->onClick();
        const int rowsCollapsed = mc.toolbarRowCountForTest();
        check (mc.groupLevelFor ("levels") == MainComponent::GroupLevel::collapsed,
               "F5 the disclosure toggle collapses the group");
        check (inten != nullptr && ! inten->isVisible()
               && dur != nullptr && ! dur->isVisible()
               && fold != nullptr && fold->isVisible(),
               "F5 collapsed hides the group's controls and keeps the toggle");
        check (rowsCollapsed <= rowsExpanded,
               "F5 collapsed does not grow the toolbar",
               juce::String (rowsCollapsed) + " vs " + juce::String (rowsExpanded));

        // and the same toggle brings the group back
        if (fold != nullptr) fold->onClick();
        check (mc.groupLevelFor ("levels") == MainComponent::GroupLevel::expanded
               && inten != nullptr && inten->isVisible()
               && dur != nullptr && dur->isVisible()
               && fold != nullptr && fold->isVisible(),
               "F5 the disclosure toggle expands the group again");
        check (mc.toolbarRowCountForTest() == rowsExpanded,
               "F5 expanding returns the previous height",
               juce::String (mc.toolbarRowCountForTest()) + " vs "
               + juce::String (rowsExpanded));

        auto* restore = button ("restoreGroupsBtn");
        check (restore != nullptr && restore->isVisible(),
               "F5 Restore is in the permanent strip");
        if (restore != nullptr) restore->onClick();
        check (mc.groupLevelFor ("levels") == MainComponent::GroupLevel::expanded
               && inten != nullptr && inten->isVisible()
               && dur != nullptr && dur->isVisible()
               && fold != nullptr && fold->isVisible(),
               "F5 Restore expands every group again");
        check (mc.toolbarRowCountForTest() == rowsExpanded,
               "F5 restoring returns the previous height",
               juce::String (mc.toolbarRowCountForTest()) + " vs "
               + juce::String (rowsExpanded));
    }

    // --- the permanent strip survives every level and the minimum size ---
    {
        const char* names[] = { "transport", "pages", "file", "tempo", "clock", "voice",
                                "levels", "pitch", "material", "sieve", "magnets", "page",
                                "tools", "scrub", "drag", "status" };
        for (auto* n : names)
            mc.setGroupLevel (n, MainComponent::GroupLevel::collapsed);

        auto stripOk = [&] (juce::String& detail) -> bool
        {
            const auto strip = mc.permanentStripBoundsForTest();
            bool ok = ! strip.isEmpty();
            for (auto* n : { "File", "Play", "Stop", "toolBox", "restoreGroupsBtn" })
            {
                auto* c = find (n);
                if (c == nullptr || ! c->isVisible() || ! strip.contains (c->getBounds()))
                {
                    ok = false;
                    detail << n << "="
                           << (c != nullptr ? c->getBounds().toString() : juce::String ("null"))
                           << "  ";
                }
            }
            return ok;
        };

        juce::String d1;
        const bool ok1 = stripOk (d1);
        check (ok1, "F5 the permanent strip keeps its controls with every group collapsed", d1);

        mc.setSize (460, 360);  // the app's minimum window
        juce::String d2;
        const bool ok2 = stripOk (d2);
        check (ok2, "F5 the permanent strip keeps its controls at the minimum window", d2);

        mc.setSize (1280, 800);
        mc.restoreAllGroups();
        check (mc.groupLevelFor ("levels") == MainComponent::GroupLevel::expanded,
               "F5 Restore after a narrow layout brings the groups back");
    }

    // --- the session settings round trip --------------------------------
    {
        mc.restoreAllGroups();
        mc.setGroupLevel ("scrub", MainComponent::GroupLevel::collapsed);
        mc.setGroupLevel ("page", MainComponent::GroupLevel::collapsed);
        const auto v = mc.sessionSettingsToVar();
        mc.restoreAllGroups();
        check (mc.groupLevelFor ("scrub") == MainComponent::GroupLevel::expanded,
               "F5 Restore clears the disclosure state before the round trip");
        mc.sessionSettingsFromVar (v);
        check (mc.groupLevelFor ("scrub") == MainComponent::GroupLevel::collapsed
               && mc.groupLevelFor ("page") == MainComponent::GroupLevel::collapsed,
               "F5 the disclosure state survives a session-settings round trip");
        check (v.hasProperty ("showWaveEd") && v.hasProperty ("showHints"),
               "F5 the session settings carry the view toggles too");

        // the old three-level fold (hidden = 2) is read as "collapsed"
        juce::var old = juce::JSON::parse ("{\"groups\":{\"scrub\":2,\"levels\":1}}");
        mc.sessionSettingsFromVar (old);
        check (mc.groupLevelFor ("scrub") == MainComponent::GroupLevel::collapsed
               && mc.groupLevelFor ("levels") == MainComponent::GroupLevel::collapsed,
               "F5 the old fold levels load as collapsed");
        mc.restoreAllGroups();
    }

    // --- the piece format is untouched ----------------------------------
    {
        mc.restoreAllGroups();
        const auto pieceBefore = mc.pieceToVar();
        const auto before = juce::JSON::toString (pieceBefore);
        mc.setGroupLevel ("scrub", MainComponent::GroupLevel::collapsed);
        const auto pieceAfter = mc.pieceToVar();
        const auto after = juce::JSON::toString (pieceAfter);
        juce::String detail;
        if (before != after)
        {
            int i = 0;
            while (i < before.length() && i < after.length() && before[i] == after[i]) ++i;
            detail << "first diff at " << i << ": "
                   << before.substring (juce::jmax (0, i - 40), i + 80) << " VS "
                   << after.substring (juce::jmax (0, i - 40), i + 80);
        }
        check (before == after, "F5 the disclosure state does not enter the piece JSON", detail);
        check (! mc.pieceToVar().hasProperty ("groups")
               && ! mc.pieceToVar().hasProperty ("session"),
               "F5 the piece JSON has no session fields");
    }

    // --- v1.34.4: the Voice section's Duplicate and Select all ------------
    // The canvas context menu's arc commands are buttons too, beside the
    // New / Assign row.
    {
        auto* dup = button ("dupArcBtn");
        auto* sel = button ("selectAllBtn");
        check (dup != nullptr && sel != nullptr
               && dup->isVisible() && sel->isVisible(),
               "v1.34.4 the Voice section carries Duplicate and Select all");

        // a piece with two arcs: Select all picks both, Duplicate copies
        // the selection
        upic::PieceModel m;
        {
            upic::Waveform w (10, "Wave 10", upic::paletteColour (0));
            w.makeSine();
            m.waveforms.push_back (w);
            upic::Envelope e (20, "Env 20", upic::paletteColour (1));
            e.presetFlat();
            m.envelopes.push_back (e);
        }
        upic::Page pg;
        pg.durationSec = 4.0;
        pg.nextArcId = 3;
        for (int i = 1; i <= 2; ++i)
        {
            upic::Arc a;
            a.id = i; a.mode = 0; a.waveformId = 10; a.envelopeId = 20;
            a.intensity = 0.8f;
            a.points = { { 0.0, 220.0 + 110.0 * (double) i },
                         { 2.0, 440.0 + 220.0 * (double) i } };
            pg.arcs.push_back (a);
        }
        m.pages.push_back (pg);

        if (mc.loadFromVar (upic::pieceToVar (m)))
        {
            mc.selectArcsForTest ({ 1 });
            const int arcsBefore = mc.arcCountForTest();
            if (sel != nullptr) sel->onClick();
            check (arcsBefore == 2 && mc.selectedArcCountForTest() == arcsBefore,
                   "v1.34.4 the Voice Select all button selects every arc",
                   juce::String (mc.selectedArcCountForTest()) + " of "
                     + juce::String (arcsBefore));
            // Duplicate copies the selection - one arc selected, one copy
            mc.selectArcsForTest ({ 1 });
            if (dup != nullptr) dup->onClick();
            check (mc.arcCountForTest() == arcsBefore + 1,
                   "v1.34.4 the Voice Duplicate button copies the selection",
                   juce::String (mc.arcCountForTest()) + " vs "
                     + juce::String (arcsBefore));
        }
    }

    // --- audit M3: every toolbar card has a rank, and the status card is
    //     the last one --------------------------------------------------
    // The card order is a rank table. A card that is not in the table sorts
    // AFTER the status line (an unknown name's rank is the table's length),
    // so the cards WP2/WP4/WP5/WP6 added - without a rank - pushed the status
    // card out of its place.
    {
        static const char* const expected[] = {
            "transport", "seq", "pages", "file", "voice", "amp", "mod", "ch",
            "sieve", "tools", "tempo", "accel", "levels", "material", "magnets",
            "scrub", "drag", "pitch", "page", "clock", "status"
        };

        juce::String detail;
        int previous = -1;
        bool ok = true;
        for (auto* name : expected)
        {
            const int idx = mc.toolbarCardIndexForGroupForTest (name);
            if (idx < 0)
            {
                ok = false;
                detail << name << " missing  ";
                continue;
            }
            if (idx <= previous)
            {
                ok = false;
                detail << name << " at " << idx << " (after " << previous << ")  ";
            }
            previous = idx;
        }
        check (ok, "M3 every toolbar card is in the rank table's order", detail);

        // the status line is the flexible card that fills the last row: it
        // has to be the last of the flow cards
        int lastIdx = -1;
        juce::String lastName;
        for (auto* name : expected)
        {
            const int idx = mc.toolbarCardIndexForGroupForTest (name);
            if (idx > lastIdx) { lastIdx = idx; lastName = name; }
        }
        check (lastName == "status",
               "M3 the status card is the last one",
               lastName + " at " + juce::String (lastIdx)
                 + ", status at "
                 + juce::String (mc.toolbarCardIndexForGroupForTest ("status")));
    }
}

//==============================================================================
// F4 (2026-11-17): the focus views.
//
// A focus view maximises one panel. The permanent strip stays, the other
// panels and the groups go, and leaving the focus restores the geometry,
// the scroll positions and the zoom levels exactly.
void upic::runFocusChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    std::function<juce::Component*(juce::Component&, const juce::String&)> findByNameF4 =
        [&findByNameF4] (juce::Component& c, const juce::String& n) -> juce::Component* {
            if (c.getName() == n) return &c;
            for (int i = 0; i < c.getNumChildComponents(); ++i)
                if (auto* r = findByNameF4 (*c.getChildComponent (i), n)) return r;
            return nullptr;
        };

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);
    mc.prepareToPlay (512, 44100.0);

    auto find = [&] (const juce::String& n) { return findByNameF4 (mc, n); };
    auto* score = find ("score");
    auto* scoreEd = dynamic_cast<ScoreEditor*> (score);
    auto* waveEd = find ("waveEd");
    auto* envEd = find ("envEd");
    auto* focusBtn = dynamic_cast<juce::Button*> (find ("scoreFocusBtn"));
    auto* restore = find ("restoreGroupsBtn");
    auto* inten = find ("intenSlider");

    check (score != nullptr && waveEd != nullptr && envEd != nullptr
           && focusBtn != nullptr && restore != nullptr && scoreEd != nullptr,
           "F4 the panels and their focus buttons are there");

    if (score == nullptr || waveEd == nullptr || envEd == nullptr
        || focusBtn == nullptr || restore == nullptr || scoreEd == nullptr)
        return;

    // the geometry and the view state before the focus
    const auto scoreBefore = score->getBounds();
    const auto waveBefore = waveEd->getBounds();
    const auto envBefore = envEd->getBounds();
    scoreEd->zoomIn();
    scoreEd->zoomIn();
    scoreEd->fitSelection();
    const double zoomBefore = scoreEd->getTimeZoom();
    const double offsetBefore = scoreEd->getTimeOffset();
    const double freqZoomBefore = scoreEd->getFreqZoom();
    const double freqOffsetBefore = scoreEd->getFreqOffset();

    // --- into the focus ---
    focusBtn->onClick();
    check (mc.focusedPanelForTest() == MainComponent::FocusPanel::score,
           "F4 the score focus is on");
    check (focusBtn->getButtonText() == "Exit",
           "F4 the focus button says Exit while the focus is on",
           focusBtn->getButtonText());
    check (score->isVisible() && score->getHeight() > scoreBefore.getHeight()
           && score->getWidth() >= scoreBefore.getWidth(),
           "F4 the score takes the space of the other panels",
           score->getBounds().toString() + " vs " + scoreBefore.toString());
    check (! waveEd->isVisible() && ! envEd->isVisible(),
           "F4 the other editors go while the score is focused");
    check (inten != nullptr && ! inten->isVisible(),
           "F4 the toolbar groups go while a panel is focused");

    // the permanent strip stays - it is what the focus keeps
    {
        const auto strip = mc.permanentStripBoundsForTest();
        bool ok = ! strip.isEmpty();
        juce::String detail;
        for (auto* n : { "File", "Play", "Stop", "toolBox", "restoreGroupsBtn" })
        {
            auto* c = find (n);
            if (c == nullptr || ! c->isVisible() || ! strip.contains (c->getBounds()))
            {
                ok = false;
                detail << n << "="
                       << (c != nullptr ? c->getBounds().toString() : juce::String ("null"))
                       << "  ";
            }
        }
        check (ok, "F4 the permanent strip stays while a panel is focused", detail);
    }

    // the transport still works while focused
    mc.clickPlayForTest();
    check (mc.isPlayingForTest(), "F4 the transport works while a panel is focused");
    mc.clickStopForTest();

    // the view state is untouched by the focus
    check (scoreEd->getTimeZoom() == zoomBefore
           && scoreEd->getTimeOffset() == offsetBefore
           && scoreEd->getFreqZoom() == freqZoomBefore
           && scoreEd->getFreqOffset() == freqOffsetBefore,
           "F4 the zoom and the scroll positions survive the focus",
           juce::String (scoreEd->getTimeZoom(), 4) + "/" + juce::String (scoreEd->getTimeOffset(), 4));

    // --- out of the focus ---
    focusBtn->onClick();
    check (mc.focusedPanelForTest() == MainComponent::FocusPanel::none,
           "F4 the focus is off again");
    check (focusBtn->getButtonText() == "Focus",
           "F4 the focus button says Focus again");
    check (score->getBounds() == scoreBefore
           && waveEd->getBounds() == waveBefore
           && envEd->getBounds() == envBefore,
           "F4 leaving the focus restores the exact geometry",
           score->getBounds().toString() + " vs " + scoreBefore.toString()
           + " / " + waveEd->getBounds().toString() + " vs " + waveBefore.toString());
    check (scoreEd->getTimeZoom() == zoomBefore
           && scoreEd->getTimeOffset() == offsetBefore,
           "F4 leaving the focus restores the zoom and the scroll");
    check (waveEd->isVisible() && envEd->isVisible()
           && (inten == nullptr || inten->isVisible()),
           "F4 leaving the focus brings the panels and the groups back");

    // the other two panels have a focus too
    for (auto* n : { "waveFocusBtn", "envFocusBtn" })
    {
        auto* b = dynamic_cast<juce::Button*> (find (n));
        if (b == nullptr) { check (false, juce::String("F4 the ") + n + " is there"); continue; }
        b->onClick();
        const bool focused = mc.focusActiveForTest();
        b->onClick();
        check (focused && ! mc.focusActiveForTest(),
               juce::String("F4 the ") + n + " enters and leaves its focus");
    }

    // the focus is session state, never piece state
    {
        const auto pieceBefore = juce::JSON::toString (mc.pieceToVar());
        mc.setFocusedPanel (MainComponent::FocusPanel::score);
        const auto pieceAfter = juce::JSON::toString (mc.pieceToVar());
        mc.setFocusedPanel (MainComponent::FocusPanel::none);
        check (pieceBefore == pieceAfter,
               "F4 the focus view does not enter the piece JSON");
    }
}

//==============================================================================
// F6 (2026-11-17): the view commands, the Fine drawing mode, the live
// readout and the numeric edit.
void upic::runViewChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the view commands (a score editor on a page) ---------------------
    {
        auto waves = upic::defaultWaveforms();
        double ph = 0.0;
        juce::CriticalSection lock;
        upic::Page pg;
        pg.durationSec = 12.0;
        upic::Arc a; a.id = 1;
        a.points = { { 2.0, 200.0 }, { 6.0, 800.0 } };
        pg.arcs.push_back (a);
        ScoreEditor ed;
        ed.setModelLock (&lock);
        ed.bind (&pg, &waves, &ph, nullptr);
        ed.setSize (1000, 400);

        // Zoom In / Zoom Out
        ed.zoomIn();
        check (ed.getTimeZoom() > 1.0 && ed.getFreqZoom() > 1.0,
               "F6 Zoom In raises the zoom",
               juce::String (ed.getTimeZoom(), 3) + "/" + juce::String (ed.getFreqZoom(), 3));
        ed.zoomOut();
        check (std::abs (ed.getTimeZoom() - 1.0) < 1e-9
               && std::abs (ed.getFreqZoom() - 1.0) < 1e-9,
               "F6 Zoom Out undoes Zoom In");

        // Fit Page
        ed.zoomIn();
        ed.zoomIn();
        ed.fitPage();
        check (ed.getTimeZoom() == 1.0 && ed.getTimeOffset() == 0.0
               && ed.getFreqZoom() == 1.0 && ed.getFreqOffset() == 0.0,
               "F6 Fit Page goes back to zoom 1 and offset 0",
               juce::String (ed.getTimeZoom(), 3) + "/" + juce::String (ed.getTimeOffset(), 3));

        // Reset Zoom: 1x, but the centre stays (unlike Fit Page)
        ed.zoomIn();
        const double centreBefore = ed.getTimeOffset() + 12.0 / (2.0 * ed.getTimeZoom());
        ed.resetZoom();
        const double centreAfter = ed.getTimeOffset() + 12.0 / (2.0 * ed.getTimeZoom());
        check (ed.getTimeZoom() == 1.0 && ed.getFreqZoom() == 1.0
               && std::abs (centreAfter - centreBefore) < 1e-9,
               "F6 Reset Zoom goes back to 1x and keeps the centre",
               juce::String (centreBefore, 4) + " -> " + juce::String (centreAfter, 4));

        // Fit Selection brackets the selection's time and pitch
        ed.setSelectedArcs ({ 1 });
        ed.fitSelection();
        const double t0 = ed.getTimeOffset();
        const double t1 = t0 + 12.0 / ed.getTimeZoom();
        check (t0 <= 2.0 && t1 >= 6.0,
               "F6 Fit Selection brackets the selection's time",
               juce::String (t0, 3) + " .. " + juce::String (t1, 3));
        const double n0 = upic::freqToNormYIn (200.0, ed.ambitusLo(), ed.ambitusHi());
        const double n1 = upic::freqToNormYIn (800.0, ed.ambitusLo(), ed.ambitusHi());
        const double f0 = ed.getFreqOffset();
        const double f1 = f0 + 1.0 / ed.getFreqZoom();
        check (f0 <= n0 && f1 >= n1,
               "F6 Fit Selection brackets the selection's pitch",
               juce::String (f0, 3) + " .. " + juce::String (f1, 3));

        // the readout round-trips through the editor's own mapping
        {
            const auto px = 300.0f, py = 120.0f;
            const double tSec = ed.xToTime (px);
            const double hz = ed.yToFreq (py);
            const float xBack = ed.timeToX (tSec);
            const float yBack = ed.freqToY (hz);
            check (std::abs (xBack - px) <= 1.0f && std::abs (yBack - py) <= 1.0f,
                   "F6 the readout round-trips through the editor's mapping",
                   juce::String (xBack, 2) + "," + juce::String (yBack, 2)
                   + " vs " + juce::String (px, 2) + "," + juce::String (py, 2));
        }

        // --- the numeric edit ---
        ed.fitPage();
        double anchorT = 0.0, anchorF = 0.0;
        check (ed.selectionAnchor (anchorT, anchorF),
               "F6 the selection has an anchor");
        check (std::abs (anchorT - 2.0) < 1e-9 && std::abs (anchorF - 200.0) < 1e-9,
               "F6 the anchor is the selection's first point",
               juce::String (anchorT, 4) + " / " + juce::String (anchorF, 4));

        // the time edit shifts the selection, like a drag does
        check (ed.setSelectionTime (3.5),
               "F6 the numeric time edit is accepted");
        check (std::abs (pg.arcs[0].points[0].t - 3.5) < 1e-9
               && std::abs (pg.arcs[0].points[1].t - 7.5) < 1e-9,
               "F6 the numeric time edit shifts every point of the selection",
               juce::String (pg.arcs[0].points[0].t, 4) + " / "
               + juce::String (pg.arcs[0].points[1].t, 4));

        // the frequency edit scales it, like a drag does
        check (ed.setSelectionFreq (400.0),
               "F6 the numeric frequency edit is accepted");
        check (std::abs (pg.arcs[0].points[0].freq - 400.0) < 1e-6
               && std::abs (pg.arcs[0].points[1].freq - 1600.0) < 1e-6,
               "F6 the numeric frequency edit scales every point of the selection",
               juce::String (pg.arcs[0].points[0].freq, 3) + " / "
               + juce::String (pg.arcs[0].points[1].freq, 3));

        // the same edit through a drag: the model state is the same
        {
            // the drag moves the selection the way the numeric edit does:
            // the time shifts and the frequency scales by a ratio
            upic::Page pg2 = pg;
            const auto before = pg2.arcs[0].points;
            std::vector<upic::ArcPoint> expected;
            for (auto& p : before)
                expected.push_back ({ juce::jlimit (0.0, pg2.durationSec, p.t + 1.5),
                                      juce::jlimit (upic::kMinFreq, upic::kMaxFreq, p.freq * 2.0) });
            std::stable_sort (expected.begin(), expected.end(),
                              [] (const upic::ArcPoint& x, const upic::ArcPoint& y){ return x.t < y.t; });

            // the same shift and scale through the numeric edit
            ScoreEditor ed2;
            ed2.setModelLock (&lock);
            ed2.bind (&pg2, &waves, &ph, nullptr);
            ed2.setSize (1000, 400);
            ed2.setSelectedArcs ({ 1 });
            const double aT = pg2.arcs[0].points.front().t;
            const double aF = pg2.arcs[0].points.front().freq;
            ed2.setSelectionTime (aT + 1.5);
            ed2.setSelectionFreq (aF * 2.0);

            bool same = pg2.arcs[0].points.size() == expected.size();
            for (size_t i = 0; same && i < expected.size(); ++i)
                same = std::abs (pg2.arcs[0].points[i].t - expected[i].t) < 1e-9
                    && std::abs (pg2.arcs[0].points[i].freq - expected[i].freq) < 1e-6;
            check (same,
                   "F6 the numeric edit moves the selection the way a drag does",
                   pg2.arcs[0].points.size() == expected.size()
                       ? juce::String (pg2.arcs[0].points[0].t, 4) + "/"
                           + juce::String (pg2.arcs[0].points[0].freq, 3)
                       : juce::String ("point count "));
        }
    }

    // --- the simplification follows the view, and Fine mode keeps all ----
    {
        // a wavy stroke: the RDP simplification drops the small wiggles at
        // 1x and keeps them at 16x
        auto stroke = [] (ScoreEditor& ed, std::vector<juce::Point<float>>& pts)
        {
            const auto b = ed.getLocalBounds().toFloat();
            const float x0 = b.getX() + b.getWidth() * 0.15f;
            const float x1 = b.getX() + b.getWidth() * 0.85f;
            const float yc = b.getY() + b.getHeight() * 0.30f;
            const int n = 120;
            pts.clear();
            for (int i = 0; i <= n; ++i)
            {
                const float u = (float) i / (float) n;
                pts.push_back ({ x0 + (x1 - x0) * u,
                                 yc + std::sin (u * 18.0f) * 6.0f });
            }
        };

        auto drawStroke = [] (ScoreEditor& ed, const std::vector<juce::Point<float>>& pts)
        {
            const auto now = juce::Time::getCurrentTime();
            const auto src = juce::Desktop::getInstance().getMainMouseSource();
            juce::MouseEvent down (src, pts.front(), juce::ModifierKeys(), 1.0f,
                                   0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed, now,
                                   pts.front(), now, 1, false);
            ed.mouseDown (down);
            for (size_t i = 1; i < pts.size(); ++i)
            {
                juce::MouseEvent drag (src, pts[i], juce::ModifierKeys(), 1.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed, now,
                                       pts.front(), now, 1, true);
                ed.mouseDrag (drag);
            }
            juce::MouseEvent up (src, pts.back(), juce::ModifierKeys(), 1.0f,
                                 0.0f, 0.0f, 0.0f, 0.0f, &ed, &ed, now,
                                 pts.front(), now, 1, true);
            ed.mouseUp (up);
        };

        auto countPoints = [] (ScoreEditor& ed) -> int
        {
            return ed.lastArcPointCountForTest();
        };

        // 1x
        int at1x = 0, at16x = 0, fine = 0;
        {
            auto waves = upic::defaultWaveforms();
            double ph = 0.0;
            juce::CriticalSection lock;
            upic::Page pg;
            pg.durationSec = 30.0;
            ScoreEditor ed;
            ed.setModelLock (&lock);
            ed.bind (&pg, &waves, &ph, nullptr);
            ed.setSize (1000, 400);
            ed.setTool (ScoreEditor::Draw);
            std::vector<juce::Point<float>> pts;
            stroke (ed, pts);
            drawStroke (ed, pts);
            at1x = countPoints (ed);
        }
        // 16x
        {
            auto waves = upic::defaultWaveforms();
            double ph = 0.0;
            juce::CriticalSection lock;
            upic::Page pg;
            pg.durationSec = 30.0;
            ScoreEditor ed;
            ed.setModelLock (&lock);
            ed.bind (&pg, &waves, &ph, nullptr);
            ed.setSize (1000, 400);
            ed.setTool (ScoreEditor::Draw);
            for (int i = 0; i < 8; ++i) ed.zoomIn();   // 1.25^8 = 5.96
            ed.zoomIn(); ed.zoomIn(); ed.zoomIn();      // 11.6 -> clamped to 16
            std::vector<juce::Point<float>> pts;
            stroke (ed, pts);
            drawStroke (ed, pts);
            at16x = countPoints (ed);
        }
        // Fine mode
        {
            auto waves = upic::defaultWaveforms();
            double ph = 0.0;
            juce::CriticalSection lock;
            upic::Page pg;
            pg.durationSec = 30.0;
            ScoreEditor ed;
            ed.setModelLock (&lock);
            ed.bind (&pg, &waves, &ph, nullptr);
            ed.setSize (1000, 400);
            ed.setTool (ScoreEditor::Draw);
            ed.setFineDrawing (true);
            std::vector<juce::Point<float>> pts;
            stroke (ed, pts);
            drawStroke (ed, pts);
            fine = countPoints (ed);
        }

        check (at1x > 0, "F6 the 1x stroke makes an arc", juce::String (at1x));
        check (at16x > at1x,
               "F6 a zoomed-in stroke keeps more points than the same stroke at 1x",
               juce::String (at1x) + " -> " + juce::String (at16x));
        check (fine > at1x && fine >= at16x,
               "F6 Fine mode keeps every sampled point",
               juce::String (at1x) + " / " + juce::String (at16x) + " / " + juce::String (fine));
    }

    // --- the app's view bar is wired to the commands ----------------------
    {
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByNameF6 =
            [&findByNameF6] (juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByNameF6 (*c.getChildComponent (i), n)) return r;
                return nullptr;
            };

        auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
        mc.setSize (1280, 800);
        auto find = [&] (const juce::String& n) { return findByNameF6 (mc, n); };
        auto* scoreEd = dynamic_cast<ScoreEditor*> (find ("score"));

        auto* zoomIn = dynamic_cast<juce::Button*> (find ("zoomInBtn"));
        auto* fit = dynamic_cast<juce::Button*> (find ("fitBtn"));
        auto* fine = dynamic_cast<juce::Button*> (find ("fineBtn"));
        auto* readout = find ("readoutLab");
        check (zoomIn != nullptr && fit != nullptr && fine != nullptr && readout != nullptr
               && scoreEd != nullptr,
               "F6 the view bar carries the commands and the readout");

        if (zoomIn != nullptr && fit != nullptr && fine != nullptr && scoreEd != nullptr)
        {
            scoreEd->zoomIn();
            const double z = scoreEd->getTimeZoom();
            zoomIn->onClick();
            check (scoreEd->getTimeZoom() > z,
                   "F6 the view bar's Zoom In reaches the editor",
                   juce::String (z, 3) + " -> " + juce::String (scoreEd->getTimeZoom(), 3));

            fit->onClick();
            check (scoreEd->getTimeZoom() == 1.0 && scoreEd->getTimeOffset() == 0.0,
                   "F6 the view bar's Fit reaches the editor");

            fine->onClick();
            check (scoreEd->getFineDrawing(),
                   "F6 the view bar's Fine toggle reaches the editor");
            fine->onClick();
            check (! scoreEd->getFineDrawing(),
                   "F6 the view bar's Fine toggle switches back");
        }
    }
}
