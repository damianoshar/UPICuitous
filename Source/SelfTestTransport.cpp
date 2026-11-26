#include <JuceHeader.h>
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
// R0 (refactoring wave R0, part 3): the transport and preview characterization.
//
// The real-time audio work (R6) extracts the transport state machine and the
// preview router. Before they move, their contracts are written down here as
// one table:
//
//   - the four transport states and what Play and Stop do from each of them
//     (the F1 checks walk the same paths, but spread across the fade and
//     voice checks - this suite pins the whole matrix in one place),
//   - a preview route is refused while the transport is not stopped,
//   - one preview route at a time: a scrub replaces a freeze and vice versa,
//   - a stale owner's cancel does not release a newer route,
//   - Play from Stopped cancels the preview routes, Stop while Stopped
//     leaves them alone.

namespace
{
    using TS = MainComponent::TransportState;

    const char* stateName (TS s)
    {
        switch (s)
        {
            case TS::stopped:  return "Stopped";
            case TS::starting: return "Starting";
            case TS::playing:  return "Playing";
            case TS::stopping: return "Stopping";
        }
        return "?";
    }
}

void upic::runTransportPreviewCharacterizationChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    const juce::String piece =
        "{\"master\":1,\"pages\":[{\"duration\":4,\"nextArcId\":2,\"arcs\":["
        "{\"id\":1,\"mode\":0,\"waveformId\":1,\"envelopeId\":1,\"intensity\":0.8,"
        "\"points\":[{\"t\":0,\"f\":220},{\"t\":4,\"f\":440}]}]}]}";

    auto mcHolder = std::make_unique<MainComponent> (true);
    auto& mc = *mcHolder;
    mc.setSize (1280, 800);
    check (mc.loadFromVar (juce::JSON::parse (piece)),
           "R0 the transport's piece loads");
    mc.prepareToPlay (512, 44100.0);

    juce::AudioBuffer<float> out (2, 512);
    juce::AudioSourceChannelInfo info (&out, 0, out.getNumSamples());
    auto render = [&] { mc.getNextAudioBlock (info); };
    auto settle = [&] (int blocks) { for (int i = 0; i < blocks; ++i) render(); };
    render();   // absorb the load

    // Drive the transport to a state from any state: stop, settle, then play
    // (and settle) as far as the target needs.
    auto driveTo = [&] (TS target)
    {
        mc.stopPlaybackForTest();
        settle (6);
        if (target == TS::stopped) return;
        mc.clickPlayForTest();
        if (target == TS::starting) return;
        settle (4);
        if (target == TS::playing) return;
        mc.stopPlaybackForTest();   // stopping
    };

    // --- every state is reachable ------------------------------------------
    {
        bool all = true;
        juce::String detail;
        for (auto target : { TS::stopped, TS::starting, TS::playing, TS::stopping })
        {
            driveTo (target);
            if (mc.transportStateForTest() != target)
            {
                all = false;
                detail << stateName (target) << " -> "
                       << stateName (mc.transportStateForTest()) << "  ";
            }
        }
        check (all, "R0 the transport reaches every state", detail.trim());
    }

    // --- the transition table, one row per state ---------------------------
    // stopped: Play starts a fresh run (a reset), Stop is a no-op
    {
        driveTo (TS::stopped);
        const int reset0 = mc.engineResetCountForTest();
        mc.clickPlayForTest();
        check (mc.transportStateForTest() == TS::starting
               && mc.isPlayingForTest() && ! mc.stopPendingForTest()
               && mc.engineResetCountForTest() == reset0 + 1,
               "R0 the table: Play from Stopped starts a fresh run",
               stateName (mc.transportStateForTest()));

        driveTo (TS::stopped);
        const int seeks = mc.seekCountForTest();
        mc.stopPlaybackForTest();
        check (mc.transportStateForTest() == TS::stopped
               && ! mc.isPlayingForTest() && ! mc.stopPendingForTest()
               && mc.seekCountForTest() == seeks,
               "R0 the table: Stop from Stopped is a no-op",
               stateName (mc.transportStateForTest()));
    }

    // starting: Play is a no-op, Stop begins the fade
    {
        driveTo (TS::starting);
        const int reset0 = mc.engineResetCountForTest();
        mc.clickPlayForTest();
        check (mc.transportStateForTest() == TS::starting
               && mc.engineResetCountForTest() == reset0,
               "R0 the table: Play from Starting is a no-op",
               stateName (mc.transportStateForTest()));

        mc.stopPlaybackForTest();
        check (mc.transportStateForTest() == TS::stopping
               && mc.isPlayingForTest() && mc.stopPendingForTest(),
               "R0 the table: Stop from Starting begins the fade",
               stateName (mc.transportStateForTest()));

        settle (6);
        check (mc.transportStateForTest() == TS::stopped,
               "R0 the fade ends in Stopped",
               stateName (mc.transportStateForTest()));
    }

    // playing: Play is a no-op, Stop begins the fade
    {
        driveTo (TS::playing);
        const int reset0 = mc.engineResetCountForTest();
        mc.clickPlayForTest();
        check (mc.transportStateForTest() == TS::playing
               && ! mc.stopPendingForTest()
               && mc.engineResetCountForTest() == reset0,
               "R0 the table: Play from Playing is a no-op",
               stateName (mc.transportStateForTest()));

        mc.stopPlaybackForTest();
        check (mc.transportStateForTest() == TS::stopping
               && mc.isPlayingForTest() && mc.stopPendingForTest(),
               "R0 the table: Stop from Playing begins the fade",
               stateName (mc.transportStateForTest()));
    }

    // stopping: Play cancels the fade without re-anchoring, Stop is a no-op
    {
        driveTo (TS::stopping);
        const int reset0 = mc.engineResetCountForTest();
        const int seeks = mc.seekCountForTest();
        mc.clickPlayForTest();
        check (mc.transportStateForTest() == TS::playing
               && ! mc.stopPendingForTest()
               && mc.engineResetCountForTest() == reset0
               && mc.seekCountForTest() == seeks,
               "R0 the table: Play from Stopping cancels the fade",
               stateName (mc.transportStateForTest()));

        mc.stopPlaybackForTest();
        mc.stopPlaybackForTest();
        check (mc.transportStateForTest() == TS::stopping,
               "R0 the table: Stop from Stopping is a no-op",
               stateName (mc.transportStateForTest()));

        settle (6);
        check (mc.transportStateForTest() == TS::stopped,
               "R0 the cancelled run settles in Stopped",
               stateName (mc.transportStateForTest()));
    }

    // --- a preview route is refused while the transport is not stopped ------
    {
        driveTo (TS::playing);
        mc.sampleExtractScrubForTest (0.1);
        check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none,
               "R0 a scrub is refused while Playing",
               juce::String ((int) mc.audioPreviewRouteForTest()));

        mc.sampleExtractFreezeForTest (0.1);
        check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none,
               "R0 a freeze is refused while Playing",
               juce::String ((int) mc.audioPreviewRouteForTest()));

        mc.stopPlaybackForTest();
        check (mc.transportStateForTest() == TS::stopping,
               "R0 the fade is under way for the Stopping check",
               stateName (mc.transportStateForTest()));
        mc.sampleExtractScrubForTest (0.1);
        check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::none,
               "R0 a scrub is refused while Stopping",
               juce::String ((int) mc.audioPreviewRouteForTest()));

        settle (6);
    }

    // --- one route at a time, and a stale owner cannot release a newer one --
    {
        driveTo (TS::stopped);

        mc.sampleExtractScrubForTest (0.1);
        check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub
               && mc.audioPreviewRouteGenerationForTest()
                    == mc.ordinaryPreviewGenerationForTest(),
               "R0 a scrub claims the route while Stopped");

        mc.sampleExtractFreezeForTest (0.1);
        check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::freeze
               && mc.audioPreviewRouteGenerationForTest()
                    == mc.ordinaryPreviewGenerationForTest(),
               "R0 a freeze replaces the scrub route");

        // The freeze is no longer the owner: its cancel must not release the
        // scrub that replaced it.
        mc.sampleExtractScrubForTest (0.2);
        mc.sampleExtractFreezeCancelForTest();
        check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub,
               "R0 a stale freeze cancel does not release the scrub",
               juce::String ((int) mc.audioPreviewRouteForTest()));

        // Stop while Stopped is a no-op: the preview keeps the route.
        mc.stopPlaybackForTest();
        check (mc.audioPreviewRouteForTest() == AudioPreviewRoute::scrub,
               "R0 Stop while Stopped leaves the preview route alone",
               juce::String ((int) mc.audioPreviewRouteForTest()));

        // Play from Stopped, on the other hand, ends the preview routes.
        mc.clickPlayForTest();
        check (mc.transportStateForTest() == TS::starting
               && mc.audioPreviewRouteForTest() == AudioPreviewRoute::none,
               "R0 Play from Stopped cancels the preview route",
               juce::String ((int) mc.audioPreviewRouteForTest()));

        settle (6);
    }
}
