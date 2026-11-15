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
        double t = 1.0, f = 450.0;
        ed.quantizeXY(t, f);
        check(std::abs(f - 392.0) < 0.6, "editor global sieve 450 -> 392",
              juce::String(f, 2));

        const int zid = ed.addZone(4.0, 6.0, 300.0, 600.0);
        check(zid > 0 && zp.zones.size() == 1, "editor addZone inserts a zone");
        zp.zones[0].m = 12;
        zp.zones[0].remainders = { 0,1,2,3,4,5,6,7,8,9,10,11 }; // chromatic zone
        t = 5.0; f = 450.0;
        ed.quantizeXY(t, f);
        check(std::abs(f - 440.0) < 0.5, "editor zone sieve overrides global",
              juce::String(f, 2));
        t = 1.0; f = 450.0;
        ed.quantizeXY(t, f);
        check(std::abs(f - 392.0) < 0.6, "editor outside zone keeps global sieve",
              juce::String(f, 2));

        ed.setTimeGrid(4.0);
        t = 5.13; f = 500.0; // inside zone (zone grid = 0 -> no time snap)
        ed.quantizeXY(t, f);
        check(std::abs(t - 5.13) < 1e-9, "editor zone timeGrid 0 overrides global grid");
        t = 2.13; f = 500.0; // outside -> global grid snaps
        ed.quantizeXY(t, f);
        check(std::abs(t - 2.25) < 1e-9, "editor outside zone global grid snaps 2.13 -> 2.25",
              juce::String(t, 4));

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
            const auto p = juce::Point<float>((float) we->getWidth() * 0.5f,
                                              (float) we->getHeight() * 0.5f);
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
            auto* addB = dynamic_cast<juce::Button*>(findByName(mc, "envAddBtn"));
            auto* delB = dynamic_cast<juce::Button*>(findByName(mc, "envDelBtn"));
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
