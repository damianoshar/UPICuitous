#pragma once

//==============================================================================
// R5.10 (the review's R052, section 28): the live input panel.
//
// The Live tab draws arcs that transpose the microphone. The panel is the
// input (the monitor, the capture, the input gain and the digitised wave
// page) and the Live tab's editor (the pitch algorithm, the granular grain and
// the reverse).
//
// The review asks for the live input controls to be decomposed:
//
//   "| `buildLiveInputControls()`                               | LiveInputPanel     |"
//   (section 28)
//
// This header is the panel's description: the controls it owns, the words of
// each one, the input gain's and the grain's domains, and the live pitch and
// reverse-buffer lists with their item ids.
//
// It holds no widget and no document. The app keeps its widgets (the material
// inspector already says which of them the Live tab shows, Source/
// MaterialInspector.h) and its commands (the input's preparation, the
// capture's start and stop, the pitch's edit, the reverse's undo transaction);
// it asks this header what they mean. Before, the gain's range was in the
// widget, the capture's "Capture"/"Stop" were written in three places, the
// pitch's three items and the buffer's three items were written where the
// lists were built and read as "id - 1" and as "1 -> 0.5, 3 -> 2, else 1",
// and the two grain sliders' range was written twice.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include <vector>

namespace upic::live_input
{
    /** A control of the live input panel. */
    enum class Control
    {
        Monitor,        // the input is monitored
        Capture,        // the input is captured into a slot
        InputGain,      // the input's gain
        WaveFromSlot,   // the slot becomes a wavetable
        Pitch,          // how the mic is transposed
        Grain,          // the granular grain size
        Reverse,        // the last seconds play backwards
        ReverseLength,  // how many seconds the reverse keeps
        count
    };

    /** Every control the panel owns. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::Monitor, Control::Capture, Control::InputGain,
            Control::WaveFromSlot, Control::Pitch, Control::Grain,
            Control::Reverse, Control::ReverseLength
        };
        return controls;
    }

    //---------------------------------------------------------------------------
    // The wording: what every control says about itself. A control that says
    // nothing returns an empty string (the app gives it no tooltip).
    inline const char* tooltip (Control control)
    {
        switch (control)
        {
            case Control::WaveFromSlot:
                return "Turn the selected slot into a wavetable\n"
                       "(the UPIC's digitised wave page: the take\n"
                       "becomes one loop of the oscillator, so its\n"
                       "pitch follows the arc).";
            case Control::Pitch:
                return "Live input pitch shift for LIVE-tab arcs.\n"
                       "Granular and Phase vocoder cover the full 1/16x - 16x range "
                       "(like the Sample tab); Tape (varispeed) reads the mic faster "
                       "than it arrives, so it keeps an octave up/down.";
            case Control::Grain:
                return "Granular grain size for the LIVE tab.\n"
                       "Only affects the Granular live pitch mode.";
            case Control::Reverse:
                return "Play the microphone's last seconds backwards";
            case Control::ReverseLength:
                return "How much of the microphone input the reverse plays back.\n"
                       "The longer the buffer, the longer the delay.";
            default:
                return "";
        }
    }

    /** The capture button while it is not recording. */
    inline const char* captureText()
    {
        return "Capture";
    }

    /** The capture button while it is recording. */
    inline const char* stopText()
    {
        return "Stop";
    }

    //---------------------------------------------------------------------------
    // The input gain's domain. 1 is the input as it arrives.
    constexpr double kInputGainMin = 0.0;
    constexpr double kInputGainMax = 1.0;
    constexpr double kInputGainInterval = 0.01;
    constexpr double kDefaultInputGain = 0.8;

    //---------------------------------------------------------------------------
    // The granular grain's domain - the same 5..500 ms for the sample and the
    // live material. The live grain starts at 40 ms (the hop the granular
    // shifter used before the control existed).
    constexpr double kGrainMinMs = 5.0;
    constexpr double kGrainMaxMs = 500.0;
    constexpr double kGrainIntervalMs = 1.0;
    constexpr double kDefaultGrainMs = 40.0;

    //---------------------------------------------------------------------------
    // The live pitch algorithm. The item ids are the engine's modes + 1, the
    // same rule the sample material's list follows.
    struct PitchMode
    {
        const char* name = "";
        int mode = 0;
    };

    inline const std::vector<PitchMode>& pitchModes()
    {
        static const std::vector<PitchMode> modes =
        {
            { "Granular (smooth)", 0 },
            { "Tape (varispeed)",  1 },
            { "Phase vocoder",     2 }
        };
        return modes;
    }

    /** The item id of a live pitch mode. */
    inline int pitchItemId (int mode)
    {
        return mode + 1;
    }

    /** The engine's mode an item id names. */
    inline int pitchModeForItemId (int itemId)
    {
        return itemId - 1;
    }

    /** The item the box starts on (granular). */
    constexpr int kDefaultPitchItemId = 1;

    //---------------------------------------------------------------------------
    // The reverse's buffer lengths. A longer buffer is a longer delay.
    struct ReverseLength
    {
        const char* name = "";
        double seconds = 0.0;
    };

    inline const std::vector<ReverseLength>& reverseLengths()
    {
        static const std::vector<ReverseLength> lengths =
        {
            { "0.5 s", 0.5 },
            { "1 s",   1.0 },
            { "2 s",   2.0 }
        };
        return lengths;
    }

    /** The item id of the reverse length at the given index in the list. */
    inline int reverseLengthItemId (int index)
    {
        return index + 1;
    }

    /** The seconds an item id names. An id the list does not carry is one
        second - the delay the reverse has always had. */
    inline double reverseLengthForItemId (int itemId)
    {
        const auto& lengths = reverseLengths();
        const int index = itemId - 1;
        return index >= 0 && index < (int) lengths.size()
                   ? lengths[(size_t) index].seconds : 1.0;
    }

    /** The item the box starts on (one second). */
    constexpr int kDefaultReverseLengthItemId = 2;
}
