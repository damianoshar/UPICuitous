#pragma once
#include <JuceHeader.h>
#include "UpicModel.h"
#include <optional>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

//==============================================================================
// Score page canvas: X = time (0..page.duration), Y = pitch log (20Hz..12kHz).
// Tools: Draw (free), Line, Sine, Select/Move, Erase, Sieve zone.
// Left-drag draws with current waveform/envelope/intensity.
//
// Navigation (UPISketch-style wheel behaviour):
//   Ctrl+wheel  = zoom time around cursor      Alt+wheel = zoom pitch around cursor
//   Shift+wheel = pan time                     Wheel     = pan pitch
//
// Selection/editing: right-click = context menu (Duplicate/Mute/Remagnetize/
// Delete/Select all), Shift-click cycles overlapping gestures, Ctrl+A = select
// all, Ctrl+D = duplicate, Del = delete selection, arrow keys nudge the
// selection (Shift = grid-sized / octave steps).
//
// Magnetic sieves (UPISketch 4.5): global sieve + optional translucent ZONES
// drawn with the Zone tool; inside a zone, points snap to the zone's sieve and
// time grid. A stickiness slider softens every snap (0 = free, 1 = magnetic).
//
// A thin line across the plot centre scrubs the playhead when dragged.
//==============================================================================
class ScoreEditor : public juce::Component, public juce::Timer
{
public:
    enum Tool { Draw = 0, Line, SineStroke, Select, Erase, Zone };

    ScoreEditor();

    // v1.33: dragging a zone - moving it, or resizing it by an edge or a
    // corner. The Zone tool used to let you draw a zone and delete it, but
    // not move it or change it - a zone you drew in the wrong place had to
    // be deleted and drawn again.
    bool draggingZone = false;
    int  zoneHandle = 0;          // 0 = move, 1..4 = an edge, 5..8 = a corner
    int  zoneDragId = -1;
    juce::Point<float> zoneDragStart;
    double zoneDragT0 = 0.0, zoneDragT1 = 0.0, zoneDragF0 = 0.0, zoneDragF1 = 0.0;

    // non-owning bindings (set by MainComponent)
    void bind(upic::Page* p, std::vector<upic::Waveform>* w, double* playhead,
              std::function<void()> changed = nullptr);

    // v1.18: while the transport plays, the audio thread owns the playhead
    // and publishes it through MainComponent - the getter lets the editor
    // draw that value, the seek hook tells the audio thread the playhead
    // moved (scrub). Both optional: without them the bound double is used.
    void setPlayheadSource(std::function<double()> getter, std::function<void(double)> seek);

    // Optional: preview audio while scrubbing (used by MainComponent).
    std::function<void(double)> onScrubPreview;
    std::function<void(double)> onScrubFreezeStart;
    std::function<void()> onScrubFreezeStop;
    std::function<void()> onScrubFreezeCancel;
    std::function<void(double)> onScrubDragRelease;
    // The status timer copies the model under this lock to build the audio
    // snapshot; every mutation done by the editor must hold it too, or the
    // snapshot copy can hit a reallocated vector (crash).
    void setModelLock(juce::CriticalSection* l);

    // v1.19: the scrub line runs across the middle of the canvas, so the
    // target is a narrow band at its TOP - a drawing stroke that starts in
    // the middle of the canvas must not move the playhead. Public so the
    // selftest can check the geometry.
    juce::Rectangle<float> timeStripBounds() const;

    juce::Rectangle<float> scrubTargetRect() const;

    // v1.25: the playhead's top triangle is also a scrub handle, so the
    // playhead can be dragged directly. Public for the selftest.
    juce::Rectangle<float> playheadHandleRect() const;

    void setTool(Tool t);

    Tool getTool() const;

    void setCurrentVoice(int wid, int eid, float inten,
                         int matMode = 0, int sampleId = 1, double refFreq = upic::kDefaultRefFreq);

    // v1.32: the discrete frequency space. Like the PM controls it is
    // per-arc, but a new arc must be born with the value currently shown in
    // the toolbar (the same "what you see is what gets drawn" rule).
    void setCurrentStep(int divisions);

    // v1.33: the frequency space a new arc is born in (the UPIC's four
    // tables). Same rule as the step: what you see is what gets drawn.
    void setCurrentSpace(int space);

    int getCurrentSpace() const;

    // The PM controls are per-arc, but a new PM arc must be born with the
    // values currently shown in the toolbar (the same "what you see is what
    // gets drawn" rule as wave / env / intensity / Ref).
    void setCurrentPmVoice(double ratio, double index, int indexEnv,
                           double feedback, int ops);

    // --- selection ---
    // selection is layer-aware: in the live tab only live arcs are selectable
    void setSelectedArc(int id);

    void setSelectedArcs(const std::vector<int>& ids);

    int getSelectedArc() const;

    const std::vector<int>& getSelectedArcs() const;

    void selectAll();

    int getSelectedZone() const;

    // Zone-menu action ids. applyZoneMenu() is the single place that mutates
    // a zone from the menu, so the menu cannot silently do nothing - the
    // selftest drives the same function.
    static constexpr int zoneMenuDelete       = 1;
    static constexpr int zoneMenuDeleteAll    = 2;
    static constexpr int zoneMenuSieveCurrent = 10;
    static constexpr int zoneMenuSieveOff     = 11;
    static constexpr int zoneMenuSievePreset  = 12;  // + preset index
    static constexpr int zoneMenuGridCurrent  = 30;
    static constexpr int zoneMenuGridOff      = 31;
    static constexpr int zoneMenuGridChoice   = 32;  // + index in grid choices
    static constexpr int zoneMenuStickFollow  = 40;
    static constexpr int zoneMenuStickCurrent = 41;
    static constexpr int zoneMenuStickChoice  = 42;  // + index in stick choices

    // v1.33: the selftest drives the zone drag directly (a synthesised
    // mouse event needs a real MouseInputSource).
    juce::Rectangle<float> zoneRectForTest (int id) const;

    double zoneTimeForTest (int id, int which) const;

    void beginZoneDragForTest (juce::Point<float> pos);

    void dragZoneToForTest (juce::Point<float> pos);

    // v1.33: the Line tool's points must stay sorted after the quantisers
    // have moved them (the engine's segment search assumes it). The
    // staircase's corners share a time, so the order is non-decreasing.
    bool commitLineForTest (juce::Point<float> p0, juce::Point<float> p1);
    bool commitStrokeForTest (const std::vector<juce::Point<float>>& points);

    bool lastArcPointsSortedForTest() const;

    // A staircase has a corner: two points at the same time, one on the
    // old step and one on the new.
    bool lastArcHasStaircaseForTest() const;

    std::vector<double> lastArcFreqsForTest() const;

    // F6: how many points the last committed stroke kept (the
    // simplification's tolerance depends on the view).
    int lastArcPointCountForTest() const;

    // v1.33.2: the zone menu actions are testable directly.
    void applyZoneMenuForTest (int id, int action);

    static juce::String remaindersTextForTest (const std::vector<int>& r);

    int zoneSieveMForTest (int id) const;

    std::vector<int> zoneSieveRForTest (int id) const;

    double zoneTimeGridForTest (int id) const;

    double zoneStickinessForTest (int id) const;

    void setSnap(bool s);

    void setShowGrid(bool s);

    // v1.8: one tab strip - each material is a canvas layer (0 osc, 1 sample,
    // 2 live). Same page and timeline as before (time zoom/scroll/playhead
    // are shared, so the tabs stay synchronised), but each tab draws and
    // edits only its own arcs: osc = mode 0, sample = mode 1, live = mode 2.
    // The live layer labels the pitch grid in Hz, osc/sample show note names.
    void setLayer(int m);

    int getLayer() const;

    // v1.7: the instruction lines can be hidden (Hints view toggle)
    void setShowHints(bool on);

    bool getShowHints() const;

    // v1.1: pen pressure -> per-point intensity multiplier (i, default 1)
    void setRecordPressure(bool on);

    bool getRecordPressure() const;

    // v1.1: Xenakis sieve quantizer applied at draw time (takes precedence over snap)
    void setSieve(bool on, int m, std::vector<int> remainders);

    bool getSieveOn() const;

    int getSieveM() const;

    const std::vector<int>& getSieveR() const;

    // v1.2: magnetism controls (UPISketch-style)
    void setStickiness(double s);

    double getStickiness() const;

    void setTimeGrid(double div);

    double getTimeGrid() const;

    // M4: the tuning the drawing snaps to. The default (no tuning) is
    // 12-tone equal temperament - the snapping is then exactly what it
    // was before.
    void setTuning (const upic::Tuning& t);

    const upic::Tuning& getTuning() const;

    // --- coordinate mapping (through the view: timeZoom/timeOffset,
    //     freqZoom/freqOffset) ---
    double xToTime(float x) const;

    float timeToX(double t) const;

    // v1.33: the page's ambitus - the interval its vertical axis covers.
    // "The scale of the millimetric surface could be selected, so that the
    // vertical axis could correspond to any interval" (Mache).
    double ambitusLo() const;

    double ambitusHi() const;

    double yToFreq(float y) const;

    float freqToY(double f) const;

    // F4/F6: the view's zoom and scroll state. It is session state (the
    // focus view must not change it, and the piece never carries it).
    double getTimeZoom() const { return timeZoom; }
    double getTimeOffset() const { return timeOffset; }
    double getFreqZoom() const { return freqZoom; }
    double getFreqOffset() const { return freqOffset; }

    // --- F6 (2026-11-17): the view commands and the drawing precision ---
    // The commands work on the same zoom/offset fields the cursor-anchored
    // wheel zoom uses, so the anchoring still works after them.
    void zoomIn();
    void zoomOut();
    void fitPage();       // the whole page, from its start
    void fitSelection();  // the selection fills the view
    void resetZoom();     // back to 1x, the view's centre stays
    void zoomBy (double factor);

    // The Fine drawing mode: the stroke is not simplified, so every
    // sampled point is kept.
    void setFineDrawing (bool on);
    bool getFineDrawing() const { return fineDrawing; }

    // The numeric edit of the selection: it moves the selected arcs the
    // same way a drag does (a time shift and a frequency ratio), so the
    // undo transaction and the model revision see it.
    bool selectionAnchor (double& tSec, double& hz) const;
    bool setSelectionTime (double tSec);
    bool setSelectionFreq (double hz);

    // The live readout: the cursor's position in the editor's own mapping.
    std::function<void(double tSec, double fHz)> onCursorMove;
    // The view changed (the app refreshes its view bar).
    std::function<void()> onViewChanged;

    // --- quantization used by draw/line/remagnetize (zone-aware, sticky) ---
    void quantizeXY(double& t, double& f) const;

    // --- editing operations (also used by the context menu / shortcuts) ---
    void deleteSelected();

    void duplicateSelected();

    void toggleMuteSelected();

    // "Remagnetize" (UPISketch): re-apply the current sieve/zone/grid to all
    // points of the selection (e.g. after changing the sieve).
    void remagnetizeSelected();

    // Arrow-key nudge of the selection: dt seconds (Shift = grid-sized step)
    // and +/-1 semitone via the next magnetically allowed pitch (Shift = octave).
    void nudgeSelected(double dt, int dSemi);

    // Zone tool: create a magnetic region from two corner times/freqs.
    // The zone inherits the current sieve (m/R), time grid and stickiness.
    int addZone(double ta, double tb, double fa, double fb);

    void deleteZone(int id);

    // Time-strip scrub: set the playhead from a canvas x position.
    void scrubTo(float x);

    // v1.29: scrub to an absolute time. The external controller (phone/OSC)
    // uses this too, so it goes through exactly the same seek + preview path.
    void scrubToTime(double t);

    // v1.29: an external controller behaves like a scrub drag. Positions keep
    // arriving while the phone slider moves; MainComponent ends the gesture
    // after a short timeout, so releasing never starts playback.
    void externalScrubStart(double t);

    void externalScrubTo(double t);

    void externalScrubEnd();

    void timerCallback() override;

    //==========================================================================
    void paint(juce::Graphics& g) override;

    void resized() override;

    //==========================================================================
    void mouseDown(const juce::MouseEvent& e) override;

    void mouseDrag(const juce::MouseEvent& e) override;

    void mouseUp(const juce::MouseEvent& e) override;

    // F6: the live readout of the cursor's position.
    void mouseMove(const juce::MouseEvent& e) override;

    bool keyPressed(const juce::KeyPress& k) override;

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    std::function<void(int)> onSelect;
    std::function<void(bool)> onArcEditingChanged;
    // v1.33.4 (L15, part 2): the undo transaction. The editor tells the app
    // when a change starts - the app keeps the state from before it - and
    // when it is over; the app keeps that state only if something really
    // changed. A pointer drag is one transaction, so a drag is one undo step
    // (the review: "coalesce pointer drags into one undo action").
    std::function<void (const juce::String& what)> onEditStart;
    std::function<void()> onEditEnd;
    // v1.33.4 (L15, part 2): the undo and redo keys (Ctrl+Z / Ctrl+Shift+Z).
    // The editor has the keyboard focus while the user draws, so it hands
    // the keys to the app.
    std::function<void()> onUndo;
    std::function<void()> onRedo;
    // v1.33.4 (M22): a lightweight "the model changed" hook. It is called
    // while a drag edits the model in place, where the full onChange (which
    // refreshes every combo box) would be far too heavy to run on every
    // mouse move. The app uses it to mark the model as changed, so the
    // status timer publishes the new model.
    std::function<void()> onModelEdited;
    // v1.33.4 (H13): a short message for the user - the editor refuses to
    // create something the piece format cannot hold, and says so instead of
    // letting the loader drop it later.
    std::function<void(const juce::String&)> onNotice;
    // sample-duration snapping (granular mode)
    std::map<int,double> sampleDurations;
    bool sampleGranularTab = false;
    void setPlaying(bool p);

private:
    upic::Page* page = nullptr;
    std::vector<upic::Waveform>* waves = nullptr;
    double* playheadSec = nullptr;
    // v1.18: the playhead the audio thread owns is read through the getter;
    // scrubbing goes back through the seek hook.
    std::function<double()> playheadGetter;
    std::function<void(double)> onSeek;

    double playheadNow() const;

    std::function<void()> onChange;
    juce::CriticalSection* modelLock = nullptr;
    juce::CriticalSection dummyLock; // used when no model lock was provided
    Tool tool = Draw;
    int curWave = 0, curEnv = 0;
    float curInten = 0.8f;
    int curMatMode = 0, curSampleId = 1;
    double curRef = upic::kDefaultRefFreq;
    double curPmRatio = 2.0, curPmIndex = 1.5, curPmFeedback = 0.0;
    int curPmIndexEnv = 0, curPmOps = 2;
    int curStep = 0;             // v1.32: the discrete frequency space (0 = continuum)
    int curSpace = 0;            // v1.33: the frequency space (0..3)
    std::vector<int> selArcs;   // multi-selection (sorted by insertion)
    int selectedZone = -1;
    bool snap = true, showGrid = true, isPlaying = false;
    int layer = 0;               // v1.8: 0 osc / 1 sample / 2 live - the tab is the material
    bool showHints = true;       // v1.7: instruction lines on/off
    double timeZoom = 1.0, timeOffset = 0.0;
    double freqZoom = 1.0, freqOffset = 0.0; // pitch view window in norm space
    bool fineDrawing = false;   // F6: the stroke is not simplified

    bool drawing = false, movingArc = false;
    bool selectingMarquee = false, marqueeAdditive = false;
    juce::Point<float> marqueeStart, marqueeCur;
    bool drawingZone = false, scrubbing = false, scrubDragged = false, freezeAfterDrag = false;
    double lastDragMs = 0.0;
    struct StrokePt { juce::Point<float> pos; float press = 1.0f; };
    std::vector<StrokePt> strokePts;
    std::optional<juce::Point<float>> lineAnchor;
    juce::Point<float> lastMouse, moveStartPos, zoneStart, zoneCur;
    std::map<int, std::vector<upic::ArcPoint>> moveStartMap;

    // --- multi-touch view gesture (pinch = zoom, 2-finger drag = pan) ---
    struct TouchPt { int id; juce::Point<float> pos; };
    std::vector<TouchPt> touches;
    bool pinchMode = false;
    bool touchSeen = false;              // a touch pointer has used this editor
    juce::Point<float> pinchCentroid0, touchDownPos;
    float pinchDist0 = 1.0f;
    double pinchTime0 = 0.0, pinchNorm0 = 0.0;
    double pinchTimeZoom0 = 1.0, pinchFreqZoom0 = 1.0;
    unsigned int touchDownMs = 0;

    bool recordPressure = false;
    bool sieveOn = false;
    int sieveM = 12;
    std::vector<int> sieveR { 0 };
    double stickiness = 1.0; // 0..1 magnetic strength
    double timeGrid = 0.0;   // divisions per second, 0 = off
    // M4: the tuning the drawing snaps to (12-TET by default).
    upic::Tuning tuning;
    static constexpr double kSimplifyEps = 0.006; // RDP tolerance (normalized box)
    // F6: the tolerance never falls below this (a stroke always keeps a
    // reasonable shape, however far the view is zoomed in).
    static constexpr double kMinSimplifyEps = 0.0004;

    // pen pressure of an event -> intensity multiplier (1 = no pressure info)
    static float pressureOf(const juce::MouseEvent& e);

    float curPress(const juce::MouseEvent& e) const;

    juce::Rectangle<float> plotArea() const;

    juce::Rectangle<float> timeStripRect() const;

    // The scrub target is the TOP of the line. It runs across the middle of
    // the canvas (the pitch-centre line), so a drawing stroke that starts
    // there used to scrub instead of draw - and the grab band reached 10 dp
    // beyond the strip, straight into the drawing area. The target is now a
    // narrow band at the top of the line and it never leaves it, so the
    // canvas stays free for drawing.
    juce::Rectangle<float> timeStripHitRect() const;

    //==========================================================================
    // multi-touch helpers
    void upsertTouch(const juce::MouseEvent& e);

    void removeTouch(int id);

    juce::Point<float> touchCentroid() const;

    float touchSpread() const;

    // A 2nd finger cancels whatever the 1st was doing (no half-drawn arcs),
    // then the pair becomes a view gesture: pinch = zoom (anchored at the
    // original centroid), centroid drag = pan. Mirrors the wheel bindings.
    void cancelInteraction();

    void beginPinch();

    void updatePinch();

    // long-press menu: arc menu on an arc, zone menu inside a zone,
    // otherwise the page-level "select all" menu
    void showTouchMenu(juce::Point<float> pos);

    juce::Rectangle<float> zoneScreenRect() const;

    juce::Rectangle<float> zoneRect(const upic::SieveZone& z) const;

    juce::String toolName() const;

    static double niceStep(double raw);

    bool arcInLayer(const upic::Arc& a) const;

    bool arcInLayer(int id) const;

    bool isSel(int id) const;

    juce::Rectangle<float> marqueeRect() const;

    int curMode() const;

    upic::Arc* findArc(int id);

    // All arcs whose polyline passes within maxD pixels of pos, nearest first.
    // Touch pointers get a wider grab band (fat fingers, no hover).
    std::vector<int> arcsNear(juce::Point<float> pos, float maxD = -1.0f) const;

    int pickArc(juce::Point<float> pos);

    int pickZone(juce::Point<float> pos) const;

    // v1.33: the zone by id.
    upic::SieveZone* findZone (int id);

    // v1.33: which edge or corner of the zone is under the cursor?
    // 0 = none (move the whole zone), 1 = left, 2 = right, 3 = top,
    // 4 = bottom, 5..8 = the corners (top-left, top-right, bottom-left,
    // bottom-right).
    int zoneHandleAt (const upic::SieveZone& z, juce::Point<float> pos) const;

    // v1.33: start a zone drag - the zone under the pointer is moved (or
    // resized by the edge or corner under the pointer).
    void beginZoneDrag (juce::Point<float> pos);

    // v1.33: the drag itself.
    void dragZoneTo (juce::Point<float> pos);

    void showArcMenu(int id);

    // Zone-menu action ids are declared with the public test hooks above.

    static juce::String remaindersText (const std::vector<int>& r);

    void showZoneMenu(int id);

    void applyZoneMenu (int id, int action);

    // Active pitch magnet at (t, f): zone > sieve > chromatic snap.
    // Returns false when pitch is free (no snap / inert sieve).
    bool pitchMagnet(int& m, std::vector<int>& R, double t, double f) const;

    // Next magnetically allowed pitch strictly above (dir>0) or below (dir<0)
    // the current one — so arrow steps always land ON the sieve.
    // v1.33: the steps stay inside the page's ambitus, like the drawing.
    double nextAllowedPitch(double f, int dir, double t) const;

    double nudgePitch(double t, double f, int dSemi) const;

    // v1.33.4 (L12): the page's time resolution. The minimum spacing between
    // two points of a stroke used to be a fixed 15 ms (and a line 50 ms), so
    // on a micro-time page (the shortest page the app allows is 5 ms) a
    // stroke kept a single point and the arc was dropped. The thresholds
    // scale with the page: a long page keeps the old values, a short one
    // gets a finer resolution. The stroke sampler (3 px) already bounds the
    // point count, and the piece format caps the points per arc anyway.
    double pageTimeResolution() const;

    double minPointSpacingSec() const;

    double minLineLengthSec() const;

    // v1.33: the quantisers, applied to a finished stroke or a finished line.
    //
    // The sieve (and the chromatic snap, and a zone's own sieve) quantise
    // the drawn curve into STEPS. The UPIC's pen stayed at the grid pitch
    // and jumped at the corner, so the drawing - and the sound - were a
    // staircase. Here the points became the snapped pitches joined by
    // straight ramps, which is "just lines connected to each other", not
    // stairs. The points are now the step corners: the pitch HOLDS until
    // the next step, and the jump is two points at the same time (the
    // engine's segment search reads a zero-width segment as the jump).
    //
    // The time grid quantises the times, and it can pull a later point in
    // front of an earlier one: the points have to be re-sorted, and the
    // ones that landed on the same time merged, or the engine's segment
    // search (which assumes they are sorted) reads the wrong segment and
    // the curve comes out distorted.
    void quantiseArcPoints (upic::Arc& a, const std::vector<bool>& magnetised);

    // Was the pitch at (t, f) pulled toward a magnet? (The sieve, a zone's
    // sieve, or the chromatic snap.) Asked BEFORE the quantisation, at the
    // drawn position.
    bool pitchMagnetAt (double t, double f) const;

    bool commitStroke();

    bool commitLine(juce::Point<float> p0, juce::Point<float> p1, float press = 1.0f);

    void notify();

    // v1.33.4 (H13): tell the user why an edit was refused.
    void notice (const juce::String& m);

    // v1.33.4 (H13): the page's arc count is capped by the piece format
    // (kMaxArcsPerPage). The editor refuses the arc that does not fit, so
    // the app never writes a piece it cannot read back.
    bool atArcLimit() const;

    bool atZoneLimit() const;

};
