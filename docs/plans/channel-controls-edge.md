# The channel controls edge

The architecture note for R5.9. It describes the boundary R5.9 introduced -
the channel card's description - as it is after that phase, not as it is meant
to be at the end of the refactoring.

> **R5.9 gave the channel card its own description.** The widgets, the
> commands and the mixer panel are still the shell's; what moved is the
> knowledge of *what the channel card holds*, *what its controls say about
> themselves* and *how the channel list's items are numbered*.

## The boundary

```
Source/ChannelControls.h   the description: the channel card's cells, the
                           roles, every control's wording, the mixer's button
                           text and the channel list's item ids
        |
        |  the app binds the roles to its widgets; the layout asks the
        |  description what to draw; the list is written and read through
        |  the description's rules
        v
Source/MainComponent.h            the binding: role -> (caption, control)
Source/MainComponentControls.cpp  (the app owns the widgets, so the app binds
Source/MainComponentLayout.cpp     the roles to them and builds the card)
```

The review asks for the channel controls to be decomposed (§28):

> | Functions | Proposed destination |
> |---|---|
> | `showChannelPanel()`, `refreshChannelBox()` | ChannelController |

and, on the panels:

> "Many panel classes already exist. Extend those boundaries instead of
> creating unnecessary duplicate wrappers." (§28)

R5.9 takes the first step of that table, the same step R5.6, R5.7 and R5.8
took for the inspectors: the *description* and the *words* become an edge, and
the widgets and the commands stay the shell's. `showChannelPanel()` and
`refreshChannelBox()` are still `MainComponent`'s: they touch the document and
the mixer's own panel. The controller itself is a later phase's business.

## The description

```cpp
namespace upic::channel_controls
{
    using Kind = upic::inspector::Kind;

    /** A control of the toolbar's channel card. */
    enum class Control
    {
        Channel,   // the mixer channel selector
        Open,      // the mixer
        count
    };

    /** One cell of the channel card. */
    struct Cell
    {
        Control control;
        Kind kind;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the channel card says about itself. */
    struct Description
    {
        const char* name;       // "Channel"
        std::vector<Cell> card; // the card's cells, in order
    };

    const Description& describe();
    const std::vector<Control>& allControls();

    const char* tooltip (Control control);
    const char* openButtonText();   // "Mix..."

    constexpr int kMainItemId = 1;
    const char* mainName();         // "Main"
    int itemIdForChannelIndex (int index);
    bool isMainItem (int itemId);
    int channelIndexForItemId (int itemId);
}
```

The header holds no widget and no document, and it is free of JUCE: the
description is data, and the data can be checked without a window.

## The shared cell vocabulary

The channel card draws its cells the same way the R5.6/R5.7/R5.8 edges do, so
it uses their vocabulary - `Source/InspectorCell.h`:

```cpp
namespace upic::inspector
{
    enum class Kind { Plain, Labelled, Inline, Knob };
}
```

`using Kind = upic::inspector::Kind;` keeps `upic::channel_controls::Kind`
compiling, and the layout's one `addCell` helper handles the card.

## The contract

**The card is written down once.**

| the card | the cells | the kinds | the widths |
|---|---|---|---|
| the channel card | the channel selector, the mix button | Inline, Plain | 26/100, 62 |

The cells' numbers are the numbers the layout had; only their home moved.

**The words are written down once.** The two controls say what they do, and
the mixer's button says "Mix...":

| the control | what it says |
|---|---|
| the channel selector | "The mixer channel the selected arcs play on" |
| the mix button | "The mixer: channel levels, mute/solo and routing" |

Before, the two tooltips and the button's text were literals in the controls
builder (and the button's text was in the widget's declaration). Now the
button's text is the edge's word too.

**The channel list's ids are one rule.** Main is the list's first item - the
channel every arc sits on when the piece has no mixer of its own - and the
mixer's own channels follow it. The item ids are one rule:

- Main is item 1;
- the channel at the bank's index `i` is item `i + 2`.

The list is written in one place and read in another, so the rule lives in the
edge. Before, "index + 2" was written when the list was built and "item - 2"
when it was read, and nothing made the two agree.

## The binding

The app owns the widgets, so the app binds the roles to them - once, where the
channel card is built:

```cpp
namespace cc = upic::channel_controls;

bindChannelControl (cc::Control::Channel, &channelLab, &channelBox);
bindChannelControl (cc::Control::Open, nullptr, &channelBtn);
```

The layout then builds the card from the description's cells, with the helper
that handles the four kinds:

```cpp
FlowGroup ch { "ch", {} };
const auto& mixer = upic::channel_controls::describe();
for (auto& cell : mixer.card)
    addCell (ch, channelControls[(size_t) cell.control],
             cell.kind, cell.labelWidth, cell.width, cell.newLine);
```

The list is written and read through the edge's rules:

```cpp
channelBox.addItem (cc::mainName(), cc::kMainItemId);
for (int i = 0; i < (int) channels.size(); ++i)
    channelBox.addItem (channels[(size_t) i].name, cc::itemIdForChannelIndex (i));
...
channelBox.setSelectedId (cc::itemIdForChannelIndex (idx), juce::dontSendNotification);
```

and the command reads the item back the same way:

```cpp
if (upic::channel_controls::isMainItem (item)) { setChannelOnSelection (0); return; }
const int idx2 = juce::jlimit (0, (int) channels.size() - 1,
                               upic::channel_controls::channelIndexForItemId (item));
```

## The suite

`Source/SelfTestChannelControls.cpp` is the channel card's contract: 26
checks that drive the real component - they press the card's button, pick
from the card's dropdown and read the app's state.

- the card: the selector, the mix button, the caption and the wording;
- the channel list: Main first, then the mixer's own channels, and the item
  ids that go with them;
- the routing: picking a channel routes the selected arcs to it, picking Main
  brings them back, each route is one undo step, and one undo takes the arcs
  back to the channel;
- the mixer: the button opens the panel and the panel closes.

The move added the checks that pin the edge against the app: every role is
bound to a widget, the app's card is the edge's cell list in the edge's
order, the card's tooltips are the edge's wording, the mix button says the
edge's word, the app's list follows the edge's ids, and the edge's own tables
are sound.

The suite came first: 19 checks were written against the old code and drove
the real component. The move gave the card its description, and the checks
that cover the new behaviour (the edge against the app) were added with the
move.

## What is not covered yet

- **The controller is still the shell's.** The review's §28 wants a
  `ChannelController`. `showChannelPanel()` and `refreshChannelBox()` still
  live in `MainComponent`. R5.9 moved the description and the words; the
  controller is the next step.
- **The mixer panel is still the shell's.** It is a class of its own
  (`Source/ChannelPanel.h`), but its commands are still wired by
  `MainComponent`, and its own wording (the gain, the mute, the solo, the
  curve presets) is still its own.
- **The channel's own rules are not the edge's.** The channel's name is cut
  at 64 characters, the gain is clamped to 0..2 and the curve is capped by
  the piece format - those are the model's and the codec's rules
  (`Source/Channel.h`, `Source/ChannelCodec.h`), not the card's.
