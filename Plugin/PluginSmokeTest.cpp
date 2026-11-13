// Smoke test for the VST3 wrapper (v1.11): builds the real
// UpicuitousAudioProcessor and drives it the way a host would - constructs it
// without an audio device, prepares, feeds the input bus, renders through
// processBlock, and round-trips the host state. No DAW required.
//
//   cmake --build Builds/Plugin --config Release --target UPICuitousPluginSmokeTest
//   Builds\Plugin\UPICuitousPluginSmokeTest_artefacts\Release\UPICuitousPluginSmokeTest.exe
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MainComponent.h"

namespace
{
int failures = 0;

void check(bool ok, const juce::String& what, const juce::String& detail = {})
{
    if (! ok) ++failures;
    std::cout << (ok ? "PASS  " : "FAIL  ") << what
              << (detail.isEmpty() ? juce::String() : "  [" + detail + "]") << std::endl;
}

juce::Component* findIn(juce::Component& c, const juce::String& n)
{
    if (c.getName() == n) return &c;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* r = findIn(*c.getChildComponent(i), n))
            return r;
    return nullptr;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::cout << "UPICuitous VST3 smoke test" << std::endl;

    {
        UpicuitousAudioProcessor proc;
        auto& main = proc.getMainComponent();
        check(main.isPluginMode(), "MainComponent runs in plugin mode (no device)");

        const double sr = 44100.0;
        const int blockSize = 512;
        proc.prepareToPlay(sr, blockSize);
        check(proc.getTotalNumInputChannels() == 2 && proc.getTotalNumOutputChannels() == 2,
              "stereo in / stereo out layout");

        // the JUCE process buffer is in-place: channel k carries input k on
        // entry, then the output - 2 channels for a 2 in / 2 out plugin
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        check(proc.getBusBuffer(buf, false, 0).getWritePointer(0) == buf.getWritePointer(0),
              "host buffer is in-place (output bus starts at channel 0)");

        // the transport and monitor buttons keep their captions as names
        auto* playBtn = dynamic_cast<juce::Button*>(findIn(main, "Play"));
        auto* stopBtn = dynamic_cast<juce::Button*>(findIn(main, "Stop"));
        auto* monBtn  = dynamic_cast<juce::Button*>(findIn(main, "Mon"));
        check(playBtn != nullptr && stopBtn != nullptr && monBtn != nullptr,
              "transport + monitor controls found in the UI");

        // 1) stopped -> silence
        buf.clear();
        proc.processBlock(buf, midi);
        check(buf.getMagnitude(0, 0, blockSize) == 0.0f
                  && buf.getMagnitude(1, 0, blockSize) == 0.0f,
              "silent output while stopped");

        // 2) the host input bus reaches the monitor mix (Live tab plumbing)
        if (monBtn != nullptr)
        {
            monBtn->setToggleState(true, juce::dontSendNotification);
            monBtn->onClick(); // plugin mode: the host bus is the live input
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.5f * std::sin(juce::MathConstants<float>::twoPi
                                                * 220.0f * (float) i / (float) sr);
                buf.setSample(0, i, s);
                buf.setSample(1, i, s);
            }
            proc.processBlock(buf, midi);
            // input gain defaults to 0.8 -> about 0.4 peak for a 0.5 sine
            check(buf.getMagnitude(0, 0, blockSize) > 0.25f,
                  "host input reaches the monitor mix",
                  juce::String(buf.getMagnitude(0, 0, blockSize)));
            monBtn->setToggleState(false, juce::dontSendNotification);
            monBtn->onClick();
        }

        // 3) Play renders the piece through processBlock (internal transport)
        if (playBtn != nullptr && stopBtn != nullptr)
        {
            playBtn->onClick();
            float pk = 0.0f;
            for (int b = 0; b < 200; ++b) // ~2.3 s of host blocks
            {
                buf.clear();
                proc.processBlock(buf, midi);
                pk = juce::jmax(pk, buf.getMagnitude(0, 0, blockSize),
                                buf.getMagnitude(1, 0, blockSize));
            }
            check(pk > 0.1f, "Play renders audio through processBlock", juce::String(pk));
            stopBtn->onClick();
        }

        // 4) host state: the same JSON text as a saved .upic.json piece
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        const auto text = mb.toString();
        check(text.contains("UPICuitous") && text.contains("pages"),
              "host state is a .upic.json piece", juce::String((int) mb.getSize()) + " bytes");
        {
            UpicuitousAudioProcessor other;
            other.prepareToPlay(sr, blockSize);
            other.setStateInformation(mb.getData(), (int) mb.getSize());
            juce::MemoryBlock mb2;
            other.getStateInformation(mb2);
            check(mb2.toString() == text, "state round-trips through host save/load");
        }

        // v1.33.4 (M23): the exports use the host's rate - in a plugin there
        // is no audio device, so the export used to fall back to 44.1 kHz and
        // sound different from the session.
        check(main.exportSampleRate() == (double) sr,
              "the export uses the host's sample rate",
              juce::String(main.exportSampleRate(), 0) + " Hz (host " + juce::String(sr, 0) + " Hz)");

        // 6) the host latency: a live-only page reports the shifter's delay
        //    (the component publishes it from its status timer, so the
        //     message loop has to run for it to reach the host)
        {
            const juce::String json = "{\"pages\":[{\"duration\":4,\"nextArcId\":9,\"arcs\":["
                                     "{\"id\":2,\"mode\":2,\"points\":[{\"t\":0,\"f\":220},{\"t\":2,\"f\":220}]}]}]}";
            main.loadFromVar(juce::JSON::parse(json));
            const int want = (int) std::ceil(upic::GrainShifter::kLagSec * (float) sr);
            check(main.liveLatencySamples() == want, "the component computes the live latency",
                  juce::String(main.liveLatencySamples()) + " samples (want " + juce::String(want) + ")");
            // the status timer publishes it in a real host; a console app has
            // no message loop (modal loops are compiled out for plugins), so
            // the publication is triggered directly here
            main.publishLatency();
            check(proc.getLatencySamples() == want,
                  "host latency reported for a live-only page",
                  juce::String(proc.getLatencySamples()) + " samples (want "
                      + juce::String(want) + ")");
        }

        // 7) the editor hosts the processor UI and detaches cleanly
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            check(ed != nullptr && ed->getWidth() == 1280 && ed->getHeight() == 800,
                  "editor opens at 1280x800");
            check(main.getParentComponent() == ed.get(), "editor hosts the processor UI");
            ed.reset(); // the host closes the window / destroys the editor
            check(main.getParentComponent() == nullptr, "UI survives closing the editor");
            buf.clear();
            proc.processBlock(buf, midi); // audio must keep running without an editor
            check(true, "audio still processes after the editor is gone");
        }

        // 8) v1.33.4 (H10): a state load that arrives after the processor is
        //    gone must not touch the dead component. The marshalled load
        //    (the one a host triggers from a foreign thread) carries a
        //    handle that is nulled when the component dies - the old code
        //    captured a raw pointer and dereferenced freed memory.
        {
            juce::Component::SafePointer<MainComponent> safe (nullptr);
            {
                UpicuitousAudioProcessor tmp;
                tmp.prepareToPlay (sr, blockSize);
                safe = &tmp.getMainComponent();
                check (safe != nullptr, "the state handle follows the component");
            }
            check (safe == nullptr, "the state handle is cleared when the component dies");
            UpicuitousAudioProcessor::applyState (safe, juce::JSON::parse ("{\"pages\":[]}"));
            check (true, "a state load after the processor's death is skipped, not a crash");
        }

        // 9) v1.33.4 (M18): the host's MIDI reaches the app. The VST3 only
        //    declares a MIDI input bus when the build asked for one
        //    (NEEDS_MIDI_INPUT in Plugin/CMakeLists.txt - the compile-time
        //    guard in PluginProcessor.cpp keeps the two in step); here the
        //    message itself is checked.
        {
            check (proc.acceptsMidi(), "the processor accepts MIDI");
            juce::MidiBuffer hostMidi;
            hostMidi.addEvent (juce::MidiMessage::controllerEvent (3, 42, 99), 0);
            proc.processBlock (buf, hostMidi);
            const auto activity = main.midiHub().pollActivity();
            check (activity.valid && activity.type == MidiHub::kTypeCc
                   && activity.channel == 3 && activity.number == 42 && activity.value == 99,
                   "the host's MIDI reaches the app",
                   "ch " + juce::String (activity.channel + 1) + " CC "
                       + juce::String (activity.number) + " = " + juce::String (activity.value));
        }
    }

    std::cout << (failures == 0 ? "ALL CHECKS PASSED"
                                : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
