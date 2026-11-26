# The bottom layout engine

The architecture note for the boundary R1.2 introduced. It describes the
tree as it is after that phase, not as it is meant to be at the end of the
refactoring.

## The boundary

```
MainComponent::resized()                 Source/BottomLayoutEngine.{h,cpp}
  builds BottomLayoutConstraints          upic::layout::layoutBottom()
  (area, scale, editor flags,      --->   BottomLayoutResult
   landscape, arc/workspace flags,
   split, preset ideal widths)
                                   <---   rectangles for the view bar, the
  applies setBounds() / setVisible(),     editor columns, the voice controls,
  PageWorkspace::boundsForCanvas(),       the tab strip, the arc list, the
  splitter setArea(), canvas origin       canvas and the splitter
```

The engine owns the arithmetic of the whole bottom half: the editor height
budget (the 150/110/300/240 dp rules, the 42% cap and the 6 px gap), the view
bar's row, each editor column's bank-manager row, preset row, focus button,
ideal-width distribution and editor area, the voice strip/column geometry,
the tab strip's left and right placement, the arc list's width rule
(300 dp, but the canvas keeps 240 dp) and the workspace splitter's position.
It is a pure function of the specification: no globals, no
`juce::Component`, no fonts and no JUCE allocation.

`MainComponent` keeps the JUCE side and is the compatibility seam:

- it supplies the UI state as plain flags (`showWaveEd`, `showEnvEd`,
  `showArcList`, `landscape`, the workspace split) and the preset buttons'
  ideal widths in design units;
- it applies the result: `setBounds` for the view bar, the editor widgets and
  their library managers, the preset and focus buttons, the voice controls,
  the material tabs, the arc list and the workspace;
- it keeps `PageWorkspace::boundsForCanvas()` and the canvas translation,
  because that is a component's own two-canvas rule, not a window layout;
- it keeps `layoutOverlays(s)` and the final `repaint()`.

The focus views use the same engine pieces directly: `layoutEditorColumn()`
for the focused wave/envelope editor and `layoutTabStrip()` for the score's
tab row. They do not call `layoutBottom()`, because a focus view deliberately
bypasses the editor budget and the voice controls.

## The rules that did not move

The bottom layout has a few behaviours that are not geometry but that the
adapter must keep, and the characterization suite pins them:

- when the tab strip is too narrow for PM, `matPmTog` is hidden and keeps its
  previous bounds;
- when there is no room for the score's focus button, it is hidden and keeps
  its previous bounds;
- when the tab strip is full, the arc-list toggle is hidden and keeps its
  previous bounds;
- when there is no room for the workspace toggle, it is given empty bounds
  (which is how the app has always expressed "gone");
- a closed workspace is given empty bounds, the splitter is hidden without
  being moved, and the score takes the canvas area;
- the view bar's row comes out of the editors' height when they are visible
  (so the canvas keeps its space) and out of the canvas's own area when they
  are not.

## Why the engine is JUCE-free

`BottomLayoutEngine.cpp` repeats the clamping of `juce::Rectangle::reduced`
and `removeFromTop/Bottom/Left/Right`; that is the only way to promise the
same pixels without including JUCE. Every constant stays in design units and
is scaled where the old code scaled it, expression for expression. The input
is plain integers and floats, so R8 can test the engine headless.

## The gate

`Source/SelfTestBottomLayout.cpp` has 71 structural checks at seven
viewports: the five R0 viewports (1600×1000, 1280×800, 1024×700, 800×600,
460×360) plus 700×900 (a wide portrait window where the editors still fit
side by side) and 460×1400 (a tall portrait window where they stack). The
window is tall on purpose: at 460×800 the toolbar's fit-shrink drops the
layout scale to 0.55, which also shrinks the 460 dp side-by-side threshold,
so the editors do not stack there. At 460×1400 the scale stays at 1.0 and the
stacked branch is actually reached.

The suite checks relations, not golden pixels: the canvas has room, the view
bar sits under it, each library row sits above its editor, the editors share
or stack, the voice controls form one strip in landscape and a column in
portrait, the arc list does not cover the canvas, the workspace splitter
stands at the split and its canvases tile the area, and the layout is the
same when it is calculated twice.

R1.2 was additionally verified with the same temporary component-tree digest
as R1.1: every descendant's bounds and visibility, hashed at the seven
viewports. The seven digests were recorded on the pre-extraction binary and
reproduced exactly by the post-extraction binary, then the digest was
removed. That comparison is a local proof, not a committed check.

## Follow-ups

- `PageWorkspace::boundsForCanvas()` stays component-side; a later UI
  decomposition (R5) may move it with the workspace itself.
- The focus path still has its own view-bar row and tab row setup; it is
  small and it is not the same budget as the normal layout, so it was not
  folded into `layoutBottom()`.
- `Plugin/CMakeLists.txt` and `UPICuitous.jucer` list `BottomLayoutEngine.cpp`
  explicitly; the Android generated source list picks it up on the next
  Projucer resave (the H5 wave owns that step).
- The include-boundary gate (R8) must treat `BottomLayoutEngine.h` as
  JUCE-free, like `CardLayoutEngine.h`.
