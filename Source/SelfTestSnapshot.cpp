#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "AllocCounter.h"

//==============================================================================
// The offscreen snapshot of the whole window: the layout at every size, the
// piece round trip, and the pixels of the rendered window.

void upic::runSnapshotChecks (upic::SelfTest& t)
{
    auto& log = t.log;
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 18. offscreen UI snapshot (no window shown, no screen access) +
    //     v1.2 in-process checks: piece JSON round-trip, named controls,
    //     translucent zone + centre scrub line pixel probes
    {
        auto png = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("upic_snapshot.png");
        bool written = false;
        juce::Image img(juce::Image::RGB, 1280, 800, true);
        juce::Rectangle<int> scoreBounds;
        juce::Rectangle<int> stripBounds;
        {
            juce::Graphics g(img);
            auto mcHolder = std::make_unique<MainComponent>();
            auto& mc = *mcHolder;
            mc.setSize(1280, 800);

            // --- responsive layout: the control grid reflows at small sizes ---
            // recursive find: e.g. the audio overlay's OK button lives one
            // level below the MainComponent
            std::function<juce::Component*(juce::Component&, const juce::String&)> findIn;
            findIn = [&](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findIn(*c.getChildComponent(i), n))
                        return r;
                return nullptr;
            };
            auto find = [&](const char* n) -> juce::Component* {
                return findIn(mc, juce::String(n));
            };
            auto layoutOk = [&](int zw, int zh, juce::String& detail) -> bool
            {
                mc.setSize(zw, zh);
                bool ok = true;
                static const char* watch[] = { "score", "genBox", "stickSlider", "gridBox",
                                               "sievePresetBox", "kbdHintLab" };

                // every visible direct child: non-empty and inside the
                // window (hidden views keep stale bounds and are skipped)
                for (int i = 0; i < mc.getNumChildComponents(); ++i)
                {
                    auto* c = mc.getChildComponent(i);
                    if (! c->isVisible()) continue;
                    const auto cb = c->getBounds();
                    if (cb.isEmpty() || cb.getX() < 0 || cb.getY() < 0
                        || cb.getRight() > zw || cb.getBottom() > zh)
                    {
                        ok = false;
                        detail << c->getName() << cb.toString() << "  ";
                    }
                }

                // the layout must be a pure tiling: no overlaps ever
                for (int i = 0; i < mc.getNumChildComponents() && ok; ++i)
                {
                    auto* a = mc.getChildComponent(i);
                    if (! a->isVisible() || a->getBounds().isEmpty()) continue;
                    for (int j = i + 1; j < mc.getNumChildComponents(); ++j)
                    {
                        auto* c2 = mc.getChildComponent(j);
                        if (c2->isVisible() && ! c2->getBounds().isEmpty()
                            && a->getBounds().intersects(c2->getBounds()))
                        {
                            ok = false;
                            detail << "OVERLAP " << a->getName() << "/"
                                   << c2->getName() << " "
                                   << a->getBounds().toString() << " vs "
                                   << c2->getBounds().toString() << "  ";
                            break;
                        }
                    }
                }

                // named key controls must keep a usable size
                for (auto* n : watch)
                {
                    auto* c = find(n);
                    if (c != nullptr && ! c->isVisible()) continue; // e.g. hints hidden
                    if (c == nullptr || c->getBounds().getWidth() < 8 || c->getBounds().getHeight() < 8
                        || c->getBounds().getRight() > zw || c->getBounds().getBottom() > zh
                        || c->getBounds().getX() < 0 || c->getBounds().getY() < 0)
                    {
                        ok = false;
                        detail << n << "=" << (c != nullptr ? c->getBounds().toString()
                                                            : juce::String("null")) << "  ";
                    }
                }
                return ok;
            };
            {
                struct Sz { int w, h; };
                const Sz sizes[] = { {1920,1080}, {1280,800}, {1024,768}, {1024,600},
                                     {800,600}, {800,480}, {640,400}, {480,700},
                                     {460,360}, {360,640} };
                for (auto z : sizes)
                {
                    juce::String detail;
                    // the layout check must run BEFORE the detail is read:
                    // the order of evaluation of the arguments is not
                    // specified, so reading detail.trim() in the same call
                    // would report an empty string.
                    const bool ok = layoutOk(z.w, z.h, detail);
                    check(ok,
                          juce::String("responsive layout ") + juce::String(z.w)
                          + "x" + juce::String(z.h), detail.trim());
                }
            }

            // v1.33.3: the toolbar's new layout - the hints have their
            // own sidebar, the groups share the lines, and no button
            // cuts its own text off.
            {
                mc.setSize (1280, 800);
                auto* kbd = find ("kbdHintLab");
                auto* mat = find ("matHintLab");
                auto* page = find ("pageBox");
                auto* play = find ("Play");
                check (kbd != nullptr && mat != nullptr
                       && kbd->isVisible() && mat->isVisible(),
                       "v1.33.3 the hints have their own sidebar (both visible)");
                check (kbd != nullptr && page != nullptr
                       && kbd->getBounds().getX() > page->getBounds().getRight(),
                       "v1.33.3 the hints sidebar is on the right of the buttons",
                       kbd != nullptr ? kbd->getBounds().toString() : juce::String());
                check (kbd != nullptr && mat != nullptr
                       && kbd->getBounds().getY() > mat->getBounds().getY(),
                       "v1.33.3 the material hint sits above the shortcut list");
                // F5: Play lives in the permanent strip above the groups;
                // the groups still share their lines (the transport's loop
                // and the pages sit side by side). v1.34.1: the cards keep
                // their own heights, so they share the row - not a control
                // baseline - and the controls stay inside their cards.
                auto* loop = find ("loopBtn");
                const auto transportCard = mc.toolbarCardBoundsForGroupForTest ("transport");
                const auto pagesCard = mc.toolbarCardBoundsForGroupForTest ("pages");
                check (play != nullptr && loop != nullptr && page != nullptr
                       && play->getBounds().getBottom() <= transportCard.getY()
                       && ! transportCard.isEmpty() && ! pagesCard.isEmpty()
                       && transportCard.getY() == pagesCard.getY()
                       && transportCard.contains (loop->getBounds())
                       && pagesCard.contains (page->getBounds()),
                       "F5 the permanent strip sits above two group cards on one line",
                       play != nullptr && loop != nullptr && page != nullptr
                         ? play->getBounds().toString() + " / transport "
                           + transportCard.toString() + " vs pages "
                           + pagesCard.toString()
                         : juce::String());

                // no button may be narrower than its own text
                {
                    bool allFit = true;
                    juce::String worst;
                    for (auto* b : { find ("sieveBtn"), find ("wSine"), find ("Play") })
                    {
                        auto* tb = dynamic_cast<juce::TextButton*> (b);
                        if (tb == nullptr) continue;
                        const auto font = mc.getLookAndFeel().getTextButtonFont (*tb, tb->getHeight());
                        const int need = juce::GlyphArrangement::getStringWidthInt (font, tb->getButtonText());
                        if (tb->getWidth() < need)
                        {
                            allFit = false;
                            worst << tb->getName() << " " << tb->getWidth() << "<" << need << "  ";
                        }
                    }
                    check (allFit, "v1.33.3 no button cuts its text off", worst.trim());
                }

                // the capture button: green, and red while it records
                {
                    auto* cap = dynamic_cast<juce::TextButton*> (find ("capBtn"));
                    check (cap != nullptr
                           && cap->findColour (juce::TextButton::buttonColourId) == juce::Colour (0xff2a6a3a)
                           && cap->findColour (juce::TextButton::buttonOnColourId) == juce::Colour (0xff6a2a2a),
                           "v1.33.3 the capture button is green, and red while it records");
                }

                // the wavetable's shape buttons follow the wave table's
                // colours - the same palette the arcs use
                {
                    auto* w0 = dynamic_cast<juce::TextButton*> (find ("wSine"));
                    check (w0 != nullptr
                           && w0->findColour (juce::TextButton::buttonOnColourId) == upic::paletteColour (0),
                           "v1.33.3 the wavetable buttons follow the wave table colours");
                }

                // every toolbar control must sit on a card. A group that
                // is wider than its line wraps inside itself; its card
                // has to follow it line by line, or the middle of the
                // group ends up floating on the bare background.
                {
                    const auto tb = mc.toolbarBoundsForTest();
                    const int nCards = mc.toolbarCardCountForTest();
                    bool allOnCards = true;
                    juce::String loose;
                    int toolbarControls = 0;
                    for (int i = 0; i < mc.getNumChildComponents(); ++i)
                    {
                        auto* c = mc.getChildComponent (i);
                        if (! c->isVisible() || c->getBounds().isEmpty()) continue;
                        if (! tb.contains (c->getBounds())) continue;
                        ++toolbarControls;
                        bool on = false;
                        for (int k = 0; k < nCards && ! on; ++k)
                            if (mc.toolbarCardBoundsForTest (k).contains (c->getBounds()))
                                on = true;
                        if (! on)
                        {
                            allOnCards = false;
                            loose << c->getName() << c->getBounds().toString() << "  ";
                        }
                    }
                    check (allOnCards && toolbarControls > 0,
                           "v1.33.3 every toolbar control sits on a card",
                           loose.trim());
                }

                // v1.34.1: a card is only as tall as its tallest cell.
                // v1.34.5: the status card is measured to its one-line text
                // - it hugs its label instead of stretching over the rest
                // of the row, and the label keeps the whole string.
                {
                    mc.setSize (1920, 1080);
                    const auto transport = mc.toolbarCardBoundsForGroupForTest ("transport");
                    const auto levels = mc.toolbarCardBoundsForGroupForTest ("levels");
                    const auto status = mc.toolbarCardBoundsForGroupForTest ("status");
                    check (transport.getHeight() > 0 && levels.getHeight() > 0
                           && transport.getHeight() < levels.getHeight(),
                           "v1.34.1 the compact cards are shorter than the knob cards",
                           transport.toString() + " vs " + levels.toString());

                    auto* statusLabel = dynamic_cast<juce::Label*> (find ("statusLab"));
                    const auto statusFont = statusLabel != nullptr
                        ? mc.getLookAndFeel().getLabelFont (*statusLabel)
                        : juce::Font (juce::FontOptions{});
                    const int statusTextW = statusLabel != nullptr
                        ? juce::GlyphArrangement::getStringWidthInt (statusFont, statusLabel->getText())
                        : 0;
                    check (status.getHeight() > 0 && status.getWidth() > 0
                           && statusLabel != nullptr
                           // the card is only as wide as its label (and the
                           // card's padding) - it does not fill the row
                           && status.getWidth() <= statusLabel->getWidth() + 24
                           // the label's text area holds the whole string on
                           // one line (the Label's own border takes 5+5 px)
                           && statusLabel->getWidth() + 10 >= statusTextW,
                           "v1.34.5 the status card is measured to its one-line text",
                           status.toString() + " / "
                             + (statusLabel != nullptr ? statusLabel->getBounds().toString()
                                                      : juce::String())
                             + " / text " + juce::String (statusTextW));

                    // the card follows its text: a longer status gets a
                    // wider card, and the label keeps the whole string
                    if (statusLabel != nullptr)
                    {
                        const juce::String longText ("Page 1/1 | 12 arcs | 3 zone(s)  --  "
                                                    "Sel #7 v3 e4 int 0.80 [12.3-45.6s]");
                        const juce::String wasText = statusLabel->getText();
                        statusLabel->setText (longText, juce::dontSendNotification);
                        mc.resized();
                        const auto card2 = mc.toolbarCardBoundsForGroupForTest ("status");
                        const int need2 = juce::GlyphArrangement::getStringWidthInt (
                            mc.getLookAndFeel().getLabelFont (*statusLabel), longText);
                        check (card2.getWidth() >= need2 - 24
                               && card2.getWidth() <= need2 + 80
                               && statusLabel->getWidth() + 10 >= need2,
                               "v1.34.5 the status card grows with its text",
                               card2.toString() + " / need " + juce::String (need2)
                                 + " / label " + statusLabel->getBounds().toString());
                        statusLabel->setText (wasText, juce::dontSendNotification);
                        mc.resized();
                    }

                    // v1.34.3: the cards follow the user's order - the
                    // acceleration card sits after the tempo, the drag card
                    // after the scrub settings, the status last.
                    {
                        const char* order[] = { "transport", "pages", "file", "voice",
                                                "sieve", "tools", "tempo", "accel",
                                                "levels", "material", "magnets", "scrub",
                                                "drag", "pitch", "page", "clock", "status" };
                        bool inOrder = true;
                        juce::String detail;
                        for (int i = 0; i + 1 < (int) juce::numElementsInArray (order); ++i)
                        {
                            const auto a = mc.toolbarCardBoundsForGroupForTest (order[i]);
                            const auto b = mc.toolbarCardBoundsForGroupForTest (order[i + 1]);
                            const bool ok = ! a.isEmpty() && ! b.isEmpty()
                                          && (a.getY() < b.getY()
                                              || (a.getY() == b.getY() && a.getX() < b.getX()));
                            if (! ok)
                            {
                                inOrder = false;
                                detail << order[i] << a.toString() << " before "
                                       << order[i + 1] << b.toString() << "  ";
                            }
                        }
                        check (inOrder, "v1.34.3 the cards follow the user's order",
                               detail.trim());
                    }

                    // v1.34.3: the cards carry the user's captions.
                    {
                        auto* gen  = dynamic_cast<juce::ToggleButton*> (find ("toolsFoldBtn"));
                        auto* acc  = dynamic_cast<juce::ToggleButton*> (find ("accelFoldBtn"));
                        auto* scr  = dynamic_cast<juce::ToggleButton*> (find ("scrubFoldBtn"));
                        check (gen != nullptr && acc != nullptr && scr != nullptr
                               && gen->getButtonText() == "Generator"
                               && acc->getButtonText() == "Acceleration"
                               && scr->getButtonText() == "Scrub settings",
                               "v1.34.3 the cards carry the user's captions");
                    }
                    mc.setSize (1280, 800);
                }
            }

            // v1.33: the four frequency spaces - each dropdown has to
            // reach the model, not just the first one.
            {
                auto* s1 = dynamic_cast<juce::ComboBox*> (find ("spaceDefBox0"));
                auto* s2 = dynamic_cast<juce::ComboBox*> (find ("spaceDefBox1"));
                auto* s3 = dynamic_cast<juce::ComboBox*> (find ("spaceDefBox2"));
                auto* s4 = dynamic_cast<juce::ComboBox*> (find ("spaceDefBox3"));
                check (s1 != nullptr && s2 != nullptr && s3 != nullptr && s4 != nullptr,
                       "the four space controls exist");
                if (s2 != nullptr && s3 != nullptr && s4 != nullptr)
                {
                    // "An octave up" is the fourth preset (id 4)
                    s2->setSelectedId (4, juce::dontSendNotification); s2->onChange();
                    s3->setSelectedId (5, juce::dontSendNotification); s3->onChange();
                    s4->setSelectedId (2, juce::dontSendNotification); s4->onChange();
                    check (mc.pageSpaceForTest (0) == 0
                        && mc.pageSpaceForTest (1) == 3
                        && mc.pageSpaceForTest (2) == 4
                        && mc.pageSpaceForTest (3) == 1,
                           "every space dropdown reaches the model",
                           juce::String (mc.pageSpaceForTest (0)) + ", "
                           + juce::String (mc.pageSpaceForTest (1)) + ", "
                           + juce::String (mc.pageSpaceForTest (2)) + ", "
                           + juce::String (mc.pageSpaceForTest (3)));
                    // back to the defaults
                    s2->setSelectedId (1, juce::dontSendNotification); s2->onChange();
                    s3->setSelectedId (1, juce::dontSendNotification); s3->onChange();
                    s4->setSelectedId (1, juce::dontSendNotification); s4->onChange();
                }
            }

            // v1.33.2: the toolbar's Sieve checkbox and its m/R fields
            // must reach the canvas. The unit tests above call setSieve()
            // directly, which is exactly how a broken checkbox can hide.
            {
                // the layout loop above ends at 360x640; the drawing
                // tests need the full-size canvas so a screen y maps to
                // the page's real pitch range.
                mc.setSize (1280, 800);
                auto* sc = dynamic_cast<ScoreEditor*> (find ("score"));
                auto* sb = dynamic_cast<juce::ToggleButton*> (find ("sieveBtn"));
                auto* me = dynamic_cast<juce::TextEditor*> (find ("sieveMEdit"));
                auto* re = dynamic_cast<juce::TextEditor*> (find ("sieveREdit"));
                check (sc != nullptr && sb != nullptr && me != nullptr && re != nullptr,
                       "the Sieve checkbox and its m/R fields exist");
                if (sc != nullptr && sb != nullptr && me != nullptr && re != nullptr)
                {
                    me->setText ("7", juce::dontSendNotification); me->onTextChange();
                    re->setText ("0,2", juce::dontSendNotification); re->onTextChange();
                    sb->setToggleState (true, juce::dontSendNotification); sb->onClick();
                    check (sc->getSieveOn() && sc->getSieveM() == 7
                           && sc->getSieveR() == std::vector<int>({ 0, 2 }),
                           "the Sieve checkbox and fields reach the canvas",
                           juce::String (sc->getSieveOn() ? "on " : "off ")
                           + juce::String (sc->getSieveM()) + " {"
                           + ScoreEditor::remaindersTextForTest (sc->getSieveR()) + "}");
                    // End-to-end: the checked box must change the drawn
                    // pitches. m12 {0} is the octave sieve: every point
                    // lands on a C. With the box off the same line is
                    // chromatic (the Snap default), so it has non-C's.
                    sc->setSnap (true);
                    sc->setTimeGrid (0.0);
                    sc->setStickiness (1.0);
                    me->setText ("12", juce::dontSendNotification); me->onTextChange();
                    re->setText ("0", juce::dontSendNotification); re->onTextChange();
                    sc->commitLineForTest ({ sc->getWidth() * 0.15f, sc->getHeight() * 0.25f },
                                            { sc->getWidth() * 0.75f, sc->getHeight() * 0.80f });
                    const auto sieved = sc->lastArcFreqsForTest();
                    bool allC = ! sieved.empty();
                    for (double f : sieved)
                    {
                        const double n = upic::freqToMidi (f);
                        const int nn = (int) std::llround (n);
                        if (std::abs (n - (double) nn) > 1e-6
                            || ((nn % 12) + 12) % 12 != 0)
                            allC = false;
                    }
                    check (allC, "the Sieve box changes the drawn pitches (C-only)",
                           juce::String ((int) sieved.size()) + " points, first "
                           + juce::String (sieved.empty() ? 0.0 : upic::freqToMidi (sieved.front()), 2)
                           + "/" + juce::String (sieved.empty() ? 0.0 : upic::freqToMidi (sieved.back()), 2));

                    sb->setToggleState (false, juce::dontSendNotification); sb->onClick();
                    check (! sc->getSieveOn(), "unchecking the Sieve box turns the sieve off");
                    sc->commitLineForTest ({ sc->getWidth() * 0.15f, sc->getHeight() * 0.25f },
                                            { sc->getWidth() * 0.75f, sc->getHeight() * 0.80f });
                    const auto freeFreqs = sc->lastArcFreqsForTest();
                    bool anyNonC = false;
                    for (double f : freeFreqs)
                    {
                        const int nn = (int) std::llround (upic::freqToMidi (f));
                        if (((nn % 12) + 12) % 12 != 0) anyNonC = true;
                    }
                    check (anyNonC, "with the Sieve box off the line is chromatic again");
                    // back to the defaults for the snapshot
                    me->setText ("12", juce::dontSendNotification); me->onTextChange();
                    re->setText ("0", juce::dontSendNotification); re->onTextChange();
                    sb->setToggleState (false, juce::dontSendNotification); sb->onClick();
                    // an empty R field must keep the last valid list: the
                    // placeholder made it look like 0,7 while the real
                    // list was empty, so the checkbox quantised nothing.
                    re->setText ("", juce::dontSendNotification); re->onTextChange();
                    check (sc->getSieveR() == std::vector<int>({ 0 }),
                           "an empty R field keeps the last valid sieve",
                           ScoreEditor::remaindersTextForTest (sc->getSieveR()));
                    re->setText ("0", juce::dontSendNotification); re->onTextChange();
                }
            }

            // v1.33: a sieve zone can be moved and resized. The zone
            // tool used to let you draw one and delete it, but not
            // touch it afterwards.
            {
                auto* sc = dynamic_cast<ScoreEditor*> (find ("score"));
                check (sc != nullptr, "the canvas exists");
                if (sc != nullptr)
                {
                    const int zid = sc->addZone (2.0, 4.0, 200.0, 800.0);
                    check (zid > 0, "a zone can be drawn");
                    // its rectangle on screen
                    const auto zr = sc->zoneRectForTest (zid);
                    check (zr.getWidth() > 1.0f && zr.getHeight() > 1.0f,
                           "the zone has a rectangle");

                    // drag it by the middle: it moves
                    const juce::Point<float> mid (zr.getCentreX(), zr.getCentreY());
                    sc->beginZoneDragForTest (mid);
                    const double tBefore = sc->zoneTimeForTest (zid, 0);
                    sc->dragZoneToForTest ({ mid.x + 40.0f, mid.y });
                    const double tAfter = sc->zoneTimeForTest (zid, 0);
                    check (std::abs (tAfter - tBefore) > 0.05,
                           "a zone can be dragged to a new place",
                           juce::String (tBefore, 2) + " s -> "
                           + juce::String (tAfter, 2) + " s");

                    // drag its right edge: it resizes
                    const auto zr2 = sc->zoneRectForTest (zid);
                    const double wBefore = zr2.getWidth();
                    sc->beginZoneDragForTest ({ zr2.getRight(), zr2.getCentreY() });
                    sc->dragZoneToForTest ({ zr2.getRight() + 60.0f, zr2.getCentreY() });
                    const double wAfter = sc->zoneRectForTest (zid).getWidth();
                    check (wAfter > wBefore + 10.0f,
                           "a zone can be resized by its edge",
                           juce::String (wBefore, 0) + " -> "
                           + juce::String (wAfter, 0) + " px");
                    sc->deleteZone (zid);
                }
            }

            // v1.33.2: every zone-menu action must reach the zone, and a
            // zone's own stick must override the global stick inside it.
            {
                auto* sc = dynamic_cast<ScoreEditor*> (find ("score"));
                check (sc != nullptr, "the canvas exists for the zone menu test");
                if (sc != nullptr)
                {
                    sc->setSieve (true, 12, { 0, 7 });
                    sc->setTimeGrid (2.0);
                    sc->setStickiness (0.8);
                    const int zid = sc->addZone (1.0, 9.0, 120.0, 900.0);
                    check (zid > 0, "the zone-menu zone can be drawn");
                    check (std::abs (sc->zoneStickinessForTest (zid) - 0.8) < 1e-9,
                           "a new zone inherits the global stick",
                           juce::String (sc->zoneStickinessForTest (zid), 2));

                    // use the toolbar's current sieve
                    sc->setSieve (true, 5, { 1, 3 });
                    sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuSieveCurrent);
                    check (sc->zoneSieveMForTest (zid) == 5
                           && sc->zoneSieveRForTest (zid) == std::vector<int>({ 1, 3 }),
                           "zone menu: use the current sieve reaches the zone");

                    // a sieve preset (Major is index 2)
                    sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuSievePreset + 2);
                    check (sc->zoneSieveMForTest (zid) == 12
                           && sc->zoneSieveRForTest (zid)
                                  == std::vector<int>({ 0, 2, 4, 5, 7, 9, 11 }),
                           "zone menu: a sieve preset reaches the zone",
                           "m" + juce::String (sc->zoneSieveMForTest (zid)) + " {"
                           + ScoreEditor::remaindersTextForTest (sc->zoneSieveRForTest (zid)) + "}");

                    // a time grid (4/s is index 3)
                    sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuGridChoice + 3);
                    check (std::abs (sc->zoneTimeGridForTest (zid) - 4.0) < 1e-9,
                           "zone menu: a time grid reaches the zone");

                    // stick 0% -> the page's global stick (1.0) is ignored
                    sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuStickChoice);
                    check (std::abs (sc->zoneStickinessForTest (zid)) < 1e-9,
                           "zone menu: a stick value reaches the zone");
                    sc->setStickiness (1.0);
                    double tSec = 5.0, f = 450.0;
                    sc->quantizeXY (tSec, f);
                    check (std::abs (f - 450.0) < 0.5,
                           "a zone's own stick overrides the global stick",
                           juce::String (f, 1) + " Hz");

                    // follow the global stick again -> the major sieve bites
                    sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuStickFollow);
                    check (sc->zoneStickinessForTest (zid) < 0.0,
                           "zone menu: follow the global stick");
                    tSec = 5.0; f = 450.0;
                    sc->quantizeXY (tSec, f);
                    check (std::abs (f - 440.0) < 0.6,
                           "follow-global uses the global stick",
                           juce::String (f, 1) + " Hz, stick "
                           + juce::String (sc->zoneStickinessForTest (zid), 2)
                           + ", m" + juce::String (sc->zoneSieveMForTest (zid))
                           + " {" + ScoreEditor::remaindersTextForTest (sc->zoneSieveRForTest (zid)) + "}");

                    // delete through the menu
                    sc->applyZoneMenuForTest (zid, ScoreEditor::zoneMenuDelete);
                    check (sc->zoneSieveMForTest (zid) == -1,
                           "zone menu: delete removes the zone");
                    sc->setStickiness (1.0);
                    sc->setSieve (false, 12, { 0 });
                    sc->setTimeGrid (0.0);
                }
            }

            // v1.33: a quantised line must keep its points in order.
            // The grid snapping can pull a later point in front of an
            // earlier one; the engine's segment search assumes the
            // points are sorted, and a line whose points were not came
            // out distorted.
            {
                auto* sc = dynamic_cast<ScoreEditor*> (find ("score"));
                check (sc != nullptr, "the canvas exists for the line test");
                if (sc != nullptr)
                {
                    sc->setSieve (true, 12, { 0 });
                    sc->setTimeGrid (2.0);
                    sc->setStickiness (1.0);
                    sc->commitLineForTest ({ 60.0f, 60.0f }, { 340.0f, 300.0f });
                    check (sc->lastArcPointsSortedForTest(),
                           "a quantised line keeps its points in order");
                    check (sc->lastArcHasStaircaseForTest(),
                           "a magnetised line is a staircase, not a ramp");
                    sc->setStickiness (0.5);
                    sc->commitLineForTest ({ 60.0f, 60.0f }, { 340.0f, 300.0f });
                    check (! sc->lastArcHasStaircaseForTest(),
                           "a soft stick leaves the line smooth (no fake steps)");
                    sc->setStickiness (1.0);
                    sc->setSieve (false, 12, { 0 });
                    sc->setTimeGrid (0.0);
                }
            }

            // v1.33: the ambitus reaches the canvas - the page's own
            // interval, not the whole world.
            {
                auto* amb = dynamic_cast<juce::ComboBox*> (find ("ambitusBox"));
                auto* sc  = dynamic_cast<ScoreEditor*> (find ("score"));
                check (amb != nullptr && sc != nullptr,
                       "the ambitus control and the canvas exist");
                if (amb != nullptr && sc != nullptr)
                {
                    // "A semitone (C4 - C#4)" - the last preset. The
                    // combo's change notification is asynchronous and the
                    // selftest runs no message loop, so the handler is
                    // called directly.
                    amb->setSelectedId (6, juce::dontSendNotification);
                    amb->onChange();
                    const double lo = sc->ambitusLo(), hi = sc->ambitusHi();
                    check (std::abs (lo - 261.63) < 1e-6 && hi > lo && hi < 300.0,
                           "the ambitus control sets the page's interval",
                           juce::String (lo, 1) + " - " + juce::String (hi, 1) + " Hz");
                    // the canvas maps through it, both ways
                    const double mid = std::sqrt (lo * hi);
                    check (std::abs (sc->yToFreq (sc->freqToY (mid)) - mid) < 1e-6,
                           "the canvas maps the page's interval");
                    // and a pitch outside the page is off the page: it
                    // cannot be drawn back
                    const double outside = sc->yToFreq (sc->freqToY (mid * 4.0));
                    check (outside <= hi + 1e-6 && outside >= lo - 1e-6,
                           "a pitch outside the page cannot be drawn on it",
                           juce::String (outside, 1) + " Hz");
                    // back to the default interval for the snapshot
                    amb->setSelectedId (1, juce::dontSendNotification);
                    amb->onChange();
                    check (std::abs (sc->ambitusLo() - upic::kDefaultPitchLo) < 1e-6,
                           "the ambitus goes back to the default interval");
                }
            }

            // v1.26: every material tab shows only its own controls and
            // still tiles cleanly. This catches controls that are in the
            // flow row but were never added to the component tree.
            {
                // the tab toggles are named by their text (JUCE Button
                // passes the button text to Component as its name)
                struct TabCheck { const char* button; const char* tab; const char* shown; const char* hidden; };
                const TabCheck tabs[] = {
                    { "Osc",    "Osc",    "waveBox",      "sampleBox" },
                    { "Sample", "Sample", "sampleBox",    "waveBox" },
                    { "Live",   "Live",   "livePitchBox", "sampleBox" },
                    { "PM",     "PM",     "pmOpsBox",     "sampleBox" },
                };
                for (auto tabCheck : tabs)
                {
                    if (auto* tab = dynamic_cast<juce::Button*> (find (tabCheck.button)))
                    {
                        tab->setToggleState (true, juce::dontSendNotification);
                        tab->onClick();
                    }
                    juce::String detail;
                    check (layoutOk (1280, 800, detail),
                           juce::String ("tab layout ") + tabCheck.tab, detail.trim());
                    auto* shown = find (tabCheck.shown);
                    auto* hidden = find (tabCheck.hidden);
                    check (shown != nullptr && shown->isVisible(),
                           juce::String ("tab ") + tabCheck.tab + " shows " + tabCheck.shown);
                    check (hidden != nullptr && ! hidden->isVisible(),
                           juce::String ("tab ") + tabCheck.tab + " hides " + tabCheck.hidden);
                }
                if (auto* tab = dynamic_cast<juce::Button*> (find ("Osc")))
                {
                    tab->setToggleState (true, juce::dontSendNotification);
                    tab->onClick();
                }
            }

            // v1.25: the sample input/capture group is Sample-only, and
            // the sieve controls sit with the sieve preset on one row.
            {
                auto clickTab = [&](const char* n)
                {
                    if (auto* t = dynamic_cast<juce::Button*> (find (n)))
                    {
                        t->setToggleState (true, juce::dontSendNotification);
                        t->onClick();
                    }
                };
                auto* in  = find ("inGainSlider");
                auto* mon = find ("Mon");
                auto* cap = find ("capBtn");   // the text changes to "Stop" while it records
                auto* sb  = find ("sieveBtn");
                auto* sp  = find ("sievePresetBox");
                auto* sm  = find ("sieveMEdit");
                auto* sr  = find ("sieveREdit");

                clickTab ("Sample");
                check (in != nullptr && mon != nullptr && cap != nullptr
                       && in->isVisible() && mon->isVisible() && cap->isVisible(),
                       "Sample tab shows the input/capture group");
                check (sb != nullptr && sp != nullptr && sm != nullptr && sr != nullptr
                       && sb->getY() == sp->getY() && sm->getY() == sp->getY()
                       && sr->getY() == sp->getY(),
                       "sieve controls share the preset row",
                       sb != nullptr && sp != nullptr && sm != nullptr && sr != nullptr
                           ? juce::String (sb->getY()) + "/" + juce::String (sp->getY())
                             + "/" + juce::String (sm->getY()) + "/" + juce::String (sr->getY())
                           : juce::String ("missing"));

                const char* others[] = { "Osc", "Live", "PM" };
                for (auto* n : others)
                {
                    clickTab (n);
                    check (in != nullptr && mon != nullptr && cap != nullptr
                           && ! in->isVisible() && ! mon->isVisible() && ! cap->isVisible(),
                           juce::String (n) + " hides the input/capture group");
                }
                clickTab ("Osc");
            }

            // PM tab snapshot: the densest material row, kept for visual
            // inspection of labels, text boxes and wrapping.
            {
                if (auto* tab = dynamic_cast<juce::Button*> (find ("PM")))
                {
                    tab->setToggleState (true, juce::dontSendNotification);
                    tab->onClick();
                }
                mc.setSize (1280, 800);
                juce::Image pi (juce::Image::RGB, 1280, 800, true);
                {
                    juce::Graphics pg (pi);
                    mc.paintEntireComponent (pg, true);
                }
                auto pf = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("upic_snapshot_pm.png");
                pf.deleteFile();
                bool pw = false;
                if (auto os = pf.createOutputStream())
                {
                    juce::PNGImageFormat fmt;
                    pw = fmt.writeImageToStream (pi, *os);
                    os->flush();
                }
                check (pw && pf.getSize() > 15000, "PM tab snapshot render",
                       juce::String (pf.getSize()) + " bytes -> " + pf.getFullPathName());
                if (auto* tab = dynamic_cast<juce::Button*> (find ("Osc")))
                {
                    tab->setToggleState (true, juce::dontSendNotification);
                    tab->onClick();
                }
            }

            // --- v1.19: the Reverse button (the selected slot plays
            //     backwards) must be there and must toggle ---
            {
                auto* rb = dynamic_cast<juce::TextButton*>(find("revBtn"));
                check(rb != nullptr, "the Reverse button is present");
                if (rb != nullptr)
                {
                    check(rb->getButtonText() == "Rev",
                          "the Reverse button starts off",
                          rb->getButtonText());
                    // a real click toggles the button and then calls onClick
                    rb->setToggleState(true, juce::dontSendNotification);
                    rb->onClick();
                    check(rb->getButtonText() == "Rev ON",
                          "the Reverse button turns on",
                          rb->getButtonText());
                    check(rb->getToggleState(), "the Reverse button is toggled on");
                    rb->setToggleState(false, juce::dontSendNotification);
                    rb->onClick();
                    check(rb->getButtonText() == "Rev" && ! rb->getToggleState(),
                          "the Reverse button turns off again",
                          rb->getButtonText());
                }
            }

            // --- v1.7 view toggles: hiding the bottom editors and/or the
            //     hints must keep every visible view in a valid layout ---
            {
                auto* wt = dynamic_cast<juce::Button*>(find("Wave"));
                auto* et = dynamic_cast<juce::Button*>(find("Env"));
                auto* ht = dynamic_cast<juce::Button*>(find("Hints"));
                check(wt != nullptr && et != nullptr && ht != nullptr,
                      "view toggles present (Wave / Env / Hints)");
                if (wt != nullptr && et != nullptr && ht != nullptr)
                {
                    auto setTog = [](juce::Button* b, bool on) {
                        b->setToggleState(on, juce::dontSendNotification);
                        if (b->onClick) b->onClick();
                    };
                    setTog(wt, false);
                    setTog(et, false);
                    setTog(ht, false);
                    auto* normBtn = find("Norm");
                    auto* strBtn  = find("Strings");
                    auto* kbd     = find("kbdHintLab");
                    check(normBtn != nullptr && ! normBtn->isVisible()
                          && strBtn != nullptr && ! strBtn->isVisible()
                          && kbd != nullptr && ! kbd->isVisible(),
                          "view toggles hide editors + hints");
                    juce::String detail;
                    bool ok = layoutOk(640, 400, detail);
                    ok = layoutOk(360, 640, detail) && ok;
                    check(ok, "hidden views: layout still tiles", detail.trim());

                    // offscreen snapshot of the hidden views for eyeballing
                    {
                        mc.setSize(1280, 800);
                        juce::Image hi(juce::Image::RGB, 1280, 800, true);
                        {
                            juce::Graphics hg(hi);
                            mc.paintEntireComponent(hg, true);
                        }
                        auto hf = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("upic_snapshot_noviews.png");
                        hf.deleteFile();
                        bool hw = false;
                        if (auto os = hf.createOutputStream())
                        {
                            juce::PNGImageFormat fmt;
                            hw = fmt.writeImageToStream(hi, *os);
                            os->flush();
                        }
                        check(hw && hf.getSize() > 15000, "hidden views snapshot render",
                              juce::String(hf.getSize()) + " bytes -> " + hf.getFullPathName());
                    }

                    setTog(wt, true);
                    setTog(et, true);
                    setTog(ht, true);
                    juce::String d2;
                    check(layoutOk(1280, 800, d2), "views restored at 1280x800", d2.trim());
                }
                mc.setSize(1280, 800); // design size for the checks below
            }

            // --- v1.8 the one tab strip: Osc | Sample | Live - each tab
            //     is a material and its own canvas layer (the old Score /
            //     Live pitch buttons and the Material dropdown are gone) ---
            {
                auto* mOsc = dynamic_cast<juce::Button*>(find("Osc"));
                auto* mSam = dynamic_cast<juce::Button*>(find("Sample"));
                auto* mLiv = dynamic_cast<juce::Button*>(find("Live"));
                check(mOsc != nullptr && mSam != nullptr && mLiv != nullptr,
                      "material tabs present (Osc / Sample / Live)");
                if (mOsc != nullptr && mSam != nullptr && mLiv != nullptr)
                {
                    check(mOsc->getToggleState(), "Osc tab is the default layer");
                    mSam->setToggleState(true, juce::dontSendNotification);
                    mSam->onClick(); // pick the Sample tab
                    check(mSam->getToggleState() && ! mOsc->getToggleState()
                          && ! mLiv->getToggleState(),
                          "Sample tab is the single active tab");

                    // offscreen snapshot of the Sample tab (speed grid)
                    {
                        juce::Image si(juce::Image::RGB, 1280, 800, true);
                        {
                            juce::Graphics sg(si);
                            mc.paintEntireComponent(sg, true);
                        }
                        auto sf = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("upic_snapshot_sample.png");
                        sf.deleteFile();
                        bool sw = false;
                        if (auto os = sf.createOutputStream())
                        {
                            juce::PNGImageFormat fmt;
                            sw = fmt.writeImageToStream(si, *os);
                            os->flush();
                        }
                        check(sw && sf.getSize() > 15000, "sample tab snapshot render",
                              juce::String(sf.getSize()) + " bytes -> " + sf.getFullPathName());
                    }

                    mOsc->setToggleState(true, juce::dontSendNotification);
                    mOsc->onClick(); // back to Osc
                    check(mOsc->getToggleState() && ! mSam->getToggleState(),
                          "Osc tab restores");
                }
            }

            // --- v1.25: the Live tab always uses the multiply-ratio grid
            //     (the old Hz grid and its toggle are gone) ---
            {
                auto* sc = dynamic_cast<ScoreEditor*>(find("score"));
                auto* mLiv = dynamic_cast<juce::Button*>(find("Live"));
                check(sc != nullptr, "score editor present");
                check(find("Ratio grid") == nullptr,
                      "the Live ratio-grid toggle is gone");
                if (sc != nullptr && mLiv != nullptr)
                {
                    // v1.17: switching tabs is pure UI work - it must not
                    // open the audio device (that blocked the message
                    // thread for seconds on the first Live click)
                    const auto tabT0 = juce::Time::getMillisecondCounterHiRes();
                    mLiv->setToggleState(true, juce::dontSendNotification);
                    mLiv->onClick();
                    const auto tabT1 = juce::Time::getMillisecondCounterHiRes();
                    check(tabT1 - tabT0 < 500.0,
                          "Live tab click does not open the audio device",
                          juce::String(tabT1 - tabT0, 2) + " ms");
                    check(sc->getLayer() == 2, "Live tab activates the live layer");

                    juce::Image li(juce::Image::RGB, 1280, 800, true);
                    {
                        juce::Graphics lg(li);
                        mc.paintEntireComponent(lg, true);
                    }
                    auto lf = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("upic_snapshot_live_ratio.png");
                    lf.deleteFile();
                    bool lw = false;
                    if (auto os = lf.createOutputStream())
                    {
                        juce::PNGImageFormat fmt;
                        lw = fmt.writeImageToStream(li, *os);
                        os->flush();
                    }
                    check(lw && lf.getSize() > 15000, "live ratio grid snapshot render",
                          juce::String(lf.getSize()) + " bytes -> " + lf.getFullPathName());

                    // back to the Osc tab for the rest of the run
                    if (auto* mOsc = dynamic_cast<juce::Button*>(find("Osc")))
                    {
                        mOsc->setToggleState(true, juce::dontSendNotification);
                        mOsc->onClick();
                    }
                    check(sc->getLayer() == 0, "Osc tab restores the layer");
                }
            }

            // --- v1.10: the sample pitch mode selector sits in the
            //     material row (varispeed default, granular, vocoder) ---
            {
                auto* sp = dynamic_cast<juce::ComboBox*>(find("samplePitchBox"));
                check(sp != nullptr && sp->getNumItems() == 4
                      && sp->getSelectedId() == 1,
                      "sample pitch selector present",
                      sp != nullptr ? sp->getText() : juce::String("selector missing"));
                if (sp != nullptr)
                {
                    sp->setSelectedId(3, juce::sendNotification); // vocoder
                    check(sp->getSelectedId() == 3 && sp->getText() == "Phase vocoder",
                          "sample pitch selector switches", sp->getText());
                    sp->setSelectedId(4, juce::sendNotification); // granular no stretch
                    check(sp->getSelectedId() == 4
                          && sp->getText() == "Granular (no stretch)",
                          "sample pitch selector adds no-stretch granular", sp->getText());
                    sp->setSelectedId(1, juce::sendNotification); // default on
                }
            }

            // --- v1.7 audio settings: the in-app overlay must open with a
            //     usable OK button and close again (Android has no dialog
            //     windows, so this is the only way to dismiss it) ---
            {
                auto* ab = dynamic_cast<juce::Button*>(find("Audio"));
                check(ab != nullptr, "audio settings button present");
                if (ab != nullptr)
                {
                    ab->onClick(); // open
                    auto* ov = find("audioOverlay");
                    auto* ok = dynamic_cast<juce::Button*>(find("OK"));
                    check(ov != nullptr && ov->isVisible()
                          && ok != nullptr && ok->isVisible() && ok->getWidth() >= 8
                          && ov->getBounds().contains(ok->getBounds()),
                          "audio overlay opens with its OK button");
                    // v1.26: the selector is added on top of the overlay's
                    // other children; the OK button must still win the hit
                    // test at its centre (the reported "OK does nothing").
                    auto* hit = (ov != nullptr && ok != nullptr)
                                    ? ov->getComponentAt(ok->getBounds().getCentre())
                                    : nullptr;
                    check(hit == ok, "audio overlay OK is clickable at its centre",
                          hit != nullptr ? hit->getName() : juce::String("no component"));

                    // v1.9: the live pitch algorithm selector lives in the
                    // overlay (granular default, tape, phase vocoder)
                    auto* lp = dynamic_cast<juce::ComboBox*>(find("livePitchBox"));
                    check(lp != nullptr && lp->getNumItems() == 3
                          && lp->getSelectedId() == 1,
                          "audio overlay has the live pitch selector",
                          lp != nullptr ? lp->getText() : juce::String("selector missing"));
                    if (lp != nullptr)
                    {
                        lp->setSelectedId(3, juce::sendNotification); // vocoder
                        check(lp->getSelectedId() == 3 && lp->getText() == "Phase vocoder",
                              "live pitch selector switches", lp->getText());
                        lp->setSelectedId(1, juce::sendNotification); // default on
                    }

                    // offscreen snapshot of the open overlay for eyeballing
                    {
                        juce::Image ai(juce::Image::RGB, 1280, 800, true);
                        {
                            juce::Graphics ag(ai);
                            mc.paintEntireComponent(ag, true);
                        }
                        auto af = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                      .getChildFile("upic_snapshot_audio.png");
                        af.deleteFile();
                        bool aw = false;
                        if (auto os = af.createOutputStream())
                        {
                            juce::PNGImageFormat fmt;
                            aw = fmt.writeImageToStream(ai, *os);
                            os->flush();
                        }
                        check(aw && af.getSize() > 15000, "audio overlay snapshot render",
                              juce::String(af.getSize()) + " bytes -> " + af.getFullPathName());
                    }

                    if (ok != nullptr) ok->onClick(); // close
                    check(ov != nullptr && ! ov->isVisible(),
                          "audio overlay OK closes it");
                }
            }

            // --- piece round trip through the public (de)serialization API ---
            mc.loadFromVar(juce::JSON::parse(
                "{\"pages\":[{\"duration\":10,\"arcs\":[]}]}")); // legacy: no v1.1/v1.2 keys
            auto legacyOut = mc.pieceToVar();
            check((double) legacyOut.getProperty("stickiness", -1.0) == 1.0
                  && (double) legacyOut.getProperty("timeGrid", -1.0) == 0.0,
                  "legacy piece defaults: full stickiness, no time grid");
            {
                auto* pgArr = legacyOut.getProperty("pages", juce::var()).getArray();
                check(pgArr != nullptr && ! pgArr->isEmpty()
                      && ! pgArr->getReference(0).getProperty("zones", juce::var()).isArray(),
                      "legacy piece keeps zones key absent");
            }
            mc.loadFromVar(juce::JSON::parse(
                "{\"stickiness\":0.45,\"timeGrid\":2,"
                "\"pages\":[{\"duration\":12,\"zones\":[{\"id\":1,\"t0\":1,\"t1\":5,"
                "\"f0\":200,\"f1\":900,\"m\":12,\"r\":[0,2,4],\"timeGrid\":4}]}]}"));
            auto out = mc.pieceToVar();
            check(std::abs((double) out.getProperty("stickiness", -1.0) - 0.45) < 1e-9,
                  "piece json round-trips stickiness");
            check(std::abs((double) out.getProperty("timeGrid", -1.0) - 2.0) < 1e-9,
                  "piece json round-trips timeGrid");
            {
                bool zoneOk = false;
                if (auto* pgArr = out.getProperty("pages", juce::var()).getArray();
                    pgArr != nullptr && ! pgArr->isEmpty())
                    if (auto* zs = pgArr->getReference(0).getProperty("zones", juce::var()).getArray();
                        zs != nullptr && zs->size() == 1)
                    {
                        const auto& z = zs->getReference(0);
                        zoneOk = (int) z.getProperty("m", 0) == 12
                              && std::abs((double) z.getProperty("timeGrid", 0.0) - 4.0) < 1e-9
                              && (double) z.getProperty("t0", -1.0) == 1.0;
                    }
                check(zoneOk, "piece json round-trips sieve zones");
            }

            // --- v1.33.4 (H6): the saver measures the piece ---
            // The loader refuses a piece over the file cap; the saver
            // used to write whatever it had, so the app could save a
            // piece it then refused to reopen.
            {
                auto saveHolder = std::make_unique<MainComponent>();
                auto& mcSave = *saveHolder;
                mcSave.setSize(1280, 800);
                mcSave.loadFromVar(juce::JSON::parse(
                    "{\"pages\":[{\"duration\":8,\"nextArcId\":2,\"arcs\":["
                    "{\"id\":1,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
                    "\"points\":[{\"t\":0,\"f\":220},{\"t\":4,\"f\":880}]}]}]}"));

                auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("upic_piece_save_test.json");
                file.deleteFile();
                const bool saved = mcSave.savePieceToFile(file);
                check(saved && file.existsAsFile() && file.getSize() > 100,
                      "the piece saver writes a piece that fits",
                      juce::String((int) file.getSize()));

                // ...and that file loads into a fresh app
                bool reloaded = false;
                if (file.existsAsFile())
                {
                    auto loadHolder = std::make_unique<MainComponent>();
                    auto& mcLoad = *loadHolder;
                    mcLoad.loadFromVar(juce::JSON::parse(file.loadFileAsString()));
                    // NB: getProperty() returns a reference into the var,
                    // so the piece has to stay alive in a named variable
                    auto piece = mcLoad.pieceToVar();
                    if (auto* pgArr = piece.getProperty("pages", juce::var()).getArray();
                        pgArr != nullptr && !pgArr->isEmpty())
                    {
                        auto* arcs = pgArr->getReference(0).getProperty("arcs", juce::var()).getArray();
                        reloaded = arcs != nullptr && arcs->size() == 1;
                    }
                }
                check(reloaded, "the saved piece reloads into a fresh app");

                // a piece over the limit is refused, and the file that was
                // there before is not touched
                const auto sizeBefore = file.getSize();
                const bool refused = mcSave.savePieceToFile(file, 1024);
                check(!refused && file.existsAsFile() && file.getSize() == sizeBefore,
                      "the piece saver refuses a piece over the limit",
                      juce::String((int) file.getSize()) + " vs " + juce::String((int) sizeBefore));

                // the two limits agree: the longest capture the app can
                // make (30 s at 96 kHz, stereo) has to fit in a piece
                const juce::int64 captureBytes = (juce::int64)(30.0 * 96000.0) * 2 * 2; // 16-bit stereo
                const juce::int64 asBase64 = (captureBytes * 4 + 2) / 3;
                check(asBase64 < (juce::int64) upic::limits::kMaxPieceBytes,
                      "the piece cap fits the longest capture",
                      juce::String((int)(asBase64 / (1024 * 1024))) + " MB vs "
                          + juce::String((int)(upic::limits::kMaxPieceBytes / (1024 * 1024))) + " MB");
                check(upic::limits::kMaxPieceBytes == (upic::limits::kMaxTotalSampleBytes / 3u) * 2u,
                      "the piece cap is derived from the sample budget");
                file.deleteFile();
            }

            // --- v1.33.4 (H7): the settings that change the sound ---
            // The arc's own pitch mode and its reversal, the sample
            // slot's reversal and the global pitch modes used to be
            // dropped when a piece was saved, so a reopened piece (or a
            // reopened DAW session) sounded different.
            {
                // a one-second tone, the piece format's way
                const int nFrames = 44100;
                juce::MemoryBlock mb ((size_t) nFrames * sizeof(short));
                {
                    auto* p = static_cast<short*> (mb.getData());
                    for (int i = 0; i < nFrames; ++i)
                        p[i] = (short) (std::sin (2.0 * juce::MathConstants<double>::pi
                                                  * 220.0 * (double) i / 44100.0) * 20000.0);
                }
                const auto b64 = juce::Base64::toBase64 (mb.getData(), mb.getSize());

                // a piece with a backwards sample slot, an arc with its
                // own algorithm, a second arc that follows the tab, and
                // the global pitch modes
                auto makePiece = [&b64, nFrames] (bool slotReversed, int arcPitchMode)
                {
                    auto* p = new juce::DynamicObject();
                    p->setProperty ("samplePitchMode", 1);   // granular (stretch)
                    p->setProperty ("livePitchMode", 2);     // phase vocoder

                    auto* slot = new juce::DynamicObject();
                    slot->setProperty ("id", 1);
                    slot->setProperty ("name", "tone");
                    slot->setProperty ("sampleRate", 44100.0);
                    slot->setProperty ("channels", 1);
                    slot->setProperty ("frames", nFrames);
                    slot->setProperty ("data", b64);
                    if (slotReversed) slot->setProperty ("reversed", true);
                    juce::Array<juce::var> slots;
                    slots.add (juce::var (slot));
                    p->setProperty ("samples", slots);

                    auto addArc = [] (juce::Array<juce::var>& arcs, int id,
                                      int pitchMode, bool reversed)
                    {
                        auto* a = new juce::DynamicObject();
                        a->setProperty ("id", id);
                        a->setProperty ("mode", 1);          // a sample arc
                        a->setProperty ("sampleId", 1);
                        a->setProperty ("refFreq", 261.63);
                        if (pitchMode >= 0) a->setProperty ("pitchMode", pitchMode);
                        if (reversed) a->setProperty ("reversed", true);
                        juce::Array<juce::var> points;
                        auto* p0 = new juce::DynamicObject();
                        p0->setProperty ("t", 0.0); p0->setProperty ("f", 261.63);
                        auto* p1 = new juce::DynamicObject();
                        p1->setProperty ("t", 4.0); p1->setProperty ("f", 523.25);
                        points.add (juce::var (p0));
                        points.add (juce::var (p1));
                        a->setProperty ("points", points);
                        arcs.add (juce::var (a));
                    };

                    auto* page = new juce::DynamicObject();
                    page->setProperty ("duration", 8.0);
                    page->setProperty ("nextArcId", 3);
                    juce::Array<juce::var> arcs;
                    addArc (arcs, 1, arcPitchMode, true);   // its own algorithm
                    addArc (arcs, 2, -1, false);            // follows the tab
                    page->setProperty ("arcs", arcs);
                    juce::Array<juce::var> pages;
                    pages.add (juce::var (page));
                    p->setProperty ("pages", pages);
                    return juce::var (p);
                };

                // the offline render of a piece - the path the WAV export
                // uses, so the comparison is the sound the user hears
                auto renderPiece = [] (const juce::var& pv, int samplePitchMode)
                {
                    upic::Page page;
                    std::vector<upic::Waveform> waves;
                    std::vector<upic::Envelope> envs;
                    std::vector<upic::SampleSlot> slots;
                    const auto pagesVar = pv.getProperty ("pages", juce::var());
                    if (auto* pages = pagesVar.getArray(); pages != nullptr && ! pages->isEmpty())
                        page = upic::Page::fromVar (pages->getReference (0));
                    const auto wavesVar = pv.getProperty ("waveforms", juce::var());
                    if (auto* arr = wavesVar.getArray(); arr != nullptr)
                        for (auto& w : *arr) waves.push_back (upic::Waveform::fromVar (w, 0));
                    const auto envsVar = pv.getProperty ("envelopes", juce::var());
                    if (auto* arr = envsVar.getArray(); arr != nullptr)
                        for (auto& e : *arr) envs.push_back (upic::Envelope::fromVar (e, 0));
                    const auto slotsVar = pv.getProperty ("samples", juce::var());
                    if (auto* arr = slotsVar.getArray(); arr != nullptr)
                        for (auto& s : *arr) slots.push_back (upic::SampleSlot::fromVar (s, 1));

                    const double sr = 44100.0;
                    juce::AudioBuffer<float> buf (2, (int) (sr * 5.0));
                    buf.clear();
                    UpicEngine eng;
                    eng.setSamplePitchMode (samplePitchMode);
                    eng.renderPageOffline (page, waves, envs, slots, buf, sr, 0.8f);
                    return buf;
                };
                auto maxDiff = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
                {
                    double d = 0.0;
                    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
                    for (int c = 0; c < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++c)
                        for (int i = 0; i < n; ++i)
                            d = std::max (d, std::abs ((double) a.getSample (c, i) - (double) b.getSample (c, i)));
                    return d;
                };
                auto peakOf = [] (const juce::AudioBuffer<float>& a)
                {
                    double p = 0.0;
                    for (int c = 0; c < a.getNumChannels(); ++c)
                        for (int i = 0; i < a.getNumSamples(); ++i)
                            p = std::max (p, std::abs ((double) a.getSample (c, i)));
                    return p;
                };

                // the piece round trip: what is written comes back
                const auto pieceVar = makePiece (true, 2);
                auto h7Holder = std::make_unique<MainComponent>();
                auto& mcH7 = *h7Holder;
                mcH7.loadFromVar (pieceVar);
                check (mcH7.samplePitchModeForTest() == 1 && mcH7.livePitchModeForTest() == 2,
                       "the global pitch modes come back with the piece",
                       juce::String (mcH7.samplePitchModeForTest()) + " / "
                           + juce::String (mcH7.livePitchModeForTest()));

                const auto saved = mcH7.pieceToVar();
                bool arcKept = false, slotKept = false;
                {
                    const auto pagesVar = saved.getProperty ("pages", juce::var());
                    if (auto* pages = pagesVar.getArray(); pages != nullptr && ! pages->isEmpty())
                    {
                        const auto arcsVar = pages->getReference (0).getProperty ("arcs", juce::var());
                        if (auto* arcs = arcsVar.getArray(); arcs != nullptr && ! arcs->isEmpty())
                        {
                            const auto& a = arcs->getReference (0);
                            arcKept = (int) a.getProperty ("pitchMode", -1) == 2
                                   && (bool) a.getProperty ("reversed", false);
                        }
                    }
                    const auto slotsVar = saved.getProperty ("samples", juce::var());
                    if (auto* slots = slotsVar.getArray(); slots != nullptr && ! slots->isEmpty())
                        slotKept = (bool) slots->getReference (0).getProperty ("reversed", false);
                }
                check (arcKept, "the arc's pitch mode and reversal travel with the piece");
                check (slotKept, "the sample slot's reversal travels with the piece");

                // an old piece (no such keys) keeps the old defaults
                {
                    const auto oldJson = "{\"pages\":[{\"duration\":8,\"nextArcId\":2,\"arcs\":["
                                         "{\"id\":1,\"mode\":1,\"sampleId\":1,\"refFreq\":261.63,"
                                         "\"points\":[{\"t\":0,\"f\":261.63},{\"t\":4,\"f\":523.25}]}]}]}";
                    auto oldHolder = std::make_unique<MainComponent>();
                    oldHolder->loadFromVar (juce::JSON::parse (oldJson));
                    const auto oldSaved = oldHolder->pieceToVar();
                    int oldPitchMode = -99;
                    bool oldReversed = true;
                    const auto pagesVar = oldSaved.getProperty ("pages", juce::var());
                    if (auto* pages = pagesVar.getArray(); pages != nullptr && ! pages->isEmpty())
                    {
                        const auto arcsVar = pages->getReference (0).getProperty ("arcs", juce::var());
                        if (auto* arcs = arcsVar.getArray(); arcs != nullptr && ! arcs->isEmpty())
                        {
                            const auto& a = arcs->getReference (0);
                            oldPitchMode = (int) a.getProperty ("pitchMode", -1);
                            oldReversed = (bool) a.getProperty ("reversed", false);
                        }
                    }
                    check (oldPitchMode == -1 && ! oldReversed,
                           "an old piece still loads with the old defaults",
                           juce::String (oldPitchMode) + (oldReversed ? " reversed" : " forwards"));
                }

                // the render before and after the round trip: the same
                // piece, loaded into a fresh app
                const auto before = renderPiece (pieceVar, mcH7.samplePitchModeForTest());
                auto h7LoadHolder = std::make_unique<MainComponent>();
                auto& mcH7Load = *h7LoadHolder;
                mcH7Load.loadFromVar (juce::JSON::parse (juce::JSON::toString (saved, true)));
                const auto after = renderPiece (saved, mcH7Load.samplePitchModeForTest());
                const double diff = maxDiff (before, after);
                check (peakOf (before) > 0.01 && diff < 0.01,
                       "a piece renders the same after a save and reload",
                       "peak " + juce::String (peakOf (before), 4) + ", diff " + juce::String (diff, 6));

                // the persisted settings are live: the reversal and the
                // arc's own algorithm change the sound
                {
                    const auto forward = renderPiece (makePiece (false, 2), 1);
                    const auto varispeed = renderPiece (makePiece (true, 0), 1);
                    check (maxDiff (before, forward) > 0.01,
                           "the slot's reversal changes the render",
                           juce::String (maxDiff (before, forward), 4));
                    check (maxDiff (before, varispeed) > 0.01,
                           "the arc's pitch mode changes the render",
                           juce::String (maxDiff (before, varispeed), 4));
                }
            }

            // --- v1.33.4 (H8): the export must cover the whole page ---
            // The export was given durationSec * sampleRate frames, but
            // the page's own time runs at a varying rate, so a warped
            // page needs more audio than it has score: the end of the
            // page was cut off (an arc at 25-29 s of a 30 s page
            // exported as silence). The page's time map is now shared by
            // the playback and the export, and the export streams the
            // render into the file.
            {
                // the map itself: a 30 s page at warp 0.9 needs 49.07 s
                // of audio to play through (the review's probe)
                {
                    upic::Page mapPg;
                    mapPg.durationSec = 30.0;
                    mapPg.warp = 0.9;
                    const double dur = mapPg.audioDurationSec();
                    check (std::abs (dur - 49.074) < 0.01
                           && std::abs (mapPg.scoreAtAudioTime (dur) - 30.0) < 1.0e-3
                           && std::abs (mapPg.audioTimeAtScore (30.0) - dur) < 1.0e-3,
                           "the page's time map: a 30 s page at warp 0.9 needs 49.07 s of audio",
                           juce::String (dur, 3) + " s");
                }

                // a 4 s page with an arc at the start and one at the end
                auto warpedPage = [] (double warp)
                {
                    upic::Page pg;
                    pg.durationSec = 4.0;
                    pg.warp = warp;
                    upic::Arc first;
                    first.id = 1; first.mode = 0; first.intensity = 1.0f;
                    first.points = { {0.0, 220.0}, {1.0, 220.0} };
                    upic::Arc last;
                    last.id = 2; last.mode = 0; last.intensity = 1.0f;
                    last.points = { {3.0, 330.0}, {3.9, 330.0} };
                    pg.arcs.push_back (first);
                    pg.arcs.push_back (last);
                    return pg;
                };

                // the export (the same path the WAV render uses) and the
                // file it wrote, read back
                auto exportPage = [] (const upic::Page& pg, const juce::File& file,
                                             juce::AudioBuffer<float>& out) -> bool
                {
                    const auto waves = upic::defaultWaveforms();
                    const auto envs = upic::defaultEnvelopes();
                    std::vector<upic::SampleSlot> noSlots;
                    const bool wrote = writePageToWavFile (file, pg, waves, envs, noSlots,
                                                           44100.0, 0.8f);
                    if (! wrote) return false;
                    std::unique_ptr<juce::FileInputStream> in (file.createInputStream());
                    juce::WavAudioFormat fmt;
                    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (in.release(), true));
                    if (reader == nullptr) return false;
                    out.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
                    reader->read (&out, 0, (int) reader->lengthInSamples, 0, true, false);
                    return true;
                };

                // the export of a warped page: the file is as long as the
                // page's own time needs, and the last arc is in it
                for (double warp : { 0.9, -0.9 })
                {
                    const auto pg = warpedPage (warp);
                    const juce::File f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                             .getChildFile ("upic_h8_warp.wav");
                    f.deleteFile();
                    juce::AudioBuffer<float> res;
                    const bool wrote = exportPage (pg, f, res);
                    const int expected = (int) std::ceil (pg.audioDurationSec() * 44100.0);
                    double headPeak = 0.0, tailPeak = 0.0;
                    if (res.getNumSamples() > 0)
                    {
                        const int headEnd = (int) (pg.audioTimeAtScore (1.0) * 44100.0);
                        const int tailStart = (int) (pg.audioTimeAtScore (3.0) * 44100.0);
                        for (int i = 0; i < juce::jmin (headEnd, res.getNumSamples()); ++i)
                            headPeak = std::max (headPeak, std::abs ((double) res.getSample (0, i)));
                        for (int i = tailStart; i < res.getNumSamples(); ++i)
                            tailPeak = std::max (tailPeak, std::abs ((double) res.getSample (0, i)));
                    }
                    check (wrote && res.getNumSamples() == expected
                           && headPeak > 0.01 && tailPeak > 0.01,
                           "the WAV export covers the whole warped page (warp "
                               + juce::String (warp, 2) + ")",
                           juce::String (res.getNumSamples()) + " / " + juce::String (expected)
                               + " frames, tail " + juce::String (tailPeak, 4));
                    f.deleteFile();
                }

                // the playback walks the same page to its end, whatever
                // the host's block size is
                for (int blockSize : { 128, 512 })
                {
                    auto mcWarpHolder = std::make_unique<MainComponent>(true);
                    auto& mcWarp = *mcWarpHolder;
                    mcWarp.setSize (1280, 800);
                    mcWarp.prepareToPlay (512, 44100.0);
                    const auto pg = warpedPage (0.9);
                    // the page through the piece format (the UI path)
                    mcWarp.loadFromVar (juce::JSON::parse (
                        "{\"pages\":[{\"duration\":4,\"warp\":0.9,\"nextArcId\":3,\"arcs\":["
                        "{\"id\":1,\"mode\":0,\"intensity\":1,\"points\":[{\"t\":0,\"f\":220},{\"t\":1,\"f\":220}]},"
                        "{\"id\":2,\"mode\":0,\"intensity\":1,\"points\":[{\"t\":3,\"f\":330},{\"t\":3.9,\"f\":330}]}]}]}"));
                    for (auto* c : mcWarp.getChildren())
                        if (auto* b = dynamic_cast<juce::Button*>(c))
                            if (b->getButtonText() == "Play") { b->onClick(); break; }

                    juce::AudioBuffer<float> buf (2, blockSize);
                    juce::AudioSourceChannelInfo info (&buf, 0, blockSize);
                    const int tailStart = (int) (pg.audioTimeAtScore (3.0) * 44100.0);
                    double tailPeak = 0.0;
                    const int blocks = (int) std::ceil (pg.audioDurationSec() * 44100.0 / blockSize) + 4;
                    for (int b = 0; b < blocks; ++b)
                    {
                        buf.clear();
                        mcWarp.getNextAudioBlock (info);
                        if (b * blockSize >= tailStart)
                            for (int i = 0; i < blockSize; ++i)
                                tailPeak = std::max (tailPeak, std::abs ((double) buf.getSample (0, i)));
                    }
                    check (tailPeak > 0.01,
                           "the playback walks a warped page to its end (" + juce::String (blockSize)
                               + " frame blocks)",
                           "tail " + juce::String (tailPeak, 4));
                }
            }

            // --- v1.33.4 (H13): one set of limits, no silent loss ---
            // The editor could create more arcs, pages, zones or points
            // than the piece format holds, and the loader dropped the
            // extra ones without a word: a page one arc over the cap came
            // back with the cap's count. The editor now refuses what does
            // not fit (and says so), and the loader says what it had to
            // drop.
            {
                std::function<juce::Component*(juce::Component&, const juce::String&)> findByNameH13 =
                    [&findByNameH13](juce::Component& c, const juce::String& n) -> juce::Component* {
                        if (c.getName() == n) return &c;
                        for (int i = 0; i < c.getNumChildComponents(); ++i)
                            if (auto* r = findByNameH13(*c.getChildComponent(i), n)) return r;
                        return nullptr;
                    };

                // a piece with n arcs on one page
                auto pieceWithArcs = [] (int n)
                {
                    juce::String j = "{\"pages\":[{\"duration\":30,\"nextArcId\":";
                    j += juce::String (n + 1);
                    j += ",\"arcs\":[";
                    for (int i = 0; i < n; ++i)
                    {
                        if (i > 0) j += ",";
                        j += "{\"id\":" + juce::String (i + 1)
                           + ",\"mode\":0,\"intensity\":0.8,"
                             "\"points\":[{\"t\":0,\"f\":220},{\"t\":1,\"f\":220}]}";
                    }
                    j += "]}]}";
                    return j;
                };

                // the loader keeps everything up to the cap
                for (int n : { upic::limits::kMaxArcsPerPage - 1, upic::limits::kMaxArcsPerPage })
                {
                    auto capHolder = std::make_unique<MainComponent>(true);
                    auto& mcCap = *capHolder;
                    mcCap.setSize (1280, 800);
                    mcCap.loadFromVar (juce::JSON::parse (pieceWithArcs (n)));
                    check (mcCap.arcCountForTest() == n
                           && ! mcCap.flashMessageForTest().contains ("bigger than"),
                           "a page with " + juce::String (n) + " arcs loads them all",
                           juce::String (mcCap.arcCountForTest()) + " arcs"
                               + (mcCap.flashMessageForTest().isEmpty() ? juce::String()
                                    : " (" + mcCap.flashMessageForTest() + ")"));
                }

                // one arc over the cap: the piece is truncated, but the
                // user is told - the music is not lost silently
                {
                    auto capHolder = std::make_unique<MainComponent>(true);
                    auto& mcCap = *capHolder;
                    mcCap.setSize (1280, 800);
                    mcCap.loadFromVar (juce::JSON::parse (pieceWithArcs (upic::limits::kMaxArcsPerPage + 1)));
                    const auto msg = mcCap.flashMessageForTest();
                    check (mcCap.arcCountForTest() == upic::limits::kMaxArcsPerPage
                           && msg.contains ("1 arc") && msg.contains ("bigger than"),
                           "a page with " + juce::String (upic::limits::kMaxArcsPerPage + 1)
                               + " arcs says which arc did not fit",
                           juce::String (mcCap.arcCountForTest()) + " arcs - " + msg);
                }

                // the editor refuses the arc that does not fit (and the
                // copy that does fit is still made)
                for (int n : { upic::limits::kMaxArcsPerPage - 1, upic::limits::kMaxArcsPerPage })
                {
                    auto capHolder = std::make_unique<MainComponent>(true);
                    auto& mcCap = *capHolder;
                    mcCap.setSize (1280, 800);
                    mcCap.loadFromVar (juce::JSON::parse (pieceWithArcs (n)));
                    auto* sc = dynamic_cast<ScoreEditor*>(findByNameH13 (mcCap, "score"));
                    if (sc == nullptr) { check (false, "the score canvas is there"); continue; }
                    sc->setSelectedArcs ({ 1 });
                    sc->duplicateSelected();
                    const bool fits = n < upic::limits::kMaxArcsPerPage;
                    const auto msg = mcCap.flashMessageForTest();
                    check (mcCap.arcCountForTest() == (fits ? n + 1 : n)
                           && (fits || msg.contains (juce::String (upic::limits::kMaxArcsPerPage) + " arcs")),
                           fits ? "the duplicate fits while the page is under the cap"
                                : "the editor refuses the arc that does not fit",
                           juce::String (mcCap.arcCountForTest()) + " arcs - " + msg);
                }

                // the generator stops at the cap too
                {
                    auto capHolder = std::make_unique<MainComponent>(true);
                    auto& mcCap = *capHolder;
                    mcCap.setSize (1280, 800);
                    mcCap.loadFromVar (juce::JSON::parse (pieceWithArcs (upic::limits::kMaxArcsPerPage)));
                    auto* gen = dynamic_cast<juce::ComboBox*>(findByNameH13 (mcCap, "genBox"));
                    if (gen != nullptr)
                    {
                        gen->setSelectedId (1, juce::dontSendNotification);
                        gen->onChange();
                    }
                    check (mcCap.arcCountForTest() == upic::limits::kMaxArcsPerPage,
                           "the generator stops at the arc cap",
                           juce::String (mcCap.arcCountForTest()) + " arcs");
                }

                // the pages are capped too
                {
                    auto pieceWithPages = [] (int n)
                    {
                        juce::String j = "{\"pages\":[";
                        for (int i = 0; i < n; ++i)
                        {
                            if (i > 0) j += ",";
                            j += "{\"duration\":30,\"nextArcId\":1,\"arcs\":[]}";
                        }
                        j += "]}";
                        return j;
                    };
                    auto capHolder = std::make_unique<MainComponent>(true);
                    auto& mcCap = *capHolder;
                    mcCap.setSize (1280, 800);
                    mcCap.loadFromVar (juce::JSON::parse (pieceWithPages (101)));
                    const auto msg = mcCap.flashMessageForTest();
                    check (mcCap.pageCountForTest() == upic::limits::kMaxPages
                           && msg.contains ("1 page") && msg.contains ("bigger than"),
                           "a piece with 101 pages says which page did not fit",
                           juce::String (mcCap.pageCountForTest()) + " pages - " + msg);
                }

                // and the editor refuses the page that does not fit
                {
                    auto capHolder = std::make_unique<MainComponent>(true);
                    auto& mcCap = *capHolder;
                    mcCap.setSize (1280, 800);
                    juce::String j = "{\"pages\":[";
                    for (int i = 0; i < upic::limits::kMaxPages; ++i)
                    {
                        if (i > 0) j += ",";
                        j += "{\"duration\":30,\"nextArcId\":1,\"arcs\":[]}";
                    }
                    j += "]}";
                    mcCap.loadFromVar (juce::JSON::parse (j));
                    auto* addBtn = dynamic_cast<juce::TextButton*>(findByNameH13 (mcCap, "pageAddBtn"));
                    if (addBtn != nullptr) addBtn->onClick();
                    const auto msg = mcCap.flashMessageForTest();
                    check (mcCap.pageCountForTest() == upic::limits::kMaxPages
                           && msg.contains ("100 pages"),
                           "the editor refuses the page that does not fit",
                           juce::String (mcCap.pageCountForTest()) + " pages - " + msg);
                }

                // the points of an arc are capped too
                {
                    auto pieceWithPoints = [] (int n)
                    {
                        juce::String j = "{\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":["
                                        "{\"id\":1,\"mode\":0,\"intensity\":0.8,\"points\":[";
                        for (int i = 0; i < n; ++i)
                        {
                            if (i > 0) j += ",";
                            j += "{\"t\":" + juce::String (i * 0.01, 4) + ",\"f\":220}";
                        }
                        j += "]}]}]}";
                        return j;
                    };
                    const auto varAt = juce::JSON::parse (pieceWithPoints (1000));
                    const auto* pagesAt = varAt.getProperty ("pages", juce::var()).getArray();
                    const auto pgAt = pagesAt != nullptr
                        ? upic::Page::fromVar (pagesAt->getReference (0)) : upic::Page();
                    const auto varOver = juce::JSON::parse (pieceWithPoints (1001));
                    const auto* pagesOver = varOver.getProperty ("pages", juce::var()).getArray();
                    const auto pgOver = pagesOver != nullptr
                        ? upic::Page::fromVar (pagesOver->getReference (0)) : upic::Page();
                    const int atPts = pgAt.arcs.empty() ? -1 : (int) pgAt.arcs[0].points.size();
                    const int overPts = pgOver.arcs.empty() ? -1 : (int) pgOver.arcs[0].points.size();
                    check (atPts == 1000 && overPts == 1000,
                           "an arc keeps 1000 points and drops the 1001st",
                           juce::String (atPts) + "/" + juce::String (overPts));
                    auto capHolder = std::make_unique<MainComponent>(true);
                    auto& mcCap = *capHolder;
                    mcCap.setSize (1280, 800);
                    mcCap.loadFromVar (varOver);
                    check (mcCap.flashMessageForTest().contains ("arc point"),
                           "the loader says which arc point did not fit",
                           mcCap.flashMessageForTest());
                }

                // the sieve zones are capped as well
                {
                    auto pieceWithZones = [] (int n)
                    {
                        juce::String j = "{\"pages\":[{\"duration\":30,\"nextArcId\":1,\"nextZoneId\":"
                                       + juce::String (n + 1) + ",\"arcs\":[],\"zones\":[";
                        for (int i = 0; i < n; ++i)
                        {
                            if (i > 0) j += ",";
                            j += "{\"id\":" + juce::String (i + 1)
                               + ",\"t0\":0,\"t1\":1,\"f0\":100,\"f1\":200}";
                        }
                        j += "]}]}";
                        return j;
                    };
                    auto capHolder = std::make_unique<MainComponent>(true);
                    auto& mcCap = *capHolder;
                    mcCap.setSize (1280, 800);
                    mcCap.loadFromVar (juce::JSON::parse (pieceWithZones (201)));
                    const auto msgBefore = mcCap.flashMessageForTest();
                    auto* sc = dynamic_cast<ScoreEditor*>(findByNameH13 (mcCap, "score"));
                    const int zoneId = sc != nullptr ? sc->addZone (0.0, 1.0, 100.0, 200.0) : -1;
                    const auto msgAfter = mcCap.flashMessageForTest();
                    check (mcCap.zoneCountForTest() == upic::limits::kMaxSieveZones
                           && zoneId < 0 && msgBefore.contains ("sieve zone")
                           && msgAfter.contains ("200 sieve zones"),
                           "the editor refuses the sieve zone that does not fit",
                           juce::String (mcCap.zoneCountForTest()) + " zones - " + msgAfter);
                }
            }

            // --- v1.33.4 (M19): the material keeps its own rate ---
            // The importer clamped the file's rate to 96 kHz and kept the
            // frames, so a 192 kHz file was played at half speed (and the
            // seamless-loop crossfade used the wrong rate too). The piece
            // loader did the same to a captured 192 kHz take. Now the rate
            // is kept, a file above the app's ceiling is refused with a
            // message instead of being retuned, and the duration cap is
            // counted at the material's own rate.
            {
                const juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory);

                // a 192 kHz file keeps its rate
                {
                    const juce::File f = dir.getChildFile ("upic_m19_192k.wav");
                    f.deleteFile();
                    juce::AudioBuffer<float> buf (1, 192000);
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                        buf.setSample (0, i, 0.5f * std::sin ((float) i * 0.01f));
                    writeWavFile (f, buf, 192000.0);
                    upic::LoadedSample loaded;
                    juce::String error;
                    const bool ok = upic::loadSampleFile (f, loaded, error);
                    // the seamless loop eats the 50 ms crossfade
                    const double secs = loaded.data.getNumSamples() / loaded.sampleRate;
                    check (ok && loaded.sampleRate == 192000.0 && std::abs (secs - 0.95) < 0.02,
                           "a 192 kHz file keeps its rate (it does not play at half speed)",
                           juce::String (loaded.sampleRate, 0) + " Hz, " + juce::String (secs, 3) + " s"
                               + (error.isEmpty() ? juce::String() : " - " + error));
                    f.deleteFile();
                }

                // a file above the app's ceiling is refused, not retuned
                {
                    const juce::File f = dir.getChildFile ("upic_m19_256k.wav");
                    f.deleteFile();
                    juce::AudioBuffer<float> buf (1, 256000);
                    writeWavFile (f, buf, 256000.0);
                    upic::LoadedSample loaded;
                    juce::String error;
                    const bool ok = upic::loadSampleFile (f, loaded, error);
                    check (! ok && error.contains ("kHz"),
                           "a file above the app's rate ceiling is refused, not retuned",
                           error);
                    f.deleteFile();
                }

                // the duration cap counts the seconds at the file's own rate
                // (the old cap was 60 s at 96 kHz whatever the file was, so
                // a 44.1 kHz file could hold 130 s)
                {
                    const juce::File f = dir.getChildFile ("upic_m19_long.wav");
                    f.deleteFile();
                    const int n = (int) (65.0 * 44100.0);
                    juce::AudioBuffer<float> buf (1, n);
                    for (int i = 0; i < n; ++i)
                        buf.setSample (0, i, 0.25f * std::sin ((float) i * 0.01f));
                    writeWavFile (f, buf, 44100.0);
                    upic::LoadedSample loaded;
                    juce::String error;
                    const bool ok = upic::loadSampleFile (f, loaded, error);
                    const double secs = loaded.data.getNumSamples() / loaded.sampleRate;
                    check (ok && secs > 59.0 && secs <= 60.1,
                           "the duration cap counts the seconds at the file's own rate",
                           juce::String (secs, 2) + " s"
                               + (loaded.warning.isEmpty() ? juce::String() : " - " + loaded.warning));
                    f.deleteFile();
                }

                // a piece keeps the rate of the material it carries
                {
                    juce::AudioBuffer<float> buf (1, 1000);
                    for (int i = 0; i < 1000; ++i) buf.setSample (0, i, 0.5f);
                    upic::SampleSlot slot;
                    slot.id = 1;
                    slot.name = "take";
                    slot.sampleRate = 192000.0;
                    slot.data = std::make_shared<const juce::AudioBuffer<float>>(buf);
                    const auto loadedSlot = upic::SampleSlot::fromVar (slot.toVar(), 1);
                    check (loadedSlot.sampleRate == 192000.0 && loadedSlot.loaded(),
                           "a piece keeps the rate of the material it carries",
                           juce::String (loadedSlot.sampleRate, 0) + " Hz, "
                               + juce::String (loadedSlot.data != nullptr
                                                   ? loadedSlot.data->getNumSamples() : 0) + " frames");
                }

                // a file the decoder cannot read is refused with a
                // message, not turned into silence
                {
                    const juce::File f = dir.getChildFile ("upic_m19_broken.wav");
                    f.deleteFile();
                    f.replaceWithText ("this is not a wav file at all");
                    upic::LoadedSample loaded;
                    juce::String error;
                    const bool ok = upic::loadSampleFile (f, loaded, error);
                    check (! ok && ! error.isEmpty(),
                           "a file the decoder cannot read is refused with a message",
                           error);
                    f.deleteFile();
                }
            }

            // --- v1.33.4 (M25): a failed load keeps the composition ---
            // loadFromVar used to clear the model before it parsed, so a
            // file that was not a piece left an empty default page
            // behind and the unsaved work was gone. Now the piece is
            // parsed into a temporary model and swapped in only when it
            // really is a piece.
            {
                auto mcM25Holder = std::make_unique<MainComponent>(true);
                auto& mcM25 = *mcM25Holder;
                mcM25.setSize (1280, 800);
                const auto piece = juce::JSON::parse (
                    "{\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":["
                    "{\"id\":1,\"mode\":0,\"intensity\":0.8,"
                    "\"points\":[{\"t\":0,\"f\":220},{\"t\":1,\"f\":220}]}]}]}");
                check (mcM25.loadFromVar (piece) && mcM25.arcCountForTest() == 1,
                       "a piece loads");
                const int arcsBefore = mcM25.arcCountForTest();
                // a file that is not a piece at all
                const bool loaded = mcM25.loadFromVar (juce::JSON::parse ("{\"foo\":1}"));
                check (! loaded && mcM25.arcCountForTest() == arcsBefore
                       && mcM25.pageCountForTest() == 1,
                       "a file that is not a piece does not wipe the composition",
                       juce::String (mcM25.arcCountForTest()) + " arcs, "
                           + juce::String (mcM25.pageCountForTest()) + " pages");
                // and an empty state
                const bool loadedVoid = mcM25.loadFromVar (juce::var());
                check (! loadedVoid && mcM25.arcCountForTest() == arcsBefore
                       && mcM25.pageCountForTest() == 1,
                       "an empty state does not wipe the composition",
                       juce::String (mcM25.arcCountForTest()) + " arcs, "
                           + juce::String (mcM25.pageCountForTest()) + " pages");
                // a piece with no pages is not a piece either
                const bool loadedEmpty = mcM25.loadFromVar (juce::JSON::parse ("{\"pages\":[]}"));
                check (! loadedEmpty && mcM25.arcCountForTest() == arcsBefore,
                       "a piece with no pages does not wipe the composition",
                       juce::String (mcM25.arcCountForTest()) + " arcs");
            }

            // --- v1.33.4 (M26): the sample PCM conversion is stable ---
            // The encoder scaled by 32767 and truncated, the decoder
            // divided by 32768, so every save/load cycle lost 1/32768 of
            // the level: 0.5 became 0.496948 after a hundred cycles. One
            // symmetric, rounded conversion makes the round trip a fixed
            // point.
            {
                // the value after n save/load cycles
                auto afterCycles = [] (float x, int cycles, float& first)
                {
                    upic::SampleSlot slot;
                    slot.id = 1;
                    slot.sampleRate = 44100.0;
                    juce::AudioBuffer<float> buf (1, 4);
                    for (int i = 0; i < 4; ++i) buf.setSample (0, i, x);
                    slot.data = std::make_shared<const juce::AudioBuffer<float>>(buf);
                    float v = x;
                    first = x;
                    for (int cycle = 0; cycle < cycles; ++cycle)
                    {
                        const auto loaded = upic::SampleSlot::fromVar (slot.toVar(), 1);
                        if (loaded.data == nullptr) return false;
                        v = loaded.data->getSample (0, 0);
                        slot.data = loaded.data;
                        if (cycle == 0) first = v;
                    }
                    return true;
                };

                float firstHalf = 0.0f, half = 0.0f, full = 0.0f, minus = 0.0f;
                const bool okHalf = afterCycles (0.5f, 100, firstHalf);
                afterCycles (0.5f, 100, half);
                afterCycles (1.0f, 1, full);
                afterCycles (-1.0f, 1, minus);
                check (okHalf && std::abs (half - 0.5f) < 1e-4f,
                       "the sample PCM survives 100 save/load cycles",
                       juce::String (half, 6));
                check (half == firstHalf,
                       "the sample PCM stops changing after the first cycle",
                       juce::String (firstHalf, 6) + " -> " + juce::String (half, 6));
                check (full == 1.0f && minus == -1.0f,
                       "the sample PCM keeps full scale",
                       juce::String (full, 6) + "/" + juce::String (minus, 6));
            }

            mc.setSize(1280, 800);

            // --- v1.2 controls exist with sane bounds ---
            auto findNamed = [&](const juce::String& s) -> juce::Component* {
                for (int i = 0; i < mc.getNumChildComponents(); ++i)
                    if (mc.getChildComponent(i)->getName() == s)
                        return mc.getChildComponent(i);
                return nullptr;
            };
            const char* v12names[] = { "score", "genBox", "stickSlider", "gridBox",
                                       "sievePresetBox", "kbdHintLab" };
            for (auto* n : v12names)
            {
                auto* c = findNamed(n);
                check(c != nullptr && ! c->getBounds().isEmpty(),
                      juce::String("v1.2 control present: ") + n,
                      c != nullptr ? c->getBounds().toString() : juce::String("not found"));
            }
            if (auto* sc = dynamic_cast<ScoreEditor*>(findNamed("score")))
            {
                scoreBounds = sc->getBounds();
                sc->setShowGrid(false); // grid off so pixel probes hit zone/strip paint
            }
            stripBounds = mc.permanentStripBoundsForTest();

            // in-process identity check: which binary made this object?
            auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
            log << "EXE: " << exe.getFullPathName()
                << "  size=" << exe.getSize() << "\n";
            juce::String kids;
            for (int i = 0; i < mc.getNumChildComponents(); ++i)
                kids << mc.getChildComponent(i)->getBounds().toString() << "  ";
            log << "CHILDREN(" << mc.getNumChildComponents() << "): " << kids << "\n";
            mc.paintEntireComponent(g, true);

            // --- small-window snapshots (side-by-side AND stacked editor
            //     paths) for eyeball overlap checks ---
            struct Sz { int w, h; };
            const Sz smalls[] = { {640, 400}, {360, 640} };
            for (auto z : smalls)
            {
                mc.setSize(z.w, z.h);
                juce::Image sml(juce::Image::RGB, z.w, z.h, true);
                {
                    juce::Graphics sg(sml);
                    mc.paintEntireComponent(sg, true);
                }
                auto smlPng = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getChildFile("upic_snapshot_" + juce::String(z.w)
                                                + "x" + juce::String(z.h) + ".png");
                smlPng.deleteFile(); // createOutputStream() appends on Windows
                bool wrote = false;
                if (auto os = smlPng.createOutputStream())
                {
                    juce::PNGImageFormat fmt;
                    wrote = fmt.writeImageToStream(sml, *os);
                    os->flush();
                }
                check(wrote && smlPng.getSize() > 4000,
                      juce::String(z.w) + "x" + juce::String(z.h) + " snapshot render",
                      juce::String(smlPng.getSize()) + " bytes -> " + smlPng.getFullPathName());
            }
            mc.setSize(1280, 800);
        } // Graphics released before encoding
        // JUCE's createOutputStream() APPENDS on Windows (OPEN_ALWAYS +
        // seek FILE_END) — delete first or old PNG data stays in front
        png.deleteFile();
        if (auto os = png.createOutputStream())
        {
            juce::PNGImageFormat fmt;
            written = fmt.writeImageToStream(img, *os);
            os->flush();
        }
        if (written)
            check(png.getSize() > 15000, "ui snapshot non-blank",
                  juce::String(png.getSize()) + " bytes -> " + png.getFullPathName());
        else
            check(false, "ui snapshot write");

        // --- pixel probes on the offscreen image ---
        if (scoreBounds.isEmpty())
        {
            check(false, "score component bounds for pixel probes");
        }
        else
        {
            // plot rect: ScoreEditor margins (top 4, bottom/left/right 6)
            const float px = (float) scoreBounds.getX() + 6.0f;
            const float py = (float) scoreBounds.getY() + 4.0f;
            const float pw = (float) scoreBounds.getWidth() - 12.0f;
            const float phh = (float) scoreBounds.getHeight() - 10.0f;
            // zone (t1..5 of 12s, 200..900Hz) -> pick t=3.07s, f=350Hz.
            // 3.07 (not 3.00) avoids the zone's 2/s time-grid line, and
            // 350 Hz sits between the zone's sieve guide lines.
            const float norm350 = (float) (std::log2(350.0 / 20.0) / std::log2(12000.0 / 20.0));
            const auto zonePx = img.getPixelAt((int) (px + pw * (3.07 / 12.0)),
                                               (int) (py + phh * (1.0 - norm350)));
            check(zonePx.getRed() >= 34 && zonePx.getRed() <= 46
                  && zonePx.getGreen() >= 52 && zonePx.getGreen() <= 64
                  && zonePx.getBlue() >= 66 && zonePx.getBlue() <= 84,
                  "v1.2 sieve zone painted (translucent)", zonePx.toString());
            // centre scrub line (inside the zone's time range)
            const auto stripPx = img.getPixelAt((int) (px + pw * 0.30f),
                                                 (int) (py + phh * 0.5f - 7.0f));
            check(stripPx.getRed() >= 42 && stripPx.getRed() <= 56
                  && stripPx.getBlue() >= 76 && stripPx.getBlue() <= 92,
                  "centre scrub line painted", stripPx.toString());
            // v1.33.3: the whole window uses one dark surface. Pure
            // black sections made the cards look like they left holes
            // and shadows wherever the background showed through.
            // v1.34: the toolbar is cards now, so probe the gap between
            // the permanent strip and the first card row.
            const auto toolbarBgPx = img.getPixelAt (stripBounds.getX() + 2,
                                                     stripBounds.getBottom() + 2);
            const auto windowBgPx  = img.getPixelAt (4, 500);
            const auto scoreBgPx   = img.getPixelAt (scoreBounds.getX() + 2,
                                                     scoreBounds.getY() + 2);
            check(toolbarBgPx == upic::uiBackground()
                  && windowBgPx == upic::uiBackground()
                  && scoreBgPx == upic::uiBackground(),
                  "v1.33.3 the whole window uses one background (no black sections)",
                  toolbarBgPx.toString() + " / " + windowBgPx.toString()
                    + " / " + scoreBgPx.toString());
        }
    }

}
