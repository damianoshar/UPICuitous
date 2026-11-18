#pragma once
#include <JuceHeader.h>
#include "UpicTheme.h"

namespace upic {

//==============================================================================
// F11 (2026-11-18): the knob.
//
// A rotary control for the toolbar's continuous amounts. It is a
// juce::Slider in rotary mode, so the value, the range, the interval, the
// suffix, the callbacks and the undo behaviour are the slider's own - only
// the shape and the drawing change.
//
// The look follows the reference archive (patches 02 and 05): the 5px cyan
// filled track (#5ac8fa) over the dark unfilled track (#4a4a5c), the 11px
// pale thumb (#e6f4ff) and the panel colour around it. The value is drawn
// on the knob; the group's label stays beside it.
//
// A narrow layout cannot read a knob, so the control falls back to the
// horizontal slider (decision 14): the same drawing as the reference
// archive's horizontal sliders, with the value beside the track. Only the
// layout knows how much room the toolbar has, so the layout decides
// (MainComponentLayout.cpp calls setFallback).
//
// The old sliders' value box could be typed into. The knob keeps that
// ability: a double click opens a small editor over the knob.
class Knob : public juce::Slider
{
public:
    // The knob's design width (in the layout's design units) and the width
    // of the horizontal fallback.
    static constexpr int kDesignSize = 40;
    static constexpr int kFallbackDesignWidth = 120;
    // Below this many pixels the knob is too small to read, so the control
    // falls back to the horizontal slider.
    static constexpr float kMinRotaryPixels = 28.0f;
    // The track's inset at each end (the reference archive's geometry).
    static constexpr int kTrackInset = 5;

    // The rotary sweep: 270 degrees, from the lower left round the top to
    // the lower right.
    static constexpr float startAngle = 2.35619f;    // 135 degrees
    static constexpr float endAngle   = 7.06858f;    // 405 degrees

    Knob()
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (startAngle, endAngle, true);

        addChildComponent (valueEditor);
        valueEditor.setVisible (false);
        valueEditor.setMultiLine (false, true);
        valueEditor.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        valueEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2a2a32));
        valueEditor.setColour (juce::TextEditor::textColourId,       juce::Colour (0xfff2f2f7));
        valueEditor.setColour (juce::TextEditor::outlineColourId,     juce::Colour (0xff5ac8fa));
        valueEditor.setColour (juce::TextEditor::highlightColourId,   juce::Colour (0x335ac8fa));
        valueEditor.onReturnKey = [this] { commitValueEntry(); };
        valueEditor.onEscapeKey = [this] { cancelValueEntry(); };
        valueEditor.onFocusLost = [this] { cancelValueEntry(); };
    }

    // --- the narrow layout's fallback ------------------------------------
    bool isFallback() const noexcept { return fallback; }

    void setFallback (bool on)
    {
        if (fallback == on)
            return;

        fallback = on;
        setSliderStyle (on ? juce::Slider::LinearHorizontal
                            : juce::Slider::RotaryHorizontalVerticalDrag);
        if (! on)
            valueEditor.setVisible (false);
        repaint();
    }

    // The layout's decision: is the knob big enough to read?
    static bool shouldFallBack (float layoutScale) noexcept
    {
        return (float) kDesignSize * layoutScale < kMinRotaryPixels;
    }

    // The track's area in the fallback - the value text takes the right end.
    // The LookAndFeel gives the slider the same area, so the drag and the
    // drawing agree.
    juce::Rectangle<int> fallbackTrackArea() const
    {
        const auto b = getLocalBounds();
        const int textW = juce::jmin ((int) std::lround ((float) b.getWidth() * 0.42f),
                                      (int) std::lround (58.0f * uiScale()));
        return b.withTrimmedRight (juce::jmax (20, textW));
    }

    // The value as it is drawn (the number, with or without the suffix).
    // This is the old text box's own formatting: the number with the
    // slider's decimal places, and the suffix.
    juce::String valueText (bool withSuffix = true) const
    {
        return formatValue (getNumDecimalPlacesToDisplay(), withSuffix);
    }

    // The value with a chosen number of decimals - the ladder that keeps
    // the text inside the knob.
    juce::String formatValue (int decimals, bool withSuffix) const
    {
        juce::String text = juce::String (getValue(), juce::jmax (0, decimals));
        if (withSuffix)
            text += getTextValueSuffix();
        return text;
    }

    // --- the typed value (the old sliders' value box) --------------------
    // The text is applied through the slider's own path, so the callbacks
    // and the undo behave exactly as a drag does.
    bool applyTypedValue (const juce::String& text)
    {
        const auto trimmed = text.trim();
        if (trimmed.isEmpty())
            return false;

        const double value = getValueFromText (trimmed);
        if (value < getMinimum() || value > getMaximum())
            return false;

        setValue (value, juce::sendNotificationSync);
        return true;
    }

    void beginValueEntry()
    {
        if (! isEnabled())
            return;

        valueEditor.setText (valueText (false), juce::dontSendNotification);
        valueEditor.setBounds (valueEditorArea());
        valueEditor.setVisible (true);
        valueEditor.toFront (false);
        valueEditor.selectAll();
        valueEditor.grabKeyboardFocus();
    }

    void commitValueEntry()
    {
        applyTypedValue (valueEditor.getText());
        valueEditor.setVisible (false);
    }

    void cancelValueEntry()
    {
        valueEditor.setVisible (false);
    }

    // The value as the face shows it: shortened until it fits the width.
    // The ladder: the full text first, then without the suffix, then with
    // fewer decimals, and at last not at all. The exact value is always in
    // the tooltip, so a short face never loses information.
    juce::String faceText (int width, int height) const
    {
        if (width < 4 || height < 4)
            return {};

        const float base = juce::jlimit (7.0f, 13.0f, (float) height * 0.42f);
        juce::Font font (juce::FontOptions (base, juce::Font::bold));

        const int decimals = getNumDecimalPlacesToDisplay();
        const juce::String full = valueText (true);
        const juce::String bare = valueText (false);
        if (fits (font, full, width))
            return full;
        if (fits (font, bare, width))
            return bare;

        for (int d = decimals - 1; d >= 0; --d)
            if (fits (font, formatValue (d, false), width))
                return formatValue (d, false);

        return {};
    }

    // The live value under the pointer's tooltip.
    juce::String getTooltip() override
    {
        const auto base = juce::Slider::getTooltip();
        const auto value = valueText (true);
        if (base.isEmpty()) return value;
        if (value.isEmpty()) return base;
        return base + "\n" + value;
    }

    void paint (juce::Graphics& g) override
    {
        const float opacity = isEnabled() ? 1.0f : 0.38f;
        if (fallback)
            paintFallback (g, opacity);
        else
            paintRotary (g, opacity);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        beginValueEntry();
    }

private:
    bool fallback = false;
    juce::TextEditor valueEditor;

    // The layout's scale, read from the control's own size (the layout
    // gives every control a design-unit size and scales it).
    float uiScale() const noexcept
    {
        return juce::jlimit (0.5f, 2.0f, (float) getHeight() / 32.0f);
    }

    void paintRotary (juce::Graphics& g, float opacity)
    {
        const auto b = getLocalBounds().toFloat();
        const float side = juce::jmin (b.getWidth(), b.getHeight());
        if (side < 6.0f)
            return;

        const float thickness = juce::jmax (3.0f, side * 0.16f);
        const float radius = juce::jmax (2.0f, side * 0.5f - thickness * 0.5f - 1.0f);
        const auto centre = b.getCentre();
        const float to = startAngle + (endAngle - startAngle)
                              * (float) valueToProportionOfLength (getValue());

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (findColour (juce::Slider::backgroundColourId)
                          .withMultipliedAlpha (opacity));
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        if (to > startAngle + 0.01f)
        {
            juce::Path filled;
            filled.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                  startAngle, to, true);
            g.setColour (findColour (juce::Slider::trackColourId)
                              .withMultipliedAlpha (opacity));
            g.strokePath (filled, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        // the thumb: the 11px pale dot of the reference archive
        const float thumb = juce::jmax (6.0f, side * 0.34f);
        const juce::Point<float> tip (centre.x + radius * std::cos (to - juce::MathConstants<float>::halfPi),
                                      centre.y + radius * std::sin (to - juce::MathConstants<float>::halfPi));
        g.setColour (findColour (juce::Slider::thumbColourId)
                          .withMultipliedAlpha (opacity));
        g.fillEllipse (tip.x - thumb * 0.5f, tip.y - thumb * 0.5f, thumb, thumb);

        // the value, on the knob
        drawValue (g, b.reduced (thickness + 2.0f).toNearestInt(), opacity, true);
    }

    void paintFallback (juce::Graphics& g, float opacity)
    {
        const auto area = fallbackTrackArea();
        if (area.getWidth() < 8)
            return;

        const float cy = (float) area.getCentreY();
        const float left = (float) area.getX() + (float) kTrackInset;
        const float right = (float) area.getRight() - (float) kTrackInset;
        if (right <= left)
            return;

        const float pos = juce::jlimit (left, right,
                                        left + (right - left)
                                                  * (float) valueToProportionOfLength (getValue()));
        const float thickness = 5.0f * uiScale();
        const float thumb = 11.0f * uiScale();

        g.setColour (findColour (juce::Slider::backgroundColourId)
                          .withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (left, cy - thickness * 0.5f, right - left, thickness,
                                thickness * 0.5f);

        if (pos > left)
        {
            g.setColour (findColour (juce::Slider::trackColourId)
                              .withMultipliedAlpha (opacity));
            g.fillRoundedRectangle (left, cy - thickness * 0.5f, pos - left, thickness,
                                    thickness * 0.5f);
        }

        g.setColour (findColour (juce::Slider::thumbColourId)
                          .withMultipliedAlpha (opacity));
        g.fillEllipse (pos - thumb * 0.5f, cy - thumb * 0.5f, thumb, thumb);
        g.setColour (upic::uiBackground().withMultipliedAlpha (opacity));
        g.drawEllipse (pos - thumb * 0.5f, cy - thumb * 0.5f, thumb, thumb, 1.0f);

        // the value, beside the track
        drawValue (g, area.withPosition (area.getRight() + 2, area.getY())
                          .withWidth (getWidth() - area.getWidth() - 2)
                          .reduced (0, 2),
                   opacity, false);
    }
    void drawValue (juce::Graphics& g, juce::Rectangle<int> area, float opacity,
                    bool centred)
    {
        if (area.getWidth() < 4 || area.getHeight() < 4)
            return;

        const float base = juce::jlimit (7.0f, 13.0f, (float) area.getHeight() * 0.42f);
        juce::Font font (juce::FontOptions (base, juce::Font::bold));

        const juce::String text = faceText (area.getWidth() - 1, area.getHeight());
        if (text.isEmpty())
            return;

        g.setColour (findColour (juce::Slider::textBoxTextColourId)
                          .withMultipliedAlpha (opacity));
        g.setFont (font);
        // the text is never squashed: the ladder above made it fit
        g.drawFittedText (text, area,
                          centred ? juce::Justification::centred : juce::Justification::centredLeft,
                          1, 1.0f);
    }

    static bool fits (const juce::Font& font, const juce::String& text, int width)
    {
        return text.isNotEmpty()
            && juce::GlyphArrangement::getStringWidthInt (font, text) <= width;
    }

    juce::Rectangle<int> valueEditorArea() const
    {
        const auto b = getLocalBounds();
        if (fallback)
        {
            const auto area = fallbackTrackArea();
            return juce::Rectangle<int> (area.getRight() + 2, b.getY() + 2,
                                         juce::jmax (4, b.getRight() - area.getRight() - 3),
                                         juce::jmax (4, b.getHeight() - 4));
        }

        const int side = juce::jmin (b.getWidth(), b.getHeight());
        return juce::Rectangle<int> (b.getCentreX() - side / 2, b.getCentreY() - side / 2,
                                     side, side).reduced ((int) (side * 0.22f));
    }
};

} // namespace upic
