#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include "UpicTheme.h"

//==============================================================================
// F2/F3 (2026-11-17): the shared library manager, used twice - once for the
// wavetable bank and once for the envelope bank.
//
// It is the editor's selector plus the bank's commands (New, Duplicate,
// Rename, Delete, Make unique). The list shows every asset's name and use
// count, and the selected asset's colour is the swatch beside it (the place
// F7's colour override will land).
//
// The manager owns no model logic. The MainComponent callbacks do the work
// (one undo transaction each) and return false when they refuse; the manager
// then stays in the mode it was in so the user can correct the name or pick
// another replacement. A refusal always comes with a flash message from the
// model (the H13 pattern: refuse and explain, never truncate silently).
class LibraryManager : public juce::Component,
                       private juce::ComboBox::Listener
{
public:
    struct Item
    {
        int id = 0;
        juce::String name;
        juce::Colour colour;
        int uses = 0;
    };

    explicit LibraryManager (const juce::String& prefix) : kind (prefix)
    {
        setName (kind + "Lib");
        newBtn.setName (kind + "NewBtn");
        dupBtn.setName (kind + "DupBtn");
        renameBtn.setName (kind + "RenameBtn");
        delBtn.setName (kind + "DelBtn");
        uniqueBtn.setName (kind + "UniqueBtn");
        nameEditor.setName (kind + "RenameEdit");
        renameOk.setName (kind + "RenameOk");
        renameCancel.setName (kind + "RenameCancel");
        replaceBox.setName (kind + "ReplaceBox");
        deleteOk.setName (kind + "DeleteOk");
        deleteCancel.setName (kind + "DeleteCancel");

        newBtn.setButtonText ("New");
        dupBtn.setButtonText ("Dup");
        renameBtn.setButtonText ("Rename");
        delBtn.setButtonText ("Delete");
        uniqueBtn.setButtonText ("Unique");
        renameOk.setButtonText ("OK");
        renameCancel.setButtonText ("Cancel");
        deleteOk.setButtonText ("Delete");
        deleteCancel.setButtonText ("Cancel");
        replaceBox.setTextWhenNothingSelected ("Replace with...");

        newBtn.setTooltip ("Add a new asset to the bank");
        dupBtn.setTooltip ("Copy the selected asset");
        renameBtn.setTooltip ("Rename the selected asset");
        delBtn.setTooltip ("Delete the selected asset (asks for a replacement when it is in use)");
        uniqueBtn.setTooltip ("Give the selected arcs their own copy of this asset");
        nameEditor.setTooltip ("The new name");
        replaceBox.setTooltip ("The asset that takes over every reference");

        for (auto* b : { &newBtn, &dupBtn, &renameBtn, &delBtn, &uniqueBtn,
                         &renameOk, &renameCancel, &deleteOk, &deleteCancel })
            addAndMakeVisible (b);
        addAndMakeVisible (nameEditor);
        addAndMakeVisible (replaceBox);

        newBtn.onClick    = [this]{ if (onNew) onNew(); };
        dupBtn.onClick    = [this]{ if (onDuplicate) onDuplicate(); };
        renameBtn.onClick = [this]{ beginRename(); };
        delBtn.onClick    = [this]{ beginDelete(); };
        uniqueBtn.onClick = [this]{ if (onMakeUnique) onMakeUnique(); };

        renameOk.onClick = [this]{ commitRename(); };
        renameCancel.onClick = [this]{ endMode(); };
        deleteOk.onClick = [this]{ commitDelete(); };
        deleteCancel.onClick = [this]{ endMode(); };
        nameEditor.onReturnKey = [this]{ commitRename(); };
        nameEditor.onEscapeKey = [this]{ endMode(); };

        endMode(); // the selector is set later (setSelector)
    }

    ~LibraryManager() override
    {
        if (selector != nullptr)
            selector->removeListener (this);
    }

    // The editor's asset selector (the old waveEditBox/envEditBox). The
    // manager takes ownership of its placement, not of the object.
    void setSelector (juce::ComboBox& box)
    {
        if (selector != nullptr)
            selector->removeListener (this);
        selector = &box;
        box.addListener (this);
        box.setName (kind + "EditBox");
        addAndMakeVisible (box);
        rebuildSelector();
        resized();
    }

    // Refill the bank. `selectedId` is an asset id, not a combo item id.
    void setItems (const std::vector<Item>& newItems, int selectedId)
    {
        items = newItems;
        selectAsset (selectedId);
        rebuildSelector(); // the item text carries the new names and counts
    }

    // Move the selection without telling the selector (the model already
    // knows): used when the toolbar follows an arc. The bank commands always
    // act on the model's selection, so this only changes what is shown.
    void selectAsset (int assetId)
    {
        int found = -1;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[(size_t) i].id == assetId) { found = (int) i; break; }
        if (found < 0 && ! items.empty()) found = 0;
        selected = found;

        if (selector != nullptr)
            selector->setSelectedId (selected + 1, juce::dontSendNotification);

        updateButtons();
        repaint();
    }

    int selectedAssetId() const
    {
        return selected >= 0 && selected < (int) items.size() ? items[(size_t) selected].id : -1;
    }

    // --- the model callbacks ------------------------------------------------
    // Return true when the command happened. A false return leaves the
    // manager in its current mode (the model has already explained why).
    std::function<bool()> onNew;
    std::function<bool()> onDuplicate;
    std::function<bool()> onMakeUnique;
    std::function<bool (const juce::String&)> onRename;
    std::function<bool (int)> onDelete; // the replacement asset id, -1 = unused

    // --- the selftest hooks ------------------------------------------------
    int itemCountForTest() const { return (int) items.size(); }
    int selectedIndexForTest() const { return selected; }
    int usesForTest (int index) const
    {
        return index >= 0 && index < (int) items.size() ? items[(size_t) index].uses : -1;
    }
    bool isRenameModeForTest() const { return mode == Mode::rename; }
    bool isDeleteModeForTest() const { return mode == Mode::remove; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (upic::uiBackground());
        if (mode == Mode::none && selected >= 0 && selected < (int) items.size())
        {
            const auto c = items[(size_t) selected].colour;
            g.setColour (c);
            g.fillRoundedRectangle (swatch.toFloat(), 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (swatch.toFloat(), 2.0f, 1.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        const float k = juce::jmax (0.70f, (float) getHeight() / 26.0f);
        const int gap = juce::jmax (2, (int) std::lround (3.0f * k));

        if (mode == Mode::rename)
        {
            const int okW = (int) std::lround (38.0f * k);
            const int cancelW = (int) std::lround (54.0f * k);
            renameOk.setBounds (r.removeFromRight (okW).reduced (1));
            r.removeFromRight (gap);
            renameCancel.setBounds (r.removeFromRight (cancelW).reduced (1));
            r.removeFromRight (gap);
            nameEditor.setBounds (r);
            return;
        }
        if (mode == Mode::remove)
        {
            const int okW = (int) std::lround (54.0f * k);
            const int cancelW = (int) std::lround (54.0f * k);
            deleteOk.setBounds (r.removeFromRight (okW).reduced (1));
            r.removeFromRight (gap);
            deleteCancel.setBounds (r.removeFromRight (cancelW).reduced (1));
            r.removeFromRight (gap);
            replaceBox.setBounds (r);
            return;
        }

        int widths[5] = { (int) std::lround (36.0f * k), (int) std::lround (42.0f * k),
                          (int) std::lround (52.0f * k), (int) std::lround (48.0f * k),
                          (int) std::lround (52.0f * k) }; // New Dup Rename Delete Unique
        const int sw = juce::jmax (8, (int) std::lround (14.0f * k));
        // keep the selector readable: the buttons give way first (a narrow
        // editor column is still a usable bank, a vanished selector is not)
        const int minSelector = (int) std::lround (70.0f * k);
        int fixed = sw + 5 * gap + minSelector;
        for (int w : widths) fixed += w;
        const int avail = r.getWidth();
        if (fixed > avail)
        {
            const int room = juce::jmax (5 * 14, avail - 5 * gap - sw - minSelector);
            int sum = 0;
            for (int w : widths) sum += w;
            if (sum > 0)
                for (int& w : widths)
                    w = juce::jmax (14, (int) std::lround ((float) w * (float) room / (float) sum));
        }

        uniqueBtn.setBounds (r.removeFromRight (widths[4]).reduced (1));
        r.removeFromRight (gap);
        delBtn.setBounds (r.removeFromRight (widths[3]).reduced (1));
        r.removeFromRight (gap);
        renameBtn.setBounds (r.removeFromRight (widths[2]).reduced (1));
        r.removeFromRight (gap);
        dupBtn.setBounds (r.removeFromRight (widths[1]).reduced (1));
        r.removeFromRight (gap);
        newBtn.setBounds (r.removeFromRight (widths[0]).reduced (1));
        r.removeFromRight (gap);

        swatch = r.removeFromRight (sw).reduced (1, juce::jmax (2, r.getHeight() / 5));
        r.removeFromRight (gap);

        if (selector != nullptr)
            selector->setBounds (r);
    }

private:
    enum class Mode { none, rename, remove };

    juce::String kind;
    std::vector<Item> items;
    int selected = -1;
    Mode mode = Mode::none;
    juce::ComboBox* selector = nullptr;
    juce::Rectangle<int> swatch;

    juce::TextButton newBtn, dupBtn, renameBtn, delBtn, uniqueBtn;
    juce::TextEditor nameEditor;
    juce::TextButton renameOk, renameCancel;
    juce::ComboBox replaceBox;
    juce::TextButton deleteOk, deleteCancel;

    bool hasSelection() const
    {
        return selected >= 0 && selected < (int) items.size();
    }

    void comboBoxChanged (juce::ComboBox*) override
    {
        if (selector == nullptr) return;
        selected = selector->getSelectedId() - 1;
        if (selected < 0 || selected >= (int) items.size()) selected = -1;
        updateButtons();
        repaint();
    }

    void rebuildSelector()
    {
        if (selector == nullptr) return;
        selector->clear (juce::dontSendNotification);
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[(size_t) i];
            selector->addItem (juce::String (it.id) + ": " + it.name
                                 + "  (" + juce::String (it.uses)
                                 + (it.uses == 1 ? " use)" : " uses)"),
                               (int) i + 1);
        }
        selector->setSelectedId (selected + 1, juce::dontSendNotification);
    }

    void updateButtons()
    {
        const bool has = hasSelection();
        dupBtn.setEnabled (has);
        renameBtn.setEnabled (has);
        delBtn.setEnabled (has && items.size() > 1);
        uniqueBtn.setEnabled (has && items[(size_t) selected].uses > 0);
    }

    void beginRename()
    {
        if (! hasSelection()) return;
        mode = Mode::rename;
        nameEditor.setText (items[(size_t) selected].name, false);
        showModeControls();
        resized();
        nameEditor.grabKeyboardFocus();
        nameEditor.selectAll();
        repaint();
    }

    void commitRename()
    {
        if (mode != Mode::rename) return;
        if (onRename && onRename (nameEditor.getText()))
            endMode();
    }

    void beginDelete()
    {
        if (! hasSelection() || items.size() <= 1) return;
        const auto& item = items[(size_t) selected];
        if (item.uses == 0)
        {
            // nothing references it: no replacement to ask for
            if (onDelete) onDelete (-1);
            return;
        }

        mode = Mode::remove;
        replaceBox.clear (juce::dontSendNotification);
        for (size_t i = 0; i < items.size(); ++i)
        {
            if ((int) i == selected) continue;
            const auto& other = items[i];
            replaceBox.addItem (juce::String (other.id) + ": " + other.name
                                  + "  (" + juce::String (other.uses)
                                  + (other.uses == 1 ? " use)" : " uses)"),
                                (int) i + 1);
        }
        const int first = selected == 0 ? 2 : 1;
        replaceBox.setSelectedId (first, juce::dontSendNotification);
        showModeControls();
        resized();
        repaint();
    }

    void commitDelete()
    {
        if (mode != Mode::remove) return;
        const int idx = replaceBox.getSelectedId() - 1;
        if (idx < 0 || idx >= (int) items.size()) return;
        if (onDelete && onDelete (items[(size_t) idx].id))
            endMode();
    }

    void showModeControls()
    {
        const bool ren = (mode == Mode::rename);
        const bool rem = (mode == Mode::remove);
        const bool normal = (mode == Mode::none);

        if (selector != nullptr) selector->setVisible (normal);
        newBtn.setVisible (normal);
        dupBtn.setVisible (normal);
        renameBtn.setVisible (normal);
        delBtn.setVisible (normal);
        uniqueBtn.setVisible (normal);

        nameEditor.setVisible (ren);
        renameOk.setVisible (ren);
        renameCancel.setVisible (ren);

        replaceBox.setVisible (rem);
        deleteOk.setVisible (rem);
        deleteCancel.setVisible (rem);
    }

    void endMode()
    {
        mode = Mode::none;
        showModeControls();
        resized();
        repaint();
    }
};
