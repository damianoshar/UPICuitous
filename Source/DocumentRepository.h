#pragma once

//==============================================================================
// R4.2: the document's file adapter - the edge.
//
// A piece reaches the disk through two adapters: the reader (the size cap,
// the read, the JSON parse and the guard around the parse) and the writer
// (the JSON build, the measurement, the refusal and the write). They are
// step 1 of the review's loading pipeline (section 32) - "Read document:
// file adapter and size limits" - and they were methods of the shell.
//
// This is that step. The edge reads a document from a picked source and
// writes a piece to a picked destination, and it owns the size limits and
// the words of the refusals that belong to the file. The piece's format is
// the codec's (Source/PieceCodec.h) and the commit is the shell's.
//
// The edge reports what happened; the shell words it for the user. "The
// current piece was kept" and "Saved X" are the shell's - they are about
// the app's state, not about the file.
//
// The plan defers the type migration (section 2), so the results carry the
// JUCE types the edge already uses. The wording of the messages does not
// change by a character: they are the strings the shell showed before, and
// the suite (Source/SelfTestDocumentIO.cpp) pins them.
#include <JuceHeader.h>

#include <exception>
#include <string>

#include "PieceCodec.h"
#include "PickedFile.h"
#include "PieceLimits.h"

namespace upic
{
    //================ why a document could not be read ======================
    //
    // The review asks for structured load errors rather than a single
    // success flag (section 32). The edge says what happened; the shell
    // words it for the user.
    enum class DocumentReadFailure
    {
        none,          // the document was read
        noFile,        // no file was chosen
        missing,       // the file does not exist
        tooLarge,      // the file is over the cap
        tooLargeText,  // the file's text is over the cap (the file's size was not)
        brokenJson,    // the file is not JSON at all
        unreadable     // the file could not be read
    };

    //================ the result of reading a document ======================
    struct DocumentReadResult
    {
        bool ok = false;
        juce::var document;                            // the document (when it was read)
        DocumentReadFailure failure = DocumentReadFailure::none;
        juce::String error;                            // the file layer's message (when the file is the problem)
    };

    //================ the result of writing a document ======================
    struct DocumentWriteResult
    {
        bool ok = false;
        juce::String error;                            // the reason it could not be written
        juce::int64 bytes = 0;                         // the size of the document that was written
    };

    //================ the size of a document, in words ======================
    //
    // "12 MB" - the size the refusals are worded in.
    inline juce::String humanPieceSize (juce::int64 bytes)
    {
        return bytes >= 1024 * 1024 ? (juce::String ((int) (bytes / (1024 * 1024))) + " MB")
                                    : (juce::String ((int) (bytes / 1024)) + " KB");
    }

    //================ the reader =============================================
    //
    // The document is read from a picked source - a local file or a
    // document provider's document (Android). The cap is checked before
    // the file is read, the parse is guarded, and a file that is not JSON
    // is refused: the caller's piece is not touched.
    inline DocumentReadResult readDocument (const juce::URL& url, juce::int64 maxBytes)
    {
        DocumentReadResult out;

        upic::PickedFile picked;
        juce::String error;
        if (! upic::readPickedFile (url, picked, maxBytes, error))
        {
            // The file layer refused the file and said why ("No file was
            // chosen", "The file does not exist: X", "The file is too
            // large: X"). The edge labels the refusal - it does not read
            // the message to do it: a URL that is not well formed is no
            // file at all, a local file that is not there is missing, and
            // a local file over the cap is too large. A provider document
            // (Android) that is refused is unreadable - the file layer's
            // message says which.
            out.error = error;
            out.failure = DocumentReadFailure::unreadable;
            if (! url.isWellFormed())
                out.failure = DocumentReadFailure::noFile;
            else if (upic::isPlainFileURL (url))
            {
                const auto f = url.getLocalFile();
                if (! f.existsAsFile())
                    out.failure = DocumentReadFailure::missing;
                else if (f.getSize() > maxBytes)
                    out.failure = DocumentReadFailure::tooLarge;
            }
            return out;
        }

        const auto f = picked.file;
        if (! f.existsAsFile())
        {
            out.failure = DocumentReadFailure::unreadable;
            return out;
        }

        // SECURITY (S-03): the whole read is wrapped, so a piece that
        // exhausts the memory or trips an assertion in the parser takes the
        // app down with a message instead of taking the app down.
        try
        {
            auto txt = f.loadFileAsString();
            if ((juce::int64) txt.getNumBytesAsUTF8() > maxBytes)
            {
                // The file was under the cap, but its text is over it: the
                // piece is kept and nothing is said. The text of a file can
                // be bigger than the file (an invalid byte becomes three).
                out.failure = DocumentReadFailure::tooLargeText;
                return out;
            }
            auto v = juce::JSON::parse (txt);
            if (v.isVoid())
            {
                // v1.33.4 (M25): the file is not a piece at all - the
                // current piece is kept, so a bad file cannot wipe the
                // unsaved work.
                out.failure = DocumentReadFailure::brokenJson;
                return out;
            }
            out.ok = true;
            out.document = v;
            return out;
        }
        catch (const std::exception& e)
        {
            out.failure = DocumentReadFailure::unreadable;
            out.error = std::string ("Could not load the piece: ") + e.what();
            return out;
        }
        catch (...)
        {
            out.failure = DocumentReadFailure::unreadable;
            out.error = "Could not load the piece";
            return out;
        }
    }

    //================ the writer =============================================
    //
    // The piece is written to a picked destination. The document is
    // measured first: a piece over the cap is refused and the destination
    // is not touched, so the app never writes a piece it cannot reopen.
    inline DocumentWriteResult writeDocument (const juce::URL& url, const PieceModel& model,
                                              juce::int64 maxBytes)
    {
        DocumentWriteResult out;

        if (! url.isWellFormed())
        {
            out.error = "No destination was chosen";
            return out;
        }

        // v1.33.4 (M28): the JSON is built from the captured model - the
        // samples become base64, and that is the heavy part. The window's
        // own save path runs this on a worker thread, so the message
        // thread is not held up by it.
        const juce::String json = juce::JSON::toString (upic::pieceToVar (model), true);
        const auto bytes = (juce::int64) json.getNumBytesAsUTF8();
        out.bytes = bytes;

        if (bytes > maxBytes)
        {
            // v1.33.4 (H6): the saver measures the piece first, refuses it
            // with a message that says what to do, and never touches the
            // destination when it refuses - the file that was there before
            // is still there.
            out.error = "Piece too large to save (" + humanPieceSize (bytes) + ", the limit is "
                      + humanPieceSize (maxBytes) + ") - shorten the capture or the sample, "
                      + "or export the audio to a WAV instead";
            return out;
        }

        juce::String error;
        if (! upic::writePickedBytes (url, json.toRawUTF8(), (size_t) bytes, error))
        {
            out.error = error;
            return out;
        }

        out.ok = true;
        return out;
    }
}
