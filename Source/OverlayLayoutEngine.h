/*
    UPICuitous - the overlay layout engine (refactoring wave R1.3).

    The app has three in-app overlays: the audio device selector, the MIDI +
    OSC settings and the "clear page?" warning. Each one is a backdrop with a
    centred card, and the card's contents were laid out inside
    MainComponent::layoutOverlays(). This engine takes them over.

    Like CardLayoutEngine and BottomLayoutEngine, it is deliberately free of
    JUCE: the input is the card's frame and the layout scale, the output is
    rectangles. The card's frame itself stays with the overlay component,
    because the overlay paints it.

    The arithmetic is the arithmetic that used to sit in layoutOverlays(),
    expression for expression, so the cards are unchanged. The R1.3
    characterization suite is the gate.
*/

#pragma once

#include "LayoutRect.h"

namespace upic::layout
{
    /** The audio settings card: the device selector and the OK button. */
    struct AudioOverlayLayout
    {
        Rect okButton;
        Rect selector;
    };

    /** The MIDI + OSC settings card. */
    struct SettingsOverlayLayout
    {
        Rect ok, apply;

        Rect midiSection, midiDeviceLabel, midiDeviceBox, midiActivity;
        Rect midiGainLabel, midiLearnGain, midiBindGain;
        Rect midiIntervalLabel, midiLearnInterval, midiBindInterval;
        Rect midiScrubLabel, midiLearnScrub, midiBindScrub;
        Rect midiTransportLabel, midiLearnTransport, midiBindTransport;

        Rect oscSection, oscStatus, oscOn, oscPortLabel, oscPortSlider;
        Rect oscMulticast, oscGroupLabel, oscGroupEdit;
        Rect oscMulticastPortLabel, oscMulticastPortSlider;
    };

    /** The "clear page?" card. */
    struct ClearOverlayLayout
    {
        Rect cancel, confirm;
    };

    /** The audio card: the device selector fills it, the OK button sits at
        the bottom right. */
    AudioOverlayLayout layoutAudioOverlay (const Rect& cardFrame);

    /** The MIDI + OSC card: the buttons at the bottom, then the MIDI rows,
        then the OSC rows. */
    SettingsOverlayLayout layoutSettingsOverlay (const Rect& cardFrame, float scale);

    /** The "clear page?" card: the two buttons at the bottom. */
    ClearOverlayLayout layoutClearOverlay (const Rect& cardFrame);
}
