#include "MainComponent.h"
#if GUERRERO_INTEGRATION
#include "GuerreroUpicAdapter.h"
#include "GuerreroDialog.h"
#include "guerrero/core/Curves.h"
#include "guerrero/upic/GuerreroUpicPlan.h"
#endif

#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The model: the controls are kept in step with the page, the generators fill
// it, and the arc list mirrors it.

//==============================================================================
upic::Page& MainComponent::curPage()
{
    if (pages.empty()) pages.push_back(upic::Page());
    currentPage = juce::jlimit(0, (int)pages.size() - 1, currentPage);
    return pages[(size_t)currentPage];
}
const upic::Page& MainComponent::curPage() const
{
    static upic::Page emptyFallback;
    if (pages.empty()) return emptyFallback;
    int idx = juce::jlimit(0, (int)pages.size() - 1, currentPage);
    return pages[(size_t)idx];
}

//==============================================================================
// F2/F3 (2026-11-17): the library helpers. The banks are small and the
// operations are message-thread only, so a linear scan is the clearest way
// to find an id or a name (and it cannot drift from the vector order).
namespace
{
    template <typename Bank>
    int nextBankId (const Bank& bank)
    {
        int id = 0;
        for (const auto& x : bank)
            id = juce::jmax (id, x.id + 1);
        return id;
    }

    template <typename Bank>
    bool bankHasName (const Bank& bank, const juce::String& name, const void* except)
    {
        for (const auto& x : bank)
            if ((const void*) &x != except && x.name.equalsIgnoreCase (name))
                return true;
        return false;
    }

    template <typename Bank>
    juce::String uniqueBankName (const Bank& bank, const juce::String& base)
    {
        if (! bankHasName (bank, base, nullptr)) return base;
        for (int n = 2; ; ++n)
        {
            const auto candidate = base + " " + juce::String (n);
            if (! bankHasName (bank, candidate, nullptr)) return candidate;
        }
    }
}

//==============================================================================
void MainComponent::refreshLibraryBoxes()
{
    std::vector<LibraryManager::Item> waveItems;
    waveItems.reserve (waveforms.size());
    for (const auto& w : waveforms)
        waveItems.push_back ({ w.id, w.name, w.colour, waveUseCount (w.id) });
    waveLib.setItems (waveItems, selWave);

    std::vector<LibraryManager::Item> envItems;
    envItems.reserve (envelopes.size());
    for (const auto& e : envelopes)
        envItems.push_back ({ e.id, e.name, e.colour, envelopeUseCount (e.id) });
    envLib.setItems (envItems, selEnv);
}

void MainComponent::refreshBoxes()
{
    arcListDirty = true; // v1.21: the panel's rows follow the model
    pageBox.setName("pageBox");
    // v1.33.4 (H13): the page buttons are named too, so the selftest can
    // press them (the page count is capped by the piece format).
    pageAddBtn.setName("pageAddBtn");
    pageDupBtn.setName("pageDupBtn");
    pageDelBtn.setName("pageDelBtn");
    pageBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < pages.size(); ++i)
        pageBox.addItem("Page " + juce::String((int)i + 1) + "  (" + juce::String(pages[i].arcs.size()) + " arcs)", (int)i + 1);
    pageBox.setSelectedId(currentPage + 1, juce::dontSendNotification);
    durSlider.setValue(curPage().durationSec, juce::dontSendNotification);

    // v1.33: the page's four frequency spaces
    for (int i = 0; i < 4; ++i)
        spaceDefBox[i].setSelectedId (juce::jlimit (0,
            (int) upic::freqSpacePresets().size() - 1,
            curPage().spaces[(size_t) i]) + 1, juce::dontSendNotification);
    refreshSpaceBox();

    // v1.33: the ambitus - the combo shows the preset that matches, or
    // "Custom" when the page carries an interval of its own
    {
        const auto& presets = upic::ambitusPresets();
        int match = 0;
        for (int i = 0; i < (int) presets.size(); ++i)
            if (std::abs (curPage().pitchLo - presets[(size_t) i].lo) < 1e-6
                && std::abs (curPage().pitchHi - presets[(size_t) i].hi) < 1e-6)
            { match = i + 1; break; }
        ambitusBox.setSelectedId (match > 0 ? match : (int) presets.size() + 1,
                                  juce::dontSendNotification);
    }
    // v1.33: the acceleration
    warpSlider.setValue (curPage().warp, juce::dontSendNotification);

    waveBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < waveforms.size(); ++i)
        waveBox.addItem(juce::String(waveforms[i].id) + ": " + waveforms[i].name, (int)i + 1);
    envBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < envelopes.size(); ++i)
        envBox.addItem(juce::String(envelopes[i].id) + ": " + envelopes[i].name, (int)i + 1);

    // F2/F3 (2026-11-17): the managers own the editor selectors. The item
    // text carries the use count (arcs on every page, both envelope slots
    // for the envelope bank); the swatch shows the selected asset's colour.
    refreshLibraryBoxes();

    pmIndexEnvBox.clear (juce::dontSendNotification);
    // ComboBox item ids must be > 0: id 1 is the plain amp envelope, the
    // envelope bank follows from id 2 (so the box never shows blank)
    pmIndexEnvBox.addItem ("(amp env)", 1);
    for (size_t i = 0; i < envelopes.size(); ++i)
        pmIndexEnvBox.addItem (juce::String (envelopes[i].id) + ": " + envelopes[i].name, (int) i + 2);
    pmIndexEnvBox.setSelectedId (1, juce::dontSendNotification);

    sampleBox.clear(juce::dontSendNotification);
    for (size_t i = 0; i < sampleSlots.size(); ++i)
        sampleBox.addItem(juce::String(sampleSlots[i].id) + ": "
                          + (sampleSlots[i].loaded() ? sampleSlots[i].name : juce::String("(empty)"))
                          + (sampleSlots[i].reversed ? " (rev)" : ""),
                          (int)i + 1);
    selSlot = juce::jlimit(0, (int) sampleSlots.size() - 1, selSlot);
    sampleBox.setSelectedId(selSlot + 1, juce::dontSendNotification);
    // M5: the Sample tab shows the slot's own Ref and level, and the
    // material's buttons follow the slot.
    syncRefToSlot();
    levelSlider.setValue (selSlot >= 0 && selSlot < (int) sampleSlots.size()
                            ? (double) sampleSlots[(size_t) selSlot].level : 1.0,
                          juce::dontSendNotification);
    updateMaterialButtons();

    // keep selections valid
    auto findIdx = [](auto& vec, int id){ for (size_t i = 0; i < vec.size(); ++i) if (vec[i].id == id) return (int)i; return 0; };
    waveBox.setSelectedId(findIdx(waveforms, selWave) + 1, juce::dontSendNotification);
    envBox.setSelectedId(findIdx(envelopes, selEnv) + 1, juce::dontSendNotification);
    updateStatus();
}

void MainComponent::refreshScoreBinding()
{
    // v1.33.4 (M22): the editors change the model, so they mark it as
    // changed - the status timer publishes the snapshot only when the
    // revision moved (it used to copy the model on every tick, whatever
    // happened). The callback is passed to every bind() because bind()
    // replaces it.
    score.bind(&curPage(), &waveforms, &playheadSec, [this]{
        markModelChanged(); updateStatus(); refreshBoxes(); });
    pushVoice();
    score.setPlaying(transportActive());
    waveEd.bind(&waveforms, &selWave, [this]{ markModelChanged(); });
    envEd.bind(&envelopes, &selEnv, [this]{ markModelChanged(); });
    // v1.33.4 (L15, part 2): the undo transaction for the envelope editor.
    // F3 (2026-11-17): a shared envelope forks first, exactly like the
    // wavetable; the fork is part of the same undo step.
    envEd.setOnEditStart([this]{
        beginEdit ("Edit the envelope");
        bool forked = false;
        if (! forkEnvelopeForSelection (forked))
        {
            endEdit(); // the bank is full: the edit is cancelled
            return false;
        }
        return true;
    });
    envEd.setOnEditEnd([this]{ endEdit(); });
    updateSampleView();
    score.repaint(); waveEd.repaint(); envEd.repaint();
}

bool MainComponent::prepareWaveEdit()
{
    // v1.27: the wavetable editor is shared by every arc that names a wave.
    // Editing it with a selection must not rewrite the arcs that are not
    // selected, so the selection gets a private copy first. Without a
    // selection this is the old library/template edit.
    if (drawMatMode != 0 && drawMatMode != 3) return false;
    const auto ids = score.getSelectedArcs();
    if (ids.empty()) return true;

    bool forked = false;
    bool changed = false;
    {
        const juce::ScopedLock l(dataLock);
        upic::Waveform* src = nullptr;
        for (auto& w : waveforms) if (w.id == selWave) { src = &w; break; }
        if (src == nullptr && ! waveforms.empty())
        {
            // match WaveformEditor::current(), which falls back to the first
            src = &waveforms.front();
            selWave = src->id;
        }
        if (src == nullptr) return false;

        // Arc ids repeat between pages, so only the current page can have
        // selected arcs; every arc on another page is unselected.
        bool shared = false;
        for (size_t p = 0; p < pages.size() && !shared; ++p)
            for (const auto& a : pages[p].arcs)
            {
                if (a.waveformId != selWave) continue;
                const bool selected = (p == (size_t) currentPage
                                       && std::find(ids.begin(), ids.end(), a.id) != ids.end());
                if (!selected) { shared = true; break; }
            }

        if (shared)
        {
            // F2 (2026-11-17): the fork creates a wavetable, so it obeys the
            // same cap as New and Duplicate. A full bank refuses the edit
            // and says why instead of growing past the piece format's limit.
            if ((int) waveforms.size() >= upic::limits::kMaxWaveforms)
            {
                flash ("A piece holds at most " + juce::String (upic::limits::kMaxWaveforms)
                       + " wavetables (the limit) - the edit was cancelled");
                return false;
            }

            upic::Waveform copy = *src;
            int newId = 0;
            for (const auto& w : waveforms) newId = juce::jmax(newId, w.id + 1);
            copy.id = newId;
            const juce::String base = copy.name;
            juce::String name = base + " copy";
            int n = 2;
            for (;;)
            {
                bool used = false;
                for (const auto& w : waveforms) if (w.name == name) { used = true; break; }
                if (!used) break;
                name = base + " copy " + juce::String(n++);
            }
            copy.name = name;
            waveforms.push_back(copy);
            selWave = newId;
            forked = true;
            changed = true;
        }

        // All selected arcs follow the wave that is about to change.
        for (auto& a : curPage().arcs)
            if (std::find(ids.begin(), ids.end(), a.id) != ids.end())
            {
                if (a.waveformId != selWave)
                {
                    a.waveformId = selWave;
                    changed = true;
                }
            }
    }

    if (changed)
    {
        refreshBoxes();
        pushVoice();
        // v1.33.4 (M22): the fork changed the model - mark it, so the
        // status timer publishes the snapshot even if no edit follows
        markModelChanged();
    }
    score.repaint();
    return true;
}

//==============================================================================
// F2/F3 (2026-11-17): the wavetable and envelope library commands.
//
// Every command works on the asset selected in the bank. The caller (the
// LibraryManager wiring in MainComponent) opens one undo transaction around
// the command; a delete rewrites every reference inside that one snapshot,
// not one transaction per reference.

int MainComponent::waveUseCount (int id) const
{
    const juce::ScopedLock l (dataLock);
    int n = 0;
    for (const auto& pg : pages)
        for (const auto& a : pg.arcs)
            if (a.waveformId == id) ++n;
    return n;
}

int MainComponent::envelopeUseCount (int id) const
{
    const juce::ScopedLock l (dataLock);
    int n = 0;
    for (const auto& pg : pages)
        for (const auto& a : pg.arcs)
        {
            if (a.envelopeId == id) ++n;
            // F3: the PM index envelope is a reference too. Counting only
            // the amp slot is the silent re-point L13 was about.
            if (a.pmIndexEnv == id) ++n;
        }
    return n;
}

void MainComponent::afterLibraryChange()
{
    refreshBoxes();
    pushVoice();
    updateWaveButtonColours();
    waveEd.repaint();
    envEd.repaint();
    score.repaint();
    markModelChanged();
    publishModelSnapshot();
}

//==============================================================================
bool MainComponent::libraryNewWave()
{
    if ((int) waveforms.size() >= upic::limits::kMaxWaveforms)
    {
        flash ("A piece holds at most " + juce::String (upic::limits::kMaxWaveforms)
               + " wavetables (the limit) - delete one before adding another");
        return false;
    }

    {
        const juce::ScopedLock l (dataLock);
        const int id = nextBankId (waveforms);
        upic::Waveform w (id, uniqueBankName (waveforms, "Wave " + juce::String (id)),
                          upic::paletteColour (id));
        w.makeSine();
        waveforms.push_back (w);
        selWave = id;
    }
    afterLibraryChange();
    return true;
}

bool MainComponent::libraryDuplicateWave()
{
    if ((int) waveforms.size() >= upic::limits::kMaxWaveforms)
    {
        flash ("A piece holds at most " + juce::String (upic::limits::kMaxWaveforms)
               + " wavetables (the limit) - delete one before duplicating one");
        return false;
    }

    {
        const juce::ScopedLock l (dataLock);
        const upic::Waveform* src = nullptr;
        for (const auto& w : waveforms)
            if (w.id == selWave) { src = &w; break; }
        if (src == nullptr) return false;

        upic::Waveform copy = *src;
        copy.id = nextBankId (waveforms);
        copy.name = uniqueBankName (waveforms, src->name + " copy");
        waveforms.push_back (copy);
        selWave = copy.id;
    }
    afterLibraryChange();
    return true;
}

bool MainComponent::libraryRenameWave (const juce::String& newName)
{
    const auto name = newName.trim();
    if (name.isEmpty())
    {
        flash ("A wavetable needs a name");
        return false;
    }

    {
        const juce::ScopedLock l (dataLock);
        upic::Waveform* w = nullptr;
        for (auto& x : waveforms)
            if (x.id == selWave) { w = &x; break; }
        if (w == nullptr) return false;
        if (bankHasName (waveforms, name, w))
        {
            flash ("Another wavetable is already called \"" + name + "\"");
            return false;
        }
        w->name = name;
    }
    afterLibraryChange();
    return true;
}

bool MainComponent::libraryDeleteWave (int replacementId)
{
    if ((int) waveforms.size() <= 1)
    {
        flash ("The last wavetable cannot be deleted - a piece needs one");
        return false;
    }

    const int id = selWave;
    bool exists = false;
    for (const auto& w : waveforms)
        if (w.id == id) { exists = true; break; }
    if (! exists) return false;

    const int uses = waveUseCount (id);
    bool replacementOk = false;
    if (uses > 0)
    {
        if (replacementId == id) replacementId = -1;
        for (const auto& w : waveforms)
            if (w.id == replacementId) { replacementOk = true; break; }
        if (! replacementOk)
        {
            flash ("Choose a wavetable to replace the one being deleted");
            return false;
        }
    }

    {
        const juce::ScopedLock l (dataLock);
        if (uses > 0)
            for (auto& pg : pages)
                for (auto& a : pg.arcs)
                    if (a.waveformId == id)
                        a.waveformId = replacementId;

        waveforms.erase (std::remove_if (waveforms.begin(), waveforms.end(),
                                         [id] (const upic::Waveform& w) { return w.id == id; }),
                         waveforms.end());

        if (uses > 0)
            selWave = replacementId;
        else
            selWave = waveforms.empty() ? 0 : waveforms.front().id;
    }
    afterLibraryChange();
    return true;
}

bool MainComponent::libraryMakeWaveUnique()
{
    if (drawMatMode != 0 && drawMatMode != 3)
    {
        flash ("Make unique applies to the Osc and PM tabs");
        return false;
    }
    if (score.getSelectedArcs().empty())
    {
        flash ("Select the arcs that should get their own wavetable");
        return false;
    }

    const auto rev = modelRevision.load (std::memory_order_relaxed);
    if (! prepareWaveEdit()) return false;
    if (modelRevision.load (std::memory_order_relaxed) == rev)
    {
        flash ("The selected arcs already have their own wavetable");
        return false;
    }
    publishModelSnapshot();
    updateWaveButtonColours();
    waveEd.repaint();
    return true;
}

//==============================================================================
bool MainComponent::libraryNewEnvelope()
{
    if ((int) envelopes.size() >= upic::limits::kMaxEnvelopes)
    {
        flash ("A piece holds at most " + juce::String (upic::limits::kMaxEnvelopes)
               + " envelopes (the limit) - delete one before adding another");
        return false;
    }

    {
        const juce::ScopedLock l (dataLock);
        const int id = nextBankId (envelopes);
        upic::Envelope e (id, uniqueBankName (envelopes, "Envelope " + juce::String (id)),
                          upic::paletteColour (id));
        e.presetFlat();
        envelopes.push_back (e);
        selEnv = id;
    }
    afterLibraryChange();
    return true;
}

bool MainComponent::libraryDuplicateEnvelope()
{
    if ((int) envelopes.size() >= upic::limits::kMaxEnvelopes)
    {
        flash ("A piece holds at most " + juce::String (upic::limits::kMaxEnvelopes)
               + " envelopes (the limit) - delete one before duplicating one");
        return false;
    }

    {
        const juce::ScopedLock l (dataLock);
        const upic::Envelope* src = nullptr;
        for (const auto& e : envelopes)
            if (e.id == selEnv) { src = &e; break; }
        if (src == nullptr) return false;

        upic::Envelope copy = *src;
        copy.id = nextBankId (envelopes);
        copy.name = uniqueBankName (envelopes, src->name + " copy");
        envelopes.push_back (copy);
        selEnv = copy.id;
    }
    afterLibraryChange();
    return true;
}

bool MainComponent::libraryRenameEnvelope (const juce::String& newName)
{
    const auto name = newName.trim();
    if (name.isEmpty())
    {
        flash ("An envelope needs a name");
        return false;
    }

    {
        const juce::ScopedLock l (dataLock);
        upic::Envelope* e = nullptr;
        for (auto& x : envelopes)
            if (x.id == selEnv) { e = &x; break; }
        if (e == nullptr) return false;
        if (bankHasName (envelopes, name, e))
        {
            flash ("Another envelope is already called \"" + name + "\"");
            return false;
        }
        e->name = name;
    }
    afterLibraryChange();
    return true;
}

bool MainComponent::libraryDeleteEnvelope (int replacementId)
{
    if ((int) envelopes.size() <= 1)
    {
        flash ("The last envelope cannot be deleted - a piece needs one");
        return false;
    }

    const int id = selEnv;
    bool exists = false;
    for (const auto& e : envelopes)
        if (e.id == id) { exists = true; break; }
    if (! exists) return false;

    const int uses = envelopeUseCount (id);
    bool replacementOk = false;
    if (uses > 0)
    {
        if (replacementId == id) replacementId = -1;
        for (const auto& e : envelopes)
            if (e.id == replacementId) { replacementOk = true; break; }
        if (! replacementOk)
        {
            flash ("Choose an envelope to replace the one being deleted");
            return false;
        }
    }

    {
        const juce::ScopedLock l (dataLock);
        if (uses > 0)
            for (auto& pg : pages)
                for (auto& a : pg.arcs)
                {
                    if (a.envelopeId == id) a.envelopeId = replacementId;
                    if (a.pmIndexEnv == id) a.pmIndexEnv = replacementId;
                }

        envelopes.erase (std::remove_if (envelopes.begin(), envelopes.end(),
                                         [id] (const upic::Envelope& e) { return e.id == id; }),
                         envelopes.end());

        if (uses > 0)
            selEnv = replacementId;
        else
            selEnv = envelopes.empty() ? 0 : envelopes.front().id;
    }
    afterLibraryChange();
    return true;
}

bool MainComponent::forkEnvelopeForSelection (bool& forked)
{
    forked = false;
    const auto ids = score.getSelectedArcs();
    if (ids.empty()) return true;

    {
        const juce::ScopedLock l (dataLock);
        upic::Envelope* src = nullptr;
        for (auto& e : envelopes)
            if (e.id == selEnv) { src = &e; break; }
        if (src == nullptr) return false;

        bool shared = false;
        for (size_t p = 0; p < pages.size() && ! shared; ++p)
            for (const auto& a : pages[p].arcs)
            {
                const bool refsAmp = (a.envelopeId == selEnv);
                const bool refsPm  = (a.pmIndexEnv == selEnv);
                if (! refsAmp && ! refsPm) continue;
                const bool selected = (p == (size_t) currentPage
                                       && std::find (ids.begin(), ids.end(), a.id) != ids.end());
                // A PM-index reference is not the amp slot the edit is
                // about, so even a selected arc's PM use makes the envelope
                // shared: the fork leaves that use on the original.
                if (! selected || refsPm) { shared = true; break; }
            }

        if (! shared) return true;

        if ((int) envelopes.size() >= upic::limits::kMaxEnvelopes)
        {
            flash ("A piece holds at most " + juce::String (upic::limits::kMaxEnvelopes)
                   + " envelopes (the limit) - the edit was cancelled");
            return false;
        }

        upic::Envelope copy = *src;
        copy.id = nextBankId (envelopes);
        copy.name = uniqueBankName (envelopes, src->name + " copy");
        envelopes.push_back (copy);
        selEnv = copy.id;
        for (auto& a : curPage().arcs)
            if (std::find (ids.begin(), ids.end(), a.id) != ids.end())
                a.envelopeId = selEnv;
        forked = true;
    }

    if (forked)
    {
        refreshBoxes();
        pushVoice();
        envEd.repaint();
        markModelChanged();
    }
    return true;
}

bool MainComponent::libraryMakeEnvelopeUnique()
{
    if (score.getSelectedArcs().empty())
    {
        flash ("Select the arcs that should get their own envelope");
        return false;
    }

    bool forked = false;
    if (! forkEnvelopeForSelection (forked)) return false;
    if (! forked)
    {
        flash ("The selected arcs already have their own envelope");
        return false;
    }
    publishModelSnapshot();
    return true;
}

void MainComponent::pushVoice()
{
    int w = waveBox.getSelectedId() - 1;
    int e = envBox.getSelectedId() - 1;
    if (w >= 0 && ! waveforms.empty()) selWave = waveforms[(size_t)(w % (int) waveforms.size())].id;
    if (e >= 0 && ! envelopes.empty()) selEnv = envelopes[(size_t)(e % (int) envelopes.size())].id;
    const int sid = (selSlot >= 0 && selSlot < (int) sampleSlots.size())
                        ? sampleSlots[(size_t) selSlot].id : 1;
    drawSampleId = sid; // remembered for procedural generation
    // M5: on the Sample tab the Ref is the slot's (the material's base
    // pitch), so the grid and the new arcs are born with it.
    syncRefToSlot();
    score.setCurrentVoice(selWave, selEnv, drawIntensity, drawMatMode, sid, drawRef);
    // v1.32: a new arc is born with the frequency space on show
    score.setCurrentStep (stepBtn.getToggleState()
                            ? juce::jlimit (1, 99, (int) stepDivSlider.getValue()) : 0);
    // v1.33: and in the frequency space on show
    score.setCurrentSpace (juce::jlimit (0, 3, spaceBox.getSelectedId() - 1));
    int pmEnvId = 0;
    const int pmIdx = pmIndexEnvBox.getSelectedId();
    if (pmIdx >= 2 && pmIdx - 2 < (int) envelopes.size())
        pmEnvId = envelopes[(size_t)(pmIdx - 2)].id;
    score.setCurrentPmVoice(pmRatioSlider.getValue(), pmIndexSlider.getValue(),
                            pmEnvId, pmFeedbackSlider.getValue(),
                            pmOpsBox.getSelectedId() == 2 ? 4 : 2);
}

// Load one arc into the toolbar. The canvas calls this when an arc is clicked;
// the arc list calls it too, so both selection paths show the same settings.
void MainComponent::syncControlsToArc(int arcId)
{
    if (arcId < 0)
    {
        selectedArcId = -1;
        lastArcSelection.clear();
        arcListPanel.setSelectedArcs(currentPage, {});
        return;
    }

    {
        juce::ScopedLock l(dataLock);
        auto& pg = curPage();
        for (auto& a : pg.arcs) if (a.id == arcId)
        {
            // reflect voice in boxes
            for (size_t i = 0; i < waveforms.size(); ++i)
                if (waveforms[i].id == a.waveformId)
                    waveBox.setSelectedId((int)i + 1, juce::dontSendNotification);
            for (size_t i = 0; i < envelopes.size(); ++i)
                if (envelopes[i].id == a.envelopeId)
                    envBox.setSelectedId((int)i + 1, juce::dontSendNotification);
            int pmIdx = 1;
            for (size_t j = 0; j < envelopes.size(); ++j)
                if (envelopes[j].id == a.pmIndexEnv) { pmIdx = (int) j + 2; break; }
            pmIndexEnvBox.setSelectedId (pmIdx, juce::dontSendNotification);
            pmRatioSlider.setValue (a.pmRatio, juce::dontSendNotification);
            pmIndexSlider.setValue (a.pmIndex, juce::dontSendNotification);
            pmFeedbackSlider.setValue (a.pmFeedback, juce::dontSendNotification);
            pmOpsBox.setSelectedId (a.pmOps == 4 ? 2 : 1, juce::dontSendNotification);
            intenSlider.setValue(a.intensity, juce::dontSendNotification);
            drawIntensity = a.intensity; // keep new strokes in sync with the slider
            // v1.31 (U1): the polyagogy controls follow the selected arc
            modUseBox.setSelectedId (juce::jlimit (0, 2, a.waveUse) + 1, juce::dontSendNotification);
            modRateSlider.setValue (a.modRate, juce::dontSendNotification);
            modDepthSlider.setValue (a.modDepth, juce::dontSendNotification);
            // v1.32: the discrete frequency space follows the selected arc
            stepBtn.setToggleState (a.stepDivisions > 0, juce::dontSendNotification);
            stepDivSlider.setValue ((double) juce::jlimit (1, 99,
                                       a.stepDivisions > 0 ? a.stepDivisions : 12),
                                    juce::dontSendNotification);
            stepDivSlider.setEnabled (a.stepDivisions > 0);
            // v1.33: the frequency space follows the selected arc
            score.setCurrentSpace (juce::jlimit (0, 3, a.space));
            refreshSpaceBox();
            // reflect material in the tabs (the arc is in this tab's layer)
            drawMatMode = juce::jlimit(0, 3, a.mode);
            syncMaterialTabs();
            for (size_t i = 0; i < sampleSlots.size(); ++i)
                if (sampleSlots[i].id == a.sampleId) { selSlot = (int) i; break; }
            sampleBox.setSelectedId(selSlot + 1, juce::dontSendNotification);
            updateRevButton();
            drawRef = a.refFreq;
            refSlider.setValue(drawRef, juce::dontSendNotification);
            // M5: on the Sample tab the Ref is the slot's (the material's
            // base pitch), so the slider shows it.
            syncRefToSlot();
            updateSampleView();
            pushVoice();
            break;
        }
    }

    selectedArcId = score.getSelectedArc();
    lastArcSelection = score.getSelectedArcs();
    arcListPanel.setSelectedArcs(currentPage, lastArcSelection);
}

void MainComponent::updateSampleView()
{
    if (drawMatMode == 1 && selSlot >= 0 && selSlot < (int) sampleSlots.size())
        waveEd.setSampleView(&sampleSlots, selSlot);
    else
        waveEd.setSampleView(nullptr, -1);
}

// v1.19: the Reverse button follows the selected slot.
void MainComponent::updateRevButton()
{
    const bool on = selSlot >= 0 && selSlot < (int) sampleSlots.size()
                    && sampleSlots[(size_t) selSlot].reversed;
    revBtn.setToggleState(on, juce::dontSendNotification);
    revBtn.setButtonText(on ? "Rev ON" : "Rev");
    revBtn.setTooltip(on ? "The slot plays backwards (click to play it forward)"
                          : "Play the selected slot backwards");
}

// M5: the Sample tab's Ref is the SLOT's - the material's base pitch (the
// UPIC's per-slot REF). The slider, the grid and the new arcs all follow
// it, and every arc that plays the slot shares it.
void MainComponent::syncRefToSlot()
{
    if (drawMatMode != 1 || selSlot < 0 || selSlot >= (int) sampleSlots.size()) return;
    const double slotRef = sampleSlots[(size_t) selSlot].refFreq;
    if (! (slotRef > 0.0)) return;
    drawRef = slotRef;
    refSlider.setValue (drawRef, juce::dontSendNotification);
}

// M5: the material's buttons follow the selected slot - they are off when
// the slot has no material to edit.
void MainComponent::updateMaterialButtons()
{
    const bool has = selSlot >= 0 && selSlot < (int) sampleSlots.size()
                     && sampleSlots[(size_t) selSlot].loaded();
    normMatBtn.setEnabled (has);
    fadeMatBtn.setEnabled (has);
    resetMatBtn.setEnabled (has);
}

// M5: one of the material's edits (1 = normalise, 2 = fade, 3 = reset).
//
// The edits are not a rewrite of the audio: the normalise is a gain, and
// the fades and the trim are read parameters - so the app's undo stack can
// step over them, and the Reset button can take them back.
void MainComponent::editMaterial (int what)
{
    if (selSlot < 0 || selSlot >= (int) sampleSlots.size()) return;
    auto& slot = sampleSlots[(size_t) selSlot];
    if (! slot.loaded()) { flash ("The slot has no material"); return; }

    beginEdit (what == 1 ? "Normalize the material"
               : what == 2 ? "Fade the material" : "Reset the material");
    bool ok = true;
    {
        juce::ScopedLock l (dataLock);
        if (what == 1) ok = slot.normalise();
        else if (what == 2) slot.applyFade();
        else slot.resetEdits();
    }
    if (ok)
    {
        markModelChanged();       // the material changed -> the piece changed
        publishModelSnapshot();   // the audio thread must see it now
    }
    endEdit();

    // the trimmed material is a different length
    score.sampleDurations[slot.id] = slot.durationSec();
    updateSampleView();
    if (! ok) flash ("The material is silent - there is nothing to normalize");
    else if (what == 1) flash ("The material is normalized");
    else if (what == 2) flash ("The material is faded at both ends");
    else flash ("The material's edits are undone");
}

void MainComponent::flash(const juce::String& m)
{
    flashMsg = m;
    flashUntilMs = juce::Time::getMillisecondCounter() + 4000;
}

//==============================================================================
// v1.33.4 (M28): the long jobs - the render, the stems, the decode, the
// save - run on a worker thread, so the window stays alive and the audio
// thread is not held up by them. The job and its thread live in JobRunner.h;
// the app starts them and shows their progress in the status line.
void MainComponent::startJob (const juce::String& label,
                              std::function<void (Job&)> work,
                              std::function<void (Job&)> report,
                              bool announce)
{
    jobs.start (label, std::move (work), std::move (report), announce);
}

void MainComponent::cancelJob()
{
    jobs.cancel();
}

void MainComponent::pushSieve()
{
    sieveOn = sieveBtn.getToggleState();
    const int m = sieveMEdit.getText().trim().getIntValue();
    if (m >= 2 && m <= 120) sieveM = m; // keep previous value while an invalid one is typed
    std::vector<int> rs;
    auto tokens = juce::StringArray::fromTokens(sieveREdit.getText(), ",; \t", "");
    for (auto& tok : tokens)
    {
        auto t = tok.trim();
        if (t.isEmpty() || ! t.containsOnly("-0123456789")) continue;
        rs.push_back(((t.getIntValue() % sieveM) + sieveM) % sieveM);
    }
    // v1.33.2: an empty/unparseable remainder field must not silently turn
    // the sieve off (the checkbox then looked like it did nothing). Keep the
    // last valid list while the field is being edited.
    if (! rs.empty()) sieveR = rs;
    score.setSieve(sieveOn, sieveM, sieveR);
    updateStatus();
}

// nUPIC-style procedural arc generation: the four generators from the reference
// (stochastic/convergent arborescence, echelon construction, ordinal spine).
// Generated arcs are stamped with the current voice and selected afterwards.
void MainComponent::runGenerator(int kind)
{
    juce::Random rng((int) juce::Time::getMillisecondCounter());
    upic::GenParams gp;
    std::vector<upic::Arc> gen;
    juce::String name = "generator";
    std::vector<int> created;
    int added = 0, skipped = 0;
    // v1.33.4 (L15, part 2): the undo transaction - the state from before
    // the arcs were generated.
    beginEdit ("Generate arcs");
    {
        juce::ScopedLock l(dataLock);
        auto& pg = curPage();
        gp.t0 = 0.0;
        gp.t1 = pg.durationSec * 0.92;
        gp.f0 = 90.0;
        gp.f1 = 4000.0;
        switch (kind)
        {
            case 0: gen = upic::genStochasticArborescence(gp, rng); name = "stochastic arborescence"; break;
            case 1: gen = upic::genConvergentArborescence(gp, rng); name = "convergent arborescence"; break;
            case 2: gen = upic::genEchelon(gp, rng);                name = "echelon construction";   break;
            case 3: gen = upic::genOrdinalSpine(gp, rng);           name = "ordinal spine";          break;
            default: break;
        }
        for (auto& a : gen)
        {
            // v1.33.4 (H13): the page's arc count is capped by the piece
            // format, so the generator stops at the cap instead of writing
            // arcs that the loader would drop on the next open.
            if ((int) pg.arcs.size() >= upic::limits::kMaxArcsPerPage) { ++skipped; continue; }
            a.id = pg.nextArcId++;
            a.waveformId = selWave;
            a.envelopeId = selEnv;
            a.intensity = drawIntensity;
            a.mode = drawMatMode; // live arcs for the live tab, etc.
            a.sampleId = drawSampleId;
            a.refFreq = drawRef;
            if (a.mode == 3)
            {
                a.pmRatio = pmRatioSlider.getValue();
                a.pmIndex = pmIndexSlider.getValue();
                const int pmIdx = pmIndexEnvBox.getSelectedId();
                a.pmIndexEnv = (pmIdx >= 2 && pmIdx - 2 < (int) envelopes.size())
                                   ? envelopes[(size_t)(pmIdx - 2)].id : 0;
                a.pmFeedback = pmFeedbackSlider.getValue();
                a.pmOps = pmOpsBox.getSelectedId() == 2 ? 4 : 2;
            }
            created.push_back(a.id);
            pg.arcs.push_back(std::move(a));
            ++added;
        }
        score.setSelectedArcs(created);
    }
    refreshBoxes();
    updateStatus();
    score.repaint();
    // v1.33.4 (L15, part 2): the generated arcs are part of the model, so
    // they must be marked as changed (the audio thread sees them right away
    // instead of at the next safety-net publish) - and the undo keeps the
    // state from before them.
    markModelChanged();
    publishModelSnapshot();
    endEdit();
    juce::String msg = gen.empty() ? ("Generator produced no arcs (" + name + ")")
                                   : ("Generated " + juce::String(added) + " arcs -- " + name);
    if (skipped > 0)
        msg += " (" + juce::String(skipped) + " more did not fit: a page holds at most "
               + juce::String (upic::limits::kMaxArcsPerPage) + " arcs)";
    flash(msg);
}

//==============================================================================
// Guerrero integration: insert externally generated arcs.
//
// This follows runGenerator() exactly, because that is the established contract
// for mutating Page::arcs from the message thread:
//
//   1. beginEdit() opens the undo transaction BEFORE the model changes.
//   2. The model mutation happens under dataLock.
//   3. Ids come from Page::nextArcId so nothing collides.
//   4. markModelChanged() + publishModelSnapshot() hand the change to the audio
//      thread.
//   5. endEdit() closes the transaction AFTER the change is published.
//
// Every host limit that Arc::fromVar() enforces on load is enforced here first,
// so an inserted arc cannot be silently truncated when the piece is reopened.
// If nothing is inserted, no model change is published and no undo entry is
// left behind.
int MainComponent::insertGuerreroArcs(const std::vector<upic::GuerreroImportArc>& arcs,
                                      const juce::String& label,
                                      std::vector<int>* insertedIds)
{
    if (insertedIds != nullptr)
        insertedIds->clear();

    if (arcs.empty())
    {
        flash("Guerrero produced no arcs");
        return 0;
    }

    std::vector<int> created;
    int added = 0, skipped = 0;
    juce::String lastReason;

    beginEdit(label.isNotEmpty() ? label : juce::String("Insert Guerrero arcs"));
    {
        juce::ScopedLock l(dataLock);
        auto& pg = curPage();

        for (const auto& in : arcs)
        {
            // Loader-relevant caps first: the page arc count and the per-arc
            // point count. Arc::fromVar() stops at kMaxPointsPerArc, so
            // accepting more here would lose points on reload.
            if ((int) pg.arcs.size() >= upic::limits::kMaxArcsPerPage)
            {
                ++skipped;
                lastReason = "page arc limit reached";
                continue;
            }
            if (in.points.size() < 2)
            {
                ++skipped;
                lastReason = "arc has fewer than two points";
                continue;
            }
            if (in.points.size() > (size_t) upic::limits::kMaxPointsPerArc)
            {
                ++skipped;
                lastReason = "arc exceeds the per-arc point limit";
                continue;
            }
            if (! std::isfinite (in.intensity) || in.intensity < 0.0f || in.intensity > 1.0f)
            {
                ++skipped;
                lastReason = "arc intensity outside [0, 1]";
                continue;
            }

            upic::Arc a;
            a.waveformId = selWave;
            a.envelopeId = selEnv;
            a.intensity = in.intensity;
            a.mode = drawMatMode;
            a.sampleId = drawSampleId;
            a.refFreq = drawRef;

            // PM mode needs the current synthesis settings, exactly as
            // runGenerator() copies them; without this an imported PM arc would
            // fall back to Arc's defaults and sound different from a generated
            // one in the same mode.
            if (a.mode == 3)
            {
                a.pmRatio = pmRatioSlider.getValue();
                a.pmIndex = pmIndexSlider.getValue();
                const int pmIdx = pmIndexEnvBox.getSelectedId();
                a.pmIndexEnv = (pmIdx >= 2 && pmIdx - 2 < (int) envelopes.size())
                                   ? envelopes[(size_t)(pmIdx - 2)].id : 0;
                a.pmFeedback = pmFeedbackSlider.getValue();
                a.pmOps = pmOpsBox.getSelectedId() == 2 ? 4 : 2;
            }

            a.points.reserve(in.points.size());
            bool bad = false;
            for (const auto& p : in.points)
            {
                // Host contract, mirrored from Arc::fromVar(): finite score time
                // inside the page, finite pitch inside the supported range,
                // pressure in 0..1, and points in non-decreasing time order.
                if (! std::isfinite (p.t) || p.t < 0.0 || p.t > pg.durationSec)
                {
                    bad = true;
                    lastReason = "point outside the page duration";
                    break;
                }
                if (! std::isfinite (p.freq) ||
                    p.freq < upic::kMinFreq || p.freq > upic::kMaxFreq)
                {
                    bad = true;
                    lastReason = "point frequency outside the host range";
                    break;
                }
                if (! std::isfinite (p.i) || p.i < 0.0f || p.i > 1.0f)
                {
                    bad = true;
                    lastReason = "point pressure outside [0, 1]";
                    break;
                }
                if (! a.points.empty() && p.t < a.points.back().t)
                {
                    bad = true;
                    lastReason = "points are not in time order";
                    break;
                }

                upic::ArcPoint pt;
                pt.t = p.t;
                pt.freq = p.freq;
                pt.i = p.i;
                a.points.push_back(pt);
            }

            if (bad)
            {
                ++skipped;
                continue;
            }

            a.id = pg.nextArcId++;
            // Provenance travels with the arc: it is serialized by Arc::toVar
            // and therefore also survives undo/redo and reload.
            if (in.hasProvenance)
            {
                a.guerrero = in.provenance;
                a.hasGuerrero = true;
                // Fingerprint what was inserted, so endEdit() can tell later
                // whether this arc has been edited.
                a.guerrero.pointCount   = (int) a.points.size();
                a.guerrero.geometryEdited = false;
                a.guerrero.geometryHash = upic::Arc::geometryFingerprint(a.points);
                a.guerrero.hasFingerprint = true;
            }
            created.push_back(a.id);
            pg.arcs.push_back(std::move(a));
            ++added;
        }

        if (added > 0)
            score.setSelectedArcs(created);
    }

    if (added == 0)
    {
        // Nothing changed: close the transaction without publishing a model
        // change, so the piece is not marked dirty and no empty undo step is
        // left in the history.
        endEdit();
        juce::String msg = "Guerrero: no arcs inserted";
        if (skipped > 0)
            msg += " (" + juce::String(skipped) + " skipped"
                   + (lastReason.isNotEmpty() ? ": " + lastReason : juce::String()) + ")";
        flash(msg);
        return 0;
    }

    refreshBoxes();
    updateStatus();
    score.repaint();

    markModelChanged();
    publishModelSnapshot();
    endEdit();

    if (insertedIds != nullptr)
        *insertedIds = created;

    juce::String msg = "Inserted " + juce::String(added) + " Guerrero arc"
                       + (added == 1 ? "" : "s");
    if (skipped > 0)
        msg += " (" + juce::String(skipped) + " skipped"
               + (lastReason.isNotEmpty() ? ": " + lastReason : juce::String()) + ")";
    flash(msg);

    return added;
}

//==============================================================================
// v1.21 (wave 19): the arc list - one row per arc, grouped by material,
// with the arc's page, pitch range and time span.
#if GUERRERO_INTEGRATION
//==============================================================================
// The complete Guerrero -> UPICuitous pipeline, behind one call.
//
//   envelope (A1 Brownian) + upper boundary
//     -> seed (A2 confinement between them)
//     -> planArcFromCurve  (beats -> score seconds, log2Hz -> Hz, host limits)
//     -> guerrero_bridge::toImportArc
//     -> insertGuerreroArcs (one undo transaction)
//
// The engine owns the musical data; the host owns ids, material selection,
// limits and the transaction. Nothing here writes into the page directly.
namespace {

// The default recipe: the settings the generator has always shipped with, now
// expressed in the persisted recipe type so the menu item and the dialog share
// one code path instead of duplicating the parameter list.
upic::GuerreroRecipe defaultGuerreroRecipe()
{
    upic::GuerreroRecipe r;
    r.generatorVersion = 1;
    r.generator = 0;                 // envelope + confined seed

    r.lower.roughness = 0.35;
    r.lower.depth     = 7;
    r.lower.freqLo    = 110.0;
    r.lower.freqHi    = 880.0;
    r.lower.timeStart = 0.0;
    r.lower.timeEnd   = 16.0;

    // The upper boundary is the lower one an octave higher.
    r.upper = r.lower;
    r.upper.freqLo = 880.0;
    r.upper.freqHi = 1760.0;

    // Only roughness and depth: the seed takes its time and frequency bounds
    // from the envelopes, which is why the dialog does not offer them.
    r.seed.roughness = 0.55;
    r.seed.depth     = 7;

    r.lowerSeed = 20261118u;
    r.upperSeed = r.lowerSeed + 1u;
    r.seedSeed  = r.lowerSeed + 2u;

    r.bpm = 120.0;
    r.frequencyPolicy = upic::GuerreroOutOfRange::Compress;
    r.pointsPolicy    = upic::GuerreroPointPolicy::Downsample;
    r.maxPoints = (int) upic::limits::kMaxPointsPerArc;
    return r;
}

// Exact comparison, field by field. `==` on the doubles is deliberate: a recipe
// the dialog did not change carries the original binary values, so equality here
// means "the user really did not change anything".
bool sameEnvelopeRecipe (const upic::GuerreroEnvelopeRecipe& a,
                         const upic::GuerreroEnvelopeRecipe& b)
{
    return a.roughness == b.roughness && a.depth == b.depth
        && a.freqLo == b.freqLo && a.freqHi == b.freqHi
        && a.timeStart == b.timeStart && a.timeEnd == b.timeEnd
        && a.allowDegenerateRangeExpansion == b.allowDegenerateRangeExpansion;
}

bool sameRecipe (const upic::GuerreroRecipe& a, const upic::GuerreroRecipe& b)
{
    return a.generatorVersion == b.generatorVersion
        && a.generator == b.generator
        && sameEnvelopeRecipe (a.lower, b.lower)
        && sameEnvelopeRecipe (a.upper, b.upper)
        && a.seed.roughness == b.seed.roughness && a.seed.depth == b.seed.depth
        && a.lowerSeed == b.lowerSeed
        && a.upperSeed == b.upperSeed
        && a.seedSeed == b.seedSeed
        && a.bpm == b.bpm
        && a.frequencyPolicy == b.frequencyPolicy
        && a.pointsPolicy == b.pointsPolicy
        && a.maxPoints == b.maxPoints;
}

} // anonymous namespace

void MainComponent::runGuerreroGenerator()
{
    juce::String error;
    if (! applyGuerreroRecipe (0, defaultGuerreroRecipe(), error))
        flash ("Guerrero: " + error);
}

void MainComponent::closeGuerreroDialog (int generation)
{
    // A deferred close carries the generation of the dialog that scheduled it.
    // If a newer dialog has opened since, the stale request must not destroy it.
    if (generation != 0 && generation != guerreroOverlayGeneration)
        return;

    // Component's destructor detaches from its parent, so resetting the
    // unique_ptr is all that is needed.
    guerreroOverlay.reset();
}

void MainComponent::openGuerreroDialog (int arcId)
{
    closeGuerreroDialog();

    // Bind the dialog to the page it was opened on. Arc ids are page-local, and
    // sequential playback can advance the active page while the dialog is open,
    // so an Apply must not follow that change onto a different page.
    {
        const juce::ScopedLock l (dataLock);
        guerreroTargetPage     = currentPage;
        guerreroTargetArcCount = (int) curPage().arcs.size();
        guerreroTargetDuration = curPage().durationSec;
    }

    // Identity for this dialog, so a delayed close belonging to it can never
    // take down a dialog opened later.
    const int generation = ++guerreroOverlayGeneration;

    auto overlay = std::make_unique<GuerreroParamOverlay>();
    overlay->editing = (arcId != 0);
    overlay->title   = overlay->editing ? juce::String ("Edit Guerrero recipe")
                                        : juce::String ("New Guerrero arc");
    overlay->recipe  = defaultGuerreroRecipe();

    if (arcId != 0)
    {
        bool found = false;
        const juce::ScopedLock l (dataLock);
        for (const auto& a : curPage().arcs)
        {
            if (a.id != arcId) continue;

            // The same preconditions as regeneration. Editing the recipe of an
            // arc whose geometry the user has already changed would overwrite
            // that work, so it is refused with an explanation.
            if (! a.hasGuerrero)              { flash ("Edit recipe: not a Guerrero arc"); return; }
            if (a.guerrero.malformed)         { flash ("Edit recipe: the provenance record is damaged"); return; }
            if (! a.guerrero.recipeValid)     { flash ("Edit recipe: this arc predates the stored recipe"); return; }
            if (a.guerrero.geometryEdited)    { flash ("Edit recipe: the geometry was edited, so it is kept as it is"); return; }

            overlay->recipe = a.guerrero.recipe;
            found = true;
            break;
        }
        if (! found) { flash ("Edit recipe: no such arc"); return; }
    }

    overlay->loadFromRecipe();

    // Closing is deferred: the dialog's button and key handlers are running
    // when these fire, so destroying it inline would use an object after its
    // own method frame had been entered.
    juce::Component::SafePointer<MainComponent> safe (this);
    auto scheduleClose = [safe, generation]
    {
        juce::MessageManager::callAsync ([safe, generation]
        {
            if (safe != nullptr) safe->closeGuerreroDialog (generation);
        });
    };

    overlay->onCancel = [this, scheduleClose]
    {
        if (guerreroOverlay != nullptr) guerreroOverlay->setVisible (false);
        scheduleClose();
    };

    overlay->onApply = [this, arcId, scheduleClose] (const upic::GuerreroRecipe& r,
                                                     juce::String& error) -> bool
    {
        // The page must be the one the dialog was opened on. Without this, an
        // automatic page advance during playback could redirect the edit onto a
        // different page's arc that happens to share the id.
        if (! guerreroTargetUnchanged())
        {
            error = "the active page changed while the dialog was open - reopen it";
            return false;
        }

        if (! applyGuerreroRecipe (arcId, r, error))
            return false;   // the dialog stays open and shows the reason

        if (guerreroOverlay != nullptr) guerreroOverlay->setVisible (false);
        scheduleClose();
        return true;
    };

    guerreroOverlay = std::move (overlay);
    addAndMakeVisible (*guerreroOverlay);
    guerreroOverlay->setBounds (getLocalBounds());
    guerreroOverlay->toFront (true);
    guerreroOverlay->grabKeyboardFocus();
}

bool MainComponent::applyGuerreroRecipe (int arcId, const upic::GuerreroRecipe& recipe,
                                         juce::String& error)
{
    // ---- existing arc: eligibility and equality BEFORE any generation -----
    // Generation is stochastic, can be expensive, and can fail (for example
    // when the envelope span no longer fits the page). None of that should
    // happen for an Apply that changes nothing, so provenance, the fingerprint
    // and exact recipe equality are settled first.
    if (arcId != 0)
    {
        bool unchanged = false;
        {
            const juce::ScopedLock l (dataLock);
            const upic::Arc* found = nullptr;
            for (const auto& a : curPage().arcs)
                if (a.id == arcId) { found = &a; break; }

            if (found == nullptr)             { error = "no such arc"; return false; }
            if (! found->hasGuerrero)         { error = "not a Guerrero arc"; return false; }
            if (found->guerrero.malformed)    { error = "the provenance record is damaged"; return false; }
            if (! found->guerrero.recipeValid){ error = "this arc predates the stored recipe"; return false; }
            if (found->guerrero.geometryEdited)
            {
                error = "the geometry was edited, so the recipe is not applied";
                return false;
            }
            if (! found->guerrero.hasFingerprint)
            {
                error = "this record has no geometry fingerprint";
                return false;
            }
            if (upic::Arc::geometryFingerprint (found->points) != found->guerrero.geometryHash
                || (int) found->points.size() != found->guerrero.pointCount)
            {
                error = "the geometry no longer matches its record";
                return false;
            }

            if (sameRecipe (recipe, found->guerrero.recipe))
                unchanged = true;
            else if (found->guerrero.revision >= std::numeric_limits<int>::max())
            {
                // Only a real change consumes a revision, so an exhausted
                // counter is not an obstacle to an Apply that changes nothing.
                error = "the revision counter is exhausted for this arc";
                return false;
            }
        }

        if (unchanged)
        {
            // Nothing to do: no generation, no transaction, no revision bump.
            // The separate "Regenerate" command is the explicit rebuild.
            flash ("Guerrero: the recipe is unchanged");
            return true;
        }
    }

    // ---- generate: a new arc, or a recipe that really changed -------------
    guerrero::upicadapter::PlannedArc plan;
    bool lossy = false;
    uint32_t omitted = 0;

    if (! upic::guerrero_bridge::regenerateFromRecipe (recipe, plan, &lossy, &omitted))
    {
        error = "this recipe could not be generated";
        return false;
    }

    if (plan.points.size() < 2)
    {
        error = "the recipe produced too little material";
        return false;
    }

    // Validate against the page before touching the model, exactly as
    // regeneration does. This is also what catches a recipe whose envelope
    // time span no longer fits the current page.
    const double pageSeconds = curPage().durationSec;
    for (const auto& p : plan.points)
    {
        if (! std::isfinite (p.t) || p.t < 0.0 || p.t > pageSeconds
            || ! std::isfinite (p.freq) || p.freq < upic::kMinFreq || p.freq > upic::kMaxFreq)
        {
            error = "the generated arc does not fit this page";
            return false;
        }
    }

    // ---- new arc ---------------------------------------------------------
    if (arcId == 0)
    {
        upic::GuerreroImportArc arc = upic::guerrero_bridge::toImportArc (plan);

        upic::GuerreroProvenance prov;
        prov.version         = 1;
        prov.compositionSeed = recipe.lowerSeed;
        prov.randomSeed      = recipe.seedSeed;
        prov.recipe          = recipe;
        prov.lossy           = lossy;
        prov.omittedPoints   = omitted;
        prov.revision        = 1;
        prov.recipeValid     = true;
        prov.geometryEdited  = false;

        arc.hasProvenance = true;
        arc.provenance    = prov;

        juce::String label = "Guerrero: envelope + seed";
        if (lossy)
            label += " (" + juce::String ((int) omitted) + " points dropped)";

        std::vector<int> ids;
        if (insertGuerreroArcs ({ arc }, label, &ids) == 0)
        {
            error = "the arc could not be inserted (page limits or host range)";
            return false;
        }
        return true;
    }

    beginEdit ("Edit Guerrero recipe");
    {
        const juce::ScopedLock l (dataLock);
        for (auto& a : curPage().arcs)
        {
            if (a.id != arcId) continue;

            a.points.clear();
            a.points.reserve (plan.points.size());
            for (const auto& p : plan.points)
            {
                upic::ArcPoint pt;
                pt.t = p.t; pt.freq = p.freq; pt.i = p.i;
                a.points.push_back (pt);
            }

            // The recipe is replaced; the fingerprint describes the geometry
            // that was just written, so endEdit() still detects a later edit.
            a.guerrero.recipe           = recipe;
            a.guerrero.lossy            = lossy;
            a.guerrero.omittedPoints    = omitted;

            // Provenance identity must move with the recipe, or the record
            // would claim a composition seed and RNG seed that no longer
            // describe the geometry it holds. The engine curve ids referred to
            // the original generation and cannot be re-derived here, so they
            // are cleared rather than left pointing at stale curves.
            a.guerrero.compositionSeed  = recipe.lowerSeed;
            a.guerrero.randomSeed       = recipe.seedSeed;
            a.guerrero.sourceSeedId     = 0;
            a.guerrero.sourceCurveId    = 0;

            a.guerrero.geometryHash     = upic::Arc::geometryFingerprint (a.points);
            a.guerrero.pointCount       = (int) a.points.size();
            a.guerrero.hasFingerprint   = true;
            a.guerrero.fingerprintAlgorithm = upic::Arc::kFingerprintQuantised;
            a.guerrero.geometryEdited   = false;
            ++a.guerrero.revision;
            break;
        }
        markModelChanged();
        publishModelSnapshot();
    }

    refreshBoxes();
    updateStatus();
    score.repaint();
    endEdit();

    flash ("Applied the Guerrero recipe");
    return true;
}
#endif // GUERRERO_INTEGRATION

#if GUERRERO_INTEGRATION
// Called from endEdit() after the model revision check, so it only runs when
// something really changed. Comparing each Guerrero arc against the fingerprint
// taken at insertion detects an edit on ANY path (drag, transpose, invert,
// reverse, point insert/remove) without instrumenting each tool.
//
// The undo snapshot was captured in beginEdit(), when the arc was still
// unbaked, so undoing the edit also restores the unbaked state.
//==============================================================================
// Regenerate one Guerrero arc from its stored recipe.
//
// The recipe is the authority for the GEOMETRY only. The arc keeps its host id
// and every sound-material setting (waveform, envelope, intensity, mode, PM
// parameters), because those are host overrides rather than generated material.
//
// Refused when: the record is not Guerrero, has no usable recipe (legacy or
// malformed), declares an unsupported recipe version, or the geometry has been
// edited by the user - regenerating over an edit would destroy work.
bool MainComponent::regenerateGuerreroArc (int arcId)
{
#if GUERRERO_INTEGRATION
    upic::GuerreroProvenance prov;
    {
        const juce::ScopedLock l (dataLock);
        const upic::Arc* found = nullptr;
        for (const auto& a : curPage().arcs)
            if (a.id == arcId) { found = &a; break; }

        if (found == nullptr)            { flash ("Regenerate: no such arc"); return false; }
        if (! found->hasGuerrero)        { flash ("Regenerate: not a Guerrero arc"); return false; }
        if (found->guerrero.malformed)   { flash ("Regenerate: the provenance record is damaged"); return false; }
        if (! found->guerrero.recipeValid)
        {
            flash ("Regenerate: this arc predates the stored recipe, so it cannot be rebuilt");
            return false;
        }
        if (found->guerrero.geometryEdited)
        {
            flash ("Regenerate: the geometry was edited, so it is kept as it is");
            return false;
        }

        // Do not trust the flag alone: verify the points against the fingerprint.
        // A record without a fingerprint cannot be integrity-checked. "No
        // fingerprint" must NOT mean "skip the check and proceed": a file whose
        // geometryHash was deleted would otherwise be regenerable over changed
        // geometry. Such a record is refused.
        if (! found->guerrero.hasFingerprint)
        {
            flash ("Regenerate: this record has no geometry fingerprint, so it cannot be verified");
            return false;
        }

        // Verify the points against the fingerprint. This catches geometry
        // changed outside any tracked transaction, such as a hand-edited
        // .upic.json whose provenance was otherwise left untouched.
        if (upic::Arc::geometryFingerprint (found->points) != found->guerrero.geometryHash
            || (int) found->points.size() != found->guerrero.pointCount)
        {
            flash ("Regenerate: the geometry no longer matches its record, so it is kept");
            return false;
        }
        // A revision read from user-supplied JSON could be INT_MAX; incrementing
        // it during regeneration would overflow. Refuse instead of wrapping.
        if (found->guerrero.revision >= std::numeric_limits<int>::max())
        {
            flash ("Regenerate: the revision counter is exhausted for this arc");
            return false;
        }

        prov = found->guerrero;
    }

    guerrero::upicadapter::PlannedArc rebuilt;
    if (! upic::guerrero_bridge::regenerateFromRecipe (prov.recipe, rebuilt))
    {
        flash ("Regenerate: this recipe version is not supported");
        return false;
    }

    // Validate the rebuilt material against the page before touching the model.
    const double pageSeconds = curPage().durationSec;
    if (rebuilt.points.size() < 2)
    {
        flash ("Regenerate: the recipe produced too little material");
        return false;
    }
    for (const auto& p : rebuilt.points)
    {
        if (! std::isfinite (p.t) || p.t < 0.0 || p.t > pageSeconds
            || ! std::isfinite (p.freq) || p.freq < upic::kMinFreq || p.freq > upic::kMaxFreq)
        {
            flash ("Regenerate: the rebuilt arc does not fit this page");
            return false;
        }
    }

    beginEdit ("Regenerate Guerrero arc");
    {
        const juce::ScopedLock l (dataLock);
        for (auto& a : curPage().arcs)
        {
            if (a.id != arcId) continue;

            a.points.clear();
            a.points.reserve (rebuilt.points.size());
            for (const auto& p : rebuilt.points)
            {
                upic::ArcPoint pt;
                pt.t = p.t; pt.freq = p.freq; pt.i = p.i;
                a.points.push_back (pt);
            }

            // New fingerprint for the new geometry, and a new revision.
            a.guerrero.geometryHash   = upic::Arc::geometryFingerprint (a.points);
            a.guerrero.pointCount     = (int) a.points.size();
            a.guerrero.hasFingerprint = true;
            a.guerrero.fingerprintAlgorithm = upic::Arc::kFingerprintQuantised;
            a.guerrero.geometryEdited = false;
            ++a.guerrero.revision;
            break;
        }
        markModelChanged();
        publishModelSnapshot();
    }

    refreshBoxes();
    updateStatus();
    score.repaint();
    endEdit();

    flash ("Regenerated Guerrero arc from its recipe");
    return true;
#else
    juce::ignoreUnused (arcId);
    return false;
#endif
}

void MainComponent::markEditedGuerreroGeometry()
{
    for (auto& pg : pages)
        for (auto& a : pg.arcs)
        {
            if (! a.hasGuerrero || a.guerrero.geometryEdited) continue;

            // A legacy record has no fingerprint. Comparing it against zero
            // would mark it edited on the next unrelated transaction, so it is
            // skipped until something actually re-records it.
            if (a.guerrero.geometryHash == 0 || a.guerrero.pointCount <= 0) continue;

            if (upic::Arc::geometryFingerprint (a.points) != a.guerrero.geometryHash
                || (int) a.points.size() != a.guerrero.pointCount)
                a.guerrero.geometryEdited = true;
        }
}
#endif

void MainComponent::rebuildArcList()
{
    arcListDirty = false;
    if (! showArcList) return;

    std::vector<ArcListPanel::Row> rows;

    struct Group { int kind; const char* name; juce::Colour colour; };
    const Group groups[] = { { 0, "OSC",    juce::Colour (0xff64d2ff) },
                              { 1, "SAMPLE", juce::Colour (0xffd2a664) },
                              { 2, "LIVE",   juce::Colour (0xffd26464) },
                              { 3, "PM",     juce::Colour (0xffa664d2) } };
    for (const auto& g : groups)
    {
        // the arcs of this kind, over every page
        std::vector<std::pair<int, const upic::Arc*>> found;
        for (size_t p = 0; p < pages.size(); ++p)
            for (const auto& a : pages[p].arcs)
                if (a.mode == g.kind) found.push_back ({ (int) p, &a });

        if (found.empty()) continue;

        ArcListPanel::Row head;
        head.kind = ArcListPanel::Row::group;
        head.title = juce::String (g.name) + "  (" + juce::String ((int) found.size()) + ")";
        head.colour = g.colour;
        rows.push_back (head);

        for (const auto& f : found)
        {
            const auto& a = *f.second;
            ArcListPanel::Row r;
            r.kind = ArcListPanel::Row::arc;
            r.arcId = a.id;
            r.page = f.first;
            r.mode = a.pitchMode;
            r.live = (a.mode == 2);
            r.title = "#" + juce::String (a.id);
            r.showMode = (a.mode == 1 || a.mode == 2); // only sample/live arcs
            r.showReverse = (a.mode == 2);   // the live arcs own the direction

            if (a.mode == 2)
            {
                r.reversed = a.reversed;
            }
            else if (a.mode == 1)
            {
                // a sample's direction belongs to the material (the slot)
                const int sid = juce::jlimit (1, (int) sampleSlots.size() - 1, a.sampleId);
                r.reversed = sampleSlots[(size_t) (sid - 1)].reversed;
            }

            // the pitch range (the arc's points) and the time span
            double lo = 0.0, hi = 0.0;
            if (! a.points.empty())
            {
                lo = hi = a.points.front().freq;
                for (const auto& pt : a.points)
                {
                    lo = juce::jmin (lo, pt.freq);
                    hi = juce::jmax (hi, pt.freq);
                }
            }
            r.hz = juce::String ((int) std::lround (lo)) + "-" + juce::String ((int) std::lround (hi));
            r.time = juce::String (a.startTime(), 1) + "-" + juce::String (a.endTime(), 1);
            r.pageText = "p" + juce::String (f.first + 1);
            r.spaceText = "S" + juce::String (a.space + 1);
            rows.push_back (r);
        }
    }

    arcListPanel.setRows (rows);
    arcListPanel.setSelectedArcs (currentPage, score.getSelectedArcs());
}

void MainComponent::showClearWarning()
{
    clearOverlay.pageNumber = currentPage + 1;
    clearOverlay.setBounds(getLocalBounds());
    clearOverlay.setVisible(true);
    clearOverlay.toFront(true);
    clearOverlay.grabKeyboardFocus();
    resized(); // position the confirm / cancel buttons
}

void MainComponent::closeClearWarning()
{
    clearOverlay.setVisible(false);
}

//==============================================================================
// v1.31 (U4): the digitised wave page.
//
// The real UPIC's wave page could be filled from a recording - the wavetable
// was digitised through an A/D converter, and Xenakis sampled the bellowing
// of bulls for Taurhiphanie (1988). Here the selected sample slot (a file
// or a mic take) becomes a wavetable: the whole take is resampled into one
// loop of the oscillator.
//==============================================================================
void MainComponent::waveFromSelectedSlot()
{
    const upic::SampleSlot* slot = nullptr;
    {
        const juce::ScopedLock l(dataLock);
        if (selSlot >= 0 && selSlot < (int) sampleSlots.size())
            slot = &sampleSlots[(size_t) selSlot];
    }

    if (slot == nullptr || slot->data == nullptr || slot->data->getNumSamples() <= 0)
    {
        flash ("No sample in this slot - load or capture one first");
        return;
    }

    upic::Waveform wave;
    {
        const juce::ScopedLock l(dataLock);
        int newId = 0;
        for (const auto& w : waveforms) newId = juce::jmax (newId, w.id + 1);

        juce::String name = slot->name.isNotEmpty() ? slot->name : juce::String ("Slot " + juce::String (slot->id));
        name += " wave";
        int n = 2;
        for (;;)
        {
            bool used = false;
            for (const auto& w : waveforms) if (w.name == name) { used = true; break; }
            if (! used) break;
            name = (slot->name.isNotEmpty() ? slot->name : juce::String ("Slot " + juce::String (slot->id)))
                 + " wave " + juce::String (n++);
        }

        // M5: the wavetable is made from the material's REGION (the trim) -
        // it must be the part that is actually played, not the whole file.
        const int ch = slot->data->getNumChannels();
        const int frames = slot->regionFrames();
        juce::AudioBuffer<float> region (ch, frames);
        for (int c = 0; c < ch; ++c)
            region.copyFrom (c, 0, *slot->data, c, slot->regionStart(), frames);

        wave = upic::waveFromTake (region, newId, name);
        waveforms.push_back (wave);
        selWave = newId;
    }

    refreshBoxes();
    waveEd.bind (&waveforms, &selWave, [this]{ markModelChanged(); });
    pushVoice();
    publishModelSnapshot();
    markModelChanged();
    flash ("Wave page digitised: \"" + wave.name + "\" (one loop)");
}

void MainComponent::clearCurrentPage()
{
    // v1.33.4 (L15, part 2): the undo transaction - the state from before
    // the page was cleared.
    beginEdit ("Clear the page");
    {
        const juce::ScopedLock l(dataLock);
        curPage().arcs.clear();
        score.setSelectedArc(-1);
        selectedArcId = -1;
        lastArcSelection.clear();
        arcListPanel.setSelectedArcs(currentPage, {});
        refreshBoxes();
        markModelChanged(); // clearing while playing fades instead of clicking
        publishModelSnapshot();
    }
    endEdit();
    repaint();
}

//==============================================================================
// v1.31 (U3): the UPIC's pitch transformations.
//
// The reverse is already here (the Rev box, the global Rev, the slot Rev).
// These are the other two: a transposition and a pitch inversion. They work
// on the drawn curve - where the music is - and they follow the same scope
// rule as the rest of the app: the selected arcs if there is a selection,
// otherwise every arc of the current page.
//
// The menu ids come from the "Transform the page..." submenu.

//==============================================================================
// Guerrero test hooks. These only read Arc fields, so they exist whether or
// not the engine was compiled in.
bool MainComponent::guerreroArcGeometryEditedForTest (int arcId) const
{
    const juce::ScopedLock l (dataLock);
    for (const auto& a : curPage().arcs)
        if (a.id == arcId) return a.hasGuerrero && a.guerrero.geometryEdited;
    return false;
}

uint64_t MainComponent::guerreroArcHashForTest (int arcId) const
{
    const juce::ScopedLock l (dataLock);
    for (const auto& a : curPage().arcs)
        if (a.id == arcId) return a.guerrero.geometryHash;
    return 0;
}

bool MainComponent::guerreroArcHasProvenanceForTest (int arcId) const
{
    const juce::ScopedLock l (dataLock);
    for (const auto& a : curPage().arcs)
        if (a.id == arcId) return a.hasGuerrero;
    return false;
}

bool MainComponent::setGuerreroRecipeForTest (int arcId, const upic::GuerreroRecipe& recipe)
{
    const juce::ScopedLock l (dataLock);
    for (auto& a : curPage().arcs)
        if (a.id == arcId && a.hasGuerrero)
        {
            a.guerrero.recipe = recipe;
            return true;
        }
    return false;
}

bool MainComponent::guerreroTargetUnchanged() const
{
    const juce::ScopedLock l (dataLock);
    if (currentPage != guerreroTargetPage) return false;

    const auto& p = curPage();
    return (int) p.arcs.size() == guerreroTargetArcCount
        && p.durationSec == guerreroTargetDuration;
}

int MainComponent::addPageCopyForTest()
{
    const juce::ScopedLock l (dataLock);
    pages.insert (pages.begin() + currentPage + 1, curPage());
    currentPage++;
    return currentPage;
}

void MainComponent::setCurrentPageForTest (int page)
{
    const juce::ScopedLock l (dataLock);
    currentPage = juce::jlimit (0, (int) pages.size() - 1, page);
}

