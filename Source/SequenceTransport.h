#pragma once

//==============================================================================
// R5.8 (the review's R050, section 28): the sequence card and the performance
// transport.
//
// A performance sequence is the performer's movement through the score: the
// recorder captures (time, page, position) points and the transport replays
// them. Three surfaces carry its switches:
//
// - the toolbar's sequence card: the selector, Record, Play, Loop and View;
// - the sequence panel: the same trio, plus the panel's Trim, Replace, Clear
//   and Close;
// - the performance view: its page, scrub, reverse, page-loop and tempo
//   switches, and the same sequence trio.
//
// The review asks for the sequence and the performance controllers to be
// extracted:
//
//   "| `showPerformanceView()`, `refreshPerformanceTransport()` | PerformanceController |"
//   "| `showSequencePanel()`, `sequenceRecordTick()`            | SequenceController    |"
//   "| `startSequencePlayback()`, `stopSequencePlayback()`       | SequenceTransport     |"
//   (section 28)
//
// This header is the first step, the same step R5.6 and R5.7 took for the
// inspectors: the *description* and the *transport's words* leave the shell.
// The widgets stay the app's (the app owns them and binds the roles), and so
// do the commands: the recorder, the model's flag, the clock and the undo
// transaction stay in MainComponent.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include "InspectorCell.h"

#include <vector>

namespace upic::sequence_transport
{
    using Kind = upic::inspector::Kind;

    /** A control of the toolbar's sequence card. */
    enum class Control
    {
        Sequence,   // the performance sequence selector
        Record,     // start / stop the recorder
        Play,       // start / stop the replay
        Loop,       // the selected sequence's loop flag
        Open,       // the sequence panel
        count
    };

    /** One cell of the sequence card. */
    struct Cell
    {
        Control control = Control::Sequence;
        Kind kind = Kind::Plain;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the sequence card says about itself. */
    struct Description
    {
        const char* name = "";    // the card's name
        std::vector<Cell> card;   // the card's cells, in order
    };

    /** The sequence card's description. */
    inline const Description& describe()
    {
        static const Description description =
        {
            "Sequence",
            {
                // the selector and the four switches
                { Control::Sequence, Kind::Inline, 30, 110 },
                { Control::Record,   Kind::Plain,  0,  52 },
                { Control::Play,     Kind::Plain,  0,  54 },
                { Control::Loop,     Kind::Plain,  0,  52 },
                { Control::Open,     Kind::Plain,  0,  54 }
            }
        };
        return description;
    }

    /** Every control the card carries. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::Sequence, Control::Record, Control::Play,
            Control::Loop, Control::Open
        };
        return controls;
    }

    //---------------------------------------------------------------------------
    // The transport's words.
    //
    // The same switch says the same thing on every surface: the card, the
    // sequence panel and the performance view all ask for the tooltip and the
    // label here, so they cannot disagree about what Record, Play or Loop
    // means. Before, the three surfaces said three different things about
    // Record, two about Loop, and the panel spelled "Record" while the other
    // two said "Rec".
    inline const char* tooltip (Control control)
    {
        switch (control)
        {
            case Control::Sequence:
                return "The performance sequence";
            case Control::Record:
                return "Record the movement through the score (page + playhead)";
            case Control::Play:
                return "Replay the recorded performance";
            case Control::Loop:
                return "Loop the performance";
            case Control::Open:
                return "Open the sequence panel";
            default:
                return "";
        }
    }

    /** The label a Record switch shows: the recorder's state names it. */
    inline const char* recordButtonText (bool recording)
    {
        return recording ? "Stop" : "Rec";
    }

    /** The label a Play switch shows: the transport's state names it. */
    inline const char* playButtonText (bool playing)
    {
        return playing ? "Stop" : "Play";
    }

    //---------------------------------------------------------------------------
    // What a Record or Play switch does when it is pressed.
    //
    // Record and Play are toggles: the same switch starts the run and ends it.
    // All three surfaces ask this rule; before, the "if running, stop" test
    // was copied at every call site, and the three copies could drift.
    enum class Action { Start, Stop };

    inline Action recordAction (bool recording)
    {
        return recording ? Action::Stop : Action::Start;
    }

    inline Action playAction (bool playing)
    {
        return playing ? Action::Stop : Action::Start;
    }

    //---------------------------------------------------------------------------
    // The page duration a surface assumes when it has no duration to show.
    // The model's own default is the same number (Page::durationSec); the
    // panels used to spell it out twice.
    constexpr double kDefaultPageSeconds = 30.0;

    //---------------------------------------------------------------------------
    // The tempo control's domain. The toolbar's tempo knob and the performance
    // view's tempo slider are the same control, so they cover the same range
    // in the same steps; before, the view's slider offered half-BPM steps and
    // no skew while the knob offered whole BPM and a skew.
    constexpr double kMinTempoBpm = 40.0;
    constexpr double kMaxTempoBpm = 240.0;
    constexpr double kTempoIntervalBpm = 1.0;
    constexpr double kTempoSkew = 0.5;

    inline const char* tempoTooltip()
    {
        return "The page's tempo in beats per minute";
    }
}
