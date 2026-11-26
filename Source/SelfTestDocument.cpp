#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "UpicModel.h"

//==============================================================================
// R0 (refactoring wave R0, part 2): the document characterization.
//
// The persistence work (R4) splits the piece codec out of loadFromVar().
// Before that happens, the document's behaviour is written down here:
//
//   - a piece with every optional feature keeps every feature through a
//     load and a save,
//   - the canonical form is stable: save, load, save gives the same
//     document both times (this is the contract the codec split must keep),
//   - a document that is not a piece is rejected, and the rejection
//     changes nothing - the active piece is untouched,
//   - a piece without the optional features does not grow them, so an
//     old file is still written exactly as it was.

namespace
{
    // The rich piece: everything the piece format can carry.
    upic::PieceModel makeRichPiece()
    {
        upic::PieceModel m;

        // the material: a wavetable and two envelopes
        upic::Waveform w (10, "Wave 10", upic::paletteColour (0));
        w.makeSine();
        m.waveforms.push_back (w);

        upic::Envelope e1 (20, "Env 20", upic::paletteColour (1));
        e1.presetFlat();
        m.envelopes.push_back (e1);

        upic::Envelope e2 (21, "Env 21", upic::paletteColour (2));
        e2.presetFlat();
        m.envelopes.push_back (e2);

        // WP2: the amplitude conversion table
        m.amplitudeTables.push_back (
            upic::makeAmplitudeTable (5, "S", juce::Colour (0xff5aa0c8), upic::ampS));

        // WP4: the performance sequence (it can move across pages)
        upic::PerformanceSequence seq;
        seq.id = 3;
        seq.name = "Seq 3";
        seq.points = { { 0.0, 0, 0.0 }, { 4.0, 1, 1.5 } };
        m.sequences.push_back (seq);

        // WP6: the mixer channel
        upic::Channel ch;
        ch.id = 1;
        ch.name = "Bus";
        ch.colour = juce::Colour (0xff5ac8fa);
        ch.gain = 0.9f;
        ch.envelope = { { 0.0f, 1.0f }, { 1.0f, 0.5f } };
        m.channels.push_back (ch);
        m.nextChannelId = 2;

        // M4: the custom tuning the piece is drawn in
        m.tuning.custom = true;
        m.tuning.baseNote = 60;
        m.tuning.cents = { 0.0, 100.0, 200.0, 300.0, 400.0, 500.0,
                           600.0, 700.0, 800.0, 900.0, 1000.0, 1100.0 };

        // two pages (the workspace needs at least two)
        for (int p = 0; p < 2; ++p)
        {
            upic::Page pg;
            pg.durationSec = 12.0;
            pg.nextArcId = 4;

            // the carrier: it names a conversion table and a channel
            upic::Arc carrier;
            carrier.id = 1;
            carrier.mode = 0;
            carrier.waveformId = 10;
            carrier.envelopeId = 20;
            carrier.intensity = 0.8f;
            carrier.amplitudeTableId = 5;   // WP2
            carrier.channelId = 1;          // WP6
            carrier.points = { { 0.0, 220.0 }, { 6.0, 440.0 } };
            pg.arcs.push_back (carrier);

            // a PM arc: its index is shaped by the second envelope
            upic::Arc pm;
            pm.id = 2;
            pm.mode = 3;
            pm.waveformId = 10;
            pm.envelopeId = 20;
            pm.pmIndexEnv = 21;
            pm.intensity = 0.6f;
            pm.points = { { 1.0, 330.0 }, { 7.0, 660.0 } };
            pg.arcs.push_back (pm);

            // WP5: a modulating arc
            upic::Arc modulator;
            modulator.id = 3;
            modulator.mode = 0;
            modulator.waveformId = 10;
            modulator.envelopeId = 20;
            modulator.modulatorArcId = 1;
            modulator.modTarget = 1;        // the amplitude
            modulator.modArcDepth = 0.4;
            modulator.intensity = 0.5f;
            modulator.points = { { 0.0, 110.0 }, { 6.0, 220.0 } };
            pg.arcs.push_back (modulator);

            m.pages.push_back (pg);
        }

        // WP9: the workspace layout (two pages)
        m.workspacePages = { 0, 1 };
        m.workspaceSplit = 0.5;

        return m;
    }

    int countIn (const juce::var& v, const juce::String& property)
    {
        if (auto* arr = v.getProperty (property, juce::var()).getArray())
            return arr->size();
        return -1;
    }
}

void upic::runDocumentCharacterizationChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- the rich piece ----------------------------------------------------
    const auto rich = upic::pieceToVar (makeRichPiece());

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);

    check (mc.loadFromVar (rich), "R0 the rich piece loads");
    check (mc.flashMessageForTest().isEmpty(),
           "R0 the rich piece loads without a repair message",
           mc.flashMessageForTest());

    const auto saved = mc.pieceToVar();

    // --- every feature survives the round trip -----------------------------
    {
        juce::String detail;
        const bool banks = countIn (saved, "waveforms") == 1
                        && countIn (saved, "envelopes") == 2
                        && countIn (saved, "ampTables") == 1
                        && countIn (saved, "sequences") == 1
                        && countIn (saved, "channels") == 1
                        && countIn (saved, "pages") == 2;
        if (! banks)
            detail << "waves " << countIn (saved, "waveforms")
                   << ", envs " << countIn (saved, "envelopes")
                   << ", tables " << countIn (saved, "ampTables")
                   << ", seqs " << countIn (saved, "sequences")
                   << ", channels " << countIn (saved, "channels")
                   << ", pages " << countIn (saved, "pages") << "  ";
        check (banks, "R0 the rich piece keeps every bank", detail.trim());

        // the workspace and the tuning
        const bool extras = saved.hasProperty ("workspace")
                         && saved.hasProperty ("tuning")
                         && (int) saved.getProperty ("nextChannelId", 0) == 2;
        check (extras, "R0 the rich piece keeps the workspace, the tuning and the channel counter");
    }

    // --- the arcs keep what they name --------------------------------------
    {
        auto* pages = saved.getProperty ("pages", juce::var()).getArray();
        auto* arcs = pages != nullptr
                       ? pages->getReference (0).getProperty ("arcs", juce::var()).getArray()
                       : nullptr;
        juce::String detail;
        bool ok = arcs != nullptr && arcs->size() == 3;
        if (! ok)
        {
            detail << "the page has " << (arcs != nullptr ? arcs->size() : -1) << " arc(s)  ";
        }
        else
        {
            const auto carrier = arcs->getReference (0);
            const auto pm      = arcs->getReference (1);
            const auto mod     = arcs->getReference (2);
            // the piece format's names: ampTable, ch, mod, modTarget
            ok = (int) carrier.getProperty ("ampTable", -1) == 5
              && (int) carrier.getProperty ("ch", 0) == 1
              && (int) pm.getProperty ("pmIndexEnv", 0) == 21
              && (int) mod.getProperty ("mod", -1) == 1
              && (int) mod.getProperty ("modTarget", -1) == 1;
            if (! ok)
            {
                for (int i = 0; i < arcs->size(); ++i)
                {
                    const auto a = arcs->getReference (i);
                    detail << "arc " << (int) a.getProperty ("id", 0)
                           << ": table " << (int) a.getProperty ("ampTable", -1)
                           << ", channel " << (int) a.getProperty ("ch", 0)
                           << ", pmEnv " << (int) a.getProperty ("pmIndexEnv", 0)
                           << ", modulator " << (int) a.getProperty ("mod", -1)
                           << ", target " << (int) a.getProperty ("modTarget", -1)
                           << "  ";
                }
            }
        }
        check (ok, "R0 the arcs keep the material they name", detail.trim());
    }

    // --- the canonical form is stable --------------------------------------
    {
        const auto text = juce::JSON::toString (saved, false);

        auto secondHolder = std::make_unique<MainComponent> (true);
        auto& second = *secondHolder;
        const bool loaded = second.loadFromVar (saved);
        const auto again = juce::JSON::toString (second.pieceToVar(), false);

        check (loaded && again == text,
               "R0 the rich piece's canonical form is stable",
               loaded ? (again == text ? juce::String()
                                      : again.substring (0, 200))
                      : juce::String ("the second load failed"));
    }

    // --- a document that is not a piece is rejected -------------------------
    {
        struct Case { const char* label; juce::var value; };
        const std::vector<Case> cases = {
            { "nothing",         juce::var() },
            { "an empty object", juce::JSON::parse ("{}") },
            { "no pages",        juce::JSON::parse ("{\"pages\":[]}") },
            { "rubbish pages",   juce::JSON::parse ("{\"pages\":\"nonsense\"}") }
        };

        const auto before = juce::JSON::toString (mc.pieceToVar(), false);
        const int arcsBefore = mc.arcCountForTest();
        const int undoBefore = mc.undoDepth();

        bool allRejected = true;
        juce::String detail;
        for (const auto& c : cases)
        {
            if (mc.loadFromVar (c.value))
            {
                allRejected = false;
                detail << c.label << " was accepted  ";
            }
        }

        check (allRejected, "R0 a document that is not a piece is rejected", detail.trim());

        // and the rejection changed nothing
        const auto after = juce::JSON::toString (mc.pieceToVar(), false);
        check (after == before
               && mc.arcCountForTest() == arcsBefore
               && mc.undoDepth() == undoBefore,
               "R0 a rejected document leaves the piece alone",
               juce::String (mc.arcCountForTest()) + " arc(s), "
                 + juce::String (mc.undoDepth()) + " undo step(s)");
    }

    // --- a plain piece does not grow the optional features ------------------
    {
        auto plainHolder = std::make_unique<MainComponent> (true);
        auto& plain = *plainHolder;
        plain.setSize (1280, 800);

        const juce::String json =
            "{\"master\":1,\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":15,\"f\":440}]}]}]}";
        check (plain.loadFromVar (juce::JSON::parse (json)),
               "R0 the plain piece loads");

        const auto out = plain.pieceToVar();
        const bool noExtras = ! out.hasProperty ("ampTables")
                           && ! out.hasProperty ("sequences")
                           && ! out.hasProperty ("channels")
                           && ! out.hasProperty ("workspace")
                           && ! out.hasProperty ("tuning");
        check (noExtras, "R0 a plain piece does not grow the optional features");
    }
}
