#pragma once

//==============================================================================
// The in-app overlays.
//
// The app cannot use a floating dialog window: on Android there is no title
// bar and no close button, so a DialogWindow could never be dismissed. Both
// overlays are therefore plain Components that paint a backdrop and a card,
// and a tap on the backdrop dismisses them.
//
// v1.30 (5.3 refactor): the overlay types moved out of MainComponent.h - they
// are self-contained and do not need any of the app's state.
//==============================================================================
#include <JuceHeader.h>

//------------------------------------------------------------------------------//
// The backdrop + card that the audio and the MIDI/OSC settings use. The
// card's contents are laid out by the owner (the card is only the frame).
//------------------------------------------------------------------------------//
struct AudioOverlay : juce::Component
{
    static constexpr int titleH = 28;
    std::function<void()> onDismiss;
    juce::String title { "Audio Settings" };

    juce::Rectangle<int> cardRect() const
    {
        auto r = getLocalBounds().reduced(10);
        return juce::Rectangle<int>(juce::jmin(560, r.getWidth()),
                                    juce::jmin(600, r.getHeight())).withCentre(r.getCentre());
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xcc000000));
        auto card = cardRect();
        g.setColour(juce::Colour(0xff1e1e26));
        g.fillRoundedRectangle(card.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff34343f));
        g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 6.0f, 1.0f);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.setColour(juce::Colour(0xffc8c8d0));
        g.drawText(title, card.reduced(12, 0).removeFromTop(titleH),
                   juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // tapping the backdrop dismisses (device changes apply live)
        if (! cardRect().contains(e.getPosition()) && onDismiss != nullptr)
            onDismiss();
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey && onDismiss != nullptr)
        {
            onDismiss();
            return true;
        }
        return false;
    }
};

//------------------------------------------------------------------------------//
// The "Clear page?" warning. Clearing is destructive, so it asks first.
//------------------------------------------------------------------------------//
struct ClearOverlay : juce::Component
{
    static constexpr int titleH = 28;
    std::function<void()> onCancel;
    int pageNumber = 1;

    juce::Rectangle<int> cardRect() const
    {
        auto r = getLocalBounds().reduced(10);
        return juce::Rectangle<int>(juce::jmin(480, r.getWidth()),
                                    juce::jmin(210, r.getHeight())).withCentre(r.getCentre());
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xcc000000));
        auto card = cardRect();
        g.setColour(juce::Colour(0xff1e1e26));
        g.fillRoundedRectangle(card.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff34343f));
        g.drawRoundedRectangle(card.toFloat().reduced(0.5f), 6.0f, 1.0f);
        auto inner = card.reduced(16, 0);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.setColour(juce::Colour(0xffe0a040));
        g.drawText("Clear page " + juce::String(pageNumber) + "?",
                   inner.removeFromTop(titleH), juce::Justification::centredLeft);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.setColour(juce::Colour(0xffc8c8d0));
        inner.removeFromBottom(54);
        g.drawFittedText("This deletes every arc on this page.\nThis cannot be undone.",
                         inner, juce::Justification::centredLeft, 3);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (! cardRect().contains(e.getPosition()) && onCancel != nullptr)
            onCancel();
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey && onCancel != nullptr)
        {
            onCancel();
            return true;
        }
        return false;
    }
};
