/*
    UPICuitous - the overlay layout engine (refactoring wave R1.3).

    Expression-for-expression port of the three overlay cards that
    MainComponent::layoutOverlays() used to lay out. The rectangle helpers
    come from LayoutRect.h, which repeats the juce::Rectangle semantics
    (including the clamping) so the extraction cannot move a pixel.
*/

#include "OverlayLayoutEngine.h"

#include <cmath>

namespace upic::layout
{
    namespace
    {
        int scaled (int dp, float scale) noexcept
        {
            return (int) std::lround ((float) dp * scale);
        }

        // The overlays' title rows are fixed, not scaled: they are the
        // frame's own title.
        constexpr int overlayTitleH = 28;
    }

    AudioOverlayLayout layoutAudioOverlay (const Rect& cardFrame)
    {
        AudioOverlayLayout out;
        Rect card = rect::reduced (cardFrame, 12, 12);
        rect::removeFromTop (card, overlayTitleH);
        rect::removeFromTop (card, 6);
        Rect btnRow = rect::removeFromBottom (card, 40);
        rect::removeFromBottom (card, 6);
        out.okButton = rect::reduced (rect::removeFromRight (btnRow, 120), 2, 2);
        out.selector = card;
        return out;
    }

    SettingsOverlayLayout layoutSettingsOverlay (const Rect& cardFrame, float scale)
    {
        SettingsOverlayLayout out;
        Rect card = rect::reduced (cardFrame, 12, 12);
        rect::removeFromTop (card, overlayTitleH);
        Rect btnRow = rect::removeFromBottom (card, 40);
        rect::removeFromBottom (card, 6);
        out.ok = rect::reduced (rect::removeFromRight (btnRow, 100), 2, 2);
        rect::removeFromRight (btnRow, 8);
        out.apply = rect::reduced (rect::removeFromRight (btnRow, 110), 2, 2);

        // --- MIDI ---
        out.midiSection = rect::reduced (rect::removeFromTop (card, scaled (22, scale)), 2, 2);
        {
            Rect devRow = rect::removeFromTop (card, scaled (28, scale));
            out.midiDeviceLabel = rect::reduced (rect::removeFromLeft (devRow, scaled (76, scale)), 2, 2);
            out.midiDeviceBox = rect::reduced (rect::removeFromLeft (devRow, scaled (220, scale)), 2, 2);
            rect::removeFromTop (card, 2);

            Rect actRow = rect::removeFromTop (card, scaled (22, scale));
            out.midiActivity = rect::reduced (rect::removeFromLeft (actRow, scaled (300, scale)), 2, 2);
            rect::removeFromTop (card, 2);

            Rect gainRow = rect::removeFromTop (card, scaled (26, scale));
            out.midiGainLabel = rect::reduced (rect::removeFromLeft (gainRow, scaled (76, scale)), 2, 2);
            out.midiLearnGain = rect::reduced (rect::removeFromLeft (gainRow, scaled (64, scale)), 2, 2);
            out.midiBindGain = rect::reduced (rect::removeFromLeft (gainRow, scaled (90, scale)), 2, 2);
            rect::removeFromTop (card, 2);

            Rect intervalRow = rect::removeFromTop (card, scaled (26, scale));
            out.midiIntervalLabel = rect::reduced (rect::removeFromLeft (intervalRow, scaled (76, scale)), 2, 2);
            out.midiLearnInterval = rect::reduced (rect::removeFromLeft (intervalRow, scaled (64, scale)), 2, 2);
            out.midiBindInterval = rect::reduced (rect::removeFromLeft (intervalRow, scaled (90, scale)), 2, 2);
            rect::removeFromTop (card, 2);

            Rect scrubRow = rect::removeFromTop (card, scaled (26, scale));
            out.midiScrubLabel = rect::reduced (rect::removeFromLeft (scrubRow, scaled (76, scale)), 2, 2);
            out.midiLearnScrub = rect::reduced (rect::removeFromLeft (scrubRow, scaled (64, scale)), 2, 2);
            out.midiBindScrub = rect::reduced (rect::removeFromLeft (scrubRow, scaled (90, scale)), 2, 2);
            rect::removeFromTop (card, 2);

            Rect transportRow = rect::removeFromTop (card, scaled (26, scale));
            out.midiTransportLabel = rect::reduced (rect::removeFromLeft (transportRow, scaled (76, scale)), 2, 2);
            out.midiLearnTransport = rect::reduced (rect::removeFromLeft (transportRow, scaled (64, scale)), 2, 2);
            out.midiBindTransport = rect::reduced (rect::removeFromLeft (transportRow, scaled (90, scale)), 2, 2);
        }

        rect::removeFromTop (card, 8);

        // --- OSC ---
        out.oscSection = rect::reduced (rect::removeFromTop (card, scaled (22, scale)), 2, 2);
        out.oscStatus = rect::reduced (rect::removeFromTop (card, scaled (20, scale)), 2, 2);

        Rect onRow = rect::removeFromTop (card, scaled (28, scale));
        out.oscOn = rect::reduced (rect::removeFromLeft (onRow, scaled (100, scale)), 2, 2);

        Rect portRow = rect::removeFromTop (card, scaled (28, scale));
        out.oscPortLabel = rect::reduced (rect::removeFromLeft (portRow, scaled (76, scale)), 2, 2);
        out.oscPortSlider = rect::reduced (rect::removeFromLeft (portRow, scaled (180, scale)), 2, 2);

        Rect mcRow = rect::removeFromTop (card, scaled (28, scale));
        out.oscMulticast = rect::reduced (rect::removeFromLeft (mcRow, scaled (100, scale)), 2, 2);
        out.oscGroupLabel = rect::reduced (rect::removeFromLeft (mcRow, scaled (46, scale)), 2, 2);
        out.oscGroupEdit = rect::reduced (rect::removeFromLeft (mcRow, scaled (150, scale)), 2, 2);
        out.oscMulticastPortLabel = rect::reduced (rect::removeFromLeft (mcRow, scaled (50, scale)), 2, 2);
        out.oscMulticastPortSlider = rect::reduced (mcRow, 2, 2);

        return out;
    }

    ClearOverlayLayout layoutClearOverlay (const Rect& cardFrame)
    {
        ClearOverlayLayout out;
        Rect card = rect::reduced (cardFrame, 16, 16);
        rect::removeFromTop (card, overlayTitleH);
        Rect btnRow = rect::removeFromBottom (card, 36);
        rect::removeFromBottom (card, 6);
        out.cancel = rect::reduced (rect::removeFromRight (btnRow, 110), 2, 2);
        rect::removeFromRight (btnRow, 8);
        out.confirm = rect::reduced (rect::removeFromRight (btnRow, 130), 2, 2);
        return out;
    }
}
