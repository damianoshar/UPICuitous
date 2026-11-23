#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"

//==============================================================================
// WP6.4: the mixer panel (minimal milestone: list, level, mute, solo,
// rename, new/delete).
//
// NOTE FOR THE PARALLEL WP7 WORK: this file is owned by WP6. WP7 (asset
// libraries / sample extraction) should add its own components in its own
// headers and its host wiring in the WP7-marked blocks, not here.
//
// It is an in-app overlay for the same reason the other editors are: Android
// has no closable dialog windows. Channel 0 is Main and is implicit, so a
// piece that never opens this panel keeps the legacy audio path.
//==============================================================================
class ChannelPanel : public juce::Component
{
public:
    std::function<void (int index)> onSelectIndex;
    std::function<void (int index, const juce::String&)> onRename;
    std::function<void (int index, bool muted, bool solo)> onMuteSolo;
    std::function<void (int index, float gain)> onGain;
    std::function<void()> onNew, onDelete, onClose;

    ChannelPanel()
    {
        setName ("channelPanel");
        setWantsKeyboardFocus (true);

        for (auto* b : { &newBtn, &delBtn, &renameBtn, &closeBtn })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff34343f));
            addAndMakeVisible (b);
        }
        newBtn.setName ("chNewBtn");       delBtn.setName ("chDelBtn");
        renameBtn.setName ("chRenameBtn"); closeBtn.setName ("chCloseBtn");
        newBtn.setTooltip ("Add a mixer channel");
        delBtn.setTooltip ("Delete the selected channel (its arcs return to Main)");
        renameBtn.setTooltip ("Rename the selected channel");
        closeBtn.setTooltip ("Close the mixer");

        newBtn.onClick = [this] { if (onNew) onNew(); };
        delBtn.onClick = [this] { if (onDelete) onDelete(); };
        renameBtn.onClick = [this] { commitRename(); };
        closeBtn.onClick = [this] { close(); };

        nameEd.setName ("chNameEd");
        nameEd.onReturnKey = [this] { commitRename(); };
        addAndMakeVisible (nameEd);

        muteBtn.setName ("chMuteBtn"); soloBtn.setName ("chSoloBtn");
        muteBtn.setClickingTogglesState (true);
        soloBtn.setClickingTogglesState (true);
        muteBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffa04a4a));
        soloBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a8a4a));
        muteBtn.setTooltip ("Silence the channel");
        soloBtn.setTooltip ("Play only the soloed channel(s)");
        muteBtn.onClick = [this] { commitMuteSolo(); };
        soloBtn.onClick = [this] { commitMuteSolo(); };
        addAndMakeVisible (muteBtn);
        addAndMakeVisible (soloBtn);

        gainSl.setName ("chGainSl");
        gainSl.setSliderStyle (juce::Slider::LinearHorizontal);
        gainSl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        gainSl.setRange (0.0, 2.0, 0.001);
        gainSl.setTooltip ("The channel's level");
        gainSl.onValueChange = [this]
        {
            if (sel < 0 || sel >= (int) shown.size()) return;
            if (onGain) onGain (sel, (float) gainSl.getValue());
        };
        addAndMakeVisible (gainSl);
        gainLab.setFont (juce::Font (juce::FontOptions (11.0f)));
        gainLab.setText ("Gain", juce::dontSendNotification);
        addAndMakeVisible (gainLab);

        setVisible (false);
    }

    // The toolbar slider theme (the slider is a child of the main component,
    // so the interface checks count it).
    void themeSliders()
    {
        gainSl.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff4a4a5c));
        gainSl.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5ac8fa));
        gainSl.setColour (juce::Slider::thumbColourId,          juce::Colour (0xffe6f4ff));
        gainSl.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xfff2f2f7));
        gainSl.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff4a4a5c));
    }

    void setChannels (const std::vector<upic::Channel>& bank, int index)
    {
        shown = bank;
        sel = shown.empty() ? -1 : juce::jlimit (0, (int) shown.size() - 1, index);
        const bool on = sel >= 0;
        for (auto* b : { &delBtn, &renameBtn }) b->setEnabled (on);
        nameEd.setEnabled (on);
        muteBtn.setEnabled (on);
        soloBtn.setEnabled (on);
        gainSl.setEnabled (on);
        if (on)
        {
            const auto& c = shown[(size_t) sel];
            nameEd.setText (c.name, false);
            muteBtn.setToggleState (c.muted, juce::dontSendNotification);
            soloBtn.setToggleState (c.solo, juce::dontSendNotification);
            gainSl.setValue (c.gain, juce::dontSendNotification);
        }
        repaint();
    }

    bool isOpen() const { return isVisible(); }
    void close()
    {
        setVisible (false);
        if (onClose) onClose();
    }

    juce::Rectangle<int> cardRect() const
    {
        auto r = getLocalBounds().reduced (10);
        return juce::Rectangle<int> (juce::jmin (640, r.getWidth()),
                                     juce::jmin (420, r.getHeight())).withCentre (r.getCentre());
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
        g.drawText ("Mixer channels", card.reduced (18, 0).removeFromTop (titleH),
                    juce::Justification::centredLeft);

        auto list = listRect();
        g.setColour (juce::Colour (0xff14141a));
        g.fillRect (list);
        g.setColour (juce::Colour (0xff34343f));
        g.drawRect (list, 1);
        for (int i = 0; i < (int) shown.size(); ++i)
        {
            auto row = juce::Rectangle<int> (list.getX() + 2, list.getY() + 2 + i * rowH,
                                             list.getWidth() - 4, rowH);
            if (! list.contains (row)) break;
            const auto& c = shown[(size_t) i];
            if (i == sel) { g.setColour (juce::Colour (0xff2c3e58)); g.fillRect (row); }
            g.setColour (c.colour);
            g.fillRect (row.removeFromLeft (10).reduced (2, 5));
            g.setColour (juce::Colour (0xffd0d0d8));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            juce::String t = c.name;
            if (c.muted) t += " M";
            if (c.solo) t += " S";
            g.drawText (t, row.reduced (6, 0), juce::Justification::centredLeft, true);
        }
        if (shown.empty())
        {
            g.setColour (juce::Colour (0xff7a7a88));
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("No channels. Arcs play on Main.\nPress New to add one.",
                              list.reduced (10), juce::Justification::centred, 3);
        }
    }

    void resized() override
    {
        auto inner = cardRect().reduced (18, 12);
        inner.removeFromTop (titleH);
        auto bottom = inner.removeFromBottom (bottomH).reduced (0, 4);
        auto left = inner.removeFromLeft (listW);
        left.removeFromTop ((int) shown.size() * rowH + 4);
        auto lrow = left.removeFromTop (28);
        newBtn.setBounds (lrow.removeFromLeft (58));
        lrow.removeFromLeft (4);
        delBtn.setBounds (lrow.removeFromLeft (54));

        closeBtn.setBounds (bottom.removeFromRight (64));
        bottom.removeFromRight (8);
        renameBtn.setBounds (bottom.removeFromRight (66));
        nameEd.setBounds (bottom.removeFromLeft (juce::jmin (180, bottom.getWidth() - 80)));

        inner.removeFromLeft (16);
        auto gainRow = inner.removeFromTop (28);
        gainLab.setBounds (gainRow.removeFromLeft (44));
        gainSl.setBounds (gainRow.removeFromLeft (juce::jmin (260, gainRow.getWidth())));
        auto ms = inner.removeFromTop (28);
        muteBtn.setBounds (ms.removeFromLeft (60));
        ms.removeFromLeft (6);
        soloBtn.setBounds (ms.removeFromLeft (60));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardRect().contains (e.getPosition())) { close(); return; }
        auto list = listRect();
        if (! list.contains (e.getPosition())) return;
        const int i = (e.getPosition().y - list.getY() - 2) / rowH;
        if (i >= 0 && i < (int) shown.size())
        {
            sel = i;
            if (onSelectIndex) onSelectIndex (sel);
            setChannels (shown, sel);
        }
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    // The selftest drives the commands without pixels.
    void newForTest() { if (onNew) onNew(); }
    void deleteForTest() { if (onDelete) onDelete(); }
    void selectForTest (int i)
    {
        if (i < 0 || i >= (int) shown.size()) return;
        sel = i;
        if (onSelectIndex) onSelectIndex (i);
        repaint();
    }
    void muteForTest (bool on) { muteBtn.setToggleState (on, juce::sendNotificationSync); }
    void soloForTest (bool on) { soloBtn.setToggleState (on, juce::sendNotificationSync); }
    void gainForTest (float v) { gainSl.setValue (v, juce::sendNotificationSync); }
    void renameForTest (const juce::String& n) { nameEd.setText (n, false); commitRename(); }

private:
    static constexpr int titleH = 26;
    static constexpr int bottomH = 34;
    static constexpr int listW = 220;
    static constexpr int rowH = 22;

    void commitRename()
    {
        if (sel < 0 || sel >= (int) shown.size()) return;
        const auto n = nameEd.getText().trim();
        if (n.isEmpty()) return;
        if (onRename) onRename (sel, n);
    }
    void commitMuteSolo()
    {
        if (sel < 0 || sel >= (int) shown.size()) return;
        if (onMuteSolo) onMuteSolo (sel, muteBtn.getToggleState(), soloBtn.getToggleState());
    }

    std::vector<upic::Channel> shown;
    int sel = -1;

    juce::TextButton newBtn { "New" }, delBtn { "Del" }, renameBtn { "Rename" },
                     closeBtn { "Close" }, muteBtn { "Mute" }, soloBtn { "Solo" };
    juce::TextEditor nameEd;
    juce::Slider gainSl;
    juce::Label gainLab;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelPanel)
};
