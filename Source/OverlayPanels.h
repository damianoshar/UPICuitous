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
#include "OverlayCard.h"
#include "CardTheme.h"

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
        // R5.11: the card's rectangle is the overlay shell's rule.
        const auto r = upic::overlay_card::cardFor (upic::overlay_card::Shell::AudioSettings,
                                                    getWidth(), getHeight());
        return juce::Rectangle<int> (r.x, r.y, r.w, r.h);
    }

    void paint(juce::Graphics& g) override
    {
        // R5.12: the card's frame is the theme's frame.
        g.fillAll(juce::Colour(upic::card_theme::kBackdrop));
        auto card = cardRect();
        g.setColour(juce::Colour(upic::card_theme::kFill));
        g.fillRoundedRectangle(card.toFloat(), upic::card_theme::kRadius);
        g.setColour(juce::Colour(upic::card_theme::kBorder));
        g.drawRoundedRectangle(card.toFloat().reduced(upic::card_theme::kBorderInset),
                               upic::card_theme::kRadius, upic::card_theme::kBorderWidth);
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
        // R5.11: the card's rectangle is the overlay shell's rule.
        const auto r = upic::overlay_card::cardFor (upic::overlay_card::Shell::ClearPage,
                                                    getWidth(), getHeight());
        return juce::Rectangle<int> (r.x, r.y, r.w, r.h);
    }

    void paint(juce::Graphics& g) override
    {
        // R5.12: the card's frame is the theme's frame.
        g.fillAll(juce::Colour(upic::card_theme::kBackdrop));
        auto card = cardRect();
        g.setColour(juce::Colour(upic::card_theme::kFill));
        g.fillRoundedRectangle(card.toFloat(), upic::card_theme::kRadius);
        g.setColour(juce::Colour(upic::card_theme::kBorder));
        g.drawRoundedRectangle(card.toFloat().reduced(upic::card_theme::kBorderInset),
                               upic::card_theme::kRadius, upic::card_theme::kBorderWidth);
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
