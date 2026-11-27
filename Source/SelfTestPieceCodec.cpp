#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"
#include "UpicModel.h"

//==============================================================================
// R4.1: the document's file format - the load.
//
// The document's load is two things today: the document is read (and
// repaired where it has to be), and the result is put in force. R4.1
// splits them: the codec reads the document and hands the candidate over,
// and the component puts it in force. The codec edge is where the file
// format lives - the same place every other format in the app lives
// (Source/PageCodec.h and its neighbours).
//
// This suite is that edge's contract, and it comes before the move: it
// pins what the loader does to a document today - what it refuses, what
// it repairs, what it drops, what it tells the user, and what the
// document looks like afterwards.
//
// The suite reaches the loader through one door (the `load` helper at the
// top of the file), so the move touches the suite in one place and the
// checks themselves do not change.

namespace
{
    // What a load did: whether the document was a piece at all, what the
    // user was told, the document the load produced and the two pitch
    // modes the document puts in force.
    struct Loaded
    {
        bool ok = false;          // the document was a piece
        juce::String message;     // what the user is told
        juce::var document;       // the document the load produced
        int samplePitchMode = -1; // the mode the document puts in force (-1: it puts none)
        int livePitchMode = -1;
    };

    // The loader, reached through one door.
    Loaded load (MainComponent& mc, const juce::var& v)
    {
        Loaded out;
        out.ok = mc.loadFromVar (v);
        out.message = mc.flashMessageForTest();
        out.document = mc.pieceToVar();
        // a document that carries a pitch mode puts it in force; a document
        // that carries none leaves the session's mode alone.
        out.samplePitchMode = (v.hasProperty ("samplePitchMode")
                               && (int) v.getProperty ("samplePitchMode", -1) >= 0)
                              ? mc.samplePitchModeForTest() : -1;
        out.livePitchMode = (v.hasProperty ("livePitchMode")
                             && (int) v.getProperty ("livePitchMode", -1) >= 0)
                            ? mc.livePitchModeForTest() : -1;
        return out;
    }

    // A root property of a document: "key":value.
    juce::String property (const juce::String& key, const juce::String& value)
    {
        return "\"" + key + "\":" + value;
    }

    // A document with one page and one arc, plus the root properties the
    // caller asks for. The properties come first, so the caller does not
    // have to think about the commas.
    juce::var document (const juce::String& rootProperties = {})
    {
        juce::String json = "{";
        if (rootProperties.isNotEmpty()) json += rootProperties + ",";
        json += "\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":["
                "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,"
                "\"intensity\":0.8,\"points\":[{\"t\":0,\"f\":220},{\"t\":15,\"f\":440}]}]}]}";
        return juce::JSON::parse (json);
    }

    // A slot with a little audio in it: the base64 of four mono frames.
    juce::String sampleAudio()
    {
        short pcm[4] = { 1000, 2000, 3000, 4000 };
        return juce::Base64::toBase64 (pcm, sizeof (pcm));
    }

    // The array a property holds. The property is held in a named variable
    // first: the array of a temporary dangles.
    juce::Array<juce::var>* arrayOf (const juce::var& v, const char* key)
    {
        const auto prop = v.getProperty (key, juce::var());
        return prop.getArray();
    }

    // How many entries an array property has (-1 when there is none).
    int countOf (const juce::var& v, const char* key)
    {
        if (auto* arr = arrayOf (v, key)) return arr->size();
        return -1;
    }

    // The arcs of a page of the document.
    juce::Array<juce::var>* arcsOfPage (const juce::var& doc, int index)
    {
        auto* pages = arrayOf (doc, "pages");
        if (pages == nullptr || index >= pages->size()) return nullptr;
        const auto page = pages->getReference (index);
        return page.getProperty ("arcs", juce::var()).getArray();
    }

    // The groups of a page of the document.
    juce::Array<juce::var>* groupsOfPage (const juce::var& doc, int index)
    {
        auto* pages = arrayOf (doc, "pages");
        if (pages == nullptr || index >= pages->size()) return nullptr;
        const auto page = pages->getReference (index);
        return page.getProperty ("groups", juce::var()).getArray();
    }

    double numberOf (const juce::var& v, const char* key, double fallback = -1.0)
    {
        return (double) v.getProperty (key, fallback);
    }

    bool closeTo (double a, double b)
    {
        return std::abs (a - b) < 1e-6;
    }
}

void upic::runPieceCodecChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // --- a document that is not a piece is refused -------------------------
    {
        struct Case { const char* label; juce::var value; };
        const std::vector<Case> cases = {
            { "nothing",         juce::var() },
            { "an empty object", juce::JSON::parse ("{}") },
            { "no pages",        juce::JSON::parse ("{\"pages\":[]}") },
            { "rubbish pages",   juce::JSON::parse ("{\"pages\":\"nonsense\"}") }
        };

        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;
        const auto before = juce::JSON::toString (mc.pieceToVar(), false);

        bool allRefused = true;
        juce::String detail;
        for (const auto& c : cases)
        {
            if (load (mc, c.value).ok)
            {
                allRefused = false;
                detail << c.label << " was accepted  ";
            }
        }

        check (allRefused, "a document that is not a piece is refused", detail.trim());
        check (juce::JSON::toString (mc.pieceToVar(), false) == before,
               "a refused document leaves the piece alone");
    }

    // --- the defaults of an old document -----------------------------------
    // A document that carries nothing but its pages keeps the app's
    // defaults for everything else - that is what makes an old file open
    // the way it did.
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, document());
        check (loaded.ok, "a document with only pages loads");
        const auto& doc = loaded.document;

        check (closeTo (numberOf (doc, "master"), 0.8),
               "the master gain of an old document is 0.8",
               juce::String (numberOf (doc, "master"), 3));
        check ((int) doc.getProperty ("currentPage", -1) == 0,
               "an old document opens on its first page");
        check (! (bool) doc.getProperty ("seqPages", true),
               "the page sequencer of an old document is off");
        check (! (bool) doc.getProperty ("recordPressure", true),
               "the pressure recording of an old document is off");
        check ((bool) doc.getProperty ("scrubAudition", false),
               "the scrub audition of an old document is on");
        check ((bool) doc.getProperty ("muteWhileDragging", false),
               "the muted dragging of an old document is on");
        check ((int) doc.getProperty ("dragSmoothMs", -1) == 20,
               "the drag smoothing of an old document is 20 ms");
        check ((int) doc.getProperty ("scrubGrainMs", -1) == 40,
               "the scrub grain of an old document is 40 ms");
        check ((int) doc.getProperty ("sampleGrainMs", -1) == 40,
               "the sample grain of an old document is 40 ms");
        check ((int) doc.getProperty ("liveGrainMs", -1) == 40,
               "the live grain of an old document is 40 ms");
        check (! (bool) doc.getProperty ("sieveOn", true),
               "the sieve of an old document is off");
        check ((int) doc.getProperty ("sieveM", -1) == 12,
               "the sieve of an old document has twelve divisions");
        check (countOf (doc, "sieveR") == 1,
               "the sieve of an old document has one remainder");
        check (closeTo (numberOf (doc, "stickiness"), 1.0),
               "the stickiness of an old document is full");
        check (closeTo (numberOf (doc, "timeGrid"), 0.0),
               "the time grid of an old document is off");

        // the banks an old document does not carry are the app's own
        check (countOf (doc, "waveforms") == 6,
               "an old document gets the app's wavetables",
               juce::String (countOf (doc, "waveforms")));
        check (countOf (doc, "envelopes") == 5,
               "an old document gets the app's envelopes",
               juce::String (countOf (doc, "envelopes")));

        // and it does not grow the features it does not have
        check (! doc.hasProperty ("ampTables") && ! doc.hasProperty ("sequences")
               && ! doc.hasProperty ("channels") && ! doc.hasProperty ("workspace")
               && ! doc.hasProperty ("tuning"),
               "an old document does not grow the optional features");
        check (loaded.message.isEmpty(),
               "an old document is loaded without a repair message",
               loaded.message);
    }

    // --- the numbers of a document are clamped -----------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto high = load (mc, document (
            property ("master", "5.0") + "," +
            property ("stickiness", "5.0") + "," +
            property ("timeGrid", "100.0") + "," +
            property ("dragSmoothMs", "5000") + "," +
            property ("scrubGrainMs", "0") + "," +
            property ("sampleGrainMs", "9999") + "," +
            property ("liveGrainMs", "1") + "," +
            property ("sieveM", "500") + "," +
            property ("currentPage", "99")));
        check (high.ok, "a document with numbers over the range loads");
        const auto& doc = high.document;
        check (closeTo (numberOf (doc, "master"), upic::limits::kMaxMasterGain),
               "the master gain over the range is pulled down to the top",
               juce::String (numberOf (doc, "master"), 3));
        check (closeTo (numberOf (doc, "stickiness"), 1.0),
               "the stickiness over the range is pulled down to full");
        check (closeTo (numberOf (doc, "timeGrid"), 64.0),
               "the time grid over the range is pulled down to the longest",
               juce::String (numberOf (doc, "timeGrid"), 1));
        check ((int) doc.getProperty ("dragSmoothMs", -1) == 1000,
               "the drag smoothing over the range is pulled down to the longest");
        check ((int) doc.getProperty ("scrubGrainMs", -1) == 5,
               "the scrub grain under the range is pulled up to the shortest");
        check ((int) doc.getProperty ("sampleGrainMs", -1) == 500,
               "the sample grain over the range is pulled down to the longest");
        check ((int) doc.getProperty ("liveGrainMs", -1) == 5,
               "the live grain under the range is pulled up to the shortest");
        check ((int) doc.getProperty ("sieveM", -1) == 120,
               "the sieve over the range is pulled down to the largest");
        check ((int) doc.getProperty ("currentPage", -1) == 0,
               "the page over the range is pulled back to the first");

        const auto low = load (mc, document (
            property ("master", "-1.0") + "," +
            property ("stickiness", "-2.0") + "," +
            property ("timeGrid", "-5.0")));
        check (low.ok, "a document with numbers under the range loads");
        check (closeTo (numberOf (low.document, "master"), 0.0),
               "the master gain under the range is pulled up to silence",
               juce::String (numberOf (low.document, "master"), 3));
        check (closeTo (numberOf (low.document, "stickiness"), 0.0),
               "the stickiness under the range is pulled up to none");
        check (closeTo (numberOf (low.document, "timeGrid"), 0.0),
               "the time grid under the range is pulled up to off");
    }

    // --- the sieve's remainders are folded into the sieve -------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, document (
            property ("sieveM", "12") + "," +
            property ("sieveR", "[0,7,13,-1]")));
        check (loaded.ok, "a document with sieve remainders loads");
        auto* remainders = arrayOf (loaded.document, "sieveR");
        const int expected[4] = { 0, 7, 1, 11 };
        bool folded = remainders != nullptr && remainders->size() == 4;
        if (folded)
            for (int i = 0; i < 4; ++i)
                folded = folded && (int) (*remainders)[i] == expected[i];
        check (folded, "the sieve remainders are folded into the sieve",
               juce::String (remainders != nullptr ? remainders->size() : -1));

        const auto empty = load (mc, document (property ("sieveR", "[]")));
        check (empty.ok, "a document with no sieve remainder loads");
        check (countOf (empty.document, "sieveR") == 1,
               "a document with no sieve remainder gets the octave sieve");
    }

    // --- what does not fit is dropped, and the user is told -----------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        // 101 pages: the piece format holds 100.
        juce::String pages = "{\"pages\":[";
        for (int i = 0; i < 101; ++i)
        {
            if (i > 0) pages += ",";
            pages += "{\"duration\":30,\"nextArcId\":2,\"arcs\":[]}";
        }
        pages += "]}";

        const auto loaded = load (mc, juce::JSON::parse (pages));
        check (loaded.ok, "a document with more pages than the format holds loads");
        check (countOf (loaded.document, "pages") == upic::limits::kMaxPages,
               "the pages over the cap are dropped",
               juce::String (countOf (loaded.document, "pages")));
        check (loaded.message == "This piece is bigger than the app supports - not loaded: 1 page",
               "the user is told that a page did not make it in", loaded.message);
    }
    {
        // every bank over its cap, in one document.
        juce::String json = "{\"waveforms\":[";
        for (int i = 0; i < upic::limits::kMaxWaveforms + 1; ++i)
        {
            if (i > 0) json += ",";
            json += "{\"id\":" + juce::String (i + 1) + ",\"name\":\"W\",\"samples\":[0.1,0.2]}";
        }
        json += "],\"envelopes\":[";
        for (int i = 0; i < upic::limits::kMaxEnvelopes + 1; ++i)
        {
            if (i > 0) json += ",";
            json += "{\"id\":" + juce::String (i + 1) + ",\"name\":\"E\"}";
        }
        json += "],\"ampTables\":[";
        for (int i = 0; i < upic::limits::kMaxAmplitudeTables + 1; ++i)
        {
            if (i > 0) json += ",";
            json += "{\"id\":" + juce::String (i + 1) + ",\"name\":\"T\"}";
        }
        json += "],\"sequences\":[";
        for (int i = 0; i < upic::limits::kMaxSequences + 1; ++i)
        {
            if (i > 0) json += ",";
            json += "{\"id\":" + juce::String (i + 1) + ",\"name\":\"S\"}";
        }
        json += "],\"channels\":[";
        for (int i = 0; i < upic::limits::kMaxChannels + 1; ++i)
        {
            if (i > 0) json += ",";
            json += "{\"id\":" + juce::String (i + 1) + ",\"name\":\"C\"}";
        }
        json += "],\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":["
                "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,"
                "\"intensity\":0.8,\"points\":[{\"t\":0,\"f\":220}]}]}]}";

        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, juce::JSON::parse (json));
        check (loaded.ok, "a document with banks over their caps loads");
        check (countOf (loaded.document, "waveforms") == upic::limits::kMaxWaveforms
               && countOf (loaded.document, "envelopes") == upic::limits::kMaxEnvelopes
               && countOf (loaded.document, "ampTables") == upic::limits::kMaxAmplitudeTables
               && countOf (loaded.document, "sequences") == upic::limits::kMaxSequences
               && countOf (loaded.document, "channels") == upic::limits::kMaxChannels,
               "the banks over their caps are dropped",
               juce::String (countOf (loaded.document, "waveforms")) + " wavetable(s), "
                 + juce::String (countOf (loaded.document, "envelopes")) + " envelope(s)");
        check (loaded.message == "This piece is bigger than the app supports - not loaded: "
                                 "1 wavetable, 1 envelope, 1 amplitude table, "
                                 "1 performance sequence, 1 channel",
               "the user is told what did not make it in, bank by bank", loaded.message);
    }

    // --- the ids are made unique -------------------------------------------
    {
        const auto doc = juce::JSON::parse (
            "{\"pages\":[{\"duration\":30,\"nextArcId\":3,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"intensity\":0.8,"
            "\"points\":[{\"t\":0,\"f\":220}]},"
            "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"intensity\":0.5,"
            "\"points\":[{\"t\":0,\"f\":330}]}]}]}");

        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, doc);
        check (loaded.ok, "a document with a duplicate arc id loads");
        auto* arcs = arcsOfPage (loaded.document, 0);
        check (arcs != nullptr && arcs->size() == 2
               && (int) (*arcs)[0].getProperty ("id", 0) == 1
               && (int) (*arcs)[1].getProperty ("id", 0) == 2,
               "the duplicate arc id is renumbered",
               arcs != nullptr && arcs->size() == 2
                   ? juce::String ((int) (*arcs)[1].getProperty ("id", 0)) : juce::String ("no arcs"));
        check (loaded.message == "This piece is bigger than the app supports - not loaded: "
                                 "1 item had a duplicate id and has been renumbered",
               "the user is told that an id was renumbered", loaded.message);
    }

    // --- the references are put back onto material that is there -----------
    {
        const auto doc = juce::JSON::parse (
            "{\"waveforms\":[{\"id\":7,\"name\":\"W\",\"samples\":[0.1,0.2]}],"
            "\"envelopes\":[{\"id\":8,\"name\":\"E\"}],"
            "\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":999,\"envelopeId\":999,"
            "\"ampTable\":99,\"pmIndexEnv\":99,\"ch\":99,"
            "\"intensity\":0.8,\"points\":[{\"t\":0,\"f\":220}]}],"
            "\"groups\":[{\"id\":1,\"name\":\"G\",\"wave\":999,\"env\":999,"
            "\"ampTable\":99,\"channel\":99}]}]}");

        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, doc);
        check (loaded.ok, "a document with references to nothing loads");
        auto* arcs = arcsOfPage (loaded.document, 0);
        check (arcs != nullptr && arcs->size() == 1
               && (int) (*arcs)[0].getProperty ("waveformId", -1) == 7,
               "an arc that points at no wavetable is put back onto one that is there",
               arcs != nullptr && arcs->size() == 1
                   ? juce::String ((int) (*arcs)[0].getProperty ("waveformId", -1))
                   : juce::String ("no arcs"));
        check (arcs != nullptr && arcs->size() == 1
               && (int) (*arcs)[0].getProperty ("envelopeId", -1) == 8,
               "an arc that points at no envelope is put back onto one that is there");
        check (arcs != nullptr && arcs->size() == 1
               && ! (*arcs)[0].hasProperty ("ampTable"),
               "an arc that points at no conversion table goes without one");
        check (arcs != nullptr && arcs->size() == 1
               && (int) (*arcs)[0].getProperty ("pmIndexEnv", -1) == 0,
               "an arc that points at no index envelope goes without one");
        check (arcs != nullptr && arcs->size() == 1
               && ! (*arcs)[0].hasProperty ("ch"),
               "an arc that points at no channel goes to the main one");

        auto* groups = groupsOfPage (loaded.document, 0);
        check (groups != nullptr && groups->size() == 1
               && ! (*groups)[0].hasProperty ("wave")
               && ! (*groups)[0].hasProperty ("env")
               && ! (*groups)[0].hasProperty ("ampTable")
               && ! (*groups)[0].hasProperty ("channel"),
               "a group that points at nothing goes without the override");

        check (loaded.message.contains ("pointed at material that was not there"),
               "the user is told that a reference was put back", loaded.message);
    }

    // --- a modulator chain that loops is broken ----------------------------
    {
        const auto doc = juce::JSON::parse (
            "{\"pages\":[{\"duration\":30,\"nextArcId\":3,\"arcs\":["
            "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"mod\":2,"
            "\"intensity\":0.8,\"points\":[{\"t\":0,\"f\":220}]},"
            "{\"id\":2,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"mod\":1,"
            "\"intensity\":0.8,\"points\":[{\"t\":0,\"f\":330}]}]}]}");

        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, doc);
        check (loaded.ok, "a document with a modulator loop loads");
        auto* arcs = arcsOfPage (loaded.document, 0);
        check (arcs != nullptr && arcs->size() == 2
               && ! (*arcs)[0].hasProperty ("mod")
               && ! (*arcs)[1].hasProperty ("mod"),
               "the modulator loop is broken",
               arcs != nullptr && arcs->size() == 2
                   ? juce::String ((int) (*arcs)[0].getProperty ("mod", -1)) + ", "
                     + juce::String ((int) (*arcs)[1].getProperty ("mod", -1))
                   : juce::String ("no arcs"));
    }

    // --- the material's Ref (M5) -------------------------------------------
    // The Ref belongs to the material, not to the arc. A document written
    // before M5 carries it on the arc, so the slot inherits the Ref of the
    // first arc that uses it - and an arc that disagreed is told.
    {
        const auto doc = juce::JSON::parse (
            "{\"samples\":[{\"id\":1,\"name\":\"Take\",\"sampleRate\":44100,"
            "\"channels\":1,\"frames\":4,\"data\":\"" + sampleAudio() + "\"}],"
            "\"pages\":[{\"duration\":30,\"nextArcId\":3,\"arcs\":["
            "{\"id\":1,\"mode\":1,\"sampleId\":1,\"waveformId\":1,\"envelopeId\":1,"
            "\"refFreq\":330,\"intensity\":0.8,\"points\":[{\"t\":0,\"f\":220}]},"
            "{\"id\":2,\"mode\":1,\"sampleId\":1,\"waveformId\":1,\"envelopeId\":1,"
            "\"refFreq\":440,\"intensity\":0.8,\"points\":[{\"t\":0,\"f\":330}]}]}]}");

        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, doc);
        check (loaded.ok, "a document with a sample loads");
        auto* samples = arrayOf (loaded.document, "samples");
        check (samples != nullptr && samples->size() == 1
               && closeTo (numberOf ((*samples)[0], "ref"), 330.0),
               "the material takes the Ref of the first arc that uses it",
               samples != nullptr && samples->size() == 1
                   ? juce::String (numberOf ((*samples)[0], "ref"), 1) : juce::String ("no sample"));
        check (loaded.message == "This piece was adjusted: 1 arc had a Ref of its own "
                                 "- the material's Ref decides now",
               "the user is told that an arc had a Ref of its own", loaded.message);
    }

    // --- the workspace layout comes back with the document -----------------
    {
        const auto twoPages = juce::JSON::parse (
            "{\"workspace\":{\"pages\":[0,1],\"split\":0.7},"
            "\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":[]},"
            "{\"duration\":30,\"nextArcId\":2,\"arcs\":[]}]}");

        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, twoPages);
        check (loaded.ok, "a document with a workspace loads");
        const auto workspace = loaded.document.getProperty ("workspace", juce::var());
        check (workspace.isObject(), "the workspace comes back with the document");
        check (countOf (workspace, "pages") == 2,
               "the workspace keeps the pages it names",
               juce::String (countOf (workspace, "pages")));
        check (closeTo (numberOf (workspace, "split"), 0.7),
               "the workspace keeps the split it names",
               juce::String (numberOf (workspace, "split"), 2));
    }
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto clamped = load (mc, juce::JSON::parse (
            "{\"workspace\":{\"pages\":[0,1],\"split\":0.9},"
            "\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":[]},"
            "{\"duration\":30,\"nextArcId\":2,\"arcs\":[]}]}"));
        check (clamped.ok, "a document with a workspace over the range loads");
        check (closeTo (numberOf (clamped.document.getProperty ("workspace", juce::var()), "split"), 0.8),
               "the split over the range is pulled back to the edge");

        const auto missing = load (mc, juce::JSON::parse (
            "{\"workspace\":{\"pages\":[0,99],\"split\":0.5},"
            "\"pages\":[{\"duration\":30,\"nextArcId\":2,\"arcs\":[]}]}"));
        check (missing.ok, "a document with a workspace over a missing page loads");
        check (! missing.document.hasProperty ("workspace"),
               "a workspace that names a page that is not there is left out");
    }

    // --- the pitch modes ---------------------------------------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto modes = load (mc, document (
            property ("samplePitchMode", "2") + "," +
            property ("livePitchMode", "1")));
        check (modes.ok, "a document with the pitch modes loads");
        check (modes.samplePitchMode == 2,
               "the sample pitch mode of the document is put in force",
               juce::String (modes.samplePitchMode));
        check (modes.livePitchMode == 1,
               "the live pitch mode of the document is put in force",
               juce::String (modes.livePitchMode));

        const auto clamped = load (mc, document (
            property ("samplePitchMode", "9") + "," +
            property ("livePitchMode", "9")));
        check (clamped.ok, "a document with pitch modes over the range loads");
        check (clamped.samplePitchMode == 3,
               "the sample pitch mode over the range is pulled down to the last",
               juce::String (clamped.samplePitchMode));
        check (clamped.livePitchMode == 2,
               "the live pitch mode over the range is pulled down to the last",
               juce::String (clamped.livePitchMode));

        const auto none = load (mc, document());
        check (none.ok, "a document without the pitch modes loads");
        check (none.samplePitchMode == -1 && none.livePitchMode == -1,
               "a document without the pitch modes leaves the session's modes alone",
               juce::String (none.samplePitchMode) + ", " + juce::String (none.livePitchMode));
    }

    // --- the document that comes back is the document that went in ---------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto first = load (mc, document (
            property ("master", "0.6") + "," +
            property ("sieveM", "7") + "," +
            property ("sieveR", "[1,3]")));
        check (first.ok, "a document loads");

        const auto text = juce::JSON::toString (first.document, false);
        const auto second = load (mc, first.document);
        check (second.ok, "the document the load produced loads again");
        check (juce::JSON::toString (second.document, false) == text,
               "the document that comes back is the document that went in");
    }

    // --- a document may carry keys the app does not know -------------------
    {
        auto mcHolder = std::make_unique<MainComponent> (true);
        auto& mc = *mcHolder;

        const auto loaded = load (mc, document (
            property ("app", "\"something else\"") + "," +
            property ("version", "99") + "," +
            property ("wibble", "{\"a\":1}")));
        check (loaded.ok, "a document with unknown keys loads");
        check (countOf (loaded.document, "pages") == 1,
               "the unknown keys do not disturb the document");
    }
}
