#include <JuceHeader.h>
#include "MainComponent.h"
#include "PickedFile.h"
#include "PieceCodec.h"
#include "SelfTest.h"

//==============================================================================
// R4.2: the document's file adapter - the contract.
//
// A piece reaches the disk through two adapters: the reader (the size cap,
// the read, the JSON parse and the guard around the parse) and the writer
// (the JSON build, the measurement, the refusal and the write). Today they
// are methods of the shell - MainComponent::loadFile() and
// MainComponent::savePieceToFile() - and the review asks for them to be an
// edge of their own (R064: "separate file picking and document I/O").
//
// This suite is that edge's contract, and it comes before the move, as every
// phase in this wave does. It pins what the adapter does today:
//
// - the writer refuses a piece over the cap, says exactly what to do about
//   it, and does not touch the destination;
// - the writer writes a piece that fits, says what it saved, and the piece
//   that comes back is the piece that went in;
// - a write that fails keeps what was there;
// - the reader refuses a file that is not there, a file over the cap and a
//   file that was never chosen, and it reads a local file in place.
//
// The writer is reached through the shell's own door
// (MainComponent::savePieceToFile) and the reader through the file layer's
// door (upic::readPickedFile). The move turns the reader's door into the
// edge's, and the checks that are here do not change.
//
// The sentences the shell words for the user when a file is not a piece at
// all ("Not a UPIC piece (the JSON is broken) - the current piece was
// kept") live in the chooser's callback today, where no test can reach
// them: the read gets its door with the move, and the sentences are pinned
// there - the R3.5 precedent, where the doors come with the move.

namespace
{
    // The size of a document, in words a person can read ("12 MB"). The
    // rule is written out here, so the suite pins the wording of the
    // refusal and not the helper that produces it.
    juce::String humanSize (juce::int64 bytes)
    {
        return bytes >= 1024 * 1024 ? (juce::String ((int) (bytes / (1024 * 1024))) + " MB")
                                    : (juce::String ((int) (bytes / 1024)) + " KB");
    }

    // A component with a piece in it: one page, one arc.
    std::unique_ptr<MainComponent> componentWithAPiece()
    {
        auto mc = std::make_unique<MainComponent>();
        mc->setSize (1280, 800);
        mc->loadFromVar (juce::JSON::parse (
            "{\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,"
            "\"intensity\":0.8,\"points\":[{\"t\":0,\"f\":220},{\"t\":15,\"f\":440}]}]}]}"));
        return mc;
    }
}

void upic::runDocumentIOChecks (SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the writer ---------------------------------------------------------
    //
    // The saver is the half of the adapter that can be reached today: the
    // shell hands it a destination and a limit, and it either writes the
    // piece or refuses it.
    {
        auto holder = componentWithAPiece();
        auto& mc = *holder;

        // the piece that would be written, measured the way the saver
        // measures it: the JSON of the captured model.
        const auto model = mc.capturePieceModel();
        const juce::String json = juce::JSON::toString (upic::pieceToVar (model), true);
        const auto bytes = (juce::int64) json.getNumBytesAsUTF8();

        const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("upic_document_io_test");
        work.deleteRecursively();
        work.createDirectory();
        const auto file = work.getChildFile ("piece.upic.json");
        file.deleteFile();

        // --- a piece over the cap is refused, and the destination is not
        //     touched ---
        const bool refused = mc.savePieceToFile (file, 1024);
        check (! refused && ! file.existsAsFile(),
               "R4.2 a piece over the cap is refused and the destination is untouched");

        // ...and the refusal says exactly what to do about it
        const juce::String expected = "Piece too large to save (" + humanSize (bytes)
                                    + ", the limit is " + humanSize (1024)
                                    + ") - shorten the capture or the sample, "
                                      "or export the audio to a WAV instead";
        check (mc.flashMessageForTest() == expected,
               "R4.2 the refusal says what to do about it",
               mc.flashMessageForTest());

        // --- a piece that fits is written ---
        mc.markModelChanged();
        const bool saved = mc.savePieceToFile (file);
        check (saved && file.existsAsFile() && file.getSize() > 0,
               "R4.2 a piece that fits is written",
               juce::String ((int) file.getSize()) + " byte(s)");

        // ...and the write says what it saved
        check (mc.flashMessageForTest() == "Saved " + file.getFileName(),
               "R4.2 the write says what it saved",
               mc.flashMessageForTest());

        // ...and the piece that is on the disk is the piece of the session
        check (! mc.pieceHasUnsavedChanges(),
               "R4.2 a written piece has nothing to save");

        // --- the piece that comes back is the piece that went in ---
        {
            auto second = std::make_unique<MainComponent>();
            second->setSize (1280, 800);
            const auto loaded = second->loadFromVar (juce::JSON::parse (file.loadFileAsString()));
            const auto before = juce::JSON::toString (mc.pieceToVar(), false);
            const auto after = juce::JSON::toString (second->pieceToVar(), false);
            check (loaded && before == after,
                   "R4.2 the written piece is the piece that was saved",
                   loaded ? after.substring (0, 200) : juce::String ("the piece did not load"));
        }

        // --- a refused save does not touch the piece that is already there ---
        {
            const auto before = file.loadFileAsString();
            const bool again = mc.savePieceToFile (file, 1024);
            check (! again && file.existsAsFile() && file.loadFileAsString() == before,
                   "R4.2 a refused save leaves the file that was there alone");
        }

        // --- a write that fails keeps what was there ---
        {
            // a directory where the file should go: the write cannot replace
            // it, and the refusal leaves it alone
            const auto blocked = work.getChildFile ("blocked.upic.json");
            blocked.deleteRecursively();
            blocked.createDirectory();
            const bool failed = mc.savePieceToFile (blocked);
            check (! failed && blocked.isDirectory(),
                   "R4.2 a write that fails keeps what was there");
            check (mc.flashMessageForTest() == "Could not write " + blocked.getFileName(),
                   "R4.2 a failed write says what went wrong",
                   mc.flashMessageForTest());
            blocked.deleteRecursively();
        }

        // --- a destination that cannot be opened is refused ---
        {
            const auto missing = work.getChildFile ("no_such_folder").getChildFile ("piece.json");
            const bool failed = mc.savePieceToFile (missing);
            check (! failed && ! missing.existsAsFile(),
                   "R4.2 a destination that cannot be opened is refused");
            check (mc.flashMessageForTest() == "Could not write " + missing.getFileName(),
                   "R4.2 a destination that cannot be opened says what went wrong",
                   mc.flashMessageForTest());
        }

        // --- no destination at all: nothing is written and nothing is said ---
        {
            const auto before = mc.flashMessageForTest();
            const bool written = mc.savePieceToFile (juce::File());
            check (! written && mc.flashMessageForTest() == before,
                   "R4.2 a piece with no destination is not written and nothing is said");
        }
    }

    // --- the reader ---------------------------------------------------------
    //
    // The reader is the file layer's door today: it takes the URL the
    // chooser handed over and refuses the files it cannot use.
    {
        const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("upic_document_io_test");
        work.deleteRecursively();
        work.createDirectory();
        const auto cap = (juce::int64) upic::limits::kMaxPieceBytes;

        // a file that is not there
        {
            const auto missing = work.getChildFile ("not_here.upic.json");
            missing.deleteFile();
            upic::PickedFile picked;
            juce::String error;
            const bool ok = upic::readPickedFile (juce::URL (missing), picked, cap, error);
            check (! ok && error == "The file does not exist: not_here.upic.json",
                   "R4.2 a file that is not there is refused", error);
        }

        // a file over the cap
        {
            const auto big = work.getChildFile ("big.upic.json");
            big.replaceWithText (juce::String::repeatedString ("x", 2000));
            upic::PickedFile picked;
            juce::String error;
            const bool ok = upic::readPickedFile (juce::URL (big), picked, 1024, error);
            check (! ok && error == "The file is too large: big.upic.json",
                   "R4.2 a file over the cap is refused", error);
        }

        // no file was chosen
        {
            upic::PickedFile picked;
            juce::String error;
            const bool ok = upic::readPickedFile (juce::URL(), picked, cap, error);
            check (! ok && error == "No file was chosen",
                   "R4.2 a file that was never chosen is refused", error);
        }

        // a local file is read in place - nothing is copied
        {
            const auto local = work.getChildFile ("local.upic.json");
            local.replaceWithText ("{}");
            upic::PickedFile picked;
            juce::String error;
            const bool ok = upic::readPickedFile (juce::URL (local), picked, cap, error);
            check (ok && picked.file == local && ! picked.isCopy,
                   "R4.2 a local file is read in place", error);
        }
    }

    // --- the parse ----------------------------------------------------------
    //
    // The reader hands the file's text to the JSON parser, and a file that
    // is not JSON at all does not become a document.
    {
        check (juce::JSON::parse ("not json at all").isVoid(),
               "R4.2 a file that is not JSON does not parse into a document");
    }
}
