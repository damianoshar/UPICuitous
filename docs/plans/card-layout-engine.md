# The card layout engine

The architecture note for the boundary R1.1 introduced. It describes the
tree as it is after that phase, not as it is meant to be at the end of the
refactoring.

## The boundary

```
MainComponent::resized()                 Source/CardLayoutEngine.{h,cpp}
  builds FlowItem / FlowGroup             upic::layout::CardSpecification
  (widget pointers, design units)  --->   CardLayoutConstraints (area, scale,
  measures the text in dp                 LayoutMetrics)
  hands over a plain spec
                                    <---  CardLayoutResult / StripLayoutResult
  applies setBounds(), visibility,        (pixel rectangles, cell order,
  knob caption / design scale             hidden labels, segments)
```

The engine owns every piece of arithmetic that turns a card into
rectangles: the card's own width and height, the packing of its cells into
lines, the two-column span of the Pitch card, the header, the permanent
strip's flow and its per-line segments. It is a pure function of the
specification: no globals, no `juce::Component`, no fonts, no allocation
beyond the result vectors.

`MainComponent` keeps the JUCE side and is the compatibility seam:

- it measures `neededWidth`, the knob caption and the header text (the
  three places a font is involved) and puts the numbers in the spec;
- it resolves `upic::Knob`'s cell metrics into `LayoutMetrics`, so the
  engine needs no `Knob.h`;
- it applies the result: `setBounds` for labels and controls, the card
  header, `setVisible(false)` for a knob's label, `setDesignScale` /
  `setCellCaption` / `setFallback` for the knob itself;
- it keeps the group names and the toolbar colour counter. The engine
  reports indices; the adapter maps them back.

## What did not move

The build model `FlowItem` / `FlowGroup` stays in `resized()`: it holds
`juce::Component*`, which is exactly what the engine must not see. The
builders `F`, `FL`, `FI`, `FX`, `FK`, `neededWidth`, `captionWidthDp` and
`headerWidthDp` are adapter code for the same reason. The engine receives
the resolved numbers in `CellSpecification`.

R5.6 gave the material card its *contents* an edge of their own
(`Source/MaterialInspector.h`): which cells the material card holds, in
which order and with which widths. R5.7 gave the same to the sieve card, the
magnets card, the page card, the pitch card's frequency cell and the
acceleration card (`Source/SievePanel.h`, `Source/FrequencyInspector.h`),
and the way a cell is drawn now lives in one vocabulary
(`Source/InspectorCell.h`) that the three descriptions share. R5.8 gives the
sequence card the same treatment (`Source/SequenceTransport.h`), and R5.9
gives the channel and the modulation cards theirs
(`Source/ChannelControls.h`, `Source/ModulationControls.h`). The engine
keeps the geometry - the packing of the cells into lines, and its rule that
the plain controls come first and the knobs after them, so the card's
reading order is not the description's order. See
[`material-inspector-edge.md`](material-inspector-edge.md),
[`sieve-panel-edge.md`](sieve-panel-edge.md),
[`frequency-inspector-edge.md`](frequency-inspector-edge.md),
[`sequence-transport-edge.md`](sequence-transport-edge.md),
[`channel-controls-edge.md`](channel-controls-edge.md) and
[`modulation-controls-edge.md`](modulation-controls-edge.md).

The strip's cell rectangles are engine output too: `layoutStrip()` returns
per-group cells and per-line segments, and the adapter pushes the segments
into `toolbarGroups` with the group's name and colour.

## Why the specification is plain ints

The engine's input has to survive a move to a headless target later (R8):
plain design-unit integers can be built in a test with no window, no font
and no JUCE message loop. That is also why the text widths are inputs, not
measurements: a test can pin a spec whose labels are 40 dp wide without
depending on the machine's fonts.

## The gate

`Source/SelfTestLayout.cpp` (R0.1, 30 checks) is the contract: the card rank
order, the placement order, no overlap, the cards and controls inside the
window and the toolbar, determinism across a repeated layout and a resize
round trip, the document untouched, and the permanent strip at five
viewports.

Because those checks are structural (a golden pixel rectangle depends on the
installed fonts), R1.1 was additionally verified with a temporary
component-tree digest: a walk of every descendant's bounds and visibility
plus every card rectangle, hashed at the five viewports. The digest was
recorded on the pre-extraction binary and compared with the post-extraction
binary; all five values were identical. The digest was then removed. That
comparison is a local proof, not a committed check.

## Follow-ups

- `layoutOverlays()` and the bottom half of `resized()` (the editors, the
  tab strip, the voice strip, the workspace) still compute their own
  geometry; they are the next R1 candidates.
- The Android exporter's generated `Builds/Android/app/CMakeLists.txt` lists
  sources explicitly. It picks up `CardLayoutEngine.cpp` on the next
  Projucer resave (the Android wave, H5, owns that step).
- The include-boundary gate (R8) should treat `CardLayoutEngine.h` as the
  first file that must never include a JUCE header.
