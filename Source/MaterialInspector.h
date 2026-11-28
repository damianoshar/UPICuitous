#pragma once

//==============================================================================
// R5.6 (the review's R048, section 28): the material inspector.
//
// The score canvas's material is one of four tabs - Osc, Sample, Live, PM -
// and each tab has its own controls in the material card. The review asks for
// the inspector to be decomposed:
//
//   "| `buildMaterialControls()`                                | MaterialInspector          |"
//   "| `buildPmControls()`                                      | PhaseModulationPanel       |"
//   "| `buildSampleControls()`                                  | SampleMaterialPanel        |"
//   "| `buildLiveInputControls()`                               | LiveInputPanel             |"
//   (section 28)
//
// This header is the inspector's description: which controls belong to which
// material, in which order, and what each material says about itself.
//
// It holds no widget and no document. The app binds the roles to its own
// controls (the app owns the widgets), and the layout asks the description
// what to draw and what to hide. The description is the one place where the
// material's contents are written down: before, the visibility rules and the
// card's cells were two separate lists that had to agree.
//
// The header is free of JUCE on purpose: the description is data, and the
// data can be checked without a window.

#include "InspectorCell.h"

#include <cstddef>
#include <vector>

namespace upic::material_inspector
{
    /** The four materials (the tabs above the canvas). */
    enum class Material
    {
        Osc = 0,      // the wavetable arcs
        Sample = 1,   // the sample slot, transposed
        Live = 2,     // the microphone, transposed
        Pm = 3        // the phase modulation arcs
    };

    /** A control of the material inspector. */
    enum class Control
    {
        // the shared ones
        Wave,            // the carrier wave - it lives in the voice card
        Ref,             // the material's reference pitch
        // the Osc tab - the polyagogy (the wave page's own)
        ModUse, ModRate, ModDepth,
        // the Sample tab
        Slot, LoadSample, Reverse, Level, SamplePitch, Grain,
        Normalise, Fade, Reset, InputGain, Monitor, Capture,
        WaveFromSlot, Extract,
        // the Live tab
        LivePitch, LiveGrain, LiveReverseLength, LiveReverse,
        // the PM tab
        PmOps, PmPreset, PmRatio, PmIndex, PmIndexEnv, PmFeedback,
        PmImport, PmExport,
        count
    };

    /** How a cell of the material card is drawn. R5.7: the vocabulary the
        panel descriptions share (Source/InspectorCell.h). */
    using Kind = upic::inspector::Kind;

    /** One cell of the material card. */
    struct Cell
    {
        Control control = Control::Wave;
        Kind kind = Kind::Plain;
        int labelWidth = 0;   // the caption's width (design units)
        int width = 0;        // the control's width (design units)
        bool newLine = false; // the cell starts a new line in its card
    };

    /** What the inspector says about one material. */
    struct Description
    {
        Material material = Material::Osc;
        const char* name = "";      // the tab's label
        const char* tooltip = "";   // the tab's tooltip
        const char* hint = "";      // the sidebar's explanation
        std::vector<Cell> card;         // the material card's cells, in order
        std::vector<Control> shown;     // the controls the material shows

        /** Does the material show this control? */
        bool shows (Control control) const
        {
            for (auto c : shown)
                if (c == control) return true;
            return false;
        }
    };

    /** The material's description. The mode is the canvas's layer. */
    inline const Description& describe (int mode)
    {
        static const Description descriptions[] =
        {
            // --- the Osc tab: the polyagogy (the wave page's own) -----------
            {
                Material::Osc, "Osc",
                "Osc tab: oscillator arcs (wavetables)",
                "Osc: draw arcs on the wavetable\n"
                "Wave = carrier, Env = amplitude",
                {
                    { Control::ModUse,   Kind::Labelled, 56, 150 },
                    { Control::ModRate,  Kind::Knob,     34,   0 },
                    { Control::ModDepth, Kind::Knob,     40,   0 }
                },
                { Control::Wave, Control::ModUse, Control::ModRate, Control::ModDepth }
            },
            // --- the Sample tab: the slot and its own edits -------------------
            {
                Material::Sample, "Sample",
                "Sample tab: sample arcs - Slot, Load sample, In/Mon/Capture above",
                "Sample: Pitch sets how arcs transpose the slot\n"
                "Ref = unison",
                {
                    { Control::Slot,         Kind::Labelled, 34, 180 },
                    { Control::LoadSample,   Kind::Plain,     0, 110 },
                    { Control::Reverse,      Kind::Plain,     0,  64 },
                    { Control::Ref,          Kind::Knob,     34,   0 },
                    { Control::Level,        Kind::Knob,     34,   0 },
                    { Control::SamplePitch,  Kind::Labelled, 34, 180 },
                    { Control::Grain,        Kind::Knob,     40,   0 },
                    { Control::Normalise,    Kind::Plain,     0,  64 },
                    { Control::Fade,         Kind::Plain,     0,  64 },
                    { Control::Reset,        Kind::Plain,     0,  64 },
                    { Control::InputGain,    Kind::Knob,     30,   0 },
                    { Control::Monitor,      Kind::Plain,     0,  64 },
                    { Control::Capture,      Kind::Plain,     0,  84 },
                    { Control::WaveFromSlot, Kind::Plain,     0, 110 },
                    { Control::Extract,      Kind::Plain,     0,  82 }
                },
                {
                    Control::Ref, Control::Slot, Control::LoadSample, Control::Reverse,
                    Control::Level, Control::SamplePitch, Control::Grain,
                    Control::Normalise, Control::Fade, Control::Reset,
                    Control::InputGain, Control::Monitor, Control::Capture,
                    Control::WaveFromSlot, Control::Extract
                }
            },
            // --- the Live tab: the microphone ---------------------------------
            {
                Material::Live, "Live",
                "Live tab: arcs transpose the mic input in realtime",
                "Live: arcs multiply the input pitch\n"
                "1x = input, 2x = octave up",
                {
                    { Control::Ref,               Kind::Knob,     34,   0 },
                    { Control::LivePitch,         Kind::Labelled, 42, 180 },
                    { Control::LiveGrain,         Kind::Knob,     40,   0 },
                    { Control::LiveReverseLength, Kind::Labelled, 52,  90 },
                    { Control::LiveReverse,       Kind::Plain,     0,  64 }
                },
                {
                    Control::Ref, Control::LivePitch, Control::LiveGrain,
                    Control::LiveReverseLength, Control::LiveReverse
                }
            },
            // --- the PM tab: the phase modulation ----------------------------
            {
                Material::Pm, "PM",
                "PM tab: 2-op phase-modulation arcs",
                "PM: Ratio x Index into the carrier phase\n"
                "IdxEnv shapes the modulation index",
                {
                    { Control::PmOps,       Kind::Labelled, 34,  80 },
                    { Control::PmPreset,    Kind::Labelled, 44, 180 },
                    { Control::PmRatio,     Kind::Knob,     40,   0 },
                    { Control::PmIndex,     Kind::Knob,     40,   0 },
                    { Control::PmIndexEnv,  Kind::Labelled, 46, 120 },
                    { Control::PmFeedback,  Kind::Knob,     42,   0 },
                    { Control::PmImport,    Kind::Plain,     0,  64 },
                    { Control::PmExport,    Kind::Plain,     0,  64 }
                },
                {
                    Control::Wave, Control::PmOps, Control::PmPreset, Control::PmRatio,
                    Control::PmIndex, Control::PmIndexEnv, Control::PmFeedback,
                    Control::PmImport, Control::PmExport
                }
            }
        };

        const int m = (mode < 0 || mode > 3) ? 0 : mode;
        return descriptions[m];
    }

    /** The material's description, by the material itself. */
    inline const Description& describe (Material material)
    {
        return describe ((int) material);
    }

    /** Every control the inspector knows - the ones the visibility pass walks. */
    inline const std::vector<Control>& allControls()
    {
        static const std::vector<Control> controls =
        {
            Control::Wave, Control::Ref,
            Control::ModUse, Control::ModRate, Control::ModDepth,
            Control::Slot, Control::LoadSample, Control::Reverse, Control::Level,
            Control::SamplePitch, Control::Grain, Control::Normalise, Control::Fade,
            Control::Reset, Control::InputGain, Control::Monitor, Control::Capture,
            Control::WaveFromSlot, Control::Extract,
            Control::LivePitch, Control::LiveGrain, Control::LiveReverseLength,
            Control::LiveReverse,
            Control::PmOps, Control::PmPreset, Control::PmRatio, Control::PmIndex,
            Control::PmIndexEnv, Control::PmFeedback, Control::PmImport, Control::PmExport
        };
        return controls;
    }
}
