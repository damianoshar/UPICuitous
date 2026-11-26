#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// R1.2: the bottom half of the layout, before its geometry moves.
//
// The card engine (R1.1) took the toolbar. The next extraction takes the
// panels below it: the editors and their library rows, the view bar, the
// voice controls, the tab strip, the arc list and the workspace. This suite
// writes down what those panels do today, in the same structural style as
// the R0.1 layout characterization: relations (inside the window, under the
// canvas, in a row or a column, no overlap), not golden pixels.
//
// Two of the viewports are portrait on purpose. The voice controls are a
// slim strip in landscape and a side column in portrait, and the two editors
// sit side by side when there is room and stack when there is not - none of
// the five R0.1 viewports reaches the portrait branches.

namespace
{
    struct BottomViewport { int w, h; const char* label; };
    const BottomViewport kBottomViewports[] = {
        { 1600, 1000, "desktop" },
        { 1280,  800, "default" },
        { 1024,  700, "compact" },
        {  800,  600, "tablet" },
        {  460,  360, "minimum" },
        {  700,  900, "portrait" },
        {  460, 1400, "tall portrait" }
    };

    juce::Rectangle<int> windowOf (const MainComponent& mc)
    {
        return { 0, 0, mc.getWidth(), mc.getHeight() };
    }

    juce::Component* findComponentByName (juce::Component& c, const juce::String& name)
    {
        if (c.getName() == name) return &c;
        for (int i = 0; i < c.getNumChildComponents(); ++i)
            if (auto* r = findComponentByName (*c.getChildComponent (i), name))
                return r;
        return nullptr;
    }
}

void upic::runBottomLayoutCharacterizationChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // Two pages, so the workspace checks have something to open. The arcs
    // are there so the arc list's rows have a document to describe.
    const juce::String piece =
        "{\"master\":1,\"pages\":["
        "{\"duration\":30,\"nextArcId\":3,\"arcs\":["
        "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"intensity\":0.8,"
        "\"points\":[{\"t\":0,\"f\":220},{\"t\":15,\"f\":440}]},"
        "{\"id\":2,\"mode\":0,\"waveformId\":2,\"envelopeId\":2,\"intensity\":0.5,"
        "\"points\":[{\"t\":5,\"f\":110},{\"t\":25,\"f\":330}]}]},"
        "{\"duration\":20,\"nextArcId\":1,\"arcs\":[]}]}";

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);
    check (mc.loadFromVar (juce::JSON::parse (piece)),
           "R1.2 the bottom layout's piece loads",
           juce::String (mc.pageCountForTest()) + " page(s)");

    // --- every viewport: the panels tile the window -------------------------
    for (const auto& vp : kBottomViewports)
    {
        mc.setSize (vp.w, vp.h);
        const auto at = juce::String (vp.label);
        const auto window = windowOf (mc);
        const auto score = mc.scoreBoundsForTest();
        const auto viewBar = mc.viewBarBoundsForTest();
        const auto wave = mc.waveEditorBoundsForTest();
        const auto env = mc.envelopeEditorBoundsForTest();
        const auto voice = mc.voiceLabelBoundsForTest();
        const auto waveLib = mc.waveLibraryBoundsForTest();
        const auto envLib = mc.envelopeLibraryBoundsForTest();
        const bool bothEditors = mc.waveEditorVisibleForTest()
                              && mc.envelopeEditorVisibleForTest();

        check (! score.isEmpty() && window.contains (score),
               "R1.2 the canvas has room at " + at + " size",
               score.toString());

        check (! viewBar.isEmpty() && window.contains (viewBar)
               && viewBar.getY() >= score.getBottom(),
               "R1.2 the view bar sits under the canvas at " + at + " size",
               score.toString() + " / " + viewBar.toString());

        check (bothEditors && ! wave.isEmpty() && window.contains (wave)
               && wave.getY() >= viewBar.getBottom()
               && ! wave.intersects (score) && ! wave.intersects (env),
               "R1.2 the wave editor sits under the view bar at " + at + " size",
               wave.toString() + " / " + env.toString());

        check (bothEditors && ! env.isEmpty() && window.contains (env)
               && env.getY() >= viewBar.getBottom()
               && ! env.intersects (score),
               "R1.2 the envelope editor sits under the view bar at " + at + " size",
               env.toString());

        check (waveLib.getWidth() == wave.getWidth()
               && waveLib.getY() < wave.getY() && waveLib.getBottom() <= wave.getY()
               && envLib.getWidth() == env.getWidth()
               && envLib.getY() < env.getY() && envLib.getBottom() <= env.getY(),
               "R1.2 each library row sits above its editor at " + at + " size",
               waveLib.toString() + " / " + wave.toString() + " / "
                 + envLib.toString() + " / " + env.toString());

        // the tab strip's controls sit directly above the canvas; a row that
        // is full hides the arc toggle rather than crushing it
        if (auto* arcToggle = findComponentByName (mc, "arcListBtn"))
        {
            const bool shown = arcToggle->isVisible();
            check (! arcToggle->getBounds().isEmpty()
                   && (! shown || arcToggle->getBounds().getBottom() <= score.getY()),
                   "R1.2 the tab strip's arc toggle sits above the canvas at " + at + " size",
                   (shown ? juce::String ("shown ") : juce::String ("hidden at this width "))
                     + arcToggle->getBounds().toString() + " / " + score.toString());
        }
        else
        {
            check (false, "R1.2 the tab strip's arc toggle sits above the canvas at " + at + " size",
                   "no arcListBtn");
        }

        // the voice controls: a row in landscape, a column in portrait
        juce::Rectangle<int> vb[5];
        bool allVoice = ! voice.isEmpty() && window.contains (voice);
        for (int i = 0; i < 5; ++i)
        {
            vb[i] = mc.voiceButtonBoundsForTest (i);
            allVoice = allVoice && ! vb[i].isEmpty() && window.contains (vb[i]);
        }

        if (vp.w > vp.h)
        {
            check (allVoice
                   && voice.getRight() <= vb[0].getX()
                   && vb[0].getY() == vb[1].getY() && vb[1].getY() == vb[2].getY()
                   && vb[2].getY() == vb[3].getY() && vb[3].getY() == vb[4].getY()
                   && vb[0].getX() < vb[1].getX() && vb[1].getX() < vb[2].getX()
                   && vb[2].getX() < vb[3].getX() && vb[3].getX() < vb[4].getX(),
                   "R1.2 the voice controls form one strip at " + at + " size",
                   voice.toString() + " / " + vb[0].toString() + " / "
                     + vb[4].toString());
        }
        else
        {
            check (allVoice
                   && wave.getRight() <= voice.getX()
                   && vb[0].getY() == vb[1].getY() && vb[1].getY() == vb[2].getY()
                   && vb[0].getX() < vb[1].getX() && vb[1].getX() < vb[2].getX()
                   && vb[0].getY() >= voice.getBottom()
                   && vb[3].getY() == vb[4].getY()
                   && vb[3].getX() < vb[4].getX()
                   && vb[3].getY() >= vb[0].getBottom(),
                   "R1.2 the voice controls form a column at " + at + " size",
                   voice.toString() + " / " + vb[0].toString() + " / "
                     + vb[3].toString());
        }

        // the editors share or stack by the room the voice column leaves
        if (bothEditors && vp.w == 700 && vp.h == 900)
        {
            check (wave.getY() == env.getY() && wave.getRight() <= env.getX()
                   && wave.getHeight() == env.getHeight(),
                   "R1.2 the editors sit side by side in a wide portrait window",
                   wave.toString() + " / " + env.toString());
        }
        else if (bothEditors && vp.w == 460 && vp.h == 1400)
        {
            check (wave.getBottom() <= env.getY()
                   && wave.getX() == env.getX()
                   && wave.getWidth() == env.getWidth(),
                   "R1.2 the editors stack in a narrow portrait window",
                   "scale " + juce::String (mc.layoutScaleForTest(), 3) + "  "
                     + wave.toString() + " / " + env.toString());
        }
    }

    // --- the editor toggles --------------------------------------------------
    {
        mc.setSize (1280, 800);
        const auto bothScore = mc.scoreBoundsForTest();
        const auto bothWave = mc.waveEditorBoundsForTest();

        mc.setEditorsVisibleForTest (false, false);
        const auto noScore = mc.scoreBoundsForTest();
        check (noScore.getHeight() > bothScore.getHeight(),
               "R1.2 hiding both editors gives the canvas their height",
               bothScore.toString() + " -> " + noScore.toString());
        check (! mc.waveEditorVisibleForTest() && ! mc.envelopeEditorVisibleForTest(),
               "R1.2 both editors can be hidden on their own");
        check (mc.voiceButtonBoundsForTest (0).getY() >= noScore.getBottom()
               && mc.viewBarBoundsForTest().getY() >= noScore.getBottom(),
               "R1.2 with no editors the voice strip sits under the canvas",
               mc.voiceButtonBoundsForTest (0).toString() + " / "
                 + mc.viewBarBoundsForTest().toString());

        mc.setEditorsVisibleForTest (true, false);
        const auto oneWave = mc.waveEditorBoundsForTest();
        check (mc.waveEditorVisibleForTest() && ! mc.envelopeEditorVisibleForTest(),
               "R1.2 the envelope editor can be hidden on its own");
        check (oneWave.getWidth() > bothWave.getWidth()
               && oneWave.getX() <= bothWave.getX()
               && oneWave.getRight() >= bothWave.getRight(),
               "R1.2 one editor takes the width the two shared",
               bothWave.toString() + " -> " + oneWave.toString());

        mc.setEditorsVisibleForTest (true, true);
    }

    // --- the focus views -----------------------------------------------------
    {
        mc.setSize (1280, 800);
        const auto normalScore = mc.scoreBoundsForTest();
        const auto normalWave = mc.waveEditorBoundsForTest();

        mc.setFocusedPanel (MainComponent::FocusPanel::score);
        const auto focusScore = mc.scoreBoundsForTest();
        check (mc.scoreVisibleForTest() && ! focusScore.isEmpty()
               && windowOf (mc).contains (focusScore)
               && focusScore.getHeight() > normalScore.getHeight(),
               "R1.2 the score focus gives the canvas the whole area",
               normalScore.toString() + " -> " + focusScore.toString());
        check (mc.viewBarVisibleForTest()
               && mc.viewBarBoundsForTest().getY() >= focusScore.getBottom(),
               "R1.2 the score focus keeps the view bar under the canvas",
               mc.viewBarBoundsForTest().toString());
        check (! mc.waveEditorVisibleForTest() && ! mc.envelopeEditorVisibleForTest(),
               "R1.2 the score focus hides the editors");

        mc.setFocusedPanel (MainComponent::FocusPanel::wave);
        check (mc.waveEditorVisibleForTest() && ! mc.scoreVisibleForTest()
               && ! mc.viewBarVisibleForTest(),
               "R1.2 the wave focus shows only the wave editor");
        check (mc.waveEditorBoundsForTest().getHeight() > normalWave.getHeight(),
               "R1.2 the wave focus gives the editor the whole area",
               normalWave.toString() + " -> "
                 + mc.waveEditorBoundsForTest().toString());

        mc.setFocusedPanel (MainComponent::FocusPanel::none);
    }

    // --- the arc list --------------------------------------------------------
    {
        mc.setSize (1280, 800);
        mc.setArcListVisibleForTest (true);
        const auto arc = mc.arcListPanelBoundsForTest();
        const auto score = mc.scoreBoundsForTest();
        check (mc.arcListPanelVisibleForTest() && ! arc.isEmpty()
               && windowOf (mc).contains (arc),
               "R1.2 the arc list takes the right side of the canvas",
               arc.toString());
        check (arc.getWidth() <= 300 && arc.getX() >= score.getRight()
               && ! arc.intersects (score),
               "R1.2 the arc list does not overlap the canvas",
               arc.toString() + " / " + score.toString());
        check (score.getWidth() >= 240,
               "R1.2 the arc list leaves the canvas its minimum",
               score.toString());
        mc.setArcListVisibleForTest (false);
    }

    // --- the workspace -------------------------------------------------------
    {
        mc.setSize (1280, 800);
        const bool opened = mc.setWorkspaceOpen (true);
        check (opened && mc.workspaceOpenForTest(), "R1.2 the workspace opens");

        const auto ws = mc.workspaceBoundsForTest();
        const auto splitter = mc.workspaceSplitterBoundsForTest();
        check (mc.workspaceSplitterVisibleForTest() && ! ws.isEmpty()
               && splitter.getWidth() == 6
               && splitter.getX() == ws.getX()
                    + juce::roundToInt ((double) ws.getWidth()
                                        * mc.workspaceSplitForTest())
                    - 3,
               "R1.2 the workspace splitter stands at the split",
               ws.toString() + " / " + splitter.toString());

        auto* c0 = mc.workspaceCanvasForTest (0);
        auto* c1 = mc.workspaceCanvasForTest (1);
        bool canvasesOk = c0 != nullptr && c1 != nullptr
                       && c0->isVisible() && c1->isVisible();
        if (canvasesOk)
        {
            const auto a = c0->getBounds();
            const auto b = c1->getBounds();
            canvasesOk = ! a.isEmpty() && ! b.isEmpty()
                       && a.getX() == ws.getX() && a.getY() == ws.getY()
                       && a.getHeight() == ws.getHeight()
                       && a.getRight() == b.getX()
                       && b.getRight() == ws.getRight()
                       && b.getHeight() == ws.getHeight();
        }
        check (canvasesOk, "R1.2 the workspace canvases tile its area",
               c0 != nullptr && c1 != nullptr
                 ? c0->getBounds().toString() + " / " + c1->getBounds().toString()
                 : juce::String ("missing canvas"));

        mc.setWorkspaceSplitForTest (0.3);
        mc.resized();
        const auto splitter2 = mc.workspaceSplitterBoundsForTest();
        check (splitter2.getX() == ws.getX()
               + juce::roundToInt ((double) ws.getWidth() * 0.3) - 3,
               "R1.2 the workspace splitter follows the split",
               splitter2.toString());

        mc.setWorkspaceOpen (false);
        check (! mc.workspaceOpenForTest() && ! mc.workspaceSplitterVisibleForTest()
               && mc.scoreBoundsForTest().getHeight() > 0,
               "R1.2 closing the workspace restores the canvas");
    }

    // --- the layout is the same twice in a row ------------------------------
    {
        mc.setSize (1280, 800);
        const auto score = mc.scoreBoundsForTest();
        const auto viewBar = mc.viewBarBoundsForTest();
        const auto wave = mc.waveEditorBoundsForTest();
        const auto voice = mc.voiceLabelBoundsForTest();
        mc.resized();
        check (score == mc.scoreBoundsForTest()
               && viewBar == mc.viewBarBoundsForTest()
               && wave == mc.waveEditorBoundsForTest()
               && voice == mc.voiceLabelBoundsForTest(),
               "R1.2 the bottom layout is the same when it is calculated again");
    }
}
