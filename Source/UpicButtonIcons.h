#pragma once

#include <JuceHeader.h>
#include "UpicButtonIconData.h"

// SVG icons for controls that already exist. Nothing here owns buttons,
// changes their text, callbacks, names, focus, toggle states or accessibility.
// The images are cached by UpicButtonIconData.h and drawn in LookAndFeel.
namespace upic { namespace buttonIcons {

struct ButtonMapping { const char* name; const char* icon; };

inline const ButtonMapping buttonMappings[] = {
    // Transport and file/page actions
    {"Play", "play"}, {"Stop", "stop"},
    {"File", "file"}, {"Settings", "settings"}, {"settingsBtn", "settings"},
    {"Clear", "trash"}, {"clearBtn", "trash"},
    {"Save", "save"}, {"Load", "folder-open"}, {"Render WAV", "file-music"},
    {"Stems", "layers"}, {"Audio", "headphones"},
    {"pageAddBtn", "plus"}, {"pageDupBtn", "copy"}, {"pageDelBtn", "minus"},
    {"Dup", "copy"},
    {"Loop", "repeat"}, {"Seq", "sequence"}, {"Pressure", "hand"},
    {"Scrub", "scrub"}, {"Snap", "magnet"}, {"Rev", "reverse"},
    {"Stepped", "sequence"}, {"Mute drag", "volume-x"},
    {"Mon", "radio"}, {"Sieve", "sieve"},
    {"OSC on", "radio"}, {"Multicast", "radio"},
    // Material tabs and views
    {"Osc", "wave-sine"}, {"Sample", "file-music"},
    {"Live", "mic"}, {"PM", "audio-lines"},
    {"Wave", "wave-sine"}, {"Env", "env-percussive"}, {"Hints", "eye"},
    {"Arcs", "arc"}, {"arcListBtn", "arc"},
    // Wavetable shape controls (pictograms plus original captions)
    {"wSine", "wave-sine"}, {"Sine", "wave-sine"},
    {"Saw", "wave-saw"}, {"Sqr", "wave-square"},
    {"Tri", "wave-triangle"}, {"Noise", "wave-noise"},
    {"Smooth", "wave-smooth"}, {"Norm", "wave-normalize"},
    // Envelope shape controls
    {"envFlatBtn", "env-flat"}, {"Flat", "env-flat"},
    {"Perc", "env-percussive"}, {"Swell", "env-swell"},
    {"Gate", "env-gate"}, {"Strings", "env-strings"},
    // Model editing and sample input
    {"Assign to sel.", "check"}, {"Del sel.", "trash"},
    {"Mute sel.", "volume-x"},
    {"capBtn", "mic"}, {"Capture", "mic"},
    {"waveFromSlotBtn", "audio-lines"}, {"Slot -> Wave", "audio-lines"},
    {"Load sample", "folder-open"},
    {"Import", "download"}, {"Export", "upload"},
    {"Fade", "fade"}, {"Reset", "reverse"},
    {"Learn", "radio"}, {"Apply", "check"}, {"OK", "check"},
    {"Clear page", "trash"}, {"Cancel", "x"},
};

inline juce::String iconName (const juce::Button& button)
{
    // Explicitly assigned component names win over a temporary label change
    // (e.g. Capture -> Stop and Play -> Playing...).
    for (const auto& mapping : buttonMappings)
        if (button.getName() == mapping.name) return mapping.icon;
    for (const auto& mapping : buttonMappings)
        if (button.getButtonText() == mapping.name) return mapping.icon;
    return {};
}

inline bool compactShapeButton (const juce::TextButton& button)
{
    // These shape buttons are deliberately narrow. Their original captions
    // are always painted together with the miniature diagram, and tooltips
    // continue to expose the full action.
    const juce::String name = button.getName();
    const juce::String label = button.getButtonText();
    return name == "wSine" || name == "envFlatBtn"
        || label == "Sine" || label == "Saw" || label == "Sqr"
        || label == "Tri" || label == "Noise" || label == "Smooth"
        || label == "Norm" || label == "Flat" || label == "Perc"
        || label == "Swell" || label == "Gate" || label == "Strings";
}

// v1.34.3: the user asked for these buttons to keep the icon on the same
// row as the caption, even when JUCE would stack a small symbol above the
// text. The list is explicit; every other button keeps the adaptive rule.
inline bool inlineIcon (const juce::Button& button)
{
    static const char* const labels[] = {
        "Osc", "Sample", "Live", "PM", "Arcs", "Wave", "Env", "Hints",
        "Reset", "Flat", "Perc", "Swell", "Gate", "Strings",
        "Sine", "Saw", "Sqr", "Tri", "Noise", "Smooth", "Norm"
    };
    for (auto* l : labels)
        if (button.getButtonText() == l || button.getName() == l)
            return true;
    return false;
}

// v1.34.3: the library windows' buttons keep their captions on one line -
// JUCE's TextButton fallback wraps to two lines when the width is tight.
inline bool singleLineText (const juce::Button& button)
{
    const juce::String name = button.getName();
    const juce::String text = button.getButtonText();
    return name.endsWith ("NewBtn") || name.endsWith ("DupBtn")
        || name.endsWith ("RenameBtn") || name.endsWith ("DelBtn")
        || name.endsWith ("UniqueBtn")
        || text == "New" || text == "Dup" || text == "Rename"
        || text == "Delete" || text == "Unique";
}

// v1.34.4: the zoom bar's Reset keeps its caption alone - the button is
// too narrow for an icon and a caption side by side.
inline bool noIcon (const juce::Button& button)
{
    return button.getName() == "resetZoomBtn";
}

inline const juce::Drawable* drawableFor (const juce::Button& button)
{
    if (noIcon (button)) return nullptr;
    const auto name = iconName (button);
    return name.isNotEmpty() ? find (name) : nullptr;
}

inline const juce::Drawable* toolDrawable (const juce::ComboBox& box)
{
    if (box.getName() != "toolBox") return nullptr;
    const char* keys[] = { "pencil", "line-tool", "wave-sine", "move", "eraser", "zone" };
    const int i = box.getSelectedId() - 1;
    return juce::isPositiveAndBelow (i, 6) ? find (keys[i]) : nullptr;
}

}} // namespace upic::buttonIcons
