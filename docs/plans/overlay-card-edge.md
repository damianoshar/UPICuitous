# The overlay card edge

The architecture note for R5.11. It describes the boundary R5.11 introduced -
the overlay card shell's geometry - as it is after that phase, not as it is
meant to be at the end of the refactoring.

> **R5.11 gave the overlay card shell one description.** The widgets, the
> painting and the dismissal are still the shells'; what moved is the knowledge
> of *where the card sits in the window* and *how much padding the card's
> content has*.

## The boundary

```
Source/OverlayCard.h    the description: the shells, the card's maximum size,
                         the window's margin, the card's padding and the rule
                         that puts the card in the middle
        |
        |  the shells ask the edge where their card is and how much padding
        |  their content has; they keep their own widgets, their own painting
        |  and their own dismissal
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

The review asks for the repeated editor functionality to be unified (§29,
"Unify repeated editor functionality"):

> | Shared facility | Potential consumers |
> |---|---|
> | Modal panel shell | Overlay-based panels |

R5.11 takes that row's **modal panel shell**: the card every overlay-based
panel draws in the middle of the window. The app cannot use a floating dialog
window - on Android there is no title bar and no close button, so a dialog
could never be dismissed - so every editor and overlay is a plain `Component`
that paints a backdrop, a card and its own contents, and a tap on the backdrop
dismisses it.

Eight of those shells wrote the same rule again:

```cpp
auto r = getLocalBounds().reduced (10);
return juce::Rectangle<int> (juce::jmin (maxW, r.getWidth()),
                             juce::jmin (maxH, r.getHeight())).withCentre (r.getCentre());
```

and fourteen call sites wrote the card's padding again (`reduced (18, 12)`).
Nothing made the eight copies agree, and a new shell would have had to copy the
rule a ninth time.

## The description

```cpp
namespace upic::overlay_card
{
    using upic::layout::Rect;

    /** The shells that draw a card in the middle of the window. */
    enum class Shell
    {
        AudioSettings = 0,   // the audio settings and the MIDI + OSC settings
        ClearPage,           // the "clear page?" warning
        Group,               // the arc-group panel
        Channel,             // the mixer
        Sequence,            // the sequence panel
        AmpTables,           // the amplitude-table bank
        FreqTable,           // the frequency-table editor
        Performance,         // the performance view
        count
    };

    /** The card's maximum size: the card never grows past it. */
    struct Size { int maxWidth = 0; int maxHeight = 0; };
    Size sizeFor (Shell shell);

    /** The window's margin: the card lives inside it. */
    constexpr int kOuterMargin = 10;

    /** The card's padding: the content lives inside it. */
    constexpr int kPaddingX = 18;
    constexpr int kPaddingY = 12;

    /** The card's rectangle inside a window of the given size. */
    Rect cardFor (Shell shell, int windowW, int windowH);

    /** The card's content rectangle: the card's padding. */
    Rect paddedFor (const Rect& card);
}
```

The header holds no widget and no document, and it is free of JUCE: the
geometry is data, and the data can be checked without a window. It reuses the
project's plain rectangle (`Source/LayoutRect.h`), so it stays inside the
layout group's vocabulary.

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

**The card is the shell's maximum, centred in the window's margin.** The
window loses a margin of ten pixels on each side, the card never grows past
the shell's maximum, and the card sits in the middle of what is left:

| the shell | the card's maximum |
|---|---|
| the audio settings (and the MIDI + OSC settings) | 560 x 600 |
| the clear warning | 480 x 210 |
| the group panel | 840 x 600 |
| the mixer | 760 x 500 |
| the sequence panel | 820 x 540 |
| the amplitude tables | 780 x 570 |
| the frequency table | 760 x 560 |
| the performance view | 980 x 620 |

So a 1280 x 800 window gives the group panel a card of 840 x 600 at
(220, 100), and a 640 x 480 window gives it a card of 620 x 460 at (10, 10) -
the card fills the window and never grows past the maximum.

**The arithmetic is JUCE's own.** The old rule was `withCentre (r.getCentre())`,
and JUCE's centre is `pos + size / 2` with an integer division that truncates.
The edge copies that division, so the card lands on the same pixel it landed on
before - including in the odd-sized windows where the rounding could have
moved.

**The padding is 18 by 12.** The card's content starts 18 pixels from the
card's left and right edges and 12 from its top and bottom. The shells ask
for it by name (`kPaddingX`, `kPaddingY`), so the fourteen call sites that
write the content's rectangle cannot drift apart.

**The card is the shell's boundary.** A tap on the backdrop closes the shell
and a tap on the card does not - that is how the shell is dismissed on a
touch screen, where there is no title bar and no close button. Seven of the
eight shells do it; the performance view is the one that does not (it has a
close button of its own).

## The binding

Every shell asks the edge where its card is:

```cpp
juce::Rectangle<int> cardRect() const
{
    // R5.11: the card's rectangle is the overlay shell's rule.
    const auto r = upic::overlay_card::cardFor (upic::overlay_card::Shell::Group,
                                                getWidth(), getHeight());
    return juce::Rectangle<int> (r.x, r.y, r.w, r.h);
}
```

and the content's rectangle asks for the padding by name:

```cpp
auto inner = cardRect().reduced (upic::overlay_card::kPaddingX,
                                 upic::overlay_card::kPaddingY);
```

The card stays in the shell's own coordinates - the same ones the old
`getLocalBounds()` rule used - so the painting, the hit testing and the
children's bounds are untouched.

## The suite

`Source/SelfTestOverlayCard.cpp` is the shell's contract: 23 checks were
written against the old code and 38 came with the move.

They pin:

- the card of every shell at a 1280 x 800 window, at a 1024 x 700 window and
  at a 640 x 480 window (the last one is the window that is too small, where
  the card fills it);
- the MIDI + OSC settings' card, which is the audio settings' card;
- the performance view's card, which its close button shows;
- the group's content, which starts inside the card's padding;
- the dismissal rule of every shell: the backdrop closes it, the card keeps it
  - and the performance view, which has no backdrop dismissal at all.

The move added the checks that pin the edge against the app: every shell's
card is the edge's card, the performance view's card is the edge's card, the
content's padding is the edge's padding, the edge gives every shell a size,
and the dismissal rule is measured against the edge's card (the card's centre
comes from the edge).

## What is not covered yet

- **The colours and the radius are still the shells'.** The backdrop
  (`0xcc000000`), the card (`0xff1e1e26`), the card's border (`0xff34343f`)
  and the 6 px corner radius are still written eight times each, one set per
  shell. They are the theme's business - the review's R055, "Extract
  LookAndFeel and theme registry" - and they move with the theme, not with
  the shell's geometry.
- **The card's contents are still each shell's.** What goes on the card, where
  it sits and how the card's own layout engine works are still the shells'
  (the group panel's list, the mixer's channels, the sequence's points, the
  tables' curves, the performance view's groups). The edge says where the
  card is, not what is on it.
- **The performance view has no backdrop dismissal.** The other seven shells
  close on a tap outside the card; the performance view does not, so a tap
  outside its card does nothing. R5.11 pins that as it is - the phase moves
  the geometry, it does not change the dismissal - and the suite records it.
  Giving the performance view the same rule is a behaviour change and belongs
  to its own phase.
- **The shell's own widgets are still the shell's.** The close buttons, the
  tooltips and the contents of the card are the shell's; only the geometry
  moved.
