# The card theme edge

The architecture note for R5.12. It describes the boundary R5.12 introduced -
the card shell's look - as it is after that phase, not as it is meant to be at
the end of the refactoring.

> **R5.12 gave the card shell one look.** The widgets, the painting and the
> contents of the card are still the shells'; what moved is the knowledge of
> *what the card is painted with* - the backdrop, the card's surface, the
> card's border and the corner radius.

## The boundary

```
Source/CardTheme.h    the description: the backdrop, the card's surface, the
                      card's border, the border's width and the corner radius
        |
        |  the shells paint their frame from the description; they keep their
        |  own widgets, their own contents and their own colours
        v
Source/GroupPanel.h           the group panel: the list, the buttons, the
Source/ChannelPanel.h         the mixer: the channels, the envelope, the
Source/SequencePanel.h        the sequence panel: the points, the transport,
Source/AmpTableEditor.h       the amplitude tables: the bank, the curves,
Source/FreqTableEditor.h      the frequency table: the curve, the flags,
Source/PerformanceView.h      the performance view: the groups, the transport,
Source/OverlayPanels.h        the audio and the MIDI/OSC settings, and the
                              clear warning
```

The review asks for the LookAndFeel and the theme registry to be extracted
(R055, "Extract LookAndFeel and theme registry"). R5.12 takes the first step of
that: the frame the eight card shells draw.

R5.11 had already moved the card's *geometry* (`Source/OverlayCard.h` - where
the card sits and how much padding its content has) and had left the look
behind, with the note that the colours were the theme's business. This phase
is that note paid.

Eight of those shells wrote the frame again:

```cpp
g.fillAll (juce::Colour (0xcc000000));
auto card = cardRect();
g.setColour (juce::Colour (0xff1e1e26));
g.fillRoundedRectangle (card.toFloat(), 6.0f);
g.setColour (juce::Colour (0xff34343f));
g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 6.0f, 1.0f);
```

Nothing made the eight copies agree, and a new shell would have had to copy the
frame a ninth time.

## The description

```cpp
namespace upic::card_theme
{
    /** The backdrop behind the card: the window's dimmed cover. */
    constexpr std::uint32_t kBackdrop = 0xcc000000;

    /** The card's own surface. */
    constexpr std::uint32_t kFill = 0xff1e1e26;

    /** The card's border. */
    constexpr std::uint32_t kBorder = 0xff34343f;

    /** The border's width, in pixels. */
    constexpr float kBorderWidth = 1.0f;

    /** The card's corner radius, in pixels. */
    constexpr float kRadius = 6.0f;

    /** The border's inset: the stroke sits half its width inside the card. */
    constexpr float kBorderInset = kBorderWidth * 0.5f;
}
```

The header holds no widget and no document, and it is free of JUCE: the
colours are ARGB values and the radius is a number, so the frame can be
checked without a window.

The header is **not** one of the gate's JUCE-free groups. The gate's five
groups are the same five as before this phase:

```
B1/B2 the layout engines (7 files): no JUCE header, no header outside the group
B1/B2 the domain's numeric core (4 files): no JUCE header, no header outside the group
B1/B2 the arc (1 file): no JUCE header, no header outside the group
B1/B2 the arc's geometry (1 file): no JUCE header, no header outside the group
B1/B2 the page (1 file): no JUCE header, no header outside the group
```

Adding the shell to the checker's list is R8's business; the suite pins the
header instead.

## The contract

**The frame is five values.** The backdrop behind the card (black at 80 per
cent), the card's own surface, the card's one-pixel border, the border's width
and the six-pixel corner radius:

| the value | the number | what it is |
|---|---|---|
| `kBackdrop` | `0xcc000000` | the dimmed cover behind the card |
| `kFill` | `0xff1e1e26` | the card's own surface |
| `kBorder` | `0xff34343f` | the card's border |
| `kBorderWidth` | `1.0f` | the border's width, in pixels |
| `kRadius` | `6.0f` | the card's corner radius, in pixels |

**The border sits half its width inside the card.** The old rule drew the
stroke on `card.toFloat().reduced (0.5f)` with a one-pixel pen, so the line
covered the card's outermost pixel exactly. The edge says the same thing once:
`kBorderInset` is `kBorderWidth * 0.5f`, and the shells ask for it by name.

**The frame is the same in all eight shells.** The suite paints each shell
off-screen and reads the pixels: the backdrop outside the card, and the card's
corner - a twelve-pixel block that holds the whole six-pixel corner and no
title (the titles start eighteen pixels in, and the overlays' twelve). The
eight corners are pixel for pixel the same.

## The binding

Every shell paints its frame from the description:

```cpp
// R5.12: the card's frame is the theme's frame.
g.fillAll (juce::Colour (upic::card_theme::kBackdrop));
auto card = cardRect();
g.setColour (juce::Colour (upic::card_theme::kFill));
g.fillRoundedRectangle (card.toFloat(), upic::card_theme::kRadius);
g.setColour (juce::Colour (upic::card_theme::kBorder));
g.drawRoundedRectangle (card.toFloat().reduced (upic::card_theme::kBorderInset),
                        upic::card_theme::kRadius, upic::card_theme::kBorderWidth);
```

The colours are ARGB values in the edge and `juce::Colour` in the shell - the
edge does not decide the value type, the shell does. The plan defers replacing
the value types (the plan's section 2), and a theme is not domain data
anyway.

## The suite

`Source/SelfTestCardTheme.cpp` is the frame's contract: 17 checks were written
against the old code and 26 came with the move.

They pin:

- the backdrop every shell paints behind the card (`0xcc000000`);
- the frame every shell paints - the card's surface, its border and its
  radius, read from the card's corner and compared with the frame the suite
  draws itself;
- the eight corners being pixel for pixel the same.

The move added the checks that pin the edge against the app: every shell's
frame is the theme's frame, and the theme holds the frame the shells have
always painted.

The suite has teeth: a seven-pixel radius instead of the six makes all eight
frame checks fail, at the corner's arc.

## What is not covered yet

- **The rest of the app's colours are still the shells'.** The panels' lists
  (`0xff14141a`), their plots' grid (`0xff2a2a36`), the selected row
  (`0xff2c3e58`), the row text (`0xffd0d0d8`), the dim text (`0xff7a7a88`)
  and the titles (`0xffc8c8d0`) are still written at each shell. They are the
  theme's business too, and they move with the rest of the theme registry
  (R055), not with the card's frame.
- **The same value can be two roles.** `0xff1e1e26` is the card's surface
  here and the score canvas's plot fill there; `0xff34343f` is the card's
  border here and a button's colour there. R5.12 moved the *card frame's*
  role, and the other roles keep their own literals until the theme registry
  says otherwise.
- **The title's own style is still each shell's.** Seven shells share a
  fourteen-pixel bold title in `0xffc8c8d0`, but the clear warning has its
  own (fifteen pixels, in `0xffe0a040`), and the title's geometry differs as
  well. The titles are the card's *contents*, and the contents are the
  shells'.
- **There is no LookAndFeel yet.** The theme is a set of values the shells
  read; the review's R055 also asks for a `juce::LookAndFeel`, which is a
  bigger step - the widgets' own colours, the fonts and the button styles -
  and it has its own phase.
