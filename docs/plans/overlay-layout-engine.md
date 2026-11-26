# The overlay layout engine

The architecture note for the boundary R1.3 introduced. It describes the
tree as it is after that phase, not as it is meant to be at the end of the
refactoring.

## The boundary

```
MainComponent::layoutOverlays()        Source/OverlayLayoutEngine.{h,cpp}
  asks the overlay for its card  --->   layoutAudioOverlay(cardFrame)
  (audioOverlay.cardRect(),              layoutSettingsOverlay(cardFrame, scale)
   settingsOverlay.cardRect(),           layoutClearOverlay(cardFrame)
   clearOverlay.cardRect())
                                  <---  the card's contents as rectangles
  applies setBounds() on the widgets
```

The engine owns the arithmetic of the three in-app overlays: the audio
device card, the MIDI + OSC settings card and the "clear page?" card. For
each card it owns the padding, the title row, the button row and every row
of the card's contents. It is a pure function of the card's frame and the
layout scale: no globals, no `juce::Component`, no fonts.

`MainComponent` keeps the JUCE side and is the compatibility seam:

- it asks the overlay component for its card frame. The frame stays with the
  overlay, because the overlay paints it (`AudioOverlay::cardRect()`,
  `ClearOverlay::cardRect()`);
- it keeps the backdrops: every overlay covers the whole window;
- it keeps the visibility rules. The audio card is laid out only when the
  device selector exists and the overlay is visible, the other two whenever
  the overlay is visible - the same conditions as before;
- it applies the result with `setBounds()`.

## The shared rectangle

The engines are JUCE-free, so they cannot call `juce::Rectangle`'s
`removeFromTop()` and friends. `Source/LayoutRect.h` holds the plain
`upic::layout::Rect` and repeats the JUCE semantics of the rectangle
operations the engines need, including the clamping. The card engine, the
bottom engine and the overlay engine now share that one implementation, so
the three of them cannot drift apart.

## The rules that did not move

- the audio card's OK button sits at the card's bottom right, and the
  device selector fills the rest of the card;
- the settings card's OK and Apply share the card's bottom row, with the
  OK button to the right of the Apply button;
- the MIDI rows come first (device, activity, gain, interval, scrub,
  transport), then the OSC rows (section, status, on, port, multicast);
- the "clear page?" card's Cancel button is the rightmost one and the
  Confirm button is to its left;
- a card that is not visible is not laid out, so its widgets keep their
  previous bounds.

## The gate

`Source/SelfTestOverlayLayout.cpp` has 76 structural checks at four
viewports (1280×800, 800×600, 640×480, 460×360). The suite opens the
overlays the way the user opens them - the same methods the buttons call -
and checks the relations that hold: the card's buttons share the bottom row
and stay inside the card, the MIDI rows are stacked in order, the widgets of
a row share that row and stand left to right, the OSC section starts below
the MIDI block, and the card is the same when it is calculated again.

The four MIDI Learn buttons all carry the name "Learn", because the button
icons are chosen by the name. The suite tells them apart by the row they
are in, not by their order in the overlay.

R1.3 was additionally verified with the same temporary component-tree digest
as R1.1 and R1.2: every widget's bounds and visibility, hashed at the four
viewports. The four digests were recorded on the pre-extraction binary and
reproduced exactly by the post-extraction binary, then the digest was
removed. That comparison is a local proof, not a committed check.

## Follow-ups

- The design-unit numbers of the three cards are still literals in the
  engine. The review's "centralize layout metrics" (R055) is the place to
  collect them, together with the bottom engine's numbers, in one table.
- The card frame itself (the centred, maximum-size rectangle) is still the
  overlay component's own rule. It could move into the engine together with
  the contents, but the overlay paints it, so it belongs to the overlay.
- `Plugin/CMakeLists.txt` and `UPICuitous.jucer` list
  `OverlayLayoutEngine.cpp` explicitly; the Android generated source list
  picks it up on the next Projucer resave (the H5 wave owns that step).
- The include-boundary gate (R8) must treat `LayoutRect.h` and the three
  engine headers as JUCE-free.
