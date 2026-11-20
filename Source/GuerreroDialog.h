#pragma once

#if GUERRERO_INTEGRATION

#include <JuceHeader.h>
#include "GuerreroImport.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <vector>

//==============================================================================
// The Guerrero generator parameter dialog.
//
// In-app overlay, not a DialogWindow: on Android a DialogWindow has no title bar
// and no close button, so it could never be dismissed. Same pattern as
// AudioOverlay/ClearOverlay.
//
// ROUND-TRIP FIDELITY
// -------------------
// The stored recipe is the authoritative definition of an arc's geometry, so
// opening it and pressing Apply without touching a field must reproduce it
// EXACTLY - including 64-bit seeds above INT64_MAX and doubles that do not
// survive a fixed decimal format.
//
// Two rules make that true:
//   1. Every editor remembers the text it was given. `collect()` only parses an
//      editor whose text has changed; otherwise it keeps the original binary
//      value from the loaded recipe. Display precision is therefore cosmetic
//      and cannot alter a value the user did not edit.
//   2. Seeds are formatted and parsed as unsigned decimal by hand, never
//      through a signed int64 conversion, so the whole uint64 range survives.
//
// The three seeds are edited independently and are never derived from one
// another: a saved recipe may legitimately hold unrelated values, and
// rebuilding seedSeed = lowerSeed + 2 would silently change the music.
//
// Deliberately NOT exposed:
//   - the seed envelope's time and frequency ranges (generateConfinementSeed()
//     takes both from the envelope curves, so such controls would do nothing);
//   - host material overrides (waveform, intensity, synthesis mode), which
//     survive regeneration untouched.
//==============================================================================
struct GuerreroParamOverlay : juce::Component
{
    static constexpr int titleH  = 26;
    static constexpr int rowH    = 26;
    static constexpr int buttonH = 30;
    static constexpr int labelW  = 168;

    upic::GuerreroRecipe recipe;
    bool editing = false;
    juce::String title { "Guerrero generator" };
    juce::String status;

    std::function<void()> onCancel;

    // Returns true when the recipe was accepted. On false, `error` is shown and
    // the dialog stays open so the typed values are not lost.
    std::function<bool (const upic::GuerreroRecipe&, juce::String&)> onApply;

    //--------------------------------------------------------------------------
    GuerreroParamOverlay()
    {
        setWantsKeyboardFocus (true);

        const char* numChars = "0123456789.+-eE";

        lowerRough.setInputRestrictions (20, numChars);
        lowerDepth.setInputRestrictions (3,  "0123456789");
        lowerLo.setInputRestrictions (16, numChars);
        lowerHi.setInputRestrictions (16, numChars);
        upperRough.setInputRestrictions (20, numChars);
        upperDepth.setInputRestrictions (3,  "0123456789");
        upperLo.setInputRestrictions (16, numChars);
        upperHi.setInputRestrictions (16, numChars);
        seedRough.setInputRestrictions (20, numChars);
        seedDepth.setInputRestrictions (3,  "0123456789");
        bpmEd.setInputRestrictions (16, numChars);
        maxPtsEd.setInputRestrictions (6, "0123456789");

        lowerSeedEd.setInputRestrictions (20, "0123456789");
        upperSeedEd.setInputRestrictions (20, "0123456789");
        seedSeedEd.setInputRestrictions (20, "0123456789");

        freqPol.addItem ("Reject notes outside the host range", 1);
        freqPol.addItem ("Compress into the host range", 2);
        ptsPol.addItem ("Reject when over the point limit", 1);
        ptsPol.addItem ("Downsample to the point limit", 2);

        addRow ("Lower roughness (0-8)",  lowerRough);
        addRow ("Lower depth (0-30)",     lowerDepth);
        addRow ("Lower freq low (Hz)",    lowerLo);
        addRow ("Lower freq high (Hz)",   lowerHi);
        addRow ("Upper roughness (0-8)",  upperRough);
        addRow ("Upper depth (0-30)",     upperDepth);
        addRow ("Upper freq low (Hz)",    upperLo);
        addRow ("Upper freq high (Hz)",   upperHi);
        addRow ("Seed roughness (0-8)",   seedRough);
        addRow ("Seed depth (0-30)",      seedDepth);
        addRow ("Tempo (BPM)",            bpmEd);
        addRow ("Max points per arc",     maxPtsEd);
        addRow ("Lower envelope seed",    lowerSeedEd);
        addRow ("Upper envelope seed",    upperSeedEd);
        addRow ("Seed curve seed",        seedSeedEd);
        addRow ("Frequency policy",       freqPol);
        addRow ("Point policy",           ptsPol);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        applyBtn.onClick  = [this] { tryApply(); };
        cancelBtn.onClick = [this] { if (onCancel != nullptr) onCancel(); };
        addAndMakeVisible (applyBtn);
        addAndMakeVisible (cancelBtn);

        loadFromRecipe();
    }

    //--------------------------------------------------------------------------
    // Fill the editors from `recipe` and remember what was rendered, so an
    // untouched field is never re-parsed.
    void loadFromRecipe()
    {
        setNum (lowerRough, recipe.lower.roughness);
        setNum (lowerDepth, recipe.lower.depth);
        setNum (lowerLo,    recipe.lower.freqLo);
        setNum (lowerHi,    recipe.lower.freqHi);

        setNum (upperRough, recipe.upper.roughness);
        setNum (upperDepth, recipe.upper.depth);
        setNum (upperLo,    recipe.upper.freqLo);
        setNum (upperHi,    recipe.upper.freqHi);

        setNum (seedRough,  recipe.seed.roughness);
        setNum (seedDepth,  recipe.seed.depth);

        setNum (bpmEd,      recipe.bpm);
        setNum (maxPtsEd,   recipe.maxPoints);

        setSeed (lowerSeedEd, recipe.lowerSeed);
        setSeed (upperSeedEd, recipe.upperSeed);
        setSeed (seedSeedEd,  recipe.seedSeed);

        freqPol.setSelectedId (recipe.frequencyPolicy == upic::GuerreroOutOfRange::Compress ? 2 : 1,
                               juce::dontSendNotification);
        ptsPol.setSelectedId  (recipe.pointsPolicy == upic::GuerreroPointPolicy::Downsample ? 2 : 1,
                               juce::dontSendNotification);

        applyBtn.setButtonText (editing ? "Apply recipe" : "Generate arc");
        cancelBtn.setButtonText ("Cancel");

        resized();
    }

    void setStatus (const juce::String& s) { status = s; repaint(); }

    //--------------------------------------------------------------------------
    // Test support. The editors are deliberately private; these two exist so the
    // headless checks can exercise "the user typed this" and inspect what was
    // rendered, without constructing a real window.
    void setTextForTest (const juce::String& rowName, const juce::String& text)
    {
        for (const auto& row : rows)
        {
            if (row.label->getText() != rowName) continue;
            if (auto* ed = dynamic_cast<juce::TextEditor*> (row.control))
                ed->setText (text, false);
            return;
        }
    }

    juce::String textForTest (const juce::String& rowName) const
    {
        for (const auto& row : rows)
        {
            if (row.label->getText() != rowName) continue;
            if (auto* ed = dynamic_cast<juce::TextEditor*> (row.control))
                return ed->getText();
            return {};
        }
        return {};
    }

    //--------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xcc000000));

        auto card = cardRect();
        g.setColour (juce::Colour (0xff1e1e26));
        g.fillRoundedRectangle (card.toFloat(), 6.0f);
        g.setColour (juce::Colour (0xff34343f));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 6.0f, 1.0f);

        auto inner = card.reduced (14, 0);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xffc8c8d0));
        g.drawText (title, inner.removeFromTop (titleH), juce::Justification::centredLeft);

        if (status.isNotEmpty())
        {
            auto statusArea = card.reduced (14, 0).removeFromBottom (buttonH + 20).removeFromTop (20);
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.setColour (juce::Colour (0xffe0a040));
            g.drawFittedText (status, statusArea, juce::Justification::centredLeft, 2);
        }
    }

    juce::Rectangle<int> cardRect() const
    {
        auto r = getLocalBounds().reduced (10);
        const int w = juce::jmin (600, r.getWidth());
        const int h = juce::jmin (620, r.getHeight());
        return juce::Rectangle<int> (w, h).withCentre (r.getCentre());
    }

    void resized() override
    {
        auto card = cardRect().reduced (14, 10);
        card.removeFromTop (titleH);

        auto buttons = card.removeFromBottom (buttonH);
        card.removeFromBottom (18);          // status line

        // The rows scroll, so a short window (Android landscape) still reaches
        // every field and both buttons instead of clipping them.
        viewport.setBounds (card);

        const int w = juce::jmax (200, card.getWidth() - 12);   // scrollbar gutter
        content.setSize (w, (int) rows.size() * rowH);

        int y = 0;
        for (const auto& row : rows)
        {
            auto line = juce::Rectangle<int> (0, y, w, rowH).reduced (0, 2);
            row.label->setBounds (line.removeFromLeft (labelW));
            row.control->setBounds (line);
            y += rowH;
        }

        const int bw = juce::jmin (150, buttons.getWidth() / 2 - 6);
        cancelBtn.setBounds (buttons.removeFromRight (bw).reduced (0, 2));
        buttons.removeFromRight (8);
        applyBtn.setBounds (buttons.removeFromRight (bw).reduced (0, 2));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cardRect().contains (e.getPosition()) && onCancel != nullptr)
            onCancel();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey && onCancel != nullptr)
        {
            onCancel();
            return true;
        }
        return false;
    }

    //--------------------------------------------------------------------------
    // Read the editors into `out`, starting from the loaded recipe so every
    // untouched field keeps its exact original value.
    bool collect (upic::GuerreroRecipe& out, juce::String& error) const
    {
        out = recipe;

        if (! readNum (lowerRough, out.lower.roughness, error, "Lower roughness")) return false;
        if (! readInt (lowerDepth, out.lower.depth,     error, "Lower depth"))     return false;
        if (! readNum (lowerLo,    out.lower.freqLo,    error, "Lower freq low"))  return false;
        if (! readNum (lowerHi,    out.lower.freqHi,    error, "Lower freq high")) return false;

        if (! readNum (upperRough, out.upper.roughness, error, "Upper roughness")) return false;
        if (! readInt (upperDepth, out.upper.depth,     error, "Upper depth"))     return false;
        if (! readNum (upperLo,    out.upper.freqLo,    error, "Upper freq low"))  return false;
        if (! readNum (upperHi,    out.upper.freqHi,    error, "Upper freq high")) return false;

        if (! readNum (seedRough,  out.seed.roughness,  error, "Seed roughness"))  return false;
        if (! readInt (seedDepth,  out.seed.depth,      error, "Seed depth"))      return false;

        if (! readNum (bpmEd,      out.bpm,             error, "Tempo"))           return false;
        if (! readInt (maxPtsEd,   out.maxPoints,       error, "Max points"))      return false;

        if (! readSeed (lowerSeedEd, out.lowerSeed, error, "Lower envelope seed")) return false;
        if (! readSeed (upperSeedEd, out.upperSeed, error, "Upper envelope seed")) return false;
        if (! readSeed (seedSeedEd,  out.seedSeed,  error, "Seed curve seed"))     return false;

        out.frequencyPolicy = (freqPol.getSelectedId() == 2)
                                  ? upic::GuerreroOutOfRange::Compress
                                  : upic::GuerreroOutOfRange::Reject;
        out.pointsPolicy = (ptsPol.getSelectedId() == 2)
                               ? upic::GuerreroPointPolicy::Downsample
                               : upic::GuerreroPointPolicy::Reject;

        // Domain checks mirroring the engine contract, so the user is told here
        // rather than by a generic generation failure later.
        if (! inRange (out.lower.roughness, 0.0, 8.0)) { error = "Lower roughness must be within [0, 8]"; return false; }
        if (! inRange (out.upper.roughness, 0.0, 8.0)) { error = "Upper roughness must be within [0, 8]"; return false; }
        if (! inRange (out.seed.roughness,  0.0, 8.0)) { error = "Seed roughness must be within [0, 8]";  return false; }

        if (out.lower.depth < 0 || out.lower.depth > 30) { error = "Lower depth must be within [0, 30]"; return false; }
        if (out.upper.depth < 0 || out.upper.depth > 30) { error = "Upper depth must be within [0, 30]"; return false; }
        if (out.seed.depth  < 0 || out.seed.depth  > 30) { error = "Seed depth must be within [0, 30]";  return false; }

        if (! (std::isfinite (out.bpm) && out.bpm > 0.0 && out.bpm <= 1000.0)) { error = "Tempo must be greater than 0 and at most 1000 BPM"; return false; }

        if (out.maxPoints < 2 || out.maxPoints > 100000)
        {
            error = "Max points must be within [2, 100000]";
            return false;
        }

        for (int which = 0; which < 2; ++which)
        {
            const auto& e = (which == 0) ? out.lower : out.upper;
            const char* name = (which == 0) ? "Lower" : "Upper";

            if (! inRange (e.freqLo, 1.0, 100000.0) || ! inRange (e.freqHi, 1.0, 100000.0))
            {
                error = juce::String (name) + " frequencies must be within [1, 100000] Hz";
                return false;
            }
            if (e.freqHi < e.freqLo)
            {
                error = juce::String (name) + " freq high must be greater than freq low";
                return false;
            }
            // A zero-width range is legal ONLY for a recipe that already opted
            // into expansion. Rejecting it unconditionally would make existing
            // valid saved recipes uneditable; allowing it without the flag would
            // let the engine fail later for no visible reason.
            if (e.freqHi == e.freqLo && ! e.allowDegenerateRangeExpansion)
            {
                error = juce::String (name) + " range is zero-width; this recipe does not allow expansion";
                return false;
            }
            if (! (e.timeEnd > e.timeStart))
            {
                error = juce::String (name) + " envelope has no time span";
                return false;
            }
        }

        return true;
    }

private:
    //--------------------------------------------------------------------------
    struct Row
    {
        juce::Label* label = nullptr;
        juce::Component* control = nullptr;
    };

    juce::OwnedArray<juce::Label> ownedLabels;
    std::vector<Row> rows;
    std::map<const juce::Component*, juce::String> initialText;

    juce::Viewport  viewport;
    juce::Component content;

    juce::TextEditor lowerRough, lowerDepth, lowerLo, lowerHi;
    juce::TextEditor upperRough, upperDepth, upperLo, upperHi;
    juce::TextEditor seedRough, seedDepth;
    juce::TextEditor bpmEd, maxPtsEd;
    juce::TextEditor lowerSeedEd, upperSeedEd, seedSeedEd;
    juce::ComboBox   freqPol, ptsPol;
    juce::TextButton applyBtn, cancelBtn;

    void addRow (const char* name, juce::Component& control)
    {
        auto* lab = ownedLabels.add (new juce::Label());
        lab->setText (name, juce::dontSendNotification);
        lab->setFont (juce::Font (juce::FontOptions (12.0f)));
        lab->setColour (juce::Label::textColourId, juce::Colour (0xffb8b8c2));
        content.addAndMakeVisible (lab);
        content.addAndMakeVisible (control);
        rows.push_back ({ lab, &control });
    }

    void setNum (juce::TextEditor& ed, double v)
    {
        ed.setText (juce::String (v, 9), false);
        initialText[&ed] = ed.getText();
    }

    void setNum (juce::TextEditor& ed, int v)
    {
        ed.setText (juce::String (v), false);
        initialText[&ed] = ed.getText();
    }

    void setSeed (juce::TextEditor& ed, uint64_t v)
    {
        ed.setText (formatU64 (v), false);
        initialText[&ed] = ed.getText();
    }

    bool isModified (const juce::TextEditor& ed) const
    {
        const auto it = initialText.find (&ed);
        if (it == initialText.end()) return true;   // unknown editor: be strict
        return ed.getText().trim() != it->second;
    }

    // Untouched field: keep the loaded value verbatim. This is what makes
    // Apply-without-edits an exact round trip regardless of display precision.
    bool readNum (const juce::TextEditor& ed, double& field,
                  juce::String& error, const char* name) const
    {
        if (! isModified (ed)) return true;

        double v = 0.0;
        if (! parseDouble (ed.getText(), v))
        {
            error = juce::String (name) + " is not a complete number";
            return false;
        }
        field = v;
        return true;
    }

    bool readInt (const juce::TextEditor& ed, int& field,
                  juce::String& error, const char* name) const
    {
        if (! isModified (ed)) return true;

        int v = 0;
        if (! parseInt (ed.getText(), v))
        {
            error = juce::String (name) + " must be a whole number";
            return false;
        }
        field = v;
        return true;
    }

    bool readSeed (const juce::TextEditor& ed, uint64_t& field,
                   juce::String& error, const char* name) const
    {
        if (! isModified (ed)) return true;

        uint64_t v = 0;
        if (! parseU64 (ed.getText(), v))
        {
            error = juce::String (name) + " must be a whole number in [0, 18446744073709551615]";
            return false;
        }
        field = v;
        return true;
    }

    //--------------------------------------------------------------------------
    static bool inRange (double v, double lo, double hi)
    {
        return std::isfinite (v) && v >= lo && v <= hi;
    }

    // Full-consumption parse: a value like "120.5.6" is rejected rather than
    // silently read as its valid prefix.
    static bool parseDouble (const juce::String& text, double& out)
    {
        const auto t = text.trim();
        if (t.isEmpty()) return false;

        const auto* s = t.toRawUTF8();
        const char c0 = s[0];
        if (! ((c0 >= '0' && c0 <= '9') || c0 == '+' || c0 == '-' || c0 == '.'))
            return false;

        errno = 0;
        char* end = nullptr;
        const double v = std::strtod (s, &end);
        if (end == s || end == nullptr || *end != '\0') return false;   // trailing junk
        if (! std::isfinite (v)) return false;
        // Overflow returns inf (caught above), but UNDERFLOW such as 1e-999
        // returns a finite 0 with ERANGE. Accepting that would silently turn a
        // number the user typed into a different, valid-looking one.
        if (errno == ERANGE) return false;
        out = v;
        return true;
    }

    static bool parseInt (const juce::String& text, int& out)
    {
        const auto t = text.trim();
        if (t.isEmpty()) return false;
        for (auto ch : t)
            if (ch < '0' || ch > '9') return false;

        long long acc = 0;
        for (auto ch : t)
        {
            acc = acc * 10 + (ch - '0');
            if (acc > 1000000000LL) return false;
        }
        out = (int) acc;
        return true;
    }

    // Strict unsigned decimal. Overflow is detected BEFORE it can wrap, so an
    // over-long seed is rejected rather than silently becoming a different one.
    static bool parseU64 (const juce::String& text, uint64_t& out)
    {
        const auto t = text.trim();
        if (t.isEmpty() || t.length() > 20) return false;
        for (auto ch : t)
            if (ch < '0' || ch > '9') return false;

        const uint64_t maxV = std::numeric_limits<uint64_t>::max();
        uint64_t acc = 0;
        for (int i = 0; i < t.length(); ++i)
        {
            const uint64_t digit = (uint64_t) (t[i] - '0');
            if (acc > (maxV - digit) / 10) return false;   // would overflow
            acc = acc * 10 + digit;
        }
        out = acc;
        return true;
    }

    // Decimal without a signed detour: formatting through int64 would print
    // large values as negative.
    static juce::String formatU64 (uint64_t v)
    {
        if (v == 0) return "0";
        char buf[21];
        int n = 0;
        while (v > 0 && n < 20) { buf[n++] = (char) ('0' + (int) (v % 10)); v /= 10; }
        juce::String s;
        while (n > 0) s += buf[--n];
        return s;
    }

    void tryApply()
    {
        upic::GuerreroRecipe candidate;
        juce::String error;
        if (! collect (candidate, error))
        {
            setStatus (error);
            return;
        }

        if (onApply == nullptr)
        {
            setStatus ("No handler is connected");
            return;
        }

        juce::String applyError;
        if (! onApply (candidate, applyError))
        {
            setStatus (applyError.isNotEmpty() ? applyError
                                               : juce::String ("Could not generate from this recipe"));
            return;
        }
        // Success: the owner removes this overlay.
    }
};

#endif // GUERRERO_INTEGRATION
