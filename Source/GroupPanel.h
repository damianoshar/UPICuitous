#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"

//==============================================================================
// WP3: the arc-group panel.
//
// A group is a named set of arcs with shared, non-destructive attributes:
// mute, solo, gain, transpose and timbre overrides (wave / envelope /
// amplitude table). The panel edits the GROUPS, never the arcs' own data:
// "Add selection" and "Remove selection" change membership, and deleting a
// group leaves every arc exactly as it was.
//
// It is an in-app overlay for the same reason the other editors are:
// Android has no closable dialog windows. Slider gestures are wrapped in one
// undo transaction each (onLevelsBegin / onLevelsEnd).
//==============================================================================
class GroupPanel : public juce::Component
{
public:
    struct BankItem { int id = -1; juce::String name; };

    std::function<void (int index)> onSelectIndex;
    std::function<void (int index, const juce::String& name)> onRename;
    std::function<void (int index, bool muted, bool solo)> onMuteSolo;
    std::function<void (int index)> onLevelsBegin;
    std::function<void (int index, float gain, double transpose)> onLevels;
    std::function<void (int index)> onLevelsEnd;
    // kind: 0 wave, 1 envelope, 2 amplitude table
    std::function<void (int index, int kind, int id)> onOverride;
    std::function<void()> onNew, onDelete, onAddSelection, onRemoveSelection,
                          onSelectMembers, onClose;

    GroupPanel()
    {
        setName ("groupPanel");
        setWantsKeyboardFocus (true);

        for (auto* b : { &newBtn, &delBtn, &addBtn, &removeBtn, &selectBtn,
                         &renameBtn, &closeBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            addAndMakeVisible (b);
        }
        newBtn.setName ("grpNewBtn");       delBtn.setName ("grpDelBtn");
        addBtn.setName ("grpAddBtn");       removeBtn.setName ("grpRemoveBtn");
        selectBtn.setName ("grpSelectBtn"); renameBtn.setName ("grpRenameBtn");
        closeBtn.setName ("grpCloseBtn");

        newBtn.setTooltip ("Add a group to the page");
        delBtn.setTooltip ("Delete the selected group (its arcs keep their own data)");
        addBtn.setTooltip ("Put the selected arcs in the selected group");
        removeBtn.setTooltip ("Take the selected arcs out of the selected group");
        selectBtn.setTooltip ("Select the arcs that belong to the selected group");
        renameBtn.setTooltip ("Rename the selected group");
        closeBtn.setTooltip ("Close the group panel");

        newBtn.onClick = [this] { if (onNew) onNew(); };
        delBtn.onClick = [this] { if (onDelete) onDelete(); };
        addBtn.onClick = [this] { if (onAddSelection) onAddSelection(); };
        removeBtn.onClick = [this] { if (onRemoveSelection) onRemoveSelection(); };
        selectBtn.onClick = [this] { if (onSelectMembers) onSelectMembers(); };
        renameBtn.onClick = [this] { commitRename(); };
        closeBtn.onClick = [this] { close(); };

        nameEd.setName ("grpNameEd");
        nameEd.onReturnKey = [this] { commitRename(); };
        addAndMakeVisible (nameEd);

        muteBtn.setName ("grpMuteBtn"); soloBtn.setName ("grpSoloBtn");
        muteBtn.setClickingTogglesState (true);
        soloBtn.setClickingTogglesState (true);
        muteBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa04a4a));
        soloBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a8a4a));
        muteBtn.setTooltip ("Silence every arc in the group");
        soloBtn.setTooltip ("Play only the soloed group(s)");
        muteBtn.onClick = [this] { commitMuteSolo(); };
        soloBtn.onClick = [this] { commitMuteSolo(); };
        addAndMakeVisible (muteBtn);
        addAndMakeVisible (soloBtn);

        setupSlider (gainSl, "grpGainSl", "The group's level (0..2)");
        setupSlider (transposeSl, "grpTransposeSl",
                     "The group's transpose in semitones, applied after the space map");
        gainSl.setRange (0.0, 2.0, 0.001);
        transposeSl.setRange (-48.0, 48.0, 0.1);
        gainSl.onValueChange = [this] { commitLevels(); };
        transposeSl.onValueChange = [this] { commitLevels(); };
        gainSl.onDragStart = [this] { if (onLevelsBegin) onLevelsBegin (sel); };
        transposeSl.onDragStart = [this] { if (onLevelsBegin) onLevelsBegin (sel); };
        gainSl.onDragEnd = [this] { if (onLevelsEnd) onLevelsEnd (sel); };
        transposeSl.onDragEnd = [this] { if (onLevelsEnd) onLevelsEnd (sel); };

        gainLab.setName ("grpGainLab");   transposeLab.setName ("grpTransposeLab");
        gainLab.setText ("Gain", juce::dontSendNotification);
        transposeLab.setText ("Transpose", juce::dontSendNotification);
        for (auto* l : { &gainLab, &transposeLab })
        {
            l->setFont (juce::Font (juce::FontOptions (11.0f)));
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        }

        for (int k = 0; k < 3; ++k)
        {
            overrideBox[k].setName (k == 0 ? "grpWaveBox" : (k == 1 ? "grpEnvBox" : "grpAmpBox"));
            overrideBox[k].setTooltip ("The group's override; 'As arc' keeps the arc's own");
            overrideBox[k].onChange = [this, k]
            {
                if (sel < 0 || sel >= (int) shownGroups.size()) return;
                const int id = overrideBox[k].getSelectedId() <= 1
                                   ? -1 : overrideBox[k].getSelectedId() - 2;
                if (onOverride) onOverride (sel, k, id);
            };
            addAndMakeVisible (overrideBox[k]);
        }
        for (auto* l : { &waveLab, &envLab, &ampLab })
        {
            l->setFont (juce::Font (juce::FontOptions (11.0f)));
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        }
        waveLab.setText ("Wave", juce::dontSendNotification);
        envLab.setText ("Env", juce::dontSendNotification);
        ampLab.setText ("Amp table", juce::dontSendNotification);

        setVisible (false);
    }

    //--------------------------------------------------------------------------
    void open (const std::vector<upic::ArcGroup>& groups,
               const std::vector<int>& memberCounts,
               const std::vector<BankItem>& waves,
               const std::vector<BankItem>& envs,
               const std::vector<BankItem>& tables,
               int index)
    {
        setBank (groups, memberCounts, waves, envs, tables, index);
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        repaint();
    }

    void setBank (const std::vector<upic::ArcGroup>& groups,
                  const std::vector<int>& memberCounts,
                  const std::vector<BankItem>& waves,
                  const std::vector<BankItem>& envs,
                  const std::vector<BankItem>& tables,
                  int index)
    {
        shownGroups = groups;
        counts = memberCounts;
        sel = shownGroups.empty() ? -1
                                  : juce::jlimit (0, (int) shownGroups.size() - 1, index);
        const bool on = sel >= 0;
        for (auto* b : { &delBtn, &renameBtn, &addBtn, &removeBtn, &selectBtn })
            b->setEnabled (on);
        nameEd.setEnabled (on);
        muteBtn.setEnabled (on);
        soloBtn.setEnabled (on);
        gainSl.setEnabled (on);
        transposeSl.setEnabled (on);
        for (auto& b : overrideBox) b.setEnabled (on);

        if (on)
        {
            const upic::ArcGroup& g = shownGroups[(size_t) sel];
            nameEd.setText (g.name, false);
            muteBtn.setToggleState (g.muted, juce::dontSendNotification);
            soloBtn.setToggleState (g.solo, juce::dontSendNotification);
            gainSl.setValue (g.gain, juce::dontSendNotification);
            transposeSl.setValue (g.transposeSemitones, juce::dontSendNotification);
            fillOverride (0, waves, g.waveformId);
            fillOverride (1, envs, g.envelopeId);
            fillOverride (2, tables, g.amplitudeTableId);
        }
        repaint();
    }

    // The toolbar's slider theme (the panel's sliders are children of the
    // main component, so the interface checks count them too).
    void themeSliders()
    {
        for (auto* s : { &gainSl, &transposeSl })
        {
            s->setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff4a4a5c));
            s->setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
            s->setColour (juce::Slider::thumbColourId,          juce::Colour (0xffe6f4ff));
            s->setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xfff2f2f7));
            s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff4a4a5c));
        }
    }

    int selectedIndex() const { return sel; }
    bool isOpen() const { return isVisible(); }

    void close()
    {
        setVisible (false);
        if (onClose) onClose();
    }

    //--------------------------------------------------------------------------
    juce::Rectangle<int> cardRect() const
    {
        auto r = getLocalBounds().reduced (10);
        return juce::Rectangle<int> (juce::jmin (840, r.getWidth()),
                                     juce::jmin (600, r.getHeight())).withCentre (r.getCentre());
    }

    juce::Rectangle<int> listRect() const
    {
        auto inner = cardRect().reduced (18, 12);
        inner.removeFromTop (titleH);
        inner.removeFromBottom (bottomH);
        return inner.removeFromLeft (listW);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xcc000000));
        auto card = cardRect();
        g.setColour (juce::Colour (0xff1e1e26));
        g.fillRoundedRectangle (card.toFloat(), 6.0f);
        g.setColour (juce::Colour (0xff34343f));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 6.0f, 1.0f);

        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffc8c8d0));
        g.drawText ("Arc groups", card.reduced (18, 0).removeFromTop (titleH),
                    juce::Justification::centredLeft);

        auto list = listRect();
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (list);
        g.setColour (juce::Colour (0xff34343f));
        g.drawRect (list, 1);
        for (int i = 0; i < (int) shownGroups.size(); ++i)
        {
            auto row = juce::Rectangle<int> (list.getX() + 2, list.getY() + 2 + i * rowH,
                                             list.getWidth() - 4, rowH);
            if (! list.contains (row)) break;
            const auto& grp = shownGroups[(size_t) i];
            if (i == sel)
            {
                g.setColour (juce::Colour (0xff2c3e58));
                g.fillRect (row);
            }
            g.setColour (grp.colour);
            g.fillRect (row.removeFromLeft (10).reduced (2, 5));
            g.setColour (juce::Colour (0xffd0d0d8));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            juce::String t = grp.name;
            const int n = (i < (int) counts.size()) ? counts[(size_t) i] : 0;
            t += "  (" + juce::String (n) + ")";
            if (grp.muted) t += " M";
            if (grp.solo) t += " S";
            g.drawText (t, row.reduced (6, 0), juce::Justification::centredLeft, true);
        }

        if (shownGroups.empty())
        {
            g.setColour (juce::Colour (0xff7a7a88));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("No groups yet. Select arcs and press New,\n"
                              "then Add sel.",
                              list.reduced (10), juce::Justification::centred, 3);
        }
        g.setColour (juce::Colour (0xff7a7a88));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("groups never change the arcs' own data",
                    card.reduced (18, 0).removeFromBottom (16),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto inner = cardRect().reduced (18, 12);
        inner.removeFromTop (titleH);
        auto bottom = inner.removeFromBottom (bottomH).reduced (0, 4);
        auto left = inner.removeFromLeft (listW);
        left.removeFromTop ((int) shownGroups.size() * rowH + 4);

        closeBtn.setBounds (bottom.removeFromRight (64));
        bottom.removeFromRight (8);
        selectBtn.setBounds (bottom.removeFromRight (74));
        bottom.removeFromRight (4);
        removeBtn.setBounds (bottom.removeFromRight (70));
        bottom.removeFromRight (4);
        addBtn.setBounds (bottom.removeFromRight (70));

        auto lrow = left.removeFromTop (28);
        newBtn.setBounds (lrow.removeFromLeft (58));
        lrow.removeFromLeft (4);
        delBtn.setBounds (lrow.removeFromLeft (54));

        inner.removeFromLeft (16);
        auto renameRow = inner.removeFromTop (26);
        renameBtn.setBounds (renameRow.removeFromRight (66));
        renameRow.removeFromRight (6);
        nameEd.setBounds (renameRow);

        auto ms = inner.removeFromTop (28);
        muteBtn.setBounds (ms.removeFromLeft (60));
        ms.removeFromLeft (6);
        soloBtn.setBounds (ms.removeFromLeft (60));

        auto gr = inner.removeFromTop (30);
        gainLab.setBounds (gr.removeFromLeft (70));
        gainSl.setBounds (gr.removeFromLeft (juce::jmin (300, gr.getWidth())));

        auto tr = inner.removeFromTop (30);
        transposeLab.setBounds (tr.removeFromLeft (70));
        transposeSl.setBounds (tr.removeFromLeft (juce::jmin (300, tr.getWidth())));

        auto six = inner.removeFromTop (112).reduced (0, 4);
        const int h = six.getHeight() / 3;
        for (int k = 0; k < 3; ++k)
        {
            auto row = six.removeFromTop (h);
            auto* lab = (k == 0 ? &waveLab : (k == 1 ? &envLab : &ampLab));
            lab->setBounds (row.removeFromLeft (70));
            overrideBox[k].setBounds (row.removeFromLeft (juce::jmin (260, row.getWidth())));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardRect().contains (e.getPosition()))
        {
            close();
            return;
        }
        auto list = listRect();
        if (! list.contains (e.getPosition())) return;
        const int i = (e.getPosition().y - list.getY() - 2) / rowH;
        if (i >= 0 && i < (int) shownGroups.size())
        {
            sel = i;
            if (onSelectIndex) onSelectIndex (sel);
            setBank (shownGroups, counts, bankWaves, bankEnvs, bankTables, sel);
        }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return false;
    }

    //--------------------------------------------------------------------------
    // The selftest drives the commands without pixels.
    void selectRowForTest (int i)
    {
        if (i < 0 || i >= (int) shownGroups.size()) return;
        sel = i;
        if (onSelectIndex) onSelectIndex (sel);
        repaint();
    }
    void newForTest() { if (onNew) onNew(); }
    void deleteForTest() { if (onDelete) onDelete(); }
    void addSelForTest() { if (onAddSelection) onAddSelection(); }
    void removeSelForTest() { if (onRemoveSelection) onRemoveSelection(); }
    void selectMembersForTest() { if (onSelectMembers) onSelectMembers(); }
    void renameForTest (const juce::String& n)
    {
        nameEd.setText (n, false);
        commitRename();
    }
    void setMuteForTest (bool on)
    {
        muteBtn.setToggleState (on, juce::sendNotificationSync);
    }
    void setSoloForTest (bool on)
    {
        soloBtn.setToggleState (on, juce::sendNotificationSync);
    }
    void setGainForTest (float v)
    {
        gainSl.setValue (v, juce::sendNotificationSync);
    }
    void setTransposeForTest (double v)
    {
        transposeSl.setValue (v, juce::sendNotificationSync);
    }
    void setOverrideForTest (int kind, int id)
    {
        if (kind < 0 || kind > 2) return;
        overrideBox[kind].setSelectedId (id < 0 ? 1 : id + 2, juce::sendNotificationSync);
    }

private:
    static constexpr int titleH = 26;
    static constexpr int bottomH = 34;
    static constexpr int listW = 230;
    static constexpr int rowH = 22;

    void setupSlider (juce::Slider& s, const juce::String& name, const juce::String& tip)
    {
        s.setName (name);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        s.setTooltip (tip);
        addAndMakeVisible (s);
    }

    void fillOverride (int kind, const std::vector<BankItem>& items, int id)
    {
        auto& box = overrideBox[kind];
        box.clear (juce::dontSendNotification);
        box.addItem ("As arc", 1);
        for (int i = 0; i < (int) items.size(); ++i)
            box.addItem (items[(size_t) i].name, i + 2);
        (kind == 0 ? bankWaves : (kind == 1 ? bankEnvs : bankTables)) = items;
        box.setSelectedId (id < 0 ? 1 : id + 2, juce::dontSendNotification);
    }

    void commitRename()
    {
        if (sel < 0 || sel >= (int) shownGroups.size()) return;
        const auto n = nameEd.getText().trim();
        if (n.isEmpty()) return;
        if (onRename) onRename (sel, n);
    }

    void commitMuteSolo()
    {
        if (sel < 0 || sel >= (int) shownGroups.size()) return;
        if (onMuteSolo) onMuteSolo (sel, muteBtn.getToggleState(), soloBtn.getToggleState());
    }

    void commitLevels()
    {
        if (sel < 0 || sel >= (int) shownGroups.size()) return;
        if (onLevels) onLevels (sel, (float) gainSl.getValue(), transposeSl.getValue());
    }

    std::vector<upic::ArcGroup> shownGroups;
    std::vector<int> counts;
    std::vector<BankItem> bankWaves, bankEnvs, bankTables;
    int sel = -1;

    juce::TextButton newBtn { "New" }, delBtn { "Del" }, addBtn { "Add sel." },
                     removeBtn { "Remove" }, selectBtn { "Select" },
                     renameBtn { "Rename" }, closeBtn { "Close" };
    juce::TextButton muteBtn { "Mute" }, soloBtn { "Solo" };
    juce::TextEditor nameEd;
    juce::Slider gainSl, transposeSl;
    juce::Label gainLab, transposeLab, waveLab, envLab, ampLab;
    juce::ComboBox overrideBox[3];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroupPanel)
};
