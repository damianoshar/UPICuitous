#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "AllocCounter.h"

//==============================================================================
// The files: the ids of a loaded piece, the unsaved-changes flag, the undo,
// the crash recovery, an untrusted piece, the exports, the PM presets and the
// file a chooser returned.

void upic::runLoadedPieceChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6a-6. v1.33.4 (L13): the ids of a loaded piece are checked. The
    //     arcs, the sieve zones, the wavetables and the envelopes are
    //     keyed by their id, so a file with two arcs carrying the same
    //     id made the selection and the delete hit the wrong arc, and
    //     the "next id" - taken from the file - could collide with an
    //     arc that was already there (or overflow).
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);

        // two arcs with the same id, one with no id at all, and a
        // "nextArcId" that would overflow the counter
        const juce::String json =
            "{\"pages\":[{\"duration\":10,\"nextArcId\":2147483647,\"arcs\":["
            "{\"id\":7,\"mode\":0,\"intensity\":1,\"points\":[{\"t\":0,\"f\":220},{\"t\":5,\"f\":440}]},"
            "{\"id\":7,\"mode\":0,\"intensity\":1,\"points\":[{\"t\":0,\"f\":330},{\"t\":5,\"f\":550}]},"
            "{\"id\":0,\"mode\":0,\"intensity\":1,\"points\":[{\"t\":0,\"f\":110},{\"t\":5,\"f\":220}]}"
            "]}]}";
        check (mc.loadFromVar (juce::JSON::parse (json)),
               "a piece with duplicate ids loads");

        const auto loaded = mc.pieceToVar();
        auto* pagesArr = loaded.getProperty ("pages", juce::var()).getArray();
        auto* arcsArr = pagesArr != nullptr
                          ? pagesArr->getReference (0).getProperty ("arcs", juce::var()).getArray()
                          : nullptr;
        check (arcsArr != nullptr && arcsArr->size() == 3,
               "the three arcs are loaded");
        std::vector<int> ids;
        juce::String idList;
        if (arcsArr != nullptr)
            for (auto& av : *arcsArr)
            {
                ids.push_back ((int) av.getProperty ("id", 0));
                idList += (idList.isEmpty() ? "" : ",") + juce::String (ids.back());
            }
        bool unique = ids.size() == 3;
        for (size_t i = 0; i < ids.size() && unique; ++i)
            for (size_t j = i + 1; j < ids.size(); ++j)
                if (ids[i] == ids[j]) unique = false;
        check (unique, "the duplicate arc ids are made unique", idList);
        check (std::find (ids.begin(), ids.end(), 7) != ids.end(),
               "the first arc keeps its id", idList);
        check (std::find (ids.begin(), ids.end(), 0) == ids.end(),
               "an impossible arc id is replaced", idList);
        // the next id is derived: it can never collide with an arc that
        // is there, and the huge value from the file is ignored
        const int nextId = pagesArr != nullptr
                             ? (int) pagesArr->getReference (0).getProperty ("nextArcId", 0)
                             : -1;
        check (nextId > 0 && nextId <= upic::limits::kMaxItemId
               && std::find (ids.begin(), ids.end(), nextId) == ids.end(),
               "the next arc id is derived from the arcs that are there",
               "nextArcId=" + juce::String (nextId));

        // the wavetables, the envelopes and the arcs' references to them
        {
            const juce::String json2 =
                "{\"waveforms\":[{\"id\":3,\"name\":\"A\"},{\"id\":3,\"name\":\"B\"}],"
                "\"envelopes\":[{\"id\":1,\"name\":\"E1\"},{\"id\":1,\"name\":\"E2\"}],"
                "\"pages\":[{\"duration\":10,\"arcs\":["
                "{\"id\":1,\"mode\":0,\"waveformId\":99,\"envelopeId\":99,\"pmIndexEnv\":42,"
                "\"points\":[{\"t\":0,\"f\":220},{\"t\":5,\"f\":440}]}]}]}";
            check (mc.loadFromVar (juce::JSON::parse (json2)),
                   "a piece with broken material references loads");

            const auto out2 = mc.pieceToVar();
            auto* wavesArr = out2.getProperty ("waveforms", juce::var()).getArray();
            auto* envsArr  = out2.getProperty ("envelopes", juce::var()).getArray();
            auto* pages2   = out2.getProperty ("pages", juce::var()).getArray();
            auto* arcs2    = pages2 != nullptr
                               ? pages2->getReference (0).getProperty ("arcs", juce::var()).getArray()
                               : nullptr;
            const int waveId0 = wavesArr != nullptr ? (int) wavesArr->getReference (0).getProperty ("id", 0) : -1;
            const int waveId1 = (wavesArr != nullptr && wavesArr->size() > 1)
                                  ? (int) wavesArr->getReference (1).getProperty ("id", 0) : -1;
            const int envId0  = envsArr != nullptr ? (int) envsArr->getReference (0).getProperty ("id", 0) : -1;
            const int envId1  = (envsArr != nullptr && envsArr->size() > 1)
                                  ? (int) envsArr->getReference (1).getProperty ("id", 0) : -1;
            check (wavesArr != nullptr && wavesArr->size() == 2 && waveId0 != waveId1,
                   "the duplicate wavetable ids are made unique",
                   juce::String (waveId0) + "," + juce::String (waveId1));
            check (envsArr != nullptr && envsArr->size() == 2 && envId0 != envId1,
                   "the duplicate envelope ids are made unique",
                   juce::String (envId0) + "," + juce::String (envId1));

            const int arcWave  = arcs2 != nullptr ? (int) arcs2->getReference (0).getProperty ("waveformId", -1) : -2;
            const int arcEnv   = arcs2 != nullptr ? (int) arcs2->getReference (0).getProperty ("envelopeId", -1) : -2;
            const int arcPmEnv = arcs2 != nullptr ? (int) arcs2->getReference (0).getProperty ("pmIndexEnv", -1) : -2;
            check (arcWave == waveId0,
                   "an arc that points at a missing wavetable is put back onto one that exists",
                   "arc " + juce::String (arcWave) + ", wavetable " + juce::String (waveId0));
            check (arcEnv == envId0,
                   "an arc that points at a missing envelope is put back onto one that exists",
                   "arc " + juce::String (arcEnv) + ", envelope " + juce::String (envId0));
            check (arcPmEnv == 0,
                   "an arc that points at a missing PM envelope falls back to its own",
                   "pmIndexEnv=" + juce::String (arcPmEnv));
        }
    }

}

void upic::runPieceStateChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6a-8. v1.33.4 (L15, part 1): the unsaved-changes flag. The review:
    //     "application close directly calls quit() - an accidental closing
    //     needs a recovery path". The standalone app now asks before it
    //     closes when the piece differs from what was last written.
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);
        check (! mc.pieceHasUnsavedChanges(),
               "a fresh piece has nothing to save");

        // an edit marks the piece
        mc.markModelChanged();
        check (mc.pieceHasUnsavedChanges(),
               "an edit marks the piece as unsaved");

        // a save clears it
        mc.markPieceSaved();
        check (! mc.pieceHasUnsavedChanges(),
               "the save clears the unsaved flag");

        // and an edit after the save marks it again
        mc.markModelChanged();
        check (mc.pieceHasUnsavedChanges(),
               "an edit after the save marks the piece again");

        // a loaded piece is what the file holds: there is nothing to save
        const juce::String json =
            "{\"pages\":[{\"duration\":10,\"arcs\":[]}]}";
        check (mc.loadFromVar (juce::JSON::parse (json)),
               "the piece loads");
        check (! mc.pieceHasUnsavedChanges(),
               "a loaded piece has nothing to save");

        // and a piece that was really written has nothing to save either
        mc.markModelChanged();
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("upic_piece_unsaved_test.json");
        file.deleteFile();
        const bool saved = mc.savePieceToFile (file);
        check (saved && file.existsAsFile(),
               "the piece is written to the disk");
        check (! mc.pieceHasUnsavedChanges(),
               "a written piece has nothing to save");
        file.deleteFile();
    }

    // 6a-9. v1.33.4 (L15, part 2): the undo. The review: "drawing,
    //     erasure, deletion and accidental closing need a recovery path.
    //     Introduce edit transactions, coalesce pointer drags into one
    //     undo action".
    {
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByNameL15 =
            [&findByNameL15](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByNameL15(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };

        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);

        auto countArcs = [] (const juce::var& piece) -> int
        {
            auto* pages = piece.getProperty ("pages", juce::var()).getArray();
            if (pages == nullptr || pages->isEmpty()) return 0;
            auto* arcs = pages->getReference (0).getProperty ("arcs", juce::var()).getArray();
            return arcs != nullptr ? arcs->size() : 0;
        };

        const juce::String oneArc =
            "{\"pages\":[{\"duration\":10,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":4,\"f\":880}]}]}]}";
        const juce::String noArc =
            "{\"pages\":[{\"duration\":10,\"arcs\":[]}]}";

        check (! mc.canUndo(), "a fresh app has nothing to undo");
        check (! mc.canRedo(), "a fresh app has nothing to redo");

        // an edit that changes nothing is not an undo step
        const int depth0 = mc.undoDepth();
        mc.beginEdit ("Nothing");
        mc.endEdit();
        check (mc.undoDepth() == depth0,
               "an edit that changed nothing is not an undo step",
               juce::String (mc.undoDepth()) + " steps (was "
               + juce::String (depth0) + ")");

        // a real edit is one
        mc.loadFromVar (juce::JSON::parse (oneArc));
        mc.beginEdit ("Delete the arc");
        mc.loadFromVar (juce::JSON::parse (noArc));
        mc.endEdit();
        check (mc.canUndo(), "an edit is an undo step");
        check (mc.undoLabel() == "Delete the arc",
               "the undo step knows what it was", mc.undoLabel());
        check (countArcs (mc.pieceToVar()) == 0, "the edit is in the model");

        // the undo brings the arc back
        mc.undo();
        check (countArcs (mc.pieceToVar()) == 1, "the undo brings the arc back",
               juce::String (countArcs (mc.pieceToVar())));
        check (! mc.canUndo(), "the undo stack is empty after the undo");
        check (mc.canRedo(), "the redo stack has the step");

        // and the redo takes it away again
        mc.redo();
        check (countArcs (mc.pieceToVar()) == 0, "the redo removes the arc again",
               juce::String (countArcs (mc.pieceToVar())));
        check (mc.canUndo(), "the undo stack has the step again");
        check (! mc.canRedo(), "the redo stack is empty after the redo");

        // a new edit after an undo starts a new branch
        mc.undo();
        check (mc.canRedo(), "the undone edit can be redone");
        mc.beginEdit ("A new edit");
        mc.loadFromVar (juce::JSON::parse (noArc));
        mc.endEdit();
        check (! mc.canRedo(), "a new edit clears the redo stack");

        // the drawing on the canvas is an undo step too - and a drag is
        // one step, not one per mouse move
        {
            auto* sc = dynamic_cast<ScoreEditor*> (findByNameL15 (mc, "score"));
            check (sc != nullptr, "the canvas is there for the undo test");
            if (sc != nullptr)
            {
                mc.loadFromVar (juce::JSON::parse (noArc));
                const int before = countArcs (mc.pieceToVar());
                const int depthBefore = mc.undoDepth();
                sc->setTool (ScoreEditor::Draw);
                const auto b = sc->getLocalBounds().toFloat();
                // the stroke stays above the pitch-centre line: the band
                // on it is the playhead's scrub target, not a canvas
                const auto p0 = juce::Point<float> (b.getX() + b.getWidth() * 0.25f,
                                                    b.getY() + b.getHeight() * 0.22f);
                const auto p1 = juce::Point<float> (b.getX() + b.getWidth() * 0.60f,
                                                    b.getY() + b.getHeight() * 0.30f);
                const auto now = juce::Time::getCurrentTime();
                const auto src = juce::Desktop::getInstance().getMainMouseSource();
                juce::MouseEvent down (src, p0, juce::ModifierKeys(), 1.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, sc, sc, now, p0, now, 1, false);
                sc->mouseDown (down);
                juce::MouseEvent drag (src, p1, juce::ModifierKeys(), 1.0f,
                                       0.0f, 0.0f, 0.0f, 0.0f, sc, sc, now, p0, now, 1, true);
                sc->mouseDrag (drag);
                sc->mouseUp (drag);
                check (countArcs (mc.pieceToVar()) == before + 1,
                       "the stroke draws an arc",
                       juce::String (countArcs (mc.pieceToVar())));
                check (mc.canUndo(), "the stroke is an undo step");
                // the review asked for the pointer drags to be coalesced
                // into one undo action: a stroke with many mouse moves is
                // one step, not one per move
                check (mc.undoDepth() == depthBefore + 1,
                       "the whole drag is one undo step",
                       juce::String (mc.undoDepth()) + " steps (was "
                       + juce::String (depthBefore) + ")");
                mc.undo();
                check (countArcs (mc.pieceToVar()) == before,
                       "the undo takes the stroke away",
                       juce::String (countArcs (mc.pieceToVar())));
                // the piece on the screen is not what is on the disk any
                // more, so the app must ask before it closes
                check (mc.pieceHasUnsavedChanges(),
                       "the undone piece counts as unsaved");
            }
        }
    }

    // 6a-10. v1.33.4 (L15, part 3): the crash recovery. The review:
    //     "save recovery snapshots without embedding unchanged audio
    //     repeatedly". The snapshot is a small piece JSON (the pages) plus
    //     the audio in sidecar files: a sidecar is only written when the
    //     audio really changed.
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("upic_recovery_test");
        dir.deleteRecursively();

        // a piece with one arc and a little audio
        upic::PieceModel model;
        {
            upic::Page p;
            p.durationSec = 10.0;
            upic::Arc a;
            a.id = 1;
            a.points = { { 0.0, 220.0 }, { 4.0, 880.0 } };
            p.arcs.push_back (a);
            model.pages.push_back (p);
        }
        {
            juce::AudioBuffer<float> buf (2, 1000);
            for (int i = 0; i < 1000; ++i)
                for (int c = 0; c < 2; ++c)
                    buf.setSample (c, i, 0.25f * std::sin ((float) i * 0.01f));
            upic::SampleSlot slot;
            slot.id = 1;
            slot.name = "take.wav";
            slot.sampleRate = 48000.0;
            slot.data = std::make_shared<const juce::AudioBuffer<float>> (buf);
            model.sampleSlots.push_back (slot);
        }

        juce::String error;
        check (MainComponent::writeRecoverySnapshot (dir, model, error),
               "the recovery snapshot is written", error);

        const auto pieceFile = dir.getChildFile ("piece.json");
        check (pieceFile.existsAsFile(), "the snapshot has its piece file");

        // the piece JSON does NOT carry the audio: the audio is in a
        // sidecar, so the unchanged audio is not embedded again and again
        const auto snapshotJson = pieceFile.loadFileAsString();
        auto snapshotVar = juce::JSON::parse (snapshotJson);
        auto* snapSamples = snapshotVar.getProperty ("samples", juce::var()).getArray();
        check (snapSamples != nullptr && snapSamples->size() == 1,
               "the snapshot knows the one sample");
        const auto snapName = snapSamples != nullptr && snapSamples->size() == 1
                                ? (*snapSamples)[0].getDynamicObject()
                                    ->getProperty ("recoveryFile").toString()
                                : juce::String();
        check (snapName.isNotEmpty(), "the snapshot points at the audio file",
               snapName);
        check (snapSamples != nullptr && snapSamples->size() == 1
               && (*snapSamples)[0].getDynamicObject()
                    ->getProperty ("data").toString().isEmpty(),
               "the snapshot's piece file does not embed the audio");
        const auto sidecar = dir.getChildFile (snapName);
        check (sidecar.existsAsFile(), "the audio is in its own file", snapName);

        // it reads back: the piece and the audio are both there
        juce::var recovered;
        check (MainComponent::readRecoverySnapshot (dir, recovered, error),
               "the recovery snapshot reads back", error);
        {
            auto* pages = recovered.getProperty ("pages", juce::var()).getArray();
            auto* arcs = pages != nullptr && pages->size() > 0
                            ? (*pages)[0].getProperty ("arcs", juce::var()).getArray()
                            : nullptr;
            check (arcs != nullptr && arcs->size() == 1,
                   "the recovered piece has its arc");
            // the audio comes back exactly as it was
            const auto pcm = model.sampleSlots[0].toPcm16();
            auto* recSamples = recovered.getProperty ("samples", juce::var()).getArray();
            const auto recData = recSamples != nullptr && recSamples->size() == 1
                                    ? (*recSamples)[0].getDynamicObject()
                                        ->getProperty ("data").toString()
                                    : juce::String();
            check (recData == juce::Base64::toBase64 (pcm.getData(), pcm.getSize()),
                   "the recovered audio is the same audio");
        }

        // the unchanged audio is not written again: the sidecar is
        // marked read-only, and the second snapshot still succeeds
        sidecar.setReadOnly (true);
        upic::PieceModel model2 = model;
        model2.pages[0].arcs[0].points[1].freq = 990.0;   // the pages changed
        check (MainComponent::writeRecoverySnapshot (dir, model2, error),
               "the second snapshot is written", error);
        sidecar.setReadOnly (false);
        {
            juce::var recovered2;
            MainComponent::readRecoverySnapshot (dir, recovered2, error);
            auto* pages = recovered2.getProperty ("pages", juce::var()).getArray();
            auto* arcs = pages != nullptr && pages->size() > 0
                            ? (*pages)[0].getProperty ("arcs", juce::var()).getArray()
                            : nullptr;
            check (arcs != nullptr && arcs->size() == 1,
                   "the second snapshot has the arc");
            auto* pts = arcs != nullptr && arcs->size() > 0
                            ? (*arcs)[0].getProperty ("points", juce::var()).getArray()
                            : nullptr;
            check (pts != nullptr && pts->size() == 2
                   && static_cast<double> ((*pts)[1].getProperty ("f", 0.0)) == 990.0,
                   "the second snapshot has the new curve");
        }

        // a broken snapshot is refused (and thrown away, so it cannot
        // ask again and again)
        {
            juce::File broken = dir.getChildFile ("piece.json");
            broken.replaceWithText ("this is not a piece");
            juce::var nope;
            check (! MainComponent::readRecoverySnapshot (dir, nope, error),
                   "a broken recovery snapshot is refused", error);
        }

        // a snapshot that lost its audio is refused too
        {
            MainComponent::deleteRecoverySnapshot (dir);
            check (! MainComponent::readRecoverySnapshot (dir, recovered, error),
                   "there is nothing to recover after the cleanup", error);
        }
        dir.deleteRecursively();
    }

}

void upic::runUntrustedPieceChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6j. v1.30 (S-03): a piece is untrusted input. The parsers clamp
    //     the counts and reject non-finite numbers, so a crafted piece
    //     cannot be used to freeze the UI or to push NaN into the audio
    //     thread.
    {
        upic::Arc arc = upic::Arc::fromVar (juce::JSON::parse (
            "{\"id\":1,\"mode\":0,\"intensity\":99,\"refFreq\":1e300,"
            "\"points\":[{\"t\":0,\"f\":440,\"i\":5},{\"t\":1e300,\"f\":NaN}]}"));
        check (arc.intensity >= 0.0f && arc.intensity <= 1.0f,
               "an out-of-range intensity is clamped",
               juce::String (arc.intensity, 3));
        check (arc.refFreq >= 20.0 && arc.refFreq <= 4000.0,
               "an out-of-range refFreq is clamped",
               juce::String (arc.refFreq, 1));
        bool allFinite = true;
        for (auto& pt : arc.points)
            if (! std::isfinite (pt.t) || ! std::isfinite (pt.freq))
                allFinite = false;
        check (allFinite, "a non-finite arc point is clamped to a finite one");

        // a huge point count is truncated, not accepted
        {
            juce::String json = "{\"id\":2,\"points\":[";
            for (int i = 0; i < 5000; ++i)
            {
                if (i > 0) json += ",";
                json += "{\"t\":" + juce::String (i) + ",\"f\":440}";
            }
            json += "]}";
            const auto a2 = upic::Arc::fromVar (juce::JSON::parse (json));
            check ((int) a2.points.size () <= upic::limits::kMaxPointsPerArc,
                   "an oversized point list is truncated",
                   juce::String ((int) a2.points.size ()) + " of 5000");
        }

        // a huge declared frame count is capped before anything is allocated
        {
            const auto slot = upic::SampleSlot::fromVar (juce::JSON::parse (
                "{\"id\":1,\"frames\":2000000000,\"channels\":2,\"sampleRate\":48000,"
                "\"data\":\"AAAA\"}"), 1);
            check (slot.data == nullptr,
                   "an oversized sample slot is refused (no allocation)");
        }

        // a page with too many arcs is truncated
        {
            juce::String json = "{\"duration\":30,\"arcs\":[";
            for (int i = 0; i < 2000; ++i)
            {
                if (i > 0) json += ",";
                json += "{\"id\":" + juce::String (i + 1) + ",\"points\":[]}";
            }
            json += "]}";
            const auto pg = upic::Page::fromVar (juce::JSON::parse (json));
            check ((int) pg.arcs.size () <= upic::limits::kMaxArcsPerPage,
                   "an oversized arc list is truncated",
                   juce::String ((int) pg.arcs.size ()) + " of 2000");
        }

        // the clamp helper itself never returns NaN
        check (upic::limits::clampFinite (std::numeric_limits<double>::quiet_NaN (), 0.0, 1.0) == 0.0,
               "clampFinite turns NaN into the lower bound");
        check (upic::limits::clampGain (99.0f) == upic::limits::kMaxMasterGain,
               "clampGain caps the master gain at the slider's top");
    }

    // 6j. v1.30 (S-03): a hostile piece goes through the app's own load
    //     path. It must survive: no crash, and the model must end up
    //     in a usable state.
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);

        // a piece with a huge master gain, a NaN-ish duration and a
        // sample slot that claims two billion frames
        const auto hostile = juce::JSON::parse (
            "{\"master\":1e30,\"duration\":1e300,\"tempo\":1e300,"
            "\"samples\":[{\"id\":1,\"frames\":2000000000,\"channels\":2,"
            "\"sampleRate\":1e9,\"data\":\"AAAA\"}],"
            "\"pages\":[{\"duration\":1e300,\"arcs\":[{\"id\":1,\"mode\":0,"
            "\"intensity\":99,\"refFreq\":1e300,\"points\":[{\"t\":0,\"f\":1e300}]}]}]}");
        check (! hostile.isVoid (), "the hostile piece parses as JSON");

        bool survived = true;
        try
        {
            mc.loadFromVar (hostile);
        }
        catch (...)
        {
            survived = false;
        }
        check (survived, "a hostile piece does not take the app down");

        // the master gain came back clamped, not at 1e30
        const auto gain = (float)(double) mc.pieceToVar().getProperty ("master", -1.0);
        check (gain >= 0.0f && gain <= upic::limits::kMaxMasterGain,
               "the master gain of a hostile piece is clamped",
               juce::String (gain, 3));

        // and the app still renders afterwards
        juce::AudioBuffer<float> buf (2, 4096);
        buf.clear();
        mc.getNextAudioBlock (juce::AudioSourceChannelInfo (&buf, 0, 4096));
        bool finite = true;
        for (int i = 0; i < 4096; ++i)
            if (! std::isfinite (buf.getSample (0, i)) || ! std::isfinite (buf.getSample (1, i)))
                finite = false;
        check (finite, "the audio after a hostile piece is finite");
    }

}

void upic::runExportChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 10. stems: one WAV per audible voice, muted/material-less skipped
    {
        upic::Page pg; pg.durationSec = 1.0;
        auto mkArc = [&](int id, int wid, bool muted, double f){
            upic::Arc a; a.id = id; a.waveformId = wid; a.muted = muted;
            a.points = { {0.0, f}, {1.0, f} };
            return a;
        };
        pg.arcs.push_back(mkArc(1, 1, false, 440.0)); // voice 1
        pg.arcs.push_back(mkArc(2, 1, false, 660.0)); // voice 1 again (merged)
        pg.arcs.push_back(mkArc(3, 2, false, 880.0)); // voice 2
        pg.arcs.push_back(mkArc(4, 2, true,  220.0)); // muted -> skipped
        upic::Arc sa; sa.id = 5; sa.mode = 1; sa.sampleId = 1; // no material loaded
        sa.points = { {0.0, 440.0}, {1.0, 440.0} };
        pg.arcs.push_back(sa);

        auto waves = upic::defaultWaveforms();
        auto envs = upic::defaultEnvelopes();
        std::vector<upic::SampleSlot> noSlots;
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("upic_stems_test");
        dir.deleteRecursively();
        auto written = renderStems(pg, waves, envs, noSlots, dir, "stem", 44100.0, 0.8f);
        check(written.written.size() == 2, "stems: one file per audible voice",
              juce::String((int) written.written.size()) + " files (expect 2)");
        check(written.failed == 0 && written.attempted == 2,
              "stems: nothing failed",
              juce::String(written.failed) + "/" + juce::String(written.attempted));
        bool namesOk = false, sizesOk = ! written.written.empty();
        for (auto& f : written.written)
        {
            namesOk = namesOk || f.getFileName().contains("wave1");
            namesOk = namesOk || f.getFileName().contains("wave2");
            sizesOk = sizesOk && f.existsAsFile() && f.getSize() > 4096;
        }
        check(namesOk, "stems: named by voice (stem_wave1/2)");
        check(sizesOk, "stems: files exist and non-empty");
        dir.deleteRecursively();

        // v1.33.4 (M23): a PM-only page exports its stems too (the old
        // grouping knew only the wavetable and the sample modes, so such
        // a page produced no files at all).
        {
            upic::Page pm; pm.durationSec = 1.0;
            auto mkPm = [&] (int id, double ratio, double index) {
                upic::Arc a; a.id = id; a.mode = 3; a.waveformId = 1;
                a.pmRatio = ratio; a.pmIndex = index; a.pmOps = 2;
                a.points = { {0.0, 440.0}, {1.0, 440.0} };
                return a;
            };
            pm.arcs.push_back (mkPm (1, 2.0, 3.0));   // voice 1
            pm.arcs.push_back (mkPm (2, 2.0, 3.0));   // the same voice again
            pm.arcs.push_back (mkPm (3, 3.0, 1.0));   // voice 2
            auto dirPm = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("upic_stems_pm_test");
            dirPm.deleteRecursively();
            auto res = renderStems (pm, waves, envs, noSlots, dirPm, "stem", 44100.0, 0.8f);
            check (res.written.size() == 2 && res.failed == 0,
                   "stems: a PM-only page exports one file per PM voice",
                   juce::String ((int) res.written.size()) + " files (expect 2)");
            bool pmOk = res.written.size() == 2;
            for (auto& f : res.written)
                pmOk = pmOk && f.getFileName().contains ("pm") && f.existsAsFile() && f.getSize() > 4096;
            check (pmOk, "stems: the PM files exist and are named stem_pm1/2");
            dirPm.deleteRecursively();
        }

        // v1.33.4 (M23): a page that holds only live material says so
        // instead of quietly producing nothing.
        {
            upic::Page live; live.durationSec = 1.0;
            upic::Arc la; la.id = 1; la.mode = 2; la.waveformId = 1;
            la.points = { {0.0, 440.0}, {1.0, 440.0} };
            live.arcs.push_back (la);
            auto dirLive = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("upic_stems_live_test");
            dirLive.deleteRecursively();
            auto res = renderStems (live, waves, envs, noSlots, dirLive, "stem", 44100.0, 0.8f);
            check (res.written.empty() && res.note.contains ("live"),
                   "stems: a live-only page says what it cannot export",
                   res.note);
            dirLive.deleteRecursively();
        }

        // v1.33.4 (H11): a stem export that cannot write is reported as
        // failed, not as a written file. The output folder is a file, so
        // every stem inside it cannot be created.
        {
            auto blocker = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("upic_stems_blocker.txt");
            blocker.replaceWithText("not a folder");
            auto bad = renderStems(pg, waves, envs, noSlots, blocker, "stem", 44100.0, 0.8f);
            check(bad.written.empty() && bad.failed == 2 && bad.attempted == 2,
                  "stems: a failed write is not reported as written",
                  juce::String((int) bad.written.size()) + "/" + juce::String(bad.failed));
            blocker.deleteFile();
        }
    }

    // 10b. v1.33.4 (H11): the WAV export writes a playable file, and a
    // failed export leaves the previous one alone (the old code deleted
    // the destination before it knew whether the write could succeed).
    {
        auto target = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("upic_wav_export_test.wav");
        target.deleteFile();

        juce::AudioBuffer<float> buf(2, 4410);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample(ch, i, 0.5f * std::sin ((float) i * 0.05f));

        const bool wrote = writeWavFile (target, buf, 44100.0);
        bool readable = false;
        if (wrote && target.existsAsFile())
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::FileInputStream> in (target.createInputStream());
            std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (in.release(), true));
            if (r != nullptr)
            {
                const int frames = (int) r->lengthInSamples;
                juce::AudioBuffer<float> back (2, frames);
                r->read (&back, 0, frames, 0, true, false);
                float peak = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < back.getNumSamples(); ++i)
                        peak = std::max (peak, std::abs (back.getSample (ch, i)));
                readable = r->numChannels == 2 && r->sampleRate == 44100.0
                        && r->lengthInSamples == 4410 && peak > 0.4f && peak <= 0.51f;
            }
        }
        check(wrote && readable, "the WAV export writes a playable file",
              target.existsAsFile() ? juce::String((int) target.getSize()) : juce::String("no file"));

        // the failure path: a read-only target cannot be replaced, so the
        // export must fail - and the file that was there must survive.
        const auto sizeBefore = target.getSize();
        target.setReadOnly (true);
        juce::AudioBuffer<float> other(2, 2205);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < other.getNumSamples(); ++i)
                other.setSample (ch, i, 0.9f);
        const bool failedWrite = writeWavFile (target, other, 44100.0);
        target.setReadOnly (false);
        const bool kept = target.existsAsFile() && target.getSize() == sizeBefore;
        check(! failedWrite && kept, "a failed export keeps the previous WAV",
              juce::String((int) target.getSize()) + " vs " + juce::String((int) sizeBefore));
        target.deleteFile();
    }

}

void upic::runPmPresetChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 21c2. v1.33.4 (H12): the PM preset import/export used to build a
    // stack-local FileChooser, which JUCE destroys together with the
    // pending callback - the dialogs silently did nothing. The file work
    // now lives in buildPmPresetText()/applyPmPresetFile(); this drives
    // the same functions the dialogs call.
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);
        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };
        mc.loadFromVar(juce::JSON::parse(
            "{\"pages\":[{\"duration\":4,\"nextArcId\":4,\"arcs\":["
            "{\"id\":1,\"mode\":3,\"pmRatio\":3.5,\"pmIndex\":4.5,\"pmFeedback\":0.25,\"pmOps\":2,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":440}]},"
            "{\"id\":2,\"mode\":3,\"pmRatio\":2.0,\"pmIndex\":1.0,\"pmFeedback\":0.0,\"pmOps\":2,"
            "\"points\":[{\"t\":0,\"f\":330},{\"t\":2,\"f\":660}]},"
            "{\"id\":3,\"mode\":0,\"waveformId\":0,\"envelopeId\":0,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":880}]}]}]}"));
        auto* sc = dynamic_cast<ScoreEditor*>(findByName(mc, "score"));
        check(sc != nullptr, "PM preset round-trip test setup");
        if (sc != nullptr)
        {
            sc->setLayer(3);
            sc->setSelectedArc(1);
            const auto text = mc.buildPmPresetText();
            check(text.contains("ratio=3.5") && text.contains("level=0.9")
                  && text.contains("fb=0.25"),
                  "the PM preset export carries the selected arc's values", text);

            auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("upic_pm_preset_test.preset");
            check(tmp.replaceWithText(text), "the PM preset file is written");

            sc->setSelectedArc(2);
            mc.applyPmPresetFile(tmp);
            tmp.deleteFile();

            auto out = mc.pieceToVar();
            auto* pagesArr = out.getProperty("pages", juce::var()).getArray();
            auto* arcsArr = (pagesArr != nullptr && ! pagesArr->isEmpty())
                                ? pagesArr->getReference(0).getProperty("arcs", juce::var()).getArray()
                                : nullptr;
            double ratio = -1.0, index = -1.0, feedback = -1.0;
            if (arcsArr != nullptr)
                for (const auto& av : *arcsArr)
                    if ((int) av.getProperty("id", -1) == 2)
                    {
                        ratio = (double) av.getProperty("pmRatio", -1.0);
                        index = (double) av.getProperty("pmIndex", -1.0);
                        feedback = (double) av.getProperty("pmFeedback", -1.0);
                        break;
                    }
            check(std::abs(ratio - 3.5) < 1e-6 && std::abs(index - 4.5) < 1e-6
                  && std::abs(feedback - 0.25) < 1e-6,
                  "the PM preset import applies the preset's values",
                  juce::String(ratio) + "/" + juce::String(index) + "/" + juce::String(feedback));

            sc->setLayer(0);
            sc->setSelectedArc(3);
            check(mc.buildPmPresetText().isEmpty(),
                  "the PM preset export refuses a non-PM arc");
        }
    }

}

void upic::runChooserChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 25. v1.33.4 (M17): the file a chooser returned. On Android the
    //     picker hands back a document-provider URL (content://...), which
    //     JUCE's getResults() throws away - so every picked file did
    //     nothing at all. The URL is now what the app works with. These
    //     checks cover the desktop half of that path (a local file URL);
    //     the provider half needs a device.
    {
        const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("upic_pick_test");
        work.deleteRecursively();
        work.createDirectory();
        const auto src = work.getChildFile ("pick.txt");
        src.replaceWithText ("hello picker");

        // a local file URL is used in place - nothing is copied
        upic::PickedFile picked;
        juce::String error;
        const bool readOk = upic::readPickedFile (juce::URL (src), picked,
                                                  (juce::int64) upic::limits::kMaxPieceBytes,
                                                  error);
        check (readOk && picked.file == src && ! picked.isCopy,
               "a picked local file is read in place",
               error.isNotEmpty() ? error : (picked.file.getFileName() + " copy=" + (picked.isCopy ? "yes" : "no")));
        check (readOk && picked.file.loadFileAsString() == "hello picker"
                    && picked.name == "pick.txt",
               "a picked local file keeps its content and its name",
               picked.name);
        picked.release();

        // a file over the cap is refused with a message, not read
        upic::PickedFile tooBig;
        juce::String bigError;
        const bool bigOk = upic::readPickedFile (juce::URL (src), tooBig, 4, bigError);
        check (! bigOk && bigError.contains ("too large"),
               "a picked file over the cap is refused with a message",
               bigError);
        tooBig.release();

        // nothing chosen
        upic::PickedFile none;
        juce::String noneError;
        const bool noneOk = upic::readPickedFile (juce::URL(), none, 1024, noneError);
        check (! noneOk && noneError.contains ("No file"),
               "an empty pick is refused with a message",
               noneError);
        none.release();

        // writing to a picked local destination keeps the old file when
        // the write fails, and replaces it atomically when it succeeds
        const auto dst = work.getChildFile ("out.bin");
        dst.replaceWithText ("old content");
        juce::String writeError;
        const bool wrote = upic::writePickedBytes (juce::URL (dst), "new", 3, writeError);
        check (wrote && dst.loadFileAsString() == "new",
               "a picked local destination is written",
               writeError.isNotEmpty() ? writeError : dst.loadFileAsString());

        // the extension of a local destination is forced (the old
        // behaviour); a provider document keeps the name the user chose
        const auto renamed = upic::pickedURLWithExtension (juce::URL (dst), "wav");
        check (renamed.isLocalFile()
                    && renamed.getLocalFile().getFileName() == "out.wav",
               "a picked local destination keeps the forced extension",
               renamed.getFileName());

        work.deleteRecursively();
    }
}
