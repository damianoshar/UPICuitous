#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#include <JuceHeader.h>

#include <cmath>
#include <functional>
#include <memory>
#include <thread>

#include "SelfTest.h"
#include "AllocCounter.h"
#include "MainComponent.h"
#include "SampleAnalysis.h"
#include "UserLibrary.h"
#include "Ids.h"

#if JUCE_WINDOWS
namespace juce
{
    extern HWND juce_messageWindowHandle;
}
#endif

//==============================================================================
// WP7 (Wave F, 2026-11-21): the user library and the sample extraction.
//
// The checks are split the same way the feature is:
//   - the library format: one file per asset, five kinds, strict validation;
//   - the analysis: the 441 Hz cycle and the RMS/Peak contours;
//   - the commands: publish, import, the one undo transaction per import,
//     the destination-bound frequency tables, the caps and the refusals;
//   - the panel: the A/B preview, the extraction saves and the invalidation
//     of a captured source;
//   - the realtime rule: the preview snapshot renders without an allocation.
//
// Everything runs on the message thread. The library root is redirected to a
// temp folder for the whole run, so the user's own library is never touched.
//==============================================================================
namespace
{
    //================ the temp library root ==================================
    juce::File wp7Parent()
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("upic_wp7_selftest");
    }

    void useLibraryRoot (const juce::String& name)
    {
        const auto folder = wp7Parent().getChildFile (name);
        folder.deleteRecursively();
        folder.createDirectory();
        upic::UserLibrary::setRootForTest (folder);
    }

#if JUCE_WINDOWS
    // The app is built with JUCE's modal-loop API disabled. This pumps the
    // hidden message-manager window directly so an off-thread destructor can
    // acquire MessageManager::Lock while the selftest remains on the message
    // thread. The same message path is used by JUCE's dispatch loop.
    bool pumpMessageManagerQueueOnce()
    {
        if (juce::juce_messageWindowHandle == nullptr)
            return false;

        MSG message;
        bool pumped = false;
        while (::PeekMessage (&message, juce::juce_messageWindowHandle,
                              0, 0, PM_REMOVE))
        {
            ::TranslateMessage (&message);
            ::DispatchMessage (&message);
            pumped = true;
        }
        return pumped;
    }
#endif

    // The selftest runs on the message thread before JUCE's dispatch loop has
    // started, so a callback queued with MessageManager::callAsync has to be
    // dispatched by hand. This drains the queue until the predicate is true
    // (or the deadline passes) and reports whether the predicate was met.
    template <typename Predicate>
    bool drainMessageManagerQueueUntil (Predicate&& predicate, double timeoutMs = 2000.0)
    {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        for (;;)
        {
        #if JUCE_WINDOWS
            if (pumpMessageManagerQueueOnce())
            {
                if (predicate())
                    return true;
                continue;
            }
        #endif
            if (predicate())
                return true;
            if (juce::Time::getMillisecondCounterHiRes() >= deadline)
                return false;
            juce::Thread::sleep (1);
        }
    }

    //================ the payloads ==========================================
    juce::var wavePayload (int id, const juce::String& name)
    {
        upic::Waveform w (id, name, juce::Colours::orange);
        w.makeSine();
        return w.toVar();
    }

    juce::var envelopePayload (int id, const juce::String& name)
    {
        upic::Envelope e (id, name, juce::Colours::cyan);
        e.points = { { 0.0f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 0.0f } };
        return e.toVar();
    }

    juce::var amplitudePayload (int id, const juce::String& name)
    {
        auto table = upic::makeAmplitudeTable (id, name, juce::Colours::green,
                                               upic::ampSquare);
        return table.toVar();
    }

    upic::FreqSpaceDef frequencyDef()
    {
        upic::FreqSpaceDef def;
        def.preset = 0;
        def.editable = true;
        def.points = { { 0.0f, (float) std::log2 (220.0) },
                       { 0.5f, (float) std::log2 (440.0) },
                       { 1.0f, (float) std::log2 (880.0) } };
        return def;
    }

    juce::var frequencyPayload (int space, int page)
    {
        auto payload = frequencyDef().toVar (space);
        if (auto* object = payload.getDynamicObject())
            object->setProperty ("page", page);
        return payload;
    }

    juce::var sequencePayload (int id, const juce::String& name)
    {
        upic::PerformanceSequence s;
        s.id = id;
        s.name = name;
        s.points = { { 0.0, 0, 0.0 }, { 1.0, 0, 2.0 }, { 2.0, 1, 0.0 } };
        return s.toVar();
    }

    juce::var minimalPiece()
    {
        upic::PieceModel m;
        upic::Page p;
        p.durationSec = 4.0;
        m.pages.push_back (p);
        m.currentPage = 0;
        return upic::pieceToVar (m);
    }

    // A piece with one audible arc, so a route that renders really produces
    // sound and a route that was cancelled really produces silence. The
    // loader fills the empty banks with the default wave and envelope.
    juce::var audiblePiece()
    {
        upic::PieceModel m;
        upic::Page p;
        p.durationSec = 4.0;
        upic::Arc a;
        a.id = 1;
        a.waveformId = 1;   // the default saw
        a.envelopeId = 0;   // the default flat envelope
        a.intensity = 0.8f;
        a.points = { { 0.0, 220.0 }, { 3.5, 440.0 } };
        p.arcs.push_back (a);
        m.pages.push_back (p);
        m.currentPage = 0;
        return upic::pieceToVar (m);
    }

    // A piece with the requested banks. Wave and envelope ids start at 5 and
    // 7 on purpose: the first free id of such a bank is 0, which is the case
    // that used to be refused by the import and the extraction saves.
    juce::var testPiece (bool withAmpTables, bool withSequences, int pages = 2)
    {
        upic::PieceModel m;

        for (int i = 0; i < 2; ++i)
        {
            upic::Waveform w (5 + i, "Wave " + juce::String (5 + i),
                              upic::paletteColour (i));
            w.makeSine();
            m.waveforms.push_back (w);

            upic::Envelope e (7 + i, "Env " + juce::String (7 + i),
                              upic::paletteColour (i + 1));
            e.presetFlat();
            m.envelopes.push_back (e);
        }

        if (withAmpTables)
            m.amplitudeTables.push_back (upic::makeAmplitudeTable (
                4, "Amp 4", juce::Colours::green, upic::ampLinear));

        if (withSequences)
        {
            upic::PerformanceSequence s;
            s.id = 5;
            s.name = "Seq 5";
            s.points = { { 0.0, 0, 0.0 }, { 1.0, 0, 2.0 }, { 2.0, 1, 0.0 } };
            m.sequences.push_back (s);
        }

        for (int i = 0; i < pages; ++i)
        {
            upic::Page p;
            p.durationSec = 4.0;
            p.nextArcId = 1;
            m.pages.push_back (p);
        }
        m.currentPage = 0;
        return upic::pieceToVar (m);
    }

    // An empty score whose piece bank owns wave id 0. Its constant waveform
    // is deliberately unlike the extracted sine, so a preview that falls
    // back to the piece bank cannot pass the rendered-identity check.
    juce::var pieceWithDistinctWaveZero()
    {
        upic::PieceModel m;
        upic::Waveform w (0, "Piece wave zero", upic::paletteColour (0));
        w.samples.assign ((size_t) upic::kWaveSize, 0.25f);
        m.waveforms.push_back (w);

        upic::Envelope e (0, "Flat", upic::paletteColour (0));
        e.presetFlat();
        m.envelopes.push_back (e);

        upic::Page p;
        p.durationSec = 4.0;
        m.pages.push_back (p);
        m.currentPage = 0;
        return upic::pieceToVar (m);
    }

    // A piece whose wavetable bank is exactly full. It is used to prove
    // that Slot -> Wave refuses the command without changing the bank or
    // creating an undo step.
    juce::var pieceWithFullWaveBank()
    {
        upic::PieceModel m;
        for (int i = 0; i < upic::limits::kMaxWaveforms; ++i)
        {
            upic::Waveform w (i, "Wave " + juce::String (i),
                              upic::paletteColour (i));
            w.makeSine();
            m.waveforms.push_back (std::move (w));
        }
        upic::Page p;
        p.durationSec = 4.0;
        m.pages.push_back (std::move (p));
        m.currentPage = 0;
        return upic::pieceToVar (m);
    }

    juce::AudioBuffer<float> sineBuffer (double hz, int frames, double rate,
                                         float amplitude = 0.8f)
    {
        juce::AudioBuffer<float> b (1, frames);
        for (int i = 0; i < frames; ++i)
            b.setSample (0, i, amplitude * (float) std::sin (
                2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate));
        return b;
    }

    // The file a published asset gets (the first one for that name).
    juce::String firstNameFile (upic::UserAssetKind kind, const juce::String& name)
    {
        for (const auto& a : upic::UserLibrary::list (kind))
            if (a.name == name) return a.fileName;
        return {};
    }

    template <typename SelectFn, typename NameFn>
    bool selectUser (int count, SelectFn select, NameFn fileName,
                     const juce::String& name)
    {
        const auto wanted = upic::UserLibrary::slugFor (name) + ".json";
        for (int i = 0; i < count; ++i)
        {
            select (i);
            if (fileName() == wanted) return true;
        }
        return false;
    }
}

//==============================================================================
void upic::runWP7Checks (upic::SelfTest& t)
{

    auto check = [&t] (bool ok, const juce::String& name,
                       const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    try
    {
        //======================================================================
        // 1. The five formats round-trip, and the file schema is exact.
        //======================================================================
        {

            useLibraryRoot ("format");

            juce::String error;
            check (upic::UserLibrary::publish (upic::UserAssetKind::Wave, 5,
                                               "Round Wave", juce::Colours::orange,
                                               wavePayload (5, "Round Wave"), error),
                   "wp7 library: a wavetable publishes", error);

            check (upic::UserLibrary::publish (upic::UserAssetKind::Envelope, 7,
                                               "Round Env", juce::Colours::cyan,
                                               envelopePayload (7, "Round Env"), error),
                   "wp7 library: an envelope publishes", error);

            check (upic::UserLibrary::publish (upic::UserAssetKind::AmpTable, 4,
                                               "Round Amp", juce::Colours::green,
                                               amplitudePayload (4, "Round Amp"), error),
                   "wp7 library: an amplitude table publishes", error);

            check (upic::UserLibrary::publish (upic::UserAssetKind::FreqTable, 0,
                                               "Round Freq", juce::Colours::blue,
                                               frequencyPayload (1, 0), error),
                   "wp7 library: a frequency table publishes", error);

            check (upic::UserLibrary::publish (upic::UserAssetKind::Sequence, 5,
                                               "Round Seq", juce::Colours::purple,
                                               sequencePayload (5, "Round Seq"), error),
                   "wp7 library: a sequence publishes", error);

            // Every kind reads back with its payload intact.
            {
                const auto file = upic::UserLibrary::folderFor (upic::UserAssetKind::Wave)
                                      .getChildFile (firstNameFile (upic::UserAssetKind::Wave,
                                                                    "Round Wave"));
                upic::UserAsset asset;
                const bool ok = upic::UserLibrary::read (upic::UserAssetKind::Wave, file,
                                                         asset, error);
                const auto w = upic::Waveform::fromVar (asset.payload, 0);
                bool sameSamples = ok && w.samples.size() == (size_t) upic::kWaveSize;
                if (sameSamples)
                {
                    const auto expected = upic::Waveform::fromVar (
                        wavePayload (5, "Round Wave"), 0);
                    for (size_t i = 0; i < w.samples.size(); ++i)
                        if (w.samples[i] != expected.samples[i]) { sameSamples = false; break; }
                }
                check (ok && asset.id == 5 && asset.name == "Round Wave"
                       && asset.colour == juce::Colours::orange && sameSamples,
                       "wp7 library: the wavetable reads back exactly", error);
            }

            {
                const auto file = upic::UserLibrary::folderFor (upic::UserAssetKind::Envelope)
                                      .getChildFile (firstNameFile (upic::UserAssetKind::Envelope,
                                                                    "Round Env"));
                upic::UserAsset asset;
                const bool ok = upic::UserLibrary::read (upic::UserAssetKind::Envelope,
                                                         file, asset, error);
                const auto e = upic::Envelope::fromVar (asset.payload, 0);
                check (ok && asset.id == 7 && e.points.size() == 3
                       && std::abs (e.points[1].y - 1.0f) < 1.0e-6f,
                       "wp7 library: the envelope reads back exactly", error);
            }

            {
                const auto file = upic::UserLibrary::folderFor (upic::UserAssetKind::AmpTable)
                                      .getChildFile (firstNameFile (upic::UserAssetKind::AmpTable,
                                                                    "Round Amp"));
                upic::UserAsset asset;
                const bool ok = upic::UserLibrary::read (upic::UserAssetKind::AmpTable,
                                                         file, asset, error);
                const auto a = upic::Envelope::fromVar (asset.payload, 0);
                check (ok && asset.id == 4 && a.points.size() >= 3,
                       "wp7 library: the amplitude table reads back", error);
            }

            {
                const auto file = upic::UserLibrary::folderFor (upic::UserAssetKind::FreqTable)
                                      .getChildFile (firstNameFile (upic::UserAssetKind::FreqTable,
                                                                    "Round Freq"));
                upic::UserAsset asset;
                const bool ok = upic::UserLibrary::read (upic::UserAssetKind::FreqTable,
                                                         file, asset, error);
                const auto d = upic::FreqSpaceDef::fromVar (asset.payload);
                check (ok && d.editable && d.points.size() == 3
                       && std::abs (std::exp2 ((double) d.points[1].log2Hz) - 440.0) < 0.5
                       && (int) asset.payload.getProperty ("space", -1) == 1
                       && (int) asset.payload.getProperty ("page", -1) == 0,
                       "wp7 library: the frequency table keeps its destination", error);
            }

            {
                const auto file = upic::UserLibrary::folderFor (upic::UserAssetKind::Sequence)
                                      .getChildFile (firstNameFile (upic::UserAssetKind::Sequence,
                                                                    "Round Seq"));
                upic::UserAsset asset;
                const bool ok = upic::UserLibrary::read (upic::UserAssetKind::Sequence,
                                                         file, asset, error);
                const auto s = upic::PerformanceSequence::fromVar (asset.payload, 0);
                check (ok && s.points.size() == 3 && s.points[2].page == 1,
                       "wp7 library: the sequence reads back", error);
            }

            // Clearing a sequence is a valid model state, so the library
            // accepts an empty point list and preserves the other metadata.
            {
                upic::PerformanceSequence empty;
                empty.id = 6;
                empty.name = "Empty Seq";
                empty.loop = false;
                check (upic::UserLibrary::publish (upic::UserAssetKind::Sequence, 6,
                                                   empty.name, juce::Colours::purple,
                                                   empty.toVar(), error),
                       "wp7 library: an empty sequence publishes", error);
                const auto file = upic::UserLibrary::folderFor (
                                      upic::UserAssetKind::Sequence)
                                      .getChildFile (firstNameFile (
                                          upic::UserAssetKind::Sequence, "Empty Seq"));
                upic::UserAsset asset;
                const bool ok = upic::UserLibrary::read (upic::UserAssetKind::Sequence,
                                                         file, asset, error);
                const auto s = upic::PerformanceSequence::fromVar (asset.payload, 0);
                check (ok && s.points.empty() && s.name == "Empty Seq" && ! s.loop,
                       "wp7 library: an empty sequence reads back without repair", error);
            }

            // The exact six-property schema (kind, id, version, name, colour,
            // payload). A future property is refused instead of being copied.
            {
                const auto file = upic::UserLibrary::folderFor (upic::UserAssetKind::Wave)
                                      .getChildFile (firstNameFile (upic::UserAssetKind::Wave,
                                                                    "Round Wave"));
                const auto parsed = juce::JSON::parse (file.loadFileAsString());
                const auto* obj = parsed.getDynamicObject();
                check (obj != nullptr && obj->getProperties().size() == 6,
                       "wp7 library: an asset file has exactly the six schema fields",
                       juce::String (obj != nullptr ? obj->getProperties().size() : -1));
            }

            // The two table kinds share the folder, so listing one kind sees
            // the other as a valid asset rather than as a broken file.
            {
                int skipped = 0;
                const auto amps = upic::UserLibrary::list (upic::UserAssetKind::AmpTable,
                                                           &skipped);
                check (amps.size() == 1 && skipped == 0,
                       "wp7 library: a table listing accepts the other table kind",
                       juce::String ((int) amps.size()) + "/" + juce::String (skipped));
            }
        }

        //======================================================================
        // 2. The refusals: broken, oversized, wrong-kind and out-of-range
        //    files are refused, never repaired.
        //======================================================================
        {

            useLibraryRoot ("refusals");

            juce::String error;
            const auto folder = upic::UserLibrary::folderFor (upic::UserAssetKind::Wave);
            folder.createDirectory();

            auto writeRaw = [&folder] (const juce::String& name, const juce::String& text)
            {
                return folder.getChildFile (name).replaceWithText (text);
            };
            auto readWave = [&folder] (const juce::String& name, upic::UserAsset& asset,
                                       juce::String& err)
            {
                return upic::UserLibrary::read (upic::UserAssetKind::Wave,
                                                folder.getChildFile (name), asset, err);
            };

            upic::UserAsset asset;

            check (! readWave ("missing.json", asset, error)
                   && error.containsIgnoreCase ("gone"),
                   "wp7 refusals: a missing file is refused", error);

            writeRaw ("malformed.json", "{not json");
            check (! readWave ("malformed.json", asset, error),
                   "wp7 refusals: malformed JSON is refused", error);
            {
                int skipped = 0;
                juce::StringArray failures;
                const auto listed = upic::UserLibrary::list (upic::UserAssetKind::Wave,
                                                             &skipped, &failures);
                check (listed.empty() && skipped == 1 && failures.size() == 1
                       && failures[0].contains ("malformed.json"),
                       "wp7 refusals: an invalid library file is reported, not hidden",
                       skipped == 1 && ! failures.isEmpty() ? failures[0] : juce::String());
            }
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
                auto& mc = *mcHolder;
                mc.refreshUserLibraryForTest (upic::UserAssetKind::Wave);
                check (mc.lastUserLibraryFailureTextForTest().contains ("malformed.json"),
                       "wp7 refusals: an invalid library file has a visible reason",
                       mc.lastUserLibraryFailureTextForTest());
            }

            writeRaw ("empty.json", "");
            check (! readWave ("empty.json", asset, error),
                   "wp7 refusals: an empty file is refused", error);

            // A well-formed asset with an unknown top-level property.
            {
                auto v = juce::JSON::parse (juce::JSON::toString (
                    [] {
                        auto* o = new juce::DynamicObject();
                        o->setProperty ("kind", "wave");
                        o->setProperty ("id", 1);
                        o->setProperty ("version", 1);
                        o->setProperty ("name", "Future");
                        o->setProperty ("colour", (int) juce::Colours::white.getARGB());
                        o->setProperty ("payload", wavePayload (1, "Future"));
                        o->setProperty ("futureField", 2);
                        return juce::var (o);
                    }(), false));
                writeRaw ("future.json", juce::JSON::toString (v, true));
                check (! readWave ("future.json", asset, error)
                       && error.containsIgnoreCase ("unknown property"),
                       "wp7 refusals: an unknown schema property is refused", error);
            }

            // A version that is not the library's version.
            writeRaw ("version.json", R"({"kind":"wave","id":1,"version":2,)"
                                     R"("name":"V2","colour":-1,"payload":{}})");
            check (! readWave ("version.json", asset, error),
                   "wp7 refusals: an unsupported version is refused", error);

            // An id that is not an integer.
            writeRaw ("floatid.json", R"({"kind":"wave","id":1.5,"version":1,)"
                                     R"("name":"Float","colour":-1,"payload":{}})");
            check (! readWave ("floatid.json", asset, error),
                   "wp7 refusals: a fractional id is refused", error);

            // A wave payload with the wrong sample count.
            {
                auto payload = wavePayload (1, "Short");
                if (auto* obj = payload.getDynamicObject())
                {
                    juce::Array<juce::var> shortArray;
                    shortArray.add (0.0);
                    obj->setProperty ("samples", shortArray);
                }
                check (! upic::UserLibrary::publish (upic::UserAssetKind::Wave, 1,
                                                     "Short", juce::Colours::white,
                                                     payload, error)
                       && error.containsIgnoreCase ("samples"),
                       "wp7 refusals: a wavetable outside the sample range is refused", error);
            }

            // The wavetable range is exactly -1..1. A tiny overshoot is
            // still an out-of-range value and must not be imported.
            {
                auto payload = wavePayload (1, "Out Of Range");
                if (auto* obj = payload.getDynamicObject())
                {
                    auto* samples = obj->getProperty ("samples").getArray();
                    if (samples != nullptr && ! samples->isEmpty())
                    {
                        samples->getReference (0) = 1.00005;
                        check (! upic::UserLibrary::publish (upic::UserAssetKind::Wave, 1,
                                                            "Out Of Range", juce::Colours::white,
                                                            payload, error)
                               && error.containsIgnoreCase ("-1..1"),
                               "wp7 refusals: a wavetable value above 1 is refused", error);
                        samples->getReference (0) = -1.00005;
                        check (! upic::UserLibrary::publish (upic::UserAssetKind::Wave, 1,
                                                            "Out Of Range", juce::Colours::white,
                                                            payload, error)
                               && error.containsIgnoreCase ("-1..1"),
                               "wp7 refusals: a wavetable value below -1 is refused", error);
                        samples->getReference (0) = 1.0;
                        check (upic::UserLibrary::publish (upic::UserAssetKind::Wave, 1,
                                                           "Exact Range", juce::Colours::white,
                                                           payload, error),
                               "wp7 refusals: the exact wavetable boundary is accepted", error);
                    }
                }
            }

            // An envelope whose points are not sorted.
            {
                auto payload = envelopePayload (1, "Unsorted");
                if (auto* obj = payload.getDynamicObject())
                {
                    juce::Array<juce::var> points;
                    auto addPoint = [&points] (double x, double y)
                    {
                        auto* p = new juce::DynamicObject();
                        p->setProperty ("x", x);
                        p->setProperty ("y", y);
                        points.add (juce::var (p));
                    };
                    addPoint (0.5, 0.0);
                    addPoint (0.1, 1.0);
                    obj->setProperty ("points", points);
                }
                check (! upic::UserLibrary::publish (upic::UserAssetKind::Envelope, 1,
                                                     "Unsorted", juce::Colours::white,
                                                     payload, error)
                       && error.containsIgnoreCase ("sorted"),
                       "wp7 refusals: an unsorted envelope is refused", error);
            }

            // A frequency table with an impossible point.
            {
                auto payload = frequencyPayload (0, 0);
                if (auto* obj = payload.getDynamicObject())
                {
                    juce::Array<juce::var> points;
                    auto* p = new juce::DynamicObject();
                    p->setProperty ("x", 0.5);
                    p->setProperty ("hz", 0.0);
                    points.add (juce::var (p));
                    p = new juce::DynamicObject();
                    p->setProperty ("x", 1.0);
                    p->setProperty ("hz", 440.0);
                    points.add (juce::var (p));
                    obj->setProperty ("points", points);
                }
                check (! upic::UserLibrary::publish (upic::UserAssetKind::FreqTable, 0,
                                                     "Zero Hz", juce::Colours::white,
                                                     payload, error),
                       "wp7 refusals: a frequency table outside the range is refused", error);
            }

            // A sequence with decreasing times.
            {
                auto payload = sequencePayload (1, "Backwards");
                if (auto* obj = payload.getDynamicObject())
                {
                    juce::Array<juce::var> points;
                    auto addPoint = [&points] (double t)
                    {
                        auto* p = new juce::DynamicObject();
                        p->setProperty ("t", t);
                        p->setProperty ("page", 0);
                        p->setProperty ("pos", 0.0);
                        points.add (juce::var (p));
                    };
                    addPoint (1.0);
                    addPoint (0.5);
                    obj->setProperty ("points", points);
                }
                check (! upic::UserLibrary::publish (upic::UserAssetKind::Sequence, 1,
                                                     "Backwards", juce::Colours::white,
                                                     payload, error)
                       && error.containsIgnoreCase ("sorted"),
                       "wp7 refusals: an unsorted sequence is refused", error);
            }

            // The publish field checks.
            check (! upic::UserLibrary::publish (upic::UserAssetKind::Wave, 1, "  ",
                                                 juce::Colours::white,
                                                 wavePayload (1, "No Name"), error),
                   "wp7 refusals: an empty name is refused", error);
            check (! upic::UserLibrary::publish (upic::UserAssetKind::Wave, -1, "Negative",
                                                 juce::Colours::white,
                                                 wavePayload (1, "Negative"), error),
                   "wp7 refusals: a negative id is refused", error);
            check (! upic::UserLibrary::publish (upic::UserAssetKind::Wave,
                                                 upic::limits::kMaxItemId + 1, "Too Big",
                                                 juce::Colours::white,
                                                 wavePayload (1, "Too Big"), error),
                   "wp7 refusals: an id above the limit is refused", error);

            // A file over the byte cap is refused before it is parsed.
            {
                const auto big = folder.getChildFile ("huge.json");
                const auto block = juce::String::repeatedString ("x", 65536);
                juce::FileOutputStream os (big);
                const int blocks = (int) (upic::limits::kMaxUserAssetBytes / 65536u) + 2;
                for (int i = 0; i < blocks; ++i)
                    os.writeText (block, false, false, nullptr);
                os.flush();
                check (! readWave ("huge.json", asset, error)
                       && error.containsIgnoreCase ("large"),
                       "wp7 refusals: an oversized file is refused before parsing", error);
            }

            // The wrong kind in the wrong folder.
            {
                check (upic::UserLibrary::publish (upic::UserAssetKind::Wave, 1,
                                                   "Kind Test", juce::Colours::white,
                                                   wavePayload (1, "Kind Test"), error),
                       "wp7 refusals: the kind fixture publishes", error);
                const auto file = folder.getChildFile (
                    firstNameFile (upic::UserAssetKind::Wave, "Kind Test"));
                check (! upic::UserLibrary::read (upic::UserAssetKind::Envelope, file,
                                                  asset, error)
                       && error.containsIgnoreCase ("envelope"),
                       "wp7 refusals: a wave file is refused as an envelope", error);
            }

            // Reveal accepts a plain file name in the asset's own folder.
            // A traversal or an absolute path is refused before Explorer is
            // asked to open anything.
            {
                bool allRefused = true;
                bool allResolved = true;
                for (const auto kind : { upic::UserAssetKind::Wave,
                                         upic::UserAssetKind::Envelope,
                                         upic::UserAssetKind::AmpTable,
                                         upic::UserAssetKind::FreqTable,
                                         upic::UserAssetKind::Sequence })
                {
                    const auto folder = upic::UserLibrary::folderFor (kind);
                    folder.createDirectory();
                    const auto file = folder.getChildFile ("plain.json");
                    file.replaceWithText ("{}");
                    juce::File target;
                    juce::String reason;
                    allResolved = allResolved
                        && upic::UserLibrary::revealTarget (
                            kind, file.getFileName(), target, reason)
                        && target == file;

                    allRefused = allRefused
                        && ! upic::UserLibrary::reveal (kind, "../outside.json")
                        && ! upic::UserLibrary::reveal (kind, "sub/inside.json")
                        && ! upic::UserLibrary::reveal (kind, "C:\\outside.json");
                }
                check (allResolved,
                       "wp7 reveal: a plain asset name resolves inside its own folder");
                check (allRefused,
                       "wp7 refusals: reveal refuses path traversal for every asset kind");
            }
        }

        //======================================================================
        // 3. The analysis: the 441 Hz cycle (within 1%), determinism, the
        //    RMS/Peak contours and their caps.
        //======================================================================
        {

            constexpr double sr = 44100.0;
            const auto sine = sineBuffer (441.0, 88200, sr);

            const auto a = upic::findBestCycle (sine, sr);
            const auto b = upic::findBestCycle (sine, sr);
            const bool same = a.ok && b.ok
                           && a.startSample == b.startSample
                           && a.lengthSamples == b.lengthSamples
                           && a.confidence == b.confidence;
            check (same, "wp7 analysis: the 441 Hz cycle is deterministic");

            check (a.ok && std::abs (a.frequencyHz - 441.0) <= 441.0 * 0.01,
                   "wp7 analysis: the 441 Hz cycle is within 1%",
                   juce::String (a.frequencyHz, 3) + " Hz, length "
                   + juce::String (a.lengthSamples));
            check (a.lengthSamples >= 99 && a.lengthSamples <= 101,
                   "wp7 analysis: the cycle is one period long",
                   juce::String (a.lengthSamples) + " frames");
            check (a.startSample >= 0 && a.startSample + a.lengthSamples <= sine.getNumSamples(),
                   "wp7 analysis: the cycle fits inside the material");

            // A silent lead-in must not hide the loop.
            {
                juce::AudioBuffer<float> lead (1, 88200);
                lead.clear();
                const int leadFrames = 8820;
                for (int i = leadFrames; i < lead.getNumSamples(); ++i)
                    lead.setSample (0, i, 0.7f * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 441.0
                        * (double) (i - leadFrames) / sr));
                const auto c = upic::findBestCycle (lead, sr);
                check (c.ok && std::abs (c.frequencyHz - 441.0) <= 441.0 * 0.01
                       && c.startSample >= leadFrames - 200,
                       "wp7 analysis: a silent lead-in does not hide the cycle",
                       juce::String (c.frequencyHz, 3) + " Hz at "
                       + juce::String (c.startSample));
            }

            // Noise has no cycle.
            {
                juce::AudioBuffer<float> noise (1, 44100);
                juce::Random rng (0x5eed);
                for (int i = 0; i < noise.getNumSamples(); ++i)
                    noise.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f);
                check (! upic::findBestCycle (noise, sr).ok,
                       "wp7 analysis: noise has no cycle");
            }

            // A constant signal has no cycle either (the correlation is
            // undefined, not a perfect match).
            {
                juce::AudioBuffer<float> constant (1, 44100);
                constant.clear();
                for (int i = 0; i < constant.getNumSamples(); ++i)
                    constant.setSample (0, i, 0.5f);
                check (! upic::findBestCycle (constant, sr).ok,
                       "wp7 analysis: a constant signal has no cycle");
            }

            // The zero-crossing alignment keeps the cycle legal.
            {
                const float* data = sine.getReadPointer (0);
                const int aligned = upic::alignCycleToZeroCrossing (
                    data, sine.getNumSamples(), a.startSample, a.lengthSamples);
                check (aligned >= 0 && aligned + a.lengthSamples <= sine.getNumSamples(),
                       "wp7 analysis: the aligned cycle still fits");
            }

            // A decaying tone: the RMS contour decreases monotonically.
            {
                juce::AudioBuffer<float> decay (1, 20000);
                for (int i = 0; i < decay.getNumSamples(); ++i)
                {
                    const float amp = 1.0f - 0.9f * (float) i / (float) decay.getNumSamples();
                    decay.setSample (0, i, amp * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 441.0 * (double) i / sr));
                }
                const auto pts = upic::extractEnvelope (decay, sr,
                                                        upic::EnvelopeMode::RMS, 32);
                bool monotonic = pts.size() == 32;
                if (monotonic)
                    for (size_t i = 1; i < pts.size(); ++i)
                        if (pts[i].y > pts[i - 1].y + 1.0e-4f) { monotonic = false; break; }
                check (monotonic && pts.front().y > pts.back().y,
                       "wp7 analysis: the RMS contour follows a decay",
                       juce::String (pts.empty() ? 0.0f : pts.front().y, 4) + " -> "
                       + juce::String (pts.empty() ? 0.0f : pts.back().y, 4));
                check (! pts.empty() && std::abs (pts.front().x) < 1.0e-6f
                       && std::abs (pts.back().x - 1.0f) < 1.0e-6f,
                       "wp7 analysis: the contour spans the whole material");
            }

            // RMS and Peak differ where the material has a sparse impulse.
            {
                juce::AudioBuffer<float> sparse (1, 20000);
                sparse.clear();
                for (int i = 0; i < 10000; ++i)
                    sparse.setSample (0, i, (i & 1) ? -1.0f : 1.0f);
                sparse.setSample (0, 15000, 1.0f);

                const auto peak = upic::extractEnvelope (sparse, sr,
                                                         upic::EnvelopeMode::Peak, 2);
                const auto rms = upic::extractEnvelope (sparse, sr,
                                                        upic::EnvelopeMode::RMS, 2);
                check (peak.size() == 2 && rms.size() == 2
                       && peak[1].y > 0.99f && rms[1].y < 0.1f,
                       "wp7 analysis: Peak sees a sparse impulse that RMS does not",
                       juce::String (peak.size() > 1 ? peak[1].y : -1.0f, 4) + " / "
                       + juce::String (rms.size() > 1 ? rms[1].y : -1.0f, 4));
            }

            // The contour's caps: 2 points at least, 1000 at most.
            {
                const auto low = upic::extractEnvelope (sine, sr,
                                                        upic::EnvelopeMode::RMS, 1);
                const auto high = upic::extractEnvelope (sine, sr,
                                                         upic::EnvelopeMode::Peak, 5000);
                check (low.size() == 2, "wp7 analysis: a contour has at least two points",
                       juce::String ((int) low.size()));
                check (high.size() == (size_t) upic::limits::kMaxPointsPerEnvelope,
                       "wp7 analysis: a contour is capped at the envelope limit",
                       juce::String ((int) high.size()));
            }

            // The stereo buffer overload cannot cancel antiphase channels:
            // the envelope is the power over both channels.
            {
                juce::AudioBuffer<float> stereo (2, 4410);
                for (int i = 0; i < stereo.getNumSamples(); ++i)
                {
                    const float v = 0.6f * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 441.0 * (double) i / sr);
                    stereo.setSample (0, i, v);
                    stereo.setSample (1, i, -v);
                }
                const auto pts = upic::extractEnvelope (stereo, sr,
                                                        upic::EnvelopeMode::RMS, 8);
                bool any = false;
                for (const auto& p : pts) any = any || p.y > 0.5f;
                check (pts.size() == 8 && any,
                       "wp7 analysis: an antiphase stereo contour is not cancelled");
            }
        }

        //======================================================================
        // 4. The extraction panel: capture, A/B preview, the saves and the
        //    invalidation of a source that no longer matches the piece.
        //======================================================================
        {

            useLibraryRoot ("extract");

            constexpr double sr = 44100.0;
            const auto sine = sineBuffer (441.0, 88200, sr);

            auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;

            check (! mc.sampleExtractPanelOpenForTest(),
                   "wp7 panel: the panel is not built before it is opened");
            check (! mc.saveSampleCycleToWave() && ! mc.saveSampleEnvelope(),
                   "wp7 panel: saving without material is refused");

            check (mc.setSampleSlotForTest (0, sine, sr),
                   "wp7 panel: the synthetic material is installed");
            check (mc.sampleExtractSourceSlotIdForTest() == -1,
                   "wp7 panel: the captured slot is empty before the panel opens");

            mc.openSampleExtractForTest();
            check (mc.sampleExtractPanelOpenForTest(),
                   "wp7 panel: the panel opens");
            check (mc.sampleExtractSourceSlotIdForTest() == 1,
                   "wp7 panel: the panel captures the selected slot",
                   juce::String (mc.sampleExtractSourceSlotIdForTest()));

            const uint64_t openGeneration = mc.sampleExtractPreviewGenerationForTest();
            check (openGeneration > 0,
                   "wp7 panel: opening publishes a preview generation",
                   juce::String (openGeneration));

            mc.sampleExtractDetectForTest();
            const uint64_t detectGeneration = mc.sampleExtractPreviewGenerationForTest();
            check (detectGeneration > openGeneration,
                   "wp7 panel: a new analysis advances the preview generation",
                   juce::String (detectGeneration));
            const int cycleLength = mc.sampleExtractCycleLengthForTest();
            const double cycleHz = cycleLength > 0 ? sr / (double) cycleLength : 0.0;
            check (cycleLength > 0 && std::abs (cycleHz - 441.0) <= 441.0 * 0.01,
                   "wp7 panel: the detected cycle is within 1%",
                   juce::String (cycleHz, 2) + " Hz, " + juce::String (cycleLength)
                   + " frames");
            check ((int) mc.sampleExtractEnvelopePointsForTest().size() == 256,
                   "wp7 panel: the panel extracts a 256-point contour",
                   juce::String ((int) mc.sampleExtractEnvelopePointsForTest().size()));

            mc.sampleExtractSetEnvelopeModeForTest (upic::EnvelopeMode::Peak);
            check ((int) mc.sampleExtractEnvelopePointsForTest().size() == 256,
                   "wp7 panel: the Peak mode also produces a contour");
            mc.sampleExtractAlignForTest();
            const uint64_t alignGeneration = mc.sampleExtractPreviewGenerationForTest();
            check (alignGeneration > detectGeneration,
                   "wp7 panel: a new alignment advances the preview generation",
                   juce::String (alignGeneration));
            check (mc.sampleExtractCycleStartForTest() >= 0
                   && mc.sampleExtractCycleStartForTest() + mc.sampleExtractCycleLengthForTest()
                      <= sine.getNumSamples(),
                   "wp7 panel: Align keeps the cycle inside the material");

            // The A/B preview: opening arms the original side; the A/B
            // button toggles to the cycle and back.
            check (mc.sampleExtractPreviewActiveForTest(),
                   "wp7 panel: opening arms the A/B preview");
            check (! mc.sampleExtractPreviewUsesCycleForTest(),
                   "wp7 panel: the preview starts on the original side");
            mc.sampleExtractPreviewForTest();
            const uint64_t extractedGeneration = mc.sampleExtractPreviewGenerationForTest();
            check (mc.sampleExtractPreviewActiveForTest()
                   && mc.sampleExtractPreviewUsesCycleForTest(),
                   "wp7 panel: the A/B button switches to the cycle");
            check (extractedGeneration > alignGeneration,
                   "wp7 panel: switching sides advances the preview generation",
                   juce::String (extractedGeneration));
            mc.sampleExtractPreviewForTest();
            const uint64_t originalGeneration = mc.sampleExtractPreviewGenerationForTest();
            check (mc.sampleExtractPreviewActiveForTest()
                   && ! mc.sampleExtractPreviewUsesCycleForTest(),
                   "wp7 panel: the A/B button switches back to the original");
            check (originalGeneration > extractedGeneration,
                   "wp7 panel: every A/B switch advances the preview generation",
                   juce::String (originalGeneration));

            mc.closeSampleExtractForTest();
            const uint64_t closedGeneration = mc.sampleExtractPreviewGenerationForTest();
            check (! mc.sampleExtractPanelOpenForTest()
                   && ! mc.sampleExtractPreviewActiveForTest(),
                   "wp7 panel: closing stops the preview and releases the material");
            check (closedGeneration > originalGeneration,
                   "wp7 panel: closing advances the cancelled generation",
                   juce::String (closedGeneration));
            check (mc.sampleExtractPublishedRouteIsNoneForTest(),
                   "wp7 panel: closing publishes no extraction route or payload");

            // A source replacement closes the panel: an old analysis can
            // never be saved against new audio.
            mc.openSampleExtractForTest();
            check (mc.sampleExtractPanelOpenForTest(), "wp7 panel: the panel reopens");
            const auto other = sineBuffer (220.0, 44100, sr);
            check (mc.setSampleSlotForTest (0, other, sr),
                   "wp7 panel: the material can be replaced");
            check (! mc.sampleExtractPanelOpenForTest()
                   && ! mc.sampleExtractPreviewActiveForTest()
                   && mc.sampleExtractPublishedRouteIsNoneForTest(),
                   "wp7 panel: replacing the material invalidates the captured source");
            check (! mc.saveSampleCycleToWave(),
                   "wp7 panel: a save after invalidation is refused");

            // Saving the cycle and the contour: each is one undo transaction.
            mc.openSampleExtractForTest();
            const int waves0 = mc.waveformCountForTest();
            const int depth0 = mc.undoDepth();
            check (mc.saveSampleCycleToWave(),
                   "wp7 panel: the cycle saves as a wavetable");
            check (mc.sampleExtractPreviewMatchesWaveForTest (
                       mc.selectedWaveformIdForTest()),
                   "wp7 panel: the saved wavetable is bit-identical to the A/B preview");
            check (mc.waveformCountForTest() == waves0 + 1
                   && mc.undoDepth() == depth0 + 1,
                   "wp7 panel: saving the cycle is one undo step",
                   juce::String (mc.waveformCountForTest()) + " waves, "
                   + juce::String (mc.undoDepth()) + " steps");
            mc.undo();
            check (mc.waveformCountForTest() == waves0,
                   "wp7 panel: undo removes the extracted wavetable");

            mc.openSampleExtractForTest();
            const int envs0 = mc.envelopeCountForTest();
            const int depth1 = mc.undoDepth();
            check (mc.saveSampleEnvelope(),
                   "wp7 panel: the contour saves as an envelope");
            check (mc.envelopeCountForTest() == envs0 + 1
                   && mc.undoDepth() == depth1 + 1,
                   "wp7 panel: saving the contour is one undo step");
            mc.undo();
            check (mc.envelopeCountForTest() == envs0,
                   "wp7 panel: undo removes the extracted envelope");

            // The panel analyses each channel independently: a signal that
            // lives only in the right channel is found. The score is empty
            // here, so every measured sample belongs to the A/B route.
            {
                juce::AudioBuffer<float> stereo (2, 44100);
                stereo.clear();
                for (int i = 0; i < stereo.getNumSamples(); ++i)
                    stereo.setSample (1, i, 0.6f * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 441.0 * (double) i / sr));

                auto parityHolder = std::make_unique<MainComponent> (true);
                auto& parity = *parityHolder;
                check (parity.loadFromVar (pieceWithDistinctWaveZero()),
                       "wp7 panel: the empty score with a distinct piece wave loads");
                check (parity.setSampleSlotForTest (0, stereo, sr),
                       "wp7 panel: the stereo fixture is installed");
                parity.prepareToPlay (512, sr);
                parity.openSampleExtractForTest();

                const int len = parity.sampleExtractCycleLengthForTest();
                check (len > 0 && std::abs (sr / (double) len - 441.0) <= 441.0 * 0.01,
                       "wp7 panel: a right-channel-only signal is analysed",
                       juce::String (len) + " frames");
                check (parity.sampleExtractAnalysisChannelForTest() == 1,
                       "wp7 panel: the detector selects the channel that carries the signal",
                       juce::String (parity.sampleExtractAnalysisChannelForTest()));

                juce::AudioBuffer<float> out (2, 512);
                juce::AudioSourceChannelInfo info (&out, 0, out.getNumSamples());
                const auto rmsOf = [] (const juce::AudioBuffer<float>& b, int channel)
                {
                    double sum = 0.0;
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const double v = b.getSample (channel, i);
                        sum += v * v;
                    }
                    return std::sqrt (sum / (double) juce::jmax (1, b.getNumSamples()));
                };

                out.clear();
                parity.getNextAudioBlock (info);
                const double originalLeft = rmsOf (out, 0);
                const double originalRight = rmsOf (out, 1);
                check (originalLeft > 0.001 && originalRight > 0.001
                       && std::abs (originalLeft - originalRight)
                              <= juce::jmax (1.0e-5, 0.01 * juce::jmax (originalLeft, originalRight)),
                       "wp7 panel: the original side renders the selected mono channel on both outputs",
                       juce::String (originalLeft, 4) + " / "
                       + juce::String (originalRight, 4));
                check (parity.sampleExtractSnapshotUsesSourceChannelForTest (1, 123),
                       "wp7 panel: the original snapshot is the selected source channel, not an average");

                parity.sampleExtractPreviewForTest();
                out.clear();
                parity.getNextAudioBlock (info);
                const double extractedLeft = rmsOf (out, 0);
                const double extractedRight = rmsOf (out, 1);
                check (extractedLeft > 0.001 && extractedRight > 0.001
                       && std::abs (extractedLeft - extractedRight)
                              <= juce::jmax (1.0e-5, 0.01 * juce::jmax (extractedLeft, extractedRight)),
                       "wp7 panel: the extracted side renders the same mono comparison on both outputs",
                       juce::String (extractedLeft, 4) + " / "
                       + juce::String (extractedRight, 4));
                check (parity.sampleExtractSnapshotMatchesPreviewForTest(),
                       "wp7 panel: the extracted route snapshots the exact preview wavetable");
                check (parity.sampleExtractRenderedMatchesIsolatedWaveForTest (out, 512),
                       "wp7 panel: the rendered extracted side uses the isolated preview wavetable");
                parity.closeSampleExtractForTest();
            }

            // The user can select the source channel. That choice is
            // authoritative even when another channel carries a stronger
            // cycle, and it governs detection, the A/B original side and the
            // saved wavetable together.
            {
                juce::AudioBuffer<float> stereo (2, 88200);
                for (int i = 0; i < stereo.getNumSamples(); ++i)
                {
                    stereo.setSample (0, i, 0.7f * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 330.0 * (double) i / sr));
                    stereo.setSample (1, i, 0.5f * (float) std::sin (
                        2.0 * juce::MathConstants<double>::pi * 441.0 * (double) i / sr));
                }

                auto selectedHolder = std::make_unique<MainComponent> (true);
                auto& selected = *selectedHolder;
                check (selected.loadFromVar (pieceWithDistinctWaveZero()),
                       "wp7 channels: the channel-selection piece loads");
                check (selected.setSampleSlotForTest (0, stereo, sr),
                       "wp7 channels: the two-channel fixture is installed");
                selected.openSampleExtractForTest();
                check (selected.sampleExtractSourceChannelCountForTest() == 2,
                       "wp7 channels: both source channels are offered");

                selected.sampleExtractSelectChannelForTest (0);
                const int leftLength = selected.sampleExtractCycleLengthForTest();
                check (selected.sampleExtractAnalysisChannelForTest() == 0
                       && leftLength > 0
                       && std::abs (sr / (double) leftLength - 330.0) <= 330.0 * 0.01,
                       "wp7 channels: the selected left channel is analysed",
                       juce::String (leftLength) + " frames");
                check (selected.sampleExtractSnapshotUsesSourceChannelForTest (0, 123),
                       "wp7 channels: the original preview uses the selected left channel");

                selected.sampleExtractPreviewForTest();
                const int waves0 = selected.waveformCountForTest();
                check (selected.saveSampleCycleToWave()
                       && selected.waveformCountForTest() == waves0 + 1
                       && selected.sampleExtractPreviewMatchesWaveForTest (
                              selected.selectedWaveformIdForTest()),
                       "wp7 channels: the saved wave uses the selected channel's cycle");

                selected.sampleExtractSelectChannelForTest (1);
                const int rightLength = selected.sampleExtractCycleLengthForTest();
                check (selected.sampleExtractAnalysisChannelForTest() == 1
                       && rightLength > 0
                       && std::abs (sr / (double) rightLength - 441.0) <= 441.0 * 0.01,
                       "wp7 channels: selecting the right channel replaces the analysis",
                       juce::String (rightLength) + " frames");
            }

            // A bank whose first free id is 0 must accept the extracted
            // assets: the built-in banks start at 0, so `nextBankId()`
            // legitimately returns it.
            {
                auto zeroHolder = std::make_unique<MainComponent> (true);
                auto& zero = *zeroHolder;
                check (zero.loadFromVar (testPiece (false, false)),
                       "wp7 ids: the bank-without-zero piece loads");
                check (! zero.waveformIdForTest (0) == false
                       || zero.waveformCountForTest() == 2,
                       "wp7 ids: the wave bank has the two loaded waves");
                check (zero.setSampleSlotForTest (0, sine, sr),
                       "wp7 ids: the material is installed");

                zero.openSampleExtractForTest();
                const int waveCount = zero.waveformCountForTest();
                check (zero.saveSampleCycleToWave(),
                       "wp7 ids: a cycle can mint id 0 in a wave bank");
                check (zero.waveformCountForTest() == waveCount + 1
                       && zero.selectedWaveformIdForTest() == 0,
                       "wp7 ids: the extracted wavetable got the free id 0",
                       juce::String (zero.selectedWaveformIdForTest()));
                zero.undo();

                zero.openSampleExtractForTest();
                const int envCount = zero.envelopeCountForTest();
                check (zero.saveSampleEnvelope(),
                       "wp7 ids: a contour can mint id 0 in an envelope bank");
                check (zero.envelopeCountForTest() == envCount + 1
                       && zero.selectedEnvelopeIdForTest() == 0,
                       "wp7 ids: the extracted envelope got the free id 0",
                       juce::String (zero.selectedEnvelopeIdForTest()));
            }

            // A failed detection must not invent a cycle or an extracted
            // preview. Aligning that failed result is equally a no-op.
            {
                auto failedHolder = std::make_unique<MainComponent> (true);
                auto& failed = *failedHolder;
                juce::AudioBuffer<float> silence (1, 44100);
                silence.clear();
                check (failed.setSampleSlotForTest (0, silence, sr),
                       "wp7 panel: the silent fixture is installed");
                failed.openSampleExtractForTest();
                failed.sampleExtractDetectForTest();
                check (failed.sampleExtractCycleLengthForTest() == 0
                       && failed.sampleExtractPreviewRouteForTest()
                            == SampleExtractRoute::original,
                       "wp7 panel: a failed detection refuses to claim an extracted cycle");
                failed.sampleExtractAlignForTest();
                check (failed.sampleExtractCycleLengthForTest() == 0
                       && failed.sampleExtractPreviewRouteForTest()
                            == SampleExtractRoute::original,
                       "wp7 panel: alignment after a failed detection does not invent a cycle");
                failed.sampleExtractPreviewForTest();
                check (failed.sampleExtractPreviewRouteForTest()
                           == SampleExtractRoute::original
                       && ! failed.saveSampleCycleToWave(),
                       "wp7 panel: a failed analysis cannot be previewed or saved as a cycle");
            }

            // Every edit that changes the captured material invalidates the
            // source. A stale cycle must never survive a trim, direction,
            // reference or level change.
            {
                auto invalidHolder = std::make_unique<MainComponent> (true);
                auto& invalid = *invalidHolder;
                check (invalid.setSampleSlotForTest (0, sine, sr),
                       "wp7 panel: the invalidation fixture is installed");
                invalid.sampleMaterialModeForTest();

                invalid.openSampleExtractForTest();
                const uint64_t trimGeneration = invalid.sampleExtractPreviewGenerationForTest();
                check (invalid.sampleTrimForTest (100, 80000),
                       "wp7 panel: the material can be trimmed");
                check (! invalid.sampleExtractPanelOpenForTest()
                       && ! invalid.sampleExtractPreviewActiveForTest()
                       && invalid.sampleExtractPreviewGenerationForTest() > trimGeneration
                       && invalid.sampleExtractPublishedRouteIsNoneForTest()
                       && ! invalid.saveSampleCycleToWave()
                       && ! invalid.saveSampleEnvelope(),
                       "wp7 panel: a trim invalidates the captured extraction source and refuses its saves");

                invalid.openSampleExtractForTest();
                const uint64_t reverseGeneration = invalid.sampleExtractPreviewGenerationForTest();
                invalid.sampleReverseForTest();
                check (! invalid.sampleExtractPanelOpenForTest()
                       && ! invalid.sampleExtractPreviewActiveForTest()
                       && invalid.sampleExtractPreviewGenerationForTest() > reverseGeneration
                       && invalid.sampleExtractPublishedRouteIsNoneForTest()
                       && ! invalid.saveSampleCycleToWave()
                       && ! invalid.saveSampleEnvelope(),
                       "wp7 panel: reversing the material invalidates the captured source and refuses its saves");

                invalid.openSampleExtractForTest();
                const uint64_t refGeneration = invalid.sampleExtractPreviewGenerationForTest();
                invalid.sampleRefForTest (330.0);
                check (! invalid.sampleExtractPanelOpenForTest()
                       && ! invalid.sampleExtractPreviewActiveForTest()
                       && invalid.sampleExtractPreviewGenerationForTest() > refGeneration
                       && invalid.sampleExtractPublishedRouteIsNoneForTest()
                       && ! invalid.saveSampleCycleToWave()
                       && ! invalid.saveSampleEnvelope(),
                       "wp7 panel: changing the sample Ref invalidates the captured source and refuses its saves");

                invalid.openSampleExtractForTest();
                const uint64_t levelGeneration = invalid.sampleExtractPreviewGenerationForTest();
                invalid.sampleLevelForTest (0.5);
                check (! invalid.sampleExtractPanelOpenForTest()
                       && ! invalid.sampleExtractPreviewActiveForTest()
                       && invalid.sampleExtractPreviewGenerationForTest() > levelGeneration
                       && invalid.sampleExtractPublishedRouteIsNoneForTest()
                       && ! invalid.saveSampleCycleToWave()
                       && ! invalid.saveSampleEnvelope(),
                       "wp7 panel: changing the sample level invalidates the captured source and refuses its saves");
            }

            // --- audit M5: the overlay's own conventions --------------------
            // The panel was built visible and had no close of its own: it
            // appeared on the screen while the (message-thread) analysis ran,
            // and Escape did nothing. It is now hidden at construction, and
            // it closes with Escape like the other editors.
            {
                SampleExtractPanel bare;
                check (! bare.isOpen(),
                       "M5 the extraction panel is hidden when it is built");

                auto escHolder = std::make_unique<MainComponent> (true);
                auto& esc = *escHolder;
                check (esc.setSampleSlotForTest (0, sine, sr),
                       "M5 the material is installed");
                esc.openSampleExtractForTest();
                check (esc.sampleExtractPanelOpenForTest(),
                       "M5 the panel opens");
                check (esc.sampleExtractEscapeForTest(),
                       "M5 the panel takes the Escape key");
                check (! esc.sampleExtractPanelOpenForTest(),
                       "M5 Escape closes the panel");
                check (! esc.sampleExtractPreviewActiveForTest(),
                       "M5 Escape also stops the preview");
            }
        }

        //======================================================================
        // 4b. Stable ids, the Slot -> Wave transaction, and refused gestures.
        //======================================================================
        {
            // The allocator always chooses the first free id. A high valid id
            // does not make the allocator jump past the hole, and an invalid
            // id is repaired into the first free slot.
            {
                std::vector<upic::Waveform> bank;
                auto add = [&bank] (int id)
                {
                    bank.emplace_back (id, "Wave " + juce::String (id),
                                       juce::Colours::white);
                };
                add (0);
                add (5);
                add (upic::limits::kMaxItemId);
                check (upic::nextBankId (bank, 0) == 1,
                       "wp7 ids: the allocator reuses the first free id below the maximum",
                       juce::String (upic::nextBankId (bank, 0)));
                check (upic::nextBankId (bank, 1) == 1,
                       "wp7 ids: the allocator respects a bank's minimum id");

                std::vector<upic::Waveform> repair;
                repair.emplace_back (7, "First", juce::Colours::white);
                repair.emplace_back (7, "Duplicate", juce::Colours::white);
                repair.emplace_back (upic::limits::kMaxItemId + 1,
                                     "Too high", juce::Colours::white);
                repair.emplace_back (-1, "Negative", juce::Colours::white);
                const int changed = upic::uniquifyIds (repair, 0);
                check (changed == 3 && repair[0].id == 7
                       && repair[1].id == 0 && repair[2].id == 1
                       && repair[3].id == 2,
                       "wp7 ids: duplicate and out-of-range ids are repaired first-free",
                       juce::String (changed) + " repaired");
            }

            // The manager borrows its selector. Some owners declare the
            // selector after the manager, so the selector is destroyed first.
            // The manager must survive that order without touching the freed
            // combo box's listener list.
            {
                struct SelectorOwner
                {
                    LibraryManager manager { "lifetime" };
                    juce::ComboBox selector;

                    SelectorOwner() { manager.setSelector (selector); }
                };

                {
                    SelectorOwner owner;
                    check (owner.manager.itemCountForTest() == 0,
                           "wp7 lifetime: a manager can outlive its borrowed selector");
                }
            }

            // Slot -> Wave is one labelled transaction. Its refusals (no
            // material, a full bank) add neither an asset nor an undo step.
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
                auto& mc = *mcHolder;
                check (mc.loadFromVar (testPiece (false, false)),
                       "wp7 slot-to-wave: the fixture piece loads");
                check (mc.setSampleSlotForTest (0, sineBuffer (220.0, 8192, 44100.0), 44100.0),
                       "wp7 slot-to-wave: the source material is installed");

                const int waves0 = mc.waveformCountForTest();
                const int depth0 = mc.undoDepth();
                check (mc.waveFromSelectedSlotForTest(),
                       "wp7 slot-to-wave: the material digitises into a wavetable");
                check (mc.waveformCountForTest() == waves0 + 1
                       && mc.selectedWaveformIdForTest() == 0,
                       "wp7 slot-to-wave: the new wavetable gets the first free id",
                       juce::String (mc.selectedWaveformIdForTest()));
                check (mc.undoDepth() == depth0 + 1
                       && mc.undoLabel() == "Digitise the wavetable",
                       "wp7 slot-to-wave: success is one labelled undo step",
                       mc.undoLabel());
                mc.undo();
                check (mc.waveformCountForTest() == waves0,
                       "wp7 slot-to-wave: undo removes the digitised wavetable");

                auto emptyHolder = std::make_unique<MainComponent> (true);
                auto& empty = *emptyHolder;
                const int emptyDepth = empty.undoDepth();
                check (! empty.waveFromSelectedSlotForTest()
                       && empty.undoDepth() == emptyDepth,
                       "wp7 slot-to-wave: an empty slot is refused without an undo step");

                auto fullHolder = std::make_unique<MainComponent> (true);
                auto& full = *fullHolder;
                check (full.loadFromVar (pieceWithFullWaveBank()),
                       "wp7 slot-to-wave: the full-bank fixture loads");
                check (full.setSampleSlotForTest (0, sineBuffer (220.0, 8192, 44100.0), 44100.0),
                       "wp7 slot-to-wave: the full-bank material is installed");
                const int fullWaves = full.waveformCountForTest();
                const int fullDepth = full.undoDepth();
                check (! full.waveFromSelectedSlotForTest()
                       && full.waveformCountForTest() == fullWaves
                       && full.undoDepth() == fullDepth,
                       "wp7 slot-to-wave: a full bank is refused without an undo step");
            }

            // A refused stroke or line changes neither the next arc id nor
            // the undo stack. A successful stroke advances both exactly once.
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
                auto& mc = *mcHolder;
                mc.setSize (1600, 1000);
                check (mc.loadFromVar (minimalPiece()),
                       "wp7 gestures: the empty-score fixture loads");
                auto& editor = mc.scoreEditorForTest();

                const int next0 = mc.nextArcIdForTest();
                const int depth0 = mc.undoDepth();
                editor.onEditStart ("Draw");
                const bool shortStroke = editor.commitStrokeForTest ({ { 100.0f, 200.0f } });
                editor.onEditEnd();
                check (! shortStroke && mc.nextArcIdForTest() == next0
                       && mc.arcCountForTest() == 0 && mc.undoDepth() == depth0,
                       "wp7 gestures: a refused stroke advances neither the id nor undo");

                editor.onEditStart ("Draw a line");
                const bool shortLine = editor.commitLineForTest (
                    { 100.0f, 200.0f }, { 100.0f, 200.0f });
                editor.onEditEnd();
                check (! shortLine && mc.nextArcIdForTest() == next0
                       && mc.arcCountForTest() == 0 && mc.undoDepth() == depth0,
                       "wp7 gestures: a refused line advances neither the id nor undo");

                editor.onEditStart ("Draw");
                const bool added = editor.commitStrokeForTest ({
                    { 100.0f, 200.0f }, { 500.0f, 300.0f } });
                editor.onEditEnd();
                check (added && mc.nextArcIdForTest() == next0 + 1
                       && mc.arcCountForTest() == 1 && mc.undoDepth() == depth0 + 1,
                       "wp7 gestures: a successful stroke advances the id and undo once",
                       juce::String (mc.nextArcIdForTest()) + " / "
                       + juce::String (mc.undoDepth()));
            }
        }

        //======================================================================
        // 5. The commands: publish changes nothing; import mints a NEW local
        //    id, is exactly one transaction and respects the caps.
        //======================================================================
        {

            useLibraryRoot ("commands");

            juce::String error;

            // --- wave publish / import ------------------------------------
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
                const auto piece = testPiece (false, false);
                check (mc.loadFromVar (piece), "wp7 import: the wave piece loads");
                mc.selectWaveformForTest (5);

                const auto before = juce::JSON::toString (mc.pieceToVar(), false);
                const int depth0 = mc.undoDepth();
                const int waves0 = mc.waveformCountForTest();

                check (mc.libraryPublishWaveForTest(),
                       "wp7 publish: the wavetable publishes");
                check (mc.undoDepth() == depth0
                       && juce::JSON::toString (mc.pieceToVar(), false) == before,
                       "wp7 publish: publishing changes neither the piece nor the undo stack");

                mc.libraryWaveSourceForTest (true);
                check (mc.libraryWaveUserItemCountForTest() == 1,
                       "wp7 import: the user list holds the published wavetable",
                       juce::String (mc.libraryWaveUserItemCountForTest()));
                check (selectUser (mc.libraryWaveItemCountForTest(),
                                   [&mc] (int i) { mc.libraryWaveSelectUserForTest (i); },
                                   [&mc] { return mc.libraryWaveUserFileForTest(); },
                                   "Wave 5"),
                       "wp7 import: the published wavetable can be selected");

                check (mc.libraryImportWaveTransactionForTest(),
                       "wp7 import: the wavetable imports");
                check (mc.waveformCountForTest() == waves0 + 1
                       && mc.undoDepth() == depth0 + 1,
                       "wp7 import: a wavetable import is one undo step",
                       juce::String (mc.waveformCountForTest()) + " waves, "
                       + juce::String (mc.undoDepth()) + " steps");

                bool id0 = false, id5Reused = false;
                for (int i = 0; i < mc.waveformCountForTest(); ++i)
                {
                    const int id = mc.waveformIdForTest (i);
                    if (id == 0) id0 = true;
                    if (i == mc.waveformCountForTest() - 1 && id == 5) id5Reused = true;
                }
                check (id0 && ! id5Reused,
                       "wp7 import: the library id never becomes the local id");

                mc.undo();
                check (mc.waveformCountForTest() == waves0,
                       "wp7 import: undo restores the wavetable bank");

                // Importing the same asset twice gives two distinct names.
                mc.libraryWaveSourceForTest (true);
                selectUser (mc.libraryWaveItemCountForTest(),
                            [&mc] (int i) { mc.libraryWaveSelectUserForTest (i); },
                            [&mc] { return mc.libraryWaveUserFileForTest(); },
                            "Wave 5");
                check (mc.libraryImportWaveTransactionForTest(),
                       "wp7 import: the same wavetable imports twice");
                mc.libraryWaveSourceForTest (true);
                selectUser (mc.libraryWaveItemCountForTest(),
                            [&mc] (int i) { mc.libraryWaveSelectUserForTest (i); },
                            [&mc] { return mc.libraryWaveUserFileForTest(); },
                            "Wave 5");
                check (mc.libraryImportWaveTransactionForTest(),
                       "wp7 import: the second copy imports");
                int sameNameCount = 0;
                for (int i = 0; i < mc.waveformCountForTest(); ++i)
                    if (mc.waveformNameForTest (i).startsWith ("Wave 5")) ++sameNameCount;
                check (sameNameCount >= 3,
                       "wp7 import: a duplicate name is made unique",
                       juce::String (sameNameCount) + " names");

                // A refusal is not an undo step.
                mc.libraryWaveSourceForTest (true);
                const int depth1 = mc.undoDepth();
                check (! mc.libraryImportWaveTransactionForTest()
                       && mc.undoDepth() == depth1,
                       "wp7 import: a refused import adds no undo step");
            }

            // --- envelope publish / import --------------------------------
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
                check (mc.loadFromVar (testPiece (false, false)),
                       "wp7 import: the envelope piece loads");
                mc.selectEnvelopeForTest (7);

                const int depth0 = mc.undoDepth();
                const int envs0 = mc.envelopeCountForTest();
                check (mc.libraryPublishEnvelopeForTest(),
                       "wp7 publish: the envelope publishes");
                check (mc.undoDepth() == depth0,
                       "wp7 publish: publishing an envelope adds no undo step");

                mc.libraryEnvelopeSourceForTest (true);
                check (selectUser (mc.libraryEnvelopeItemCountForTest(),
                                   [&mc] (int i) { mc.libraryEnvelopeSelectUserForTest (i); },
                                   [&mc] { return mc.libraryEnvelopeUserFileForTest(); },
                                   "Env 7"),
                       "wp7 import: the published envelope can be selected");
                check (mc.libraryImportEnvelopeTransactionForTest(),
                       "wp7 import: the envelope imports");
                check (mc.envelopeCountForTest() == envs0 + 1
                       && mc.undoDepth() == depth0 + 1,
                       "wp7 import: an envelope import is one undo step");
                check (mc.selectedEnvelopeIdForTest() == 0,
                       "wp7 import: the envelope got the free id 0",
                       juce::String (mc.selectedEnvelopeIdForTest()));
                mc.undo();
                check (mc.envelopeCountForTest() == envs0,
                       "wp7 import: undo restores the envelope bank");
            }

            // --- amplitude tables -----------------------------------------
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
                check (mc.loadFromVar (testPiece (true, false)),
                       "wp7 import: the amplitude-table piece loads");
                mc.openAmpTableEditorForTest();
                mc.ampTableSelectForTest (0);

                const int depth0 = mc.undoDepth();
                const int count0 = mc.ampTableCountForTest();
                check (mc.libraryPublishAmpTableForTest(),
                       "wp7 publish: the amplitude table publishes");
                check (mc.undoDepth() == depth0,
                       "wp7 publish: publishing a table adds no undo step");

                mc.libraryAmpTableSourceForTest (true);
                check (selectUser (mc.libraryAmpTableItemCountForTest(),
                                   [&mc] (int i) { mc.libraryAmpTableSelectUserForTest (i); },
                                   [&mc] { return mc.libraryAmpTableUserFileForTest(); },
                                   "Amp 4"),
                       "wp7 import: the published table can be selected");
                check (mc.libraryImportAmpTableTransactionForTest(),
                       "wp7 import: the amplitude table imports");
                check (mc.ampTableCountForTest() == count0 + 1
                       && mc.undoDepth() == depth0 + 1,
                       "wp7 import: a table import is one undo step");
                check (mc.ampTableIdAtForTest (mc.ampTableCountForTest() - 1) == 1,
                       "wp7 import: the table got the first free id",
                       juce::String (mc.ampTableIdAtForTest (mc.ampTableCountForTest() - 1)));
                mc.undo();
                check (mc.ampTableCountForTest() == count0,
                       "wp7 import: undo restores the table bank");
            }

            // --- frequency tables are destination-bound -------------------
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
                check (mc.loadFromVar (testPiece (false, false)),
                       "wp7 freq: the two-page piece loads");

                mc.setCurrentPageForTest (0);
                mc.openFreqTableForTest (1);
                check (mc.freqTablePageForTest() == 0
                       && mc.freqTableSpaceForTest() == 1,
                       "wp7 freq: the destination is captured when the editor opens");

                mc.commitFreqTableCurveForTest (
                    { { 0.0f, (float) std::log2 (220.0) },
                      { 0.5f, (float) std::log2 (440.0) },
                      { 1.0f, (float) std::log2 (880.0) } });
                check (mc.spaceDefEditableForTest (1)
                       && mc.spaceDefPointCountForTest (1) == 3,
                       "wp7 freq: the drawn curve reaches the model");

                const int depth0 = mc.undoDepth();
                check (mc.libraryPublishFreqTableForTest(),
                       "wp7 publish: the frequency table publishes");
                check (mc.undoDepth() == depth0,
                       "wp7 publish: publishing a frequency table adds no undo step");

                // The file records its source destination.
                {
                    const auto file = upic::UserLibrary::folderFor (
                                          upic::UserAssetKind::FreqTable)
                                          .getChildFile (firstNameFile (
                                              upic::UserAssetKind::FreqTable,
                                              "Page 1 space 2"));
                    const auto parsed = juce::JSON::parse (file.loadFileAsString());
                    const auto& payload = parsed.getProperty ("payload", juce::var());
                    check ((int) payload.getProperty ("page", -1) == 0
                           && (int) payload.getProperty ("space", -1) == 1,
                           "wp7 freq: the library file records its source page/space");
                }

                // A frequency table has no bank in the piece, so the library
                // gives it a real id of its own: the first free one in the
                // library's frequency-table bank. The id never becomes a
                // model id (the import lands in the captured destination).
                {
                    const auto file = upic::UserLibrary::folderFor (
                                          upic::UserAssetKind::FreqTable)
                                          .getChildFile (firstNameFile (
                                              upic::UserAssetKind::FreqTable,
                                              "Page 1 space 2"));
                    const auto parsed = juce::JSON::parse (file.loadFileAsString());
                    check ((int) parsed.getProperty ("id", -1) == 0
                           && upic::UserLibrary::nextLibraryId (
                                  upic::UserAssetKind::FreqTable) == 1,
                           "wp7 freq: the published table takes the first free library id",
                           juce::String ((int) parsed.getProperty ("id", -1)));
                }

                // Move the current page after the editor captured its
                // destination: the import must still land on page 0, space 1.
                mc.setCurrentPageForTest (1);
                mc.libraryFreqTableSourceForTest (true);
                check (selectUser (mc.libraryFreqTableItemCountForTest(),
                                   [&mc] (int i) { mc.libraryFreqTableSelectUserForTest (i); },
                                   [&mc] { return mc.libraryFreqTableUserFileForTest(); },
                                   "Page 1 space 2"),
                       "wp7 freq: the published table can be selected");
                const int depth1 = mc.undoDepth();
                check (mc.libraryImportFreqTableTransactionForTest(),
                       "wp7 freq: the table imports into the captured destination");
                check (mc.undoDepth() == depth1 + 1,
                       "wp7 freq: a frequency-table import is one undo step");
                check (mc.pageSpaceDefEditableForTest (0, 1)
                       && mc.pageSpaceDefPointCountForTest (0, 1) == 3,
                       "wp7 freq: the import landed on the captured page/space",
                       juce::String (mc.pageSpaceDefPointCountForTest (0, 1)));
                check (! mc.pageSpaceDefEditableForTest (1, 0),
                       "wp7 freq: the current page was not the destination");

                mc.undo();
                check (mc.pageSpaceDefEditableForTest (0, 1)
                       && mc.pageSpaceDefPointCountForTest (0, 1) == 3,
                       "wp7 freq: undo restores the destination's previous table",
                       juce::String (mc.pageSpaceDefPointCountForTest (0, 1)));

                // A second destination is filled from the same file, and the
                // first one is left alone.
                mc.setCurrentPageForTest (1);
                mc.openFreqTableForTest (2);
                mc.libraryFreqTableSourceForTest (true);
                selectUser (mc.libraryFreqTableItemCountForTest(),
                            [&mc] (int i) { mc.libraryFreqTableSelectUserForTest (i); },
                            [&mc] { return mc.libraryFreqTableUserFileForTest(); },
                            "Page 1 space 2");
                check (mc.libraryImportFreqTableTransactionForTest(),
                       "wp7 freq: a second destination imports");
                check (mc.pageSpaceDefEditableForTest (1, 2)
                       && mc.pageSpaceDefPointCountForTest (1, 2) == 3,
                       "wp7 freq: the second destination holds the table");
                check (mc.pageSpaceDefEditableForTest (0, 1)
                       && mc.pageSpaceDefPointCountForTest (0, 1) == 3,
                       "wp7 freq: the first destination is unchanged");

                // A refusal (no user row) is not a transaction.
                mc.libraryFreqTableSourceForTest (true);
                const int depth2 = mc.undoDepth();
                check (! mc.libraryImportFreqTableTransactionForTest()
                       && mc.undoDepth() == depth2,
                       "wp7 freq: a refused import adds no undo step");
            }

            // A model replacement closes the captured frequency-table
            // destination. Undo and redo restore through the same loader, so
            // neither can revive an editor that belongs to another piece.
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
                auto& mc = *mcHolder;
                check (mc.loadFromVar (testPiece (false, false)),
                       "wp7 freq stale: the source piece loads");
                mc.setCurrentPageForTest (0);
                mc.openFreqTableForTest (1);
                mc.commitFreqTableCurveForTest (
                    { { 0.0f, (float) std::log2 (220.0) },
                      { 1.0f, (float) std::log2 (880.0) } });
                check (mc.libraryPublishFreqTableForTest(),
                       "wp7 freq stale: the source table publishes");
                mc.libraryFreqTableSourceForTest (true);
                check (selectUser (mc.libraryFreqTableItemCountForTest(),
                                   [&mc] (int i) { mc.libraryFreqTableSelectUserForTest (i); },
                                   [&mc] { return mc.libraryFreqTableUserFileForTest(); },
                                   "Page 1 space 2"),
                       "wp7 freq stale: the source table can be selected");

                check (mc.loadFromVar (minimalPiece()),
                       "wp7 freq stale: the replacement piece loads");
                check (! mc.freqTableOpenForTest()
                       && mc.freqTablePageForTest() == -1
                       && mc.freqTableSpaceForTest() == -1,
                       "wp7 freq stale: loading another piece closes the frequency editor");

                const int depth0 = mc.undoDepth();
                mc.commitFreqTableCurveForTest (
                    { { 0.0f, (float) std::log2 (330.0) },
                      { 1.0f, (float) std::log2 (660.0) } });
                check (! mc.libraryImportFreqTableTransactionForTest()
                       && mc.undoDepth() == depth0,
                       "wp7 freq stale: a stale commit and import are both refused");

                mc.undo();
                check (! mc.freqTableOpenForTest()
                       && mc.freqTablePageForTest() == -1,
                       "wp7 freq stale: undo does not revive the old destination");
                mc.commitFreqTableCurveForTest (
                    { { 0.0f, (float) std::log2 (440.0) },
                      { 1.0f, (float) std::log2 (880.0) } });
                check (mc.undoDepth() == depth0 - 1,
                       "wp7 freq stale: an undo-restored destination still refuses a commit");

                mc.redo();
                check (! mc.freqTableOpenForTest()
                       && mc.freqTablePageForTest() == -1,
                       "wp7 freq stale: redo does not revive the old destination");
                check (! mc.libraryImportFreqTableTransactionForTest()
                       && mc.undoDepth() == depth0,
                       "wp7 freq stale: a redo-restored destination still refuses an import");
            }

            // --- sequences -------------------------------------------------
            {
                auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
                check (mc.loadFromVar (testPiece (false, true)),
                       "wp7 import: the sequence piece loads");
                check (mc.seqCountForTest() == 1 && mc.selectedSeqIdForTest() == 5,
                       "wp7 import: the loaded sequence is selected");

                const int depth0 = mc.undoDepth();
                const int count0 = mc.seqCountForTest();
                check (mc.libraryPublishSequenceForTest(),
                       "wp7 publish: the sequence publishes");
                check (mc.undoDepth() == depth0,
                       "wp7 publish: publishing a sequence adds no undo step");

                mc.librarySequenceSourceForTest (true);
                check (selectUser (mc.librarySequenceItemCountForTest(),
                                   [&mc] (int i) { mc.librarySequenceSelectUserForTest (i); },
                                   [&mc] { return mc.librarySequenceUserFileForTest(); },
                                   "Seq 5"),
                       "wp7 import: the published sequence can be selected");
                check (mc.libraryImportSequenceTransactionForTest(),
                       "wp7 import: the sequence imports");
                check (mc.seqCountForTest() == count0 + 1
                       && mc.undoDepth() == depth0 + 1,
                       "wp7 import: a sequence import is one undo step");
                check (mc.seqIdAtForTest (1) == 1,
                       "wp7 import: the sequence got the first free id",
                       juce::String (mc.seqIdAtForTest (1)));
                mc.undo();
                check (mc.seqCountForTest() == count0,
                       "wp7 import: undo restores the sequence bank");

                // An empty sequence is a valid model state and must survive
                // the complete publish/import round trip.
                {
                    upic::PerformanceSequence empty;
                    empty.id = 9;
                    empty.name = "Empty Import";
                    empty.loop = false;
                    check (upic::UserLibrary::publish (
                               upic::UserAssetKind::Sequence, empty.id,
                               empty.name, juce::Colours::purple,
                               empty.toVar(), error),
                           "wp7 import: the empty sequence publishes", error);
                    mc.librarySequenceSourceForTest (true);
                    check (selectUser (mc.librarySequenceItemCountForTest(),
                                       [&mc] (int i) { mc.librarySequenceSelectUserForTest (i); },
                                       [&mc] { return mc.librarySequenceUserFileForTest(); },
                                       "Empty Import"),
                           "wp7 import: the empty sequence can be selected");
                    const int emptyDepth = mc.undoDepth();
                    check (mc.libraryImportSequenceTransactionForTest(),
                           "wp7 import: the empty sequence imports", error);
                    const int importedIndex = mc.seqCountForTest() - 1;
                    check (mc.seqCountForTest() == count0 + 1
                           && mc.seqPointCountAtForTest (importedIndex) == 0
                           && mc.seqNameAtForTest (importedIndex) == "Empty Import"
                           && mc.undoDepth() == emptyDepth + 1,
                           "wp7 import: the empty sequence round-trips without repair",
                           mc.seqNameAtForTest (importedIndex));
                    mc.undo();
                }
            }

            // A sequence's page and position references belong to the
            // destination piece. A missing page or a position beyond that
            // page is refused instead of being silently clamped in playback.
            {
                upic::PerformanceSequence badPage;
                badPage.id = 20;
                badPage.name = "Bad Page";
                badPage.points = { { 0.0, 1, 0.0 } };
                check (upic::UserLibrary::publish (
                           upic::UserAssetKind::Sequence, badPage.id,
                           badPage.name, juce::Colours::purple,
                           badPage.toVar(), error),
                       "wp7 import: the missing-page sequence publishes", error);

                upic::PerformanceSequence badPosition;
                badPosition.id = 21;
                badPosition.name = "Bad Position";
                badPosition.points = { { 0.0, 0, 99.0 } };
                check (upic::UserLibrary::publish (
                           upic::UserAssetKind::Sequence, badPosition.id,
                           badPosition.name, juce::Colours::purple,
                           badPosition.toVar(), error),
                       "wp7 import: the invalid-position sequence publishes", error);

                auto mcHolder = std::make_unique<MainComponent> (true);
                auto& mc = *mcHolder;
                check (mc.loadFromVar (minimalPiece()),
                       "wp7 import: the one-page destination loads");
                const int count0 = mc.seqCountForTest();
                const int depth0 = mc.undoDepth();

                mc.librarySequenceSourceForTest (true);
                check (selectUser (mc.librarySequenceItemCountForTest(),
                                   [&mc] (int i) { mc.librarySequenceSelectUserForTest (i); },
                                   [&mc] { return mc.librarySequenceUserFileForTest(); },
                                   "Bad Page"),
                       "wp7 import: the missing-page sequence can be selected");
                check (! mc.libraryImportSequenceTransactionForTest()
                       && mc.seqCountForTest() == count0
                       && mc.undoDepth() == depth0,
                       "wp7 import: a sequence naming a missing page is refused");

                mc.librarySequenceSourceForTest (true);
                check (selectUser (mc.librarySequenceItemCountForTest(),
                                   [&mc] (int i) { mc.librarySequenceSelectUserForTest (i); },
                                   [&mc] { return mc.librarySequenceUserFileForTest(); },
                                   "Bad Position"),
                       "wp7 import: the invalid-position sequence can be selected");
                check (! mc.libraryImportSequenceTransactionForTest()
                       && mc.seqCountForTest() == count0
                       && mc.undoDepth() == depth0,
                       "wp7 import: a sequence pointing past its destination page is refused");
            }

            // --- the caps refuse instead of truncating --------------------
            {
                // The sequence bank holds two; a third import is refused.
                auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
                auto piece = testPiece (false, true);
                if (auto* pages = piece.getProperty ("pages", juce::var()).getArray())
                    (void) pages;
                {
                    upic::PieceModel m;
                    for (int i = 0; i < upic::limits::kMaxSequences; ++i)
                    {
                        upic::PerformanceSequence s;
                        s.id = i + 1;
                        s.name = "Seq " + juce::String (i + 1);
                        s.points = { { 0.0, 0, 0.0 } };
                        m.sequences.push_back (s);
                    }
                    upic::Page p;
                    m.pages.push_back (p);
                    piece = upic::pieceToVar (m);
                }
                check (mc.loadFromVar (piece),
                       "wp7 caps: the full sequence piece loads");
                check (mc.seqCountForTest() == upic::limits::kMaxSequences,
                       "wp7 caps: the sequence bank is full");
                mc.openSeqPanelForTest();
                const int depth0 = mc.undoDepth();
                mc.libraryPublishSequenceForTest(); // the first sequence
                mc.librarySequenceSourceForTest (true);
                selectUser (mc.librarySequenceItemCountForTest(),
                            [&mc] (int i) { mc.librarySequenceSelectUserForTest (i); },
                            [&mc] { return mc.librarySequenceUserFileForTest(); },
                            "Seq 1");
                check (! mc.libraryImportSequenceTransactionForTest()
                       && mc.undoDepth() == depth0,
                       "wp7 caps: a full sequence bank refuses the import");
            }

            {
                // The amplitude-table bank is capped at 16.
                upic::PieceModel m;
                for (int i = 0; i < upic::limits::kMaxAmplitudeTables; ++i)
                    m.amplitudeTables.push_back (upic::makeAmplitudeTable (
                        i + 1, "Amp " + juce::String (i + 1),
                        upic::paletteColour (i), upic::ampLinear));
                upic::Page p;
                m.pages.push_back (p);
                auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
                check (mc.loadFromVar (upic::pieceToVar (m)),
                       "wp7 caps: the full table piece loads");
                mc.openAmpTableEditorForTest();
                mc.ampTableSelectForTest (0);
                const int depth0 = mc.undoDepth();
                mc.libraryPublishAmpTableForTest();
                mc.libraryAmpTableSourceForTest (true);
                selectUser (mc.libraryAmpTableItemCountForTest(),
                            [&mc] (int i) { mc.libraryAmpTableSelectUserForTest (i); },
                            [&mc] { return mc.libraryAmpTableUserFileForTest(); },
                            "Amp 1");
                check (! mc.libraryImportAmpTableTransactionForTest()
                       && mc.undoDepth() == depth0,
                       "wp7 caps: a full table bank refuses the import");
            }
        }

        //======================================================================
        // 6. The piece does not depend on the library: deleting the folder
        //    changes neither the piece nor its banks.
        //======================================================================
        {

            useLibraryRoot ("independence");

            auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
            check (mc.loadFromVar (testPiece (true, true)),
                   "wp7 independence: the piece loads");
            mc.selectWaveformForTest (5);
            check (mc.libraryPublishWaveForTest(),
                   "wp7 independence: the wavetable publishes");
            mc.selectEnvelopeForTest (7);
            check (mc.libraryPublishEnvelopeForTest(),
                   "wp7 independence: the envelope publishes");

            const auto before = juce::JSON::toString (mc.pieceToVar(), false);
            const int waves = mc.waveformCountForTest();
            const int envs = mc.envelopeCountForTest();

            upic::UserLibrary::rootFolder().deleteRecursively();

            check (juce::JSON::toString (mc.pieceToVar(), false) == before
                   && mc.waveformCountForTest() == waves
                   && mc.envelopeCountForTest() == envs,
                   "wp7 independence: deleting the library does not change the piece");
        }

        //======================================================================
        // 6b. Route arbitration: a block uses one route, and a stale
        //     extraction generation cannot render beside another route.
        //======================================================================
        {
            useLibraryRoot ("routes");

            constexpr double sr = 44100.0;
            const auto sine = sineBuffer (441.0, 88200, sr);
            auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
            mc.setSize (1280, 800);
            check (mc.loadFromVar (minimalPiece()),
                   "wp7 routes: the empty-score fixture loads");
            mc.prepareToPlay (512, sr);
            check (mc.setSampleSlotForTest (0, sine, sr),
                   "wp7 routes: the preview material is installed");

            mc.openSampleExtractForTest();
            mc.sampleExtractDetectForTest();
            mc.sampleExtractPreviewForTest();
            check (mc.sampleExtractPreviewUsesCycleForTest()
                   && mc.audioPreviewRouteForTest() == AudioPreviewRoute::sampleExtract
                   && mc.audioPreviewRouteGenerationForTest()
                        == mc.sampleExtractPreviewGenerationForTest(),
                   "wp7 routes: the extraction route publishes its snapshot generation",
                   juce::String (mc.audioPreviewRouteGenerationForTest()));

            juce::AudioBuffer<float> out (2, 512);
            juce::AudioSourceChannelInfo info (&out, 0, out.getNumSamples());
            const auto peakOf = [&out]
            {
                float peak = 0.0f;
                for (int c = 0; c < out.getNumChannels(); ++c)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                        peak = juce::jmax (peak, std::abs (out.getSample (c, i)));
                return peak;
            };

            out.clear();
            mc.getNextAudioBlock (info);
            check (peakOf() > 0.001f,
                   "wp7 routes: the extracted route renders its own material",
                   juce::String (peakOf(), 4) + " peak");

            // The forced scrub route is applied after the snapshot is taken.
            // It must not render the extraction payload from that snapshot.
            mc.setAudioPreviewRouteOverrideForTest (
                static_cast<int> (AudioPreviewRoute::scrub));
            out.clear();
            mc.getNextAudioBlock (info);
            check (mc.audioPreviewRouteSeenForTest() == AudioPreviewRoute::scrub
                   && peakOf() <= 1.0e-6f,
                   "wp7 routes: a scrub route excludes the extraction material",
                   juce::String (peakOf(), 7) + " peak");

            // The same transition, but with an extraction generation that is
            // one newer than the snapshot. The generation gate must reject it.
            const auto generation = mc.sampleExtractPreviewGenerationForTest();
            mc.setAudioPreviewRouteOverrideForTest (
                static_cast<int> (AudioPreviewRoute::sampleExtract), 1);
            out.clear();
            mc.getNextAudioBlock (info);
            check (mc.audioPreviewRouteSeenForTest() == AudioPreviewRoute::sampleExtract
                   && mc.audioPreviewRouteGenerationSeenForTest() == generation + 1
                   && peakOf() <= 1.0e-6f,
                   "wp7 routes: a stale extraction generation renders nothing",
                   juce::String (mc.audioPreviewRouteGenerationSeenForTest())
                   + " / " + juce::String (peakOf(), 7));

            mc.setAudioPreviewRouteOverrideForTest (-1);
            out.clear();
            mc.getNextAudioBlock (info);
            check (peakOf() > 0.001f,
                   "wp7 routes: clearing the override restores the extraction route",
                   juce::String (peakOf(), 4) + " peak");

            // The ordinary routes claim the same token. A scrub or freeze
            // cancels the extraction request instead of mixing with it.
            mc.sampleExtractScrubForTest (0.1);
            check (mc.sampleExtractPreviewRouteForTest() == SampleExtractRoute::none
                   && mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub
                   && mc.audioPreviewRouteGenerationForTest()
                        == mc.ordinaryPreviewGenerationForTest()
                   && mc.sampleExtractPublishedRouteIsNoneForTest(),
                   "wp7 routes: a scrub cancels extraction and claims the route");

            // Closing the extraction panel must not release the ordinary
            // route that replaced it.
            mc.closeSampleExtractForTest();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub,
                   "wp7 routes: closing extraction does not steal the scrub route");

            mc.sampleExtractFreezeForTest (0.1);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::freeze
                   && mc.audioPreviewRouteGenerationForTest()
                        == mc.ordinaryPreviewGenerationForTest()
                   && mc.sampleExtractPreviewRouteForTest() == SampleExtractRoute::none,
                   "wp7 routes: a freeze replaces the scrub route exclusively");

            // A freeze-tail update is a new versioned record, published with
            // a matching route token. The held voice keeps its transport, but
            // no audio block may combine the new deadline with an old token.
            mc.sampleExtractFreezeStopForTest();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::freeze
                   && mc.audioPreviewRouteGenerationForTest()
                        == mc.ordinaryPreviewGenerationForTest()
                   && mc.scrubFreezeStopAtForTest() > 0.0,
                   "wp7 routes: a freeze-tail update republishes one matching generation");

            // Expiry belongs to one freeze generation. A later tail update
            // must re-adopt the held freeze even if an older block expired
            // the previous generation just before the update was published.
            mc.sampleExtractFreezeStopAtForTest (
                juce::Time::getMillisecondCounterHiRes() + 1.0);
            const auto expiringGeneration = mc.ordinaryPreviewGenerationForTest();
            juce::Thread::sleep (5);
            out.clear();
            mc.getNextAudioBlock (info);
            check (mc.audioFreezeExpiredForTest()
                   && mc.audioFreezeGenerationForTest() == expiringGeneration,
                   "wp7 routes: freeze expiry is tagged with its generation");
            mc.sampleExtractFreezeStopAtForTest (
                juce::Time::getMillisecondCounterHiRes() + 1000.0);
            out.clear();
            mc.getNextAudioBlock (info);
            check (! mc.audioFreezeExpiredForTest()
                   && mc.audioFreezeActiveForTest()
                   && mc.audioFreezeGenerationForTest()
                        == mc.ordinaryPreviewGenerationForTest(),
                   "wp7 routes: a newer freeze tail re-adopts the held freeze");

            // Invalidating the captured extraction source is also an
            // extraction-only cancellation; the freeze keeps the token.
            check (mc.setSampleSlotForTest (
                       0, sineBuffer (330.0, 8192, sr), sr),
                   "wp7 routes: the replacement material is installed");
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::freeze,
                   "wp7 routes: invalidating extraction does not steal the freeze route");

            mc.sampleExtractFreezeCancelForTest();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none,
                   "wp7 routes: cancelling the freeze releases the route");

            // A deferred lifecycle cancellation owns one exact extraction
            // generation. A newer preview armed after the host request must
            // survive that old cancellation.
            mc.openSampleExtractForTest();
            mc.sampleExtractDetectForTest();
            mc.sampleExtractPreviewForTest();
            check (mc.sampleExtractPreviewRouteForTest()
                       == SampleExtractRoute::extracted
                   && mc.audioPreviewRouteForTest()
                       == AudioPreviewRoute::sampleExtract,
                   "wp7 routes: the extraction route is armed before the generation test");
            const auto oldExtractionGeneration =
                mc.sampleExtractPreviewGenerationForTest();
            std::thread oldRelease ([&mc] { mc.releaseResources(); });
            oldRelease.join();
            mc.sampleExtractPreviewForTest();
            const auto newExtractionGeneration =
                mc.sampleExtractPreviewGenerationForTest();

            // The release above queued the old generation's cancellation on
            // the message thread. Drain that real callback now: it must see
            // the newer generation and leave the new preview untouched.
#if JUCE_MODAL_LOOPS_PERMITTED
            if (auto* const messages =
                    juce::MessageManager::getInstanceWithoutCreating();
                messages != nullptr && messages->isThisTheMessageThread())
                messages->runDispatchLoopUntil (20);
#else
            mc.cancelSampleExtractPreviewGenerationForTest (
                oldExtractionGeneration);
#endif
            check (newExtractionGeneration > oldExtractionGeneration
                   && mc.sampleExtractPreviewRouteForTest()
                          != SampleExtractRoute::none
                   && mc.audioPreviewRouteForTest()
                          == AudioPreviewRoute::sampleExtract,
                   "wp7 routes: an old deferred cancellation preserves a newer extraction");

            // A device/host release cancels every route and clears the
            // ordinary route state, not only the extraction snapshot.
            mc.sampleExtractScrubForTest (0.2);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub
                   && mc.scrubPreviewActiveForTest()
                   && mc.scrubPreviewRemainingForTest() > 0,
                   "wp7 routes: the scrub route is armed before the release");
            mc.releaseResources();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest()
                   && ! mc.scrubFreezeActiveForTest()
                   && mc.scrubPreviewRemainingForTest() == 0
                   && mc.scrubFreezeStopAtForTest() == 0.0
                   && mc.scrubFreezeTransitionStartForTest() == 0.0
                   && mc.scrubFreezeTransitionEndForTest() == 0.0,
                   "wp7 routes: a host release clears every preview route");

            // The host may release from a non-message thread. The atomic
            // route state must disappear before that call returns; only the
            // panel cancellation is deferred to the message thread.
            mc.sampleExtractScrubForTest (0.2);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub,
                   "wp7 routes: the scrub route is armed before the off-thread release");
            std::thread hostRelease ([&mc] { mc.releaseResources(); });
            hostRelease.join();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest()
                   && ! mc.scrubFreezeActiveForTest()
                   && mc.scrubPreviewRemainingForTest() == 0,
                   "wp7 routes: an off-thread host release clears the route immediately");

            // Preparation is also a lifecycle boundary, even if the host did
            // not send a separate release first.
            mc.sampleExtractScrubForTest (0.2);
            mc.prepareToPlay (512, sr);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest()
                   && ! mc.scrubFreezeActiveForTest()
                   && mc.scrubPreviewRemainingForTest() == 0,
                   "wp7 routes: host preparation clears every preview route");

            // An ordinary request belongs to one piece. Replacing that piece
            // must end the request before the new model can be rendered.
            mc.sampleExtractScrubForTest (0.2);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub,
                   "wp7 routes: the scrub route is armed before the model replacement");
            check (mc.loadFromVar (minimalPiece()),
                   "wp7 routes: the replacement piece loads");
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest()
                   && ! mc.scrubFreezeActiveForTest()
                   && mc.scrubPreviewRemainingForTest() == 0,
                   "wp7 routes: a model replacement cancels the ordinary route");

            // Play owns the score output. It ends the ordinary request, and
            // no new scrub/freeze claim is accepted until the transport is
            // stopped again.
            mc.sampleExtractFreezeForTest (0.2);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::freeze,
                   "wp7 routes: the freeze route is armed before transport start");
            mc.startPlaybackForTest();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest()
                   && ! mc.scrubFreezeActiveForTest(),
                   "wp7 routes: transport start cancels the ordinary route");
            mc.sampleExtractScrubForTest (0.2);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none,
                   "wp7 routes: a running transport refuses an ordinary preview");
            mc.stopPlaybackForTest();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest(),
                   "wp7 routes: Stop does not revive the ordinary preview");
            for (int i = 0; i < 100 && mc.isPlayingForTest(); ++i)
                mc.getNextAudioBlock (info);
            check (! mc.isPlayingForTest(),
                   "wp7 routes: the stop fade settles before the next route test");
            mc.sampleExtractScrubForTest (0.2);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub,
                   "wp7 routes: a new ordinary preview is accepted after Stop");
            mc.prepareToPlay (512, sr);

            // Undo and redo restore through the same model loader. Both
            // boundaries must cancel a route armed against the old model.
            mc.beginEdit ("Route ownership");
            mc.markModelChanged();
            mc.endEdit();
            check (mc.undoDepth() > 0,
                   "wp7 routes: the route-ownership fixture creates an undo step");
            mc.sampleExtractScrubForTest (0.2);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub,
                   "wp7 routes: the scrub route is armed before undo");
            mc.undo();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest(),
                   "wp7 routes: undo cancels the ordinary route");
            mc.sampleExtractScrubForTest (0.2);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub,
                   "wp7 routes: the scrub route is armed before redo");
            mc.redo();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest(),
                   "wp7 routes: redo cancels the ordinary route");

            // A claim that captured its epoch just before a host boundary
            // must be stale when it finally takes the route lock. Pause the
            // claim at that exact point, run the boundary on a host thread,
            // and let the claim continue: it must refuse rather than re-arm
            // a route after preparation/release has closed the gate.
            mc.pauseNextAudioPreviewRouteClaimForTest();
            std::atomic<bool> boundarySawPause { false };
            std::thread boundary ([&mc, &boundarySawPause]
            {
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
                while (! mc.audioPreviewRouteClaimPausedForTest()
                       && juce::Time::getMillisecondCounterHiRes() < deadline)
                    juce::Thread::sleep (1);
                boundarySawPause.store (
                    mc.audioPreviewRouteClaimPausedForTest(),
                    std::memory_order_release);
                mc.releaseResources();
                mc.resumeAudioPreviewRouteClaimForTest();
            });
            mc.sampleExtractScrubForTest (0.25);
            boundary.join();
            check (boundarySawPause.load (std::memory_order_acquire)
                   && mc.audioPreviewRouteForTest() == AudioPreviewRoute::none
                   && ! mc.scrubPreviewActiveForTest()
                   && mc.scrubPreviewRemainingForTest() == 0,
                   "wp7 routes: a claim racing a lifecycle boundary is refused");

            // The host release invalidated the preparation, so the audio
            // callback stays silent until the next preparation is applied.
            // The message thread applies it here, so the generation becomes
            // current again straight away.
            mc.prepareToPlay (512, sr);
            check (mc.audioPreparationIsCurrentForTest(),
                   "wp7 routes: the preparation is current again after a host release");

            // The forced extraction route must also be safe before the audio
            // reader owns a snapshot. It may select the route, but it cannot
            // dereference a missing snapshot or render stale material.
            mc.setAudioPreviewSkipSnapshotForTest (true);
            mc.setAudioPreviewRouteOverrideForTest (
                static_cast<int> (AudioPreviewRoute::sampleExtract));
            out.clear();
            mc.getNextAudioBlock (info);
            check (mc.audioPreviewRouteSeenForTest()
                       == AudioPreviewRoute::sampleExtract
                   && mc.audioPreviewRouteGenerationSeenForTest() == 0
                   && peakOf() <= 1.0e-6f,
                   "wp7 routes: a forced extraction route is null-safe without a snapshot",
                   juce::String (mc.audioPreviewRouteGenerationSeenForTest())
                   + " / " + juce::String (peakOf(), 7));
            mc.setAudioPreviewRouteOverrideForTest (-1);
            mc.setAudioPreviewSkipSnapshotForTest (false);
        }

        //======================================================================
        // 6a-4. The audio itself proves the route ownership: a model
        //       replacement and a Stop leave the ordinary preview silent,
        //       and cancelling the ordinary route never steals the
        //       extraction route.
        //======================================================================
        {
            useLibraryRoot ("routeaudio");

            constexpr double sr = 44100.0;
            auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
            check (mc.loadFromVar (audiblePiece()),
                   "wp7 route audio: the audible fixture loads");
            mc.prepareToPlay (512, sr);

            juce::AudioBuffer<float> out (2, 512);
            juce::AudioSourceChannelInfo info (&out, 0, out.getNumSamples());
            const auto peakOf = [&out]
            {
                float peak = 0.0f;
                for (int c = 0; c < out.getNumChannels(); ++c)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                        peak = juce::jmax (peak, std::abs (out.getSample (c, i)));
                return peak;
            };

            // The armed scrub preview really renders, so the silence checks
            // below are not vacuous.
            mc.sampleExtractScrubForTest (0.2);
            out.clear();
            mc.getNextAudioBlock (info);
            check (peakOf() > 0.001f,
                   "wp7 route audio: the armed scrub preview renders sound",
                   juce::String (peakOf(), 6) + " peak");

            // A model replacement ends the request: the next block is silent.
            check (mc.loadFromVar (audiblePiece()),
                   "wp7 route audio: the replacement fixture loads");
            out.clear();
            mc.getNextAudioBlock (info);
            check (peakOf() <= 1.0e-6f,
                   "wp7 route audio: a model replacement leaves the ordinary preview silent",
                   juce::String (peakOf(), 7) + " peak");

            // Stop must not revive the ordinary preview either. The block
            // after the stop fade is silent.
            mc.sampleExtractScrubForTest (0.2);
            mc.startPlaybackForTest();
            mc.stopPlaybackForTest();
            for (int i = 0; i < 200 && mc.isPlayingForTest(); ++i)
                mc.getNextAudioBlock (info);
            out.clear();
            mc.getNextAudioBlock (info);
            check (! mc.isPlayingForTest() && peakOf() <= 1.0e-6f,
                   "wp7 route audio: Stop leaves the ordinary preview silent",
                   juce::String (peakOf(), 7) + " peak");

            // The ordinary cancellation is the ordinary routes' business
            // only: an armed extraction preview keeps its route and its
            // material.
            check (mc.setSampleSlotForTest (
                       0, sineBuffer (441.0, 88200, sr), sr),
                   "wp7 route audio: the extraction material is installed");
            mc.openSampleExtractForTest();
            mc.sampleExtractDetectForTest();
            mc.sampleExtractPreviewForTest();
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::sampleExtract,
                   "wp7 route audio: the extraction route is armed");
            mc.cancelOrdinaryPreviewRoutesForTest();
            out.clear();
            mc.getNextAudioBlock (info);
            check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::sampleExtract
                   && peakOf() > 0.001f,
                   "wp7 route audio: cancelling the ordinary routes preserves the extraction",
                   juce::String (peakOf(), 6) + " peak");
        }

        //======================================================================
        // 6b. The ordinary preview record is coherent under concurrent
        //     publication and audio-thread consumption.
        //======================================================================
        {
            useLibraryRoot ("coherence");

            constexpr double sr = 44100.0;
            auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
            check (mc.loadFromVar (minimalPiece()),
                   "wp7 coherence: the fixture loads");
            mc.prepareToPlay (512, sr);

            juce::AudioBuffer<float> out (2, 512);
            juce::AudioSourceChannelInfo info (&out, 0, out.getNumSamples());
            std::atomic<bool> writerDone { false };
            std::atomic<bool> readerStarted { false };
            std::atomic<int> blocksRead { 0 };
            std::thread audioReader ([&]
            {
                readerStarted.store (true, std::memory_order_release);
                while (! writerDone.load (std::memory_order_acquire))
                {
                    out.clear();
                    mc.getNextAudioBlock (info);
                    blocksRead.fetch_add (1, std::memory_order_relaxed);
                }
            });

            const auto readerDeadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
            while (! readerStarted.load (std::memory_order_acquire)
                   && juce::Time::getMillisecondCounterHiRes() < readerDeadline)
                juce::Thread::sleep (1);
            for (int i = 0; i < 400; ++i)
            {
                if ((i & 1) == 0)
                    mc.sampleExtractScrubForTest (0.01 + (double) i * 0.001);
                else
                    mc.sampleExtractFreezeForTest (0.01 + (double) i * 0.001);
            }
            writerDone.store (true, std::memory_order_release);
            audioReader.join();

            check (blocksRead.load (std::memory_order_relaxed) > 0
                   && mc.ordinaryPreviewCoherenceFailureCountForTest() == 0,
                   "wp7 coherence: concurrent route publication never forms a mixed record",
                   juce::String (blocksRead.load (std::memory_order_relaxed))
                   + " blocks, "
                   + juce::String (mc.ordinaryPreviewCoherenceFailureCountForTest())
                   + " failures");
        }

        //======================================================================
        // 6b-2. A host setup thread may prepare while the message thread
        //       replaces the model. Preparation must not read the model.
        //======================================================================
        {
            auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
            check (mc.loadFromVar (minimalPiece()),
                   "wp7 host prepare: the fixture loads");

            std::atomic<int> preparations { 0 };
            std::thread host ([&]
            {
                for (int i = 0; i < 20; ++i)
                {
                    mc.prepareToPlay (512, 44100.0);
                    preparations.fetch_add (1, std::memory_order_relaxed);
                }
            });

            bool loadsSucceeded = true;
            for (int i = 0; i < 20; ++i)
                loadsSucceeded = loadsSucceeded && mc.loadFromVar (minimalPiece());
            host.join();

            check (preparations.load (std::memory_order_relaxed) == 20
                   && loadsSucceeded,
                   "wp7 host prepare: off-thread preparation is safe during model replacement");

            // The host thread only records the request; the message thread
            // applies it. The newest request must become the published
            // generation, otherwise the audio callback would stay silent for
            // ever after a host preparation.
            const bool generationPublished = drainMessageManagerQueueUntil (
                [&mc] { return mc.audioPreparationIsCurrentForTest(); });

            check (generationPublished,
                   "wp7 host prepare: the message thread publishes the requested preparation generation",
                   juce::String (mc.audioPreparationGenerationForTest()));
        }

        //======================================================================
        // 6b-3. A deferred lifecycle cancellation cannot outlive an
        //       off-message-thread destruction of the component.
        //======================================================================
        {
            auto mcHolder = std::make_unique<MainComponent> (true);
            const auto gateClosed =
                mcHolder->audioPreviewCallbackGateClosedFlagForTest();
            std::thread hostRelease ([&mcHolder] { mcHolder->releaseResources(); });
            hostRelease.join();

            std::atomic<bool> destroyStarted { false };
            std::atomic<bool> destroyFinished { false };
            std::thread destroyer ([&mcHolder, &destroyStarted, &destroyFinished]
            {
                destroyStarted.store (true, std::memory_order_release);
                mcHolder.reset();
                destroyFinished.store (true, std::memory_order_release);
            });

            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
            while (! destroyFinished.load (std::memory_order_acquire)
                   && juce::Time::getMillisecondCounterHiRes() < deadline)
            {
#if JUCE_WINDOWS
                // Wait until the destructor has closed the callback gate
                // before dispatching the queued cancellation. The callback
                // must observe the closed gate while the derived destructor
                // is still in progress.
                if (gateClosed != nullptr
                    && gateClosed->load (std::memory_order_acquire)
                    && pumpMessageManagerQueueOnce())
                    continue;
                juce::Thread::sleep (1);
#else
                juce::Thread::sleep (1);
#endif
            }
            destroyer.join();

            check (destroyFinished.load (std::memory_order_acquire),
                   "wp7 teardown: an off-thread release and destruction complete safely");
        }

        //======================================================================
        // 6c. The idle timer does not repaint the score on every tick.
        //======================================================================
        {
            auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
            mc.stopTimer(); // make the count deterministic
            check (mc.loadFromVar (minimalPiece()),
                   "wp7 idle paint: the idle fixture loads");
            const auto before = mc.scoreTimerRepaintRequestCountForTest();
            mc.timerCallback();
            const auto afterFirst = mc.scoreTimerRepaintRequestCountForTest();
            mc.timerCallback();
            const auto afterSecond = mc.scoreTimerRepaintRequestCountForTest();
            check (afterFirst == before + 1 && afterSecond == afterFirst,
                   "wp7 idle paint: an idle timer tick does not repaint the score",
                   juce::String (before) + "/" + juce::String (afterFirst)
                   + "/" + juce::String (afterSecond));
        }

        //======================================================================
        // 7. The realtime rule: the extraction preview snapshot renders with
        //    no allocation on the audio thread.
        //======================================================================
        {

            useLibraryRoot ("realtime");

            constexpr double sr = 44100.0;
            const auto sine = sineBuffer (441.0, 88200, sr);

            auto mcHolder = std::make_unique<MainComponent> (true);
            auto& mc = *mcHolder;
            mc.setSize (1280, 800);
            mc.prepareToPlay (512, sr);
            check (mc.setSampleSlotForTest (0, sine, sr),
                   "wp7 realtime: the preview material is installed");
            mc.openSampleExtractForTest();
            mc.sampleExtractPreviewForTest();
            check (mc.sampleExtractPreviewActiveForTest(),
                   "wp7 realtime: the extracted preview is armed");

            juce::AudioBuffer<float> out (2, 512);
            juce::AudioSourceChannelInfo info (&out, 0, out.getNumSamples());

            // The first extracted block must be allocation-free too: the
            // engine's phase pool and the immutable payload are prepared on
            // the message thread before the route reaches the callback.
            upic::allocCount = 0;
            upic::allocCounting = true;
            out.clear();
            mc.getNextAudioBlock (info);
            upic::allocCounting = false;
            check (upic::allocCount == 0,
                   "wp7 realtime: the first extraction preview block does not allocate",
                   juce::String (upic::allocCount) + " allocation(s)");

            // Warm up the steady-state path, then count it as well.
            for (int i = 0; i < 2; ++i)
            {
                out.clear();
                mc.getNextAudioBlock (info);
            }

            check (mc.sampleExtractSnapshotMatchesPreviewForTest(),
                   "wp7 realtime: the snapshot carries the exact converted preview wave");

            upic::allocCount = 0;
            upic::allocCounting = true;
            for (int i = 0; i < 4; ++i)
            {
                out.clear();
                mc.getNextAudioBlock (info);
            }
            upic::allocCounting = false;

            check (upic::allocCount == 0,
                   "wp7 realtime: the extraction preview does not allocate",
                   juce::String (upic::allocCount) + " allocation(s) in 4 blocks");

            float peak = 0.0f;
            for (int c = 0; c < out.getNumChannels(); ++c)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    peak = juce::jmax (peak, std::abs (out.getSample (c, i)));
            check (peak > 0.001f && peak <= 1.5f,
                   "wp7 realtime: the preview renders audio",
                   juce::String (peak, 3) + " peak");

            // The original side follows the same immutable route and must be
            // allocation-free too, including its first block after the A/B
            // switch publishes a new generation.
            mc.sampleExtractPreviewForTest();
            upic::allocCount = 0;
            upic::allocCounting = true;
            out.clear();
            mc.getNextAudioBlock (info);
            upic::allocCounting = false;
            check (upic::allocCount == 0,
                   "wp7 realtime: the first original preview block does not allocate",
                   juce::String (upic::allocCount) + " allocation(s)");

            for (int i = 0; i < 2; ++i)
            {
                out.clear();
                mc.getNextAudioBlock (info);
            }
            upic::allocCount = 0;
            upic::allocCounting = true;
            for (int i = 0; i < 4; ++i)
            {
                out.clear();
                mc.getNextAudioBlock (info);
            }
            upic::allocCounting = false;
            check (upic::allocCount == 0,
                   "wp7 realtime: the original preview does not allocate",
                   juce::String (upic::allocCount) + " allocation(s) in 4 blocks");

            // An ordinary scrub is another preview route. It cancels the
            // extraction route instead of mixing the two materials.
            mc.sampleExtractPreviewForTest();
            check (mc.sampleExtractPreviewRouteForTest()
                       == SampleExtractRoute::extracted,
                   "wp7 realtime: the extracted route is armed before the scrub");
            mc.sampleExtractScrubForTest (0.1);
            check (mc.sampleExtractPreviewRouteForTest() == SampleExtractRoute::none
                   && ! mc.sampleExtractPreviewActiveForTest(),
                   "wp7 realtime: a scrub cancels the exclusive extraction route");

            // A device/host restart must cancel the private extraction
            // transport before another preparation lifecycle can run.
            mc.openSampleExtractForTest();
            check (mc.sampleExtractPreviewActiveForTest(),
                   "wp7 realtime: the extraction route can be armed before a device restart");
            const uint64_t restartGeneration = mc.sampleExtractPreviewGenerationForTest();
            mc.releaseResources();
            check (mc.sampleExtractPreviewRouteForTest() == SampleExtractRoute::none
                   && ! mc.sampleExtractPreviewActiveForTest()
                   && mc.sampleExtractPreviewGenerationForTest() > restartGeneration,
                   "wp7 realtime: a device restart cancels and advances the extraction generation");

            mc.closeSampleExtractForTest();
        }
    }
    catch (const std::exception& e)
    {
        t.check (false, "wp7: no exception", e.what());
    }
    catch (...)
    {
        t.check (false, "wp7: no exception", "unknown exception");
    }

    upic::UserLibrary::clearRootForTest();

    wp7Parent().deleteRecursively();
}
