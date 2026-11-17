#pragma once
#include <JuceHeader.h>

//==============================================================================
// F6 (2026-11-17): the score's view bar.
//
// The bar carries the view commands (zoom in / out, fit the page, fit the
// selection, reset the zoom), the Fine drawing toggle, the live readout of
// the cursor's position (time in ms, frequency in Hz) and the numeric edit
// fields for the selected point.
//
// It lays itself out: when the row is narrow the optional parts go (the
// numeric fields first, then the fit commands), and the zoom commands and
// the readout always stay. The bar never overlaps the canvas - it has its
// own row (the layout is a pure tiling).
class ScoreViewBar : public juce::Component
{
public:
    ScoreViewBar()
    {
        setName ("viewBar");

        for (auto* b : { &zoomInBtn, &zoomOutBtn, &fitBtn, &fitSelBtn,
                         &resetBtn, &fineBtn })
            addAndMakeVisible (b);

        zoomInBtn.setTooltip  ("Zoom in (the view's centre stays)");
        zoomOutBtn.setTooltip ("Zoom out (the view's centre stays)");
        fitBtn.setTooltip     ("Fit the whole page");
        fitSelBtn.setTooltip  ("Fit the selection");
        resetBtn.setTooltip   ("Back to 1x (the view's centre stays)");
        fineBtn.setTooltip    ("Fine drawing: the stroke is not simplified");
        fineBtn.setClickingTogglesState (true);

        // the buttons call the callbacks the app sets
        zoomInBtn.onClick  = [this] { if (onZoomIn)  onZoomIn(); };
        zoomOutBtn.onClick = [this] { if (onZoomOut) onZoomOut(); };
        fitBtn.onClick     = [this] { if (onFit)     onFit(); };
        fitSelBtn.onClick  = [this] { if (onFitSel)  onFitSel(); };
        resetBtn.onClick   = [this] { if (onReset)   onReset(); };
        fineBtn.onClick    = [this] { if (onFineToggle) onFineToggle(); };

        addAndMakeVisible (readoutLab);
        readoutLab.setJustificationType (juce::Justification::centredLeft);
        readoutLab.setColour (juce::Label::textColourId, juce::Colours::white);
        readoutLab.setText ("t 0.0 ms    f 0.0 Hz", juce::dontSendNotification);

        addAndMakeVisible (timeLab);
        addAndMakeVisible (freqLab);
        addAndMakeVisible (timeEdit);
        addAndMakeVisible (freqEdit);
        timeLab.setText ("t", juce::dontSendNotification);
        freqLab.setText ("f", juce::dontSendNotification);
        for (auto* l : { &timeLab, &freqLab })
        {
            l->setJustificationType (juce::Justification::centredRight);
            l->setColour (juce::Label::textColourId, juce::Colour (0xffc9c9d4));
        }
        for (auto* e : { &timeEdit, &freqEdit })
        {
            e->setTextToShowWhenEmpty ("-", juce::Colour (0xff7a7a86));
            e->setEnabled (false);
        }
        timeEdit.setTooltip ("The selected point's time (ms) - Enter writes it");
        freqEdit.setTooltip ("The selected point's frequency (Hz) - Enter writes it");
        timeEdit.onReturnKey = [this] { commitTime(); };
        timeEdit.onFocusLost = [this] { commitTime(); };
        freqEdit.onReturnKey = [this] { commitFreq(); };
        freqEdit.onFocusLost = [this] { commitFreq(); };

        setLayoutScale (1.0f);
    }

    juce::TextButton zoomInBtn { "Zoom +" }, zoomOutBtn { "Zoom -" },
                      fitBtn { "Fit" }, fitSelBtn { "Fit sel" },
                      resetBtn { "Reset" }, fineBtn { "Fine" };
    juce::Label readoutLab;
    juce::TextEditor timeEdit, freqEdit;

    // the callbacks (the app owns the score)
    std::function<void()> onZoomIn, onZoomOut, onFit, onFitSel, onReset, onFineToggle;
    std::function<void(double seconds)> onTimeEdit;
    std::function<void(double hz)> onFreqEdit;

    // The layout's scale factor: the bar's controls shrink with the
    // window, like every other control in the toolbar.
    void setLayoutScale (float sc)
    {
        layoutScale = juce::jlimit (0.55f, 1.6f, sc);
        const float f = juce::jmax (9.0f, 11.0f * layoutScale);
        readoutLab.setFont (juce::Font (juce::FontOptions (f, juce::Font::bold)));
        for (auto* l : { &timeLab, &freqLab })
            l->setFont (juce::Font (juce::FontOptions (f, juce::Font::bold)));
        for (auto* e : { &timeEdit, &freqEdit })
            e->setFont (juce::Font (juce::FontOptions (f)));
        resized();
    }

    // the live readout (time in ms, frequency in Hz)
    void setReadout (double tSec, double fHz)
    {
        readoutLab.setText ("t " + juce::String (tSec * 1000.0, 1) + " ms    f "
                            + juce::String (fHz, 1) + " Hz", juce::dontSendNotification);
    }

    // the numeric fields follow the selection
    void setPointFields (bool hasPoint, double tSec, double fHz)
    {
        timeEdit.setEnabled (hasPoint);
        freqEdit.setEnabled (hasPoint);
        if (! hasPoint)
        {
            timeEdit.setText ("", juce::dontSendNotification);
            freqEdit.setText ("", juce::dontSendNotification);
            return;
        }
        if (! timeEdit.hasKeyboardFocus (true))
            timeEdit.setText (juce::String (tSec * 1000.0, 1), juce::dontSendNotification);
        if (! freqEdit.hasKeyboardFocus (true))
            freqEdit.setText (juce::String (fHz, 1), juce::dontSendNotification);
    }

    void setFineState (bool on)
    {
        fineBtn.setToggleState (on, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        const float sc = layoutScale;
        const int gap = juce::jmax (1, (int) std::lround (3.0f * sc));
        const int zoomW = (int) std::lround (56.0f * sc);
        const int fitW = (int) std::lround (36.0f * sc);
        const int fitSelW = (int) std::lround (52.0f * sc);
        const int resetW = (int) std::lround (44.0f * sc);
        const int fineW = (int) std::lround (38.0f * sc);
        const int fieldW = (int) std::lround (62.0f * sc);
        const int labW = (int) std::lround (9.0f * sc);
        const int readoutMin = (int) std::lround (140.0f * sc);

        const int fitsW = fitW + fitSelW + resetW + fineW + 4 * gap;
        const int fieldsW = 2 * (labW + fieldW) + 2 * gap;
        const int zoomBothW = zoomW * 2 + 2 * gap;

        const bool showFits = r.getWidth() >= zoomBothW + fitsW + readoutMin;
        const bool showFields = r.getWidth() >= zoomBothW
                                 + (showFits ? fitsW : 0) + readoutMin + fieldsW;

        auto place = [&] (juce::Component& c, int w)
        {
            const int take = juce::jmin (w, r.getWidth());
            c.setBounds (r.removeFromLeft (take).reduced (1));
            r.removeFromLeft (gap);
        };

        place (zoomInBtn, zoomW);
        place (zoomOutBtn, zoomW);

        fitBtn.setVisible (showFits);
        fitSelBtn.setVisible (showFits);
        resetBtn.setVisible (showFits);
        fineBtn.setVisible (showFits);
        if (showFits)
        {
            place (fitBtn, fitW);
            place (fitSelBtn, fitSelW);
            place (resetBtn, resetW);
            place (fineBtn, fineW);
        }

        timeLab.setVisible (showFields);
        freqLab.setVisible (showFields);
        timeEdit.setVisible (showFields);
        freqEdit.setVisible (showFields);
        if (showFields)
        {
            // the fields are at the right end, the readout takes the rest
            auto right = r.removeFromRight (fieldsW);
            timeLab.setBounds (right.removeFromLeft (labW).reduced (0, 2));
            timeEdit.setBounds (right.removeFromLeft (fieldW).reduced (1));
            freqLab.setBounds (right.removeFromLeft (labW).reduced (0, 2));
            freqEdit.setBounds (right.removeFromLeft (fieldW).reduced (1));
        }

        readoutLab.setBounds (r.reduced (1, 2));
    }

private:
    juce::Label timeLab, freqLab;
    float layoutScale = 1.0f;

    void commitTime()
    {
        if (onTimeEdit == nullptr) return;
        const juce::String text = timeEdit.getText().trim();
        if (! text.isEmpty())
            onTimeEdit (text.getDoubleValue() / 1000.0);
    }

    void commitFreq()
    {
        if (onFreqEdit == nullptr) return;
        const juce::String text = freqEdit.getText().trim();
        if (! text.isEmpty())
            onFreqEdit (text.getDoubleValue());
    }
};
