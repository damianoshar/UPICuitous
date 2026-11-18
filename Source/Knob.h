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
// The look follows the EQ reference: a dark, slightly glossy disc, a thin
// arc just outside it (the unfilled track, then the value's accent) and a
// coloured dot on the arc. In the toolbar's cards the knob is a vertical
// cell: the caption sits above the disc and the value in its own dark pill
// below it (the reference's "cutoff / resonance" cells). A knob without a
// caption keeps the older horizontal arrangement, so the control can still
// be used outside a card.
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
    // The knob's design width (in the layout's design units), the value
    // box's width and the gap between them - the reference's knob + box.
    static constexpr int kDesignSize = 40;
    static constexpr int kValueWidth = 48;
    static constexpr int kValueGap = 4;
    // The width a knob item needs: the knob, the gap and the value box.
    static constexpr int preferredWidth() { return kDesignSize + kValueGap + kValueWidth; }
    static constexpr int kFallbackDesignWidth = 124;
    // The card cell: caption above, a large disc, the value box below.
    static constexpr int kCellDesignSize = 56;
    static constexpr int kCellCaptionH = 14;
    static constexpr int kCellPillH = 16;
    static constexpr int kCellGap = 2;
    static constexpr int kCellWidth = 60;
    static constexpr int kCellHeight = kCellCaptionH + kCellGap
                                      + kCellDesignSize + kCellGap + kCellPillH;
    // Below this many pixels the knob is too small to read, so the control
    // falls back to the horizontal slider.
    static constexpr float kMinRotaryPixels = 28.0f;
    // The track's inset at each end (the reference archive's geometry).
    static constexpr int kTrackInset = 5;

    // The rotary sweep: 270 degrees, from the lower left round the top to
    // the lower right (the reference's gap at the bottom).
    static constexpr float startAngle = 3.92699f;    // 225 degrees
    static constexpr float endAngle   = 8.63938f;    // 495 degrees

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

    // --- the toolbar card's cell -----------------------------------------
    // The layout gives every knob in a card its caption (the group's label).
    // The knob then draws the caption above the disc and the value below -
    // the reference's vertical cell. An empty caption keeps the horizontal
    // arrangement.
    void setCellCaption (const juce::String& text)
    {
        if (cellCaption == text)
            return;
        cellCaption = text;
        repaint();
    }
    const juce::String& getCellCaption() const noexcept { return cellCaption; }
    bool hasCellCaption() const noexcept { return cellCaption.isNotEmpty(); }

    // The layout's scale. The cell is taller than the old row, so the
    // control cannot derive the scale from its own height in cell mode.
    void setDesignScale (float scale)
    {
        const float s = juce::jlimit (0.3f, 2.5f, scale);
        if (designScale == s)
            return;
        designScale = s;
        repaint();
    }
    float getDesignScale() const noexcept { return designScale; }

    // The knob's square inside the cell: below the caption, above the pill.
    juce::Rectangle<int> cellKnobArea() const
    {
        const auto b = getLocalBounds();
        const float sc = uiScale();
        const int capH = (int) std::lround ((float) kCellCaptionH * sc);
        const int gap = juce::jmax (1, (int) std::lround ((float) kCellGap * sc));
        const int pillH = (int) std::lround ((float) kCellPillH * sc);
        const int side = juce::jmax (0, juce::jmin (b.getWidth(),
                                                    b.getHeight() - capH - pillH - 2 * gap));
        return juce::Rectangle<int> (b.getX() + (b.getWidth() - side) / 2,
                                     b.getY() + capH + gap, side, side);
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
        return (float) kCellDesignSize * layoutScale < kMinRotaryPixels;
    }

    // The track's area in the fallback - the value text takes the right end.
    // The LookAndFeel gives the slider the same area, so the drag and the
    // drawing agree. In a cell the caption and the pill take the top and the
    // bottom, so the track sits between them.
    juce::Rectangle<int> fallbackTrackArea() const
    {
        const auto b = getLocalBounds();
        if (hasCellCaption())
        {
            const float sc = uiScale();
            const int capH = (int) std::lround ((float) kCellCaptionH * sc);
            const int gap = juce::jmax (1, (int) std::lround ((float) kCellGap * sc));
            const int pillH = (int) std::lround ((float) kCellPillH * sc);
            return b.withTrimmedTop (capH + gap).withTrimmedBottom (pillH + gap)
                    .withTrimmedLeft (2).withTrimmedRight (2);
        }

        const int textW = juce::jmin ((int) std::lround ((float) b.getWidth() * 0.42f),
                                      (int) std::lround (58.0f * uiScale()));
        return b.withTrimmedRight (juce::jmax (20, textW));
    }

    // The value's box - the reference's pill. In a card cell it sits below
    // the knob (the reference's vertical cell); without a caption it stays
    // beside the knob or the fallback's track.
    juce::Rectangle<int> valuePillArea() const
    {
        const auto b = getLocalBounds();
        const float sc = uiScale();
        if (hasCellCaption())
        {
            const float side = (float) cellKnobArea().getWidth();
            const int pillH = juce::jmax (6, (int) std::lround ((float) kCellPillH * sc));
            const int pillW = juce::jmin (b.getWidth() - 2,
                                          juce::jmax ((int) side,
                                                      (int) std::lround (56.0f * sc)));
            return juce::Rectangle<int> (b.getCentreX() - pillW / 2, b.getBottom() - pillH,
                                         pillW, pillH);
        }

        const int side = juce::jmin (b.getWidth(), b.getHeight());
        const int gap = juce::jmax (2, (int) std::lround ((float) kValueGap * uiScale()));
        const int pillH = juce::jmax (4, (int) std::lround ((float) side * 0.58f));

        if (fallback)
        {
            const auto track = fallbackTrackArea();
            return juce::Rectangle<int> (track.getRight() + gap, b.getCentreY() - pillH / 2,
                                         juce::jmax (4, b.getRight() - track.getRight() - gap - 1),
                                         pillH);
        }

        return juce::Rectangle<int> (b.getX() + side + gap, b.getCentreY() - pillH / 2,
                                     juce::jmax (4, b.getWidth() - side - gap), pillH);
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
        return shortenValue (juce::Font (juce::FontOptions (base, juce::Font::bold)), width);
    }

    // The ladder itself: the full text first, then without the suffix, then
    // with fewer decimals, and at last not at all.
    juce::String shortenValue (const juce::Font& font, int width) const
    {
        if (width < 4)
            return {};

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
    juce::String cellCaption;
    float designScale = 0.0f;
    juce::TextEditor valueEditor;

    // The layout's scale: the card cell sets it explicitly; without one the
    // control derives it from its own size (the layout gives every control a
    // design-unit size and scales it).
    float uiScale() const noexcept
    {
        if (designScale > 0.0f)
            return designScale;
        return juce::jlimit (0.5f, 2.0f, (float) getHeight() / 32.0f);
    }

    void paintRotary (juce::Graphics& g, float opacity)
    {
        if (hasCellCaption())
        {
            paintCell (g, opacity);
            return;
        }

        const auto b = getLocalBounds().toFloat();
        const float side = juce::jmin (b.getWidth(), b.getHeight());
        if (side < 10.0f)
            return;

        // the knob's own square; the value's pill takes the rest of the bounds
        const auto knob = b.withWidth (side);
        paintDisc (g, knob, opacity);

        // the value, in its own box beside the knob
        paintValuePill (g, valuePillArea(), opacity);
    }

    // The reference's vertical cell: caption, large disc, value below.
    void paintCell (juce::Graphics& g, float opacity)
    {
        const auto b = getLocalBounds();
        if (b.getWidth() < 8 || b.getHeight() < 8)
            return;

        paintCaption (g, opacity);

        const auto knob = cellKnobArea().toFloat();
        if (knob.getWidth() >= 10.0f)
            paintDisc (g, knob, opacity);

        paintValuePill (g, valuePillArea(), opacity);
    }

    void paintCaption (juce::Graphics& g, float opacity)
    {
        if (! hasCellCaption())
            return;

        const auto b = getLocalBounds();
        const float sc = uiScale();
        const int capH = (int) std::lround ((float) kCellCaptionH * sc);
        if (capH < 6)
            return;

        const float base = juce::jlimit (7.0f, 13.0f, 10.5f * sc);
        juce::Font font (juce::FontOptions (base, juce::Font::bold));
        g.setColour (findColour (juce::Slider::textBoxTextColourId)
                          .withMultipliedAlpha (opacity * 0.86f));
        g.setFont (font);
        g.drawFittedText (cellCaption, b.withHeight (capH).reduced (1, 0),
                          juce::Justification::centred, 1, 1.0f);
    }

    // The disc, its track, the value's arc and the dot - shared by the
    // horizontal knob and the card cell.
    void paintDisc (juce::Graphics& g, juce::Rectangle<float> knob, float opacity)
    {
        const float side = juce::jmin (knob.getWidth(), knob.getHeight());
        if (side < 10.0f)
            return;

        const float sc = uiScale();
        const auto centre = knob.getCentre();

        const float arcThickness = juce::jmax (1.5f, 2.0f * sc);
        const float discSide = juce::jmax (6.0f, side - 8.0f * sc);
        const float radius = discSide * 0.5f + arcThickness * 0.5f + 0.5f;
        const float to = startAngle + (endAngle - startAngle)
                              * (float) valueToProportionOfLength (getValue());

        // the body: the reference's dark, slightly glossy disc
        const juce::Rectangle<float> disc (centre.x - discSide * 0.5f,
                                           centre.y - discSide * 0.5f,
                                           discSide, discSide);
        juce::ColourGradient body (juce::Colour (0xff313146).withMultipliedAlpha (opacity),
                                   centre.x, disc.getY(),
                                   juce::Colour (0xff1a1a26).withMultipliedAlpha (opacity),
                                   centre.x, disc.getBottom(), false);
        g.setGradientFill (body);
        g.fillEllipse (disc);
        g.setColour (juce::Colour (0xff3e3e58).withMultipliedAlpha (opacity));
        g.drawEllipse (disc, juce::jmax (1.0f, sc));

        // the track: a thin arc just outside the disc
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                             startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff3a3a4c).withMultipliedAlpha (opacity));
        g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        // the value's own arc, in the slider's accent
        const auto accent = findColour (juce::Slider::trackColourId);
        if (to > startAngle + 0.01f)
        {
            juce::Path filled;
            filled.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                  startAngle, to, true);
            g.setColour (accent.withMultipliedAlpha (opacity));
            g.strokePath (filled, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        // the thumb: the reference's coloured dot on the arc
        const float dot = juce::jmax (3.0f, 4.5f * sc);
        const juce::Point<float> tip (centre.x + radius * std::cos (to - juce::MathConstants<float>::halfPi),
                                      centre.y + radius * std::sin (to - juce::MathConstants<float>::halfPi));
        g.setColour (accent.withMultipliedAlpha (opacity));
        g.fillEllipse (tip.x - dot * 0.5f, tip.y - dot * 0.5f, dot, dot);
    }

    void paintFallback (juce::Graphics& g, float opacity)
    {
        paintCaption (g, opacity);

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

        // the value, in its own box beside the track
        paintValuePill (g, valuePillArea(), opacity);
    }

    void paintValuePill (juce::Graphics& g, juce::Rectangle<int> area, float opacity)
    {
        if (area.getWidth() < 6 || area.getHeight() < 6)
            return;

        const float sc = uiScale();
        const float radius = juce::jmax (2.0f, 3.5f * sc);
        const auto rf = area.toFloat();
        g.setColour (juce::Colour (0xff15151f).withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (rf, radius);
        g.setColour (juce::Colour (0xff33334a).withMultipliedAlpha (opacity));
        g.drawRoundedRectangle (rf.reduced (0.5f), radius, 1.0f);

        const float base = juce::jlimit (7.0f, 13.0f, (float) area.getHeight() * 0.46f);
        juce::Font font (juce::FontOptions (base, juce::Font::bold));
        const int inset = juce::jmax (2, (int) std::lround (3.0f * sc));
        const juce::String text = shortenValue (font, area.getWidth() - inset * 2);
        if (text.isEmpty())
            return;

        g.setColour (findColour (juce::Slider::textBoxTextColourId)
                          .withMultipliedAlpha (opacity));
        g.setFont (font);
        // the text is never squashed: the ladder above made it fit
        g.drawFittedText (text, area, juce::Justification::centred, 1, 1.0f);
    }

    static bool fits (const juce::Font& font, const juce::String& text, int width)
    {
        return text.isNotEmpty()
            && juce::GlyphArrangement::getStringWidthInt (font, text) <= width;
    }

    juce::Rectangle<int> valueEditorArea() const
    {
        // the editor opens over the value's own box, like the old sliders'
        // value box did
        return valuePillArea().reduced (1);
    }
};

} // namespace upic
