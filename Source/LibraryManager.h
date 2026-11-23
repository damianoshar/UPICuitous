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
        // WP7: the user library's assets carry the file they live in (the
        // piece's own assets have none - they are the piece's).
        juce::String fileName;
    };

    // WP7: where the list comes from. The piece's bank is the working set;
    // the user library is the folder of assets that outlive the piece.
    enum class Source { Piece, User };

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
        // WP7: the source switch and the library commands.
        sourceBox.setName (kind + "SourceBox");
        publishBtn.setName (kind + "PublishBtn");
        importBtn.setName (kind + "ImportBtn");
        revealBtn.setName (kind + "RevealBtn");

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
        publishBtn.setButtonText ("Publish");
        importBtn.setButtonText ("Import");
        revealBtn.setButtonText ("Folder");

        newBtn.setTooltip ("Add a new asset to the bank");
        dupBtn.setTooltip ("Copy the selected asset");
        renameBtn.setTooltip ("Rename the selected asset");
        delBtn.setTooltip ("Delete the selected asset (asks for a replacement when it is in use)");
        uniqueBtn.setTooltip ("Give the selected arcs their own copy of this asset");
        nameEditor.setTooltip ("The new name");
        replaceBox.setTooltip ("The asset that takes over every reference");
        sourceBox.setTooltip ("The piece's own bank, or the user library\n"
                              "(the folder of assets that outlive the piece)");
        publishBtn.setTooltip ("Copy the selected asset into the user library");
        importBtn.setTooltip ("Copy the selected library asset into this piece");
        revealBtn.setTooltip ("Open the user library's folder");

        sourceBox.addItem ("Piece", 1);
        sourceBox.addItem ("User", 2);
        sourceBox.setSelectedId (1, juce::dontSendNotification);
        sourceBox.onChange = [this]
        {
            setSource (sourceBox.getSelectedId() == 2 ? Source::User : Source::Piece);
        };

        for (auto* b : { &newBtn, &dupBtn, &renameBtn, &delBtn, &uniqueBtn,
                         &renameOk, &renameCancel, &deleteOk, &deleteCancel,
                         &publishBtn, &importBtn, &revealBtn })
            addAndMakeVisible (b);
        addAndMakeVisible (nameEditor);
        addAndMakeVisible (replaceBox);
        addAndMakeVisible (sourceBox);

        newBtn.onClick    = [this]{ if (onNew) onNew(); };
        dupBtn.onClick    = [this]{ if (onDuplicate) onDuplicate(); };
        renameBtn.onClick = [this]{ beginRename(); };
        delBtn.onClick    = [this]{ beginDelete(); };
        uniqueBtn.onClick = [this]{ if (onMakeUnique) onMakeUnique(); };
        publishBtn.onClick = [this]{ if (onPublish) onPublish(); };
        importBtn.onClick  = [this]{ if (onImport) onImport(); };
        revealBtn.onClick  = [this]{ if (onReveal) onReveal(); };

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
        pieceItems = newItems;
        if (currentSource == Source::Piece)
            selectAsset (selectedId);
        rebuildSelector(); // the item text carries the new names and counts
    }

    // WP7: the user library's assets (the folder outside the piece).
    void setUserItems (const std::vector<Item>& newItems)
    {
        // A refresh of the other library must not move this list's
        // selection. Keep the selected file when it is still there; only a
        // file that really disappeared falls back to the first row.
        const auto keep = selectedFileName();
        userItems = newItems;
        if (currentSource == Source::User)
        {
            selected = -1;
            for (size_t i = 0; i < userItems.size(); ++i)
                if (userItems[i].fileName == keep) { selected = (int) i; break; }
            rebuildSelector();
            updateButtons();
            repaint();
        }
    }

    // WP7: the source switch. In "User" the list is the library's, and the
    // bank commands (New, Dup, Rename, Delete, Unique) are hidden - they act
    // on the piece's own bank.
    void setSource (Source newSource)
    {
        if (currentSource == newSource) return;
        currentSource = newSource;
        sourceBox.setSelectedId (currentSource == Source::User ? 2 : 1, juce::dontSendNotification);
        // The source switch deliberately starts with no selection. The owner
        // then fills the requested source; an item from the other source
        // must never be mistaken for a library asset.
        selected = -1;
        rebuildSelector();
        showModeControls();
        resized();
        repaint();
        ++sourceChangeCount;
        if (onSourceChanged) onSourceChanged (currentSource);
    }

    Source source() const { return currentSource; }

    // Move the selection without telling the selector (the model already
    // knows): used when the toolbar follows an arc. The bank commands always
    // act on the model's selection, so this only changes what is shown.
    void selectAsset (int assetId)
    {
        // A model id belongs to the piece bank. Never use it to select a
        // row from the user library: the two id spaces are deliberately
        // independent, and a coincidentally equal number must not turn a
        // library row into a model selection.
        if (currentSource != Source::Piece)
            return;

        const auto& list = activeItems();
        int found = -1;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[(size_t) i].id == assetId) { found = (int) i; break; }
        if (found < 0 && ! list.empty()) found = 0;
        selected = found;

        if (selector != nullptr)
            selector->setSelectedId (selected + 1, juce::dontSendNotification);

        updateButtons();
        repaint();
    }

    int selectedAssetId() const
    {
        const auto& list = activeItems();
        return selected >= 0 && selected < (int) list.size() ? list[(size_t) selected].id : -1;
    }

    // WP7: the selected user asset's file (empty in the piece's own list).
    juce::String selectedFileName() const
    {
        const auto& list = activeItems();
        return selected >= 0 && selected < (int) list.size()
             ? list[(size_t) selected].fileName : juce::String();
    }

    // --- the model callbacks ------------------------------------------------
    // Return true when the command happened. A false return leaves the
    // manager in its current mode (the model has already explained why).
    std::function<bool()> onNew;
    std::function<bool()> onDuplicate;
    std::function<bool()> onMakeUnique;
    std::function<bool (const juce::String&)> onRename;
    std::function<bool (int)> onDelete; // the replacement asset id, -1 = unused
    // WP7: the user library's commands. Publish copies the piece's selected
    // asset into the library; Import copies a library asset into the piece;
    // Reveal opens the library's folder.
    std::function<bool()> onPublish, onImport, onReveal;
    // WP7: the source changed. The owner reads the new source's folder and
    // fills the list (the manager owns no disk or model logic).
    std::function<void (Source)> onSourceChanged;
    // The owner can follow a selection made in the manager's selector. It
    // is only sent for the piece source; a user-library id belongs to the
    // file and is never treated as a model id.
    std::function<void (int)> onSelectedId;

    // Some panels already have their own piece-bank commands (the amplitude
    // tables, the sequences and the four page spaces do). In those panels
    // this manager is only the Piece/User bridge, so the duplicate New /
    // Dup / Rename / Delete / Unique buttons are hidden.
    void setPieceCommandsVisible (bool visible)
    {
        pieceCommandsVisible = visible;
        showModeControls();
        resized();
        repaint();
    }

    // --- the selftest hooks ------------------------------------------------
    int itemCountForTest() const { return (int) activeItems().size(); }
    int selectedIndexForTest() const { return selected; }
    int usesForTest (int index) const
    {
        const auto& list = activeItems();
        return index >= 0 && index < (int) list.size() ? list[(size_t) index].uses : -1;
    }
    bool isRenameModeForTest() const { return mode == Mode::rename; }
    bool isDeleteModeForTest() const { return mode == Mode::remove; }
    // WP7
    bool isUserSourceForTest() const { return currentSource == Source::User; }
    int sourceChangeCountForTest() const { return sourceChangeCount; }
    int userItemCountForTest() const { return (int) userItems.size(); }
    int pieceItemCountForTest() const { return (int) pieceItems.size(); }
    juce::String selectedFileNameForTest() const { return selectedFileName(); }
    void setSourceForTest (bool user) { setSource (user ? Source::User : Source::Piece); }
    void selectIndexForTest (int index)
    {
        selected = index;
        if (selector != nullptr) selector->setSelectedId (selected + 1, juce::dontSendNotification);
        updateButtons();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (upic::uiBackground());
        const auto& list = activeItems();
        if (mode == Mode::none && selected >= 0 && selected < (int) list.size())
        {
            const auto c = list[(size_t) selected].colour;
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

        // WP7: the source switch is on the left, the buttons on the right.
        // The button set follows the source: the piece's bank has the five
        // bank commands plus Publish, the user library has Import and the
        // folder button.
        const int srcW = (int) std::lround (58.0f * k);
        sourceBox.setBounds (r.removeFromLeft (srcW).reduced (1));
        r.removeFromLeft (gap);

        std::vector<juce::TextButton*> row;
        std::vector<int> widths;
        if (currentSource == Source::Piece)
        {
            row = { &newBtn, &dupBtn, &renameBtn, &delBtn, &uniqueBtn, &publishBtn };
            widths = { (int) std::lround (36.0f * k), (int) std::lround (42.0f * k),
                       (int) std::lround (52.0f * k), (int) std::lround (48.0f * k),
                       (int) std::lround (52.0f * k), (int) std::lround (56.0f * k) };
            if (! pieceCommandsVisible)
            {
                row = { &publishBtn };
                widths = { (int) std::lround (56.0f * k) };
            }
        }
        else
        {
            row = { &importBtn, &revealBtn };
            widths = { (int) std::lround (56.0f * k), (int) std::lround (52.0f * k) };
        }

        const int sw = juce::jmax (8, (int) std::lround (14.0f * k));
        // Keep the selector readable: the buttons give way first (a narrow
        // editor column is still a usable bank, a vanished selector is not).
        // The source switch has already been removed from `r`, so it is not
        // part of the space this calculation divides.
        const int minSelector = (int) std::lround (70.0f * k);
        int fixed = sw + (int) widths.size() * gap + minSelector;
        for (int w : widths) fixed += w;
        const int avail = r.getWidth();
        if (fixed > avail)
        {
            const int room = juce::jmax ((int) widths.size() * 14,
                                         avail - (int) widths.size() * gap - sw - minSelector);
            int sum = 0;
            for (int w : widths) sum += w;
            if (sum > 0)
                for (auto& w : widths)
                    w = juce::jmax (14, (int) std::lround ((float) w * (float) room / (float) sum));
        }

        for (int i = (int) row.size() - 1; i >= 0; --i)
        {
            row[(size_t) i]->setBounds (r.removeFromRight (widths[(size_t) i]).reduced (1));
            if (i > 0) r.removeFromRight (gap);
        }

        swatch = r.removeFromRight (sw).reduced (1, juce::jmax (2, r.getHeight() / 5));
        r.removeFromRight (gap);

        if (selector != nullptr)
            selector->setBounds (r);
    }

private:
    enum class Mode { none, rename, remove };

    juce::String kind;
    std::vector<Item> pieceItems;   // the piece's own bank
    std::vector<Item> userItems;    // WP7: the user library
    Source currentSource = Source::Piece;
    bool pieceCommandsVisible = true;
    int selected = -1;
    int sourceChangeCount = 0;
    Mode mode = Mode::none;
    // The selector belongs to the owner, not to this manager. Some owners
    // declare their selector after the manager, so C++ destroys the combo
    // first. A SafePointer becomes null as soon as that happens and keeps
    // the manager's destructor from touching the freed listener list.
    juce::Component::SafePointer<juce::ComboBox> selector;
    juce::Rectangle<int> swatch;

    juce::TextButton newBtn, dupBtn, renameBtn, delBtn, uniqueBtn;
    juce::TextEditor nameEditor;
    juce::TextButton renameOk, renameCancel;
    juce::ComboBox replaceBox;
    juce::TextButton deleteOk, deleteCancel;
    // WP7: the source switch and the library commands.
    juce::ComboBox sourceBox;
    juce::TextButton publishBtn, importBtn, revealBtn;

    const std::vector<Item>& activeItems() const
    {
        return currentSource == Source::User ? userItems : pieceItems;
    }

    bool hasSelection() const
    {
        const auto& list = activeItems();
        return selected >= 0 && selected < (int) list.size();
    }

    void comboBoxChanged (juce::ComboBox*) override
    {
        if (selector == nullptr) return;
        selected = selector->getSelectedId() - 1;
        if (selected < 0 || selected >= (int) activeItems().size()) selected = -1;
        updateButtons();
        repaint();
        if (currentSource == Source::Piece && selected >= 0 && onSelectedId)
            onSelectedId (activeItems()[(size_t) selected].id);
    }

    void rebuildSelector()
    {
        if (selector == nullptr) return;
        selector->clear (juce::dontSendNotification);
        for (size_t i = 0; i < activeItems().size(); ++i)
        {
            const auto& it = activeItems()[(size_t) i];
            selector->addItem (juce::String (it.id) + ": " + it.name
                                 + "  (" + juce::String (it.uses)
                                 + (it.uses == 1 ? " use)" : " uses)"),
                               (int) i + 1);
        }
        selector->setSelectedId (selected + 1, juce::dontSendNotification);
    }

    void updateButtons()
    {
        const auto& list = activeItems();
        const bool has = hasSelection();
        const bool piece = (currentSource == Source::Piece);
        dupBtn.setEnabled (has && piece);
        renameBtn.setEnabled (has && piece);
        delBtn.setEnabled (has && piece && list.size() > 1);
        uniqueBtn.setEnabled (has && piece && list[(size_t) selected].uses > 0);
        publishBtn.setEnabled (has && piece);
        importBtn.setEnabled (has && ! piece);
        revealBtn.setEnabled (! piece);
    }

    void beginRename()
    {
        if (! hasSelection() || currentSource != Source::Piece) return;
        mode = Mode::rename;
        nameEditor.setText (activeItems()[(size_t) selected].name, false);
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
        if (! hasSelection() || currentSource != Source::Piece || activeItems().size() <= 1) return;
        const auto& item = activeItems()[(size_t) selected];
        if (item.uses == 0)
        {
            // nothing references it: no replacement to ask for
            if (onDelete) onDelete (-1);
            return;
        }

        mode = Mode::remove;
        replaceBox.clear (juce::dontSendNotification);
        for (size_t i = 0; i < activeItems().size(); ++i)
        {
            if ((int) i == selected) continue;
            const auto& other = activeItems()[i];
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
        if (idx < 0 || idx >= (int) activeItems().size()) return;
        if (onDelete && onDelete (activeItems()[(size_t) idx].id))
            endMode();
    }

    void showModeControls()
    {
        const bool ren = (mode == Mode::rename);
        const bool rem = (mode == Mode::remove);
        const bool normal = (mode == Mode::none);
        const bool piece = (currentSource == Source::Piece);

        if (selector != nullptr) selector->setVisible (normal);
        sourceBox.setVisible (normal);
        newBtn.setVisible (normal && piece && pieceCommandsVisible);
        dupBtn.setVisible (normal && piece && pieceCommandsVisible);
        renameBtn.setVisible (normal && piece && pieceCommandsVisible);
        delBtn.setVisible (normal && piece && pieceCommandsVisible);
        uniqueBtn.setVisible (normal && piece && pieceCommandsVisible);
        publishBtn.setVisible (normal && piece);
        importBtn.setVisible (normal && ! piece);
        revealBtn.setVisible (normal && ! piece);

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
