#include <JuceHeader.h>
#include <thread>
#include "MainComponent.h"
#include "SelfTest.h"
#include "AllocCounter.h"

//==============================================================================
// The MIDI and the host: the transport follow, the jog and the MIDI input.

void upic::runHostTransportChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6a-11. v1.33.4 (H3): the host transport follow. The review: "the
    //     DAW's transport does not drive the app, so the plugin can
    //     drift from the host" and "offer a clear internal/host clock
    //     choice".
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);
        mc.loadFromVar (juce::JSON::parse (
            "{\"pages\":[{\"duration\":10,\"arcs\":[]}]}"));

        check (! mc.isFollowingHostTransport(),
               "the app's own clock is the default");

        // the mode is off: the host's transport changes nothing
        mc.setHostTransport (true, true, 3.0);
        mc.applyHostTransport();
        check (! mc.isPlayingForTest(),
               "with the app's own clock the host's transport is ignored");
        check (std::abs (mc.playheadForTest()) < 1e-6,
               "and the playhead does not move either",
               juce::String (mc.playheadForTest(), 3));

        // the host's clock: the host plays, the app plays
        mc.setFollowHostTransport (true);
        check (mc.isFollowingHostTransport(),
               "the host's clock can be chosen");
        mc.setHostTransport (true, true, 3.0);
        mc.applyHostTransport();
        check (mc.isPlayingForTest(),
               "the host's transport starts the app");
        check (std::abs (mc.playheadForTest() - 3.0) < 0.01,
               "the app starts where the host is",
               juce::String (mc.playheadForTest(), 3));

        // the host stops, the app stops (the fade-out, as usual)
        mc.setHostTransport (true, false, 3.0);
        mc.applyHostTransport();
        check (mc.stopPendingForTest(),
               "the host's transport stops the app");

        // a host without a playhead leaves the app alone
        {
            auto mc2Holder = std::make_unique<MainComponent>();
            auto& mc2 = *mc2Holder;
            mc2.setSize (1280, 800);
            mc2.setFollowHostTransport (true);
            mc2.setHostTransport (false, false, 0.0);
            mc2.applyHostTransport();
            check (! mc2.isPlayingForTest(),
                   "a host without a playhead does not start the app");
        }
    }

}

void upic::runMidiScrubChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 6a-13. v1.33.4 (H4, UPIC-28): the MIDI scrub transport. The
    //     roadmap: "a CC/notes move a signed playhead".
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize (1280, 800);
        mc.loadFromVar (juce::JSON::parse ("{\"pages\":[{\"duration\":10,\"arcs\":[]}]}"));

        // a NOTE bound to "Scrub" is a signed jog too: its velocity
        // around the centre (64) sets the speed and the direction
        {
            MidiHub::Binding b;
            b.bound = true; b.note = true; b.number = 64; b.channel = 0;
            mc.midiHub().setBinding (MidiHub::Target::scrub, b);
        }
        mc.injectMidiMessage (juce::MidiMessage::noteOn (1, 64, 100.0f / 127.0f));
        mc.pollMidiForTest();
        check (mc.midiScrubRunning(), "the note starts the jog");
        mc.updateMidiScrubForTest();
        check (std::abs (mc.playRateForTest() - 1.0) > 0.1,
               "the note drives the transport's speed",
               juce::String (mc.playRateForTest(), 3));
        check (mc.isPlayingForTest(), "the jog starts the transport");

        // and the note's release does not stop it right away: the note
        // moves the playhead, it is not a play button
        mc.injectMidiMessage (juce::MidiMessage::noteOff (1, 64));
        mc.pollMidiForTest();
        check (mc.isPlayingForTest(),
               "the release of a scrub note does not stop the transport");

        // a note can move the playhead backwards too (it is signed): a
        // low velocity scrubs backwards
        mc.injectMidiMessage (juce::MidiMessage::noteOn (1, 64, 20.0f / 127.0f));
        mc.pollMidiForTest();
        mc.updateMidiScrubForTest();
        check (mc.playRateForTest() < 0.0,
               "a scrub note moves the playhead backwards as well",
               juce::String (mc.playRateForTest(), 3));

        // the release ends the jog and the speed goes back to normal
        mc.injectMidiMessage (juce::MidiMessage::noteOff (1, 64));
        mc.pollMidiForTest();
        for (int i = 0; i < 200 && mc.midiScrubRunning(); ++i)
            mc.updateMidiScrubForTest();
        check (! mc.midiScrubRunning(),
               "the jog ends when the note is released");
        check (std::abs (mc.playRateForTest() - 1.0) < 1e-6,
               "the transport's speed is back to normal",
               juce::String (mc.playRateForTest(), 3));

        // a CC bound to "Scrub" is the signed jog: the value around the
        // centre sets the speed, and the speed goes back when the jog ends
        {
            MidiHub::Binding b;
            b.bound = true; b.note = false; b.number = 7; b.channel = 0;
            mc.midiHub().setBinding (MidiHub::Target::scrub, b);
        }
        mc.injectMidiMessage (juce::MidiMessage::controllerEvent (1, 7, 127));
        mc.pollMidiForTest();
        check (mc.midiScrubRunning(), "the jog is running");
        // a relative CC (a jog wheel) keeps sending its value, so the jog
        // runs at the control's speed
        for (int i = 0; i < 10; ++i)
        {
            mc.injectMidiMessage (juce::MidiMessage::controllerEvent (1, 7, 127));
            mc.pollMidiForTest();
            mc.updateMidiScrubForTest();
        }
        check (mc.playRateForTest() > 1.0,
               "the jog drives the transport's speed",
               juce::String (mc.playRateForTest(), 3));

        // the control comes back to the centre: the jog winds down
        for (int i = 0; i < 200 && mc.midiScrubRunning(); ++i)
        {
            mc.injectMidiMessage (juce::MidiMessage::controllerEvent (1, 7, 64));
            mc.pollMidiForTest();
            mc.updateMidiScrubForTest();
        }
        check (! mc.midiScrubRunning(),
               "the jog ends when the control comes back to the centre");
        check (std::abs (mc.playRateForTest() - 1.0) < 1e-6,
               "the transport's speed is back to normal",
               juce::String (mc.playRateForTest(), 3));
        // the host's transport yields to the jog: the two must not fight
        // over the playhead (the design note for UPIC-28)
        {
            auto mc2Holder = std::make_unique<MainComponent>();
            auto& mc2 = *mc2Holder;
            mc2.setSize (1280, 800);
            mc2.loadFromVar (juce::JSON::parse ("{\"pages\":[{\"duration\":10,\"arcs\":[]}]}"));
            MidiHub::Binding b;
            b.bound = true; b.note = false; b.number = 7; b.channel = 0;
            mc2.midiHub().setBinding (MidiHub::Target::scrub, b);
            mc2.injectMidiMessage (juce::MidiMessage::controllerEvent (1, 7, 127));
            mc2.pollMidiForTest();
            mc2.updateMidiScrubForTest();
            check (mc2.midiScrubRunning() && mc2.isPlayingForTest(),
                   "the jog starts the transport");
            mc2.setFollowHostTransport (true);
            mc2.setHostTransport (true, false, 0.0);   // the host says "stopped"
            mc2.applyHostTransport();
            check (! mc2.stopPendingForTest(),
                   "the host's transport does not stop a running jog");
        }
    }

}

void upic::runMidiInputChecks (upic::SelfTest& t)
{
    auto check = [&t] (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        t.check (ok, name, detail);
    };

    // 22. v1.23 (wave 20): the MIDI input foundation - the device list,
    //     the activity monitor and the learn
    {
        auto mcHolder = std::make_unique<MainComponent>();
        auto& mc = *mcHolder;
        mc.setSize(1280, 800);
        auto& hub = mc.midiHub();

        std::function<juce::Component*(juce::Component&, const juce::String&)> findByName =
            [&findByName](juce::Component& c, const juce::String& n) -> juce::Component* {
                if (c.getName() == n) return &c;
                for (int i = 0; i < c.getNumChildComponents(); ++i)
                    if (auto* r = findByName(*c.getChildComponent(i), n)) return r;
                return nullptr;
            };

        auto* dev = dynamic_cast<juce::ComboBox*>(findByName(mc, "midiDevBox"));
        check(dev != nullptr && dev->getNumItems() >= 1 && dev->getItemText(0) == "None",
              "the MIDI device list is present (None when there is no device)");

        // the learn: a CC binds to the master gain and drives it
        hub.startLearn(MidiHub::Target::masterGain);
        check(hub.isLearning() && hub.learningTarget() == MidiHub::Target::masterGain,
              "MIDI learn starts on the master gain");
        hub.injectForTest(MidiHub::kTypeCc, 2, 7, 100);
        mc.timerCallback();
        const auto bg = hub.binding(MidiHub::Target::masterGain);
        check(bg.bound && !bg.note && bg.number == 7 && bg.channel == 2 && !hub.isLearning(),
              "MIDI learn binds the next CC");

        auto* gain = dynamic_cast<juce::Slider*>(findByName(mc, "masterSlider"));
        check(gain != nullptr && std::abs(gain->getValue() - 100.0 / 127.0) < 0.01,
              "the learned CC drives the master gain",
              gain != nullptr ? juce::String(gain->getValue(), 3) : juce::String("no slider"));

        auto* act = dynamic_cast<juce::Label*>(findByName(mc, "midiActLab"));
        check(act != nullptr && act->getText().contains("CC 7"),
              "the activity monitor shows the last message",
              act != nullptr ? act->getText() : juce::String());

        // a note can be learned for the intensity
        hub.startLearn(MidiHub::Target::drawIntensity);
        hub.injectForTest(MidiHub::kTypeNoteOn, 0, 60, 64);
        mc.timerCallback();
        const auto bi = hub.binding(MidiHub::Target::drawIntensity);
        auto* inten = dynamic_cast<juce::Slider*>(findByName(mc, "intenSlider"));
        check(bi.bound && bi.note && bi.number == 60
              && inten != nullptr && std::abs(inten->getValue() - 64.0 / 127.0) < 0.02,
              "a learned note drives the draw intensity",
              "bound " + juce::String (bi.bound ? 1 : 0) + " note " + juce::String (bi.note ? 1 : 0)
                + " number " + juce::String (bi.number) + " channel " + juce::String (bi.channel)
                + " value " + juce::String (inten != nullptr ? inten->getValue() : -1.0, 3));

        // a real-time message must not steal or cancel the learn
        hub.startLearn(MidiHub::Target::masterGain);
        hub.injectForTest(MidiHub::kTypeOther, 0, 0, 0);
        mc.timerCallback();
        check(hub.isLearning(), "a clock message does not cancel the learn");
        hub.cancelLearn();
        check(!hub.isLearning(), "the learn can be cancelled");

        // clearing a binding stops the parameter from following
        hub.clearBinding(MidiHub::Target::masterGain);
        if (gain != nullptr)
        {
            gain->setValue(0.5, juce::dontSendNotification);
            hub.injectForTest(MidiHub::kTypeCc, 2, 7, 127);
            mc.timerCallback();
            check(!hub.binding(MidiHub::Target::masterGain).bound
                  && std::abs(gain->getValue() - 0.5) < 1e-4,
                  "a cleared binding leaves the parameter alone");
        }

        // v1.33.4 (M20): the messages must not vanish between the polls.
        // The hub used to keep only the newest message, so a note-on and
        // its note-off collapsed into one and the pair was lost.
        {
            juce::Array<MidiHub::Activity> seen;
            hub.injectForTest(MidiHub::kTypeNoteOn, 0, 60, 100);
            hub.injectForTest(MidiHub::kTypeNoteOff, 0, 60, 0);
            hub.pollActivities([&seen](const MidiHub::Activity& a) { seen.add(a); });
            check(seen.size() == 2
                  && seen[0].type == MidiHub::kTypeNoteOn && seen[0].value == 100
                  && seen[1].type == MidiHub::kTypeNoteOff,
                  "a note-on and its note-off both arrive (the pair does not vanish)",
                  juce::String(seen.size()) + " message(s)");
        }

        // v1.33.4 (M20): ONE channel convention. JUCE's channels are
        // 1..16; the app uses the channel index (0..15) and adds one
        // when it shows it. The old code packed the channel with
        // "& 0xf" and showed it +1, so channel 1 read as 2 and channel
        // 16 as 1.
        {
            hub.startLearn(MidiHub::Target::masterGain);
            hub.injectMessage(juce::MidiMessage::controllerEvent(1, 7, 100));
            mc.timerCallback();
            const auto b1 = hub.binding(MidiHub::Target::masterGain);
            check(b1.bound && b1.channel == 0,
                  "MIDI channel 1 is the app's channel 0",
                  "channel " + juce::String(b1.channel));

            hub.startLearn(MidiHub::Target::masterGain);
            hub.injectMessage(juce::MidiMessage::controllerEvent(16, 7, 100));
            mc.timerCallback();
            const auto b16 = hub.binding(MidiHub::Target::masterGain);
            check(b16.bound && b16.channel == 15,
                  "MIDI channel 16 is the app's channel 15",
                  "channel " + juce::String(b16.channel));
            check(act != nullptr && act->getText().contains("ch 16"),
                  "the monitor shows the real MIDI channel",
                  act != nullptr ? act->getText() : juce::String());
        }

        // v1.33.4 (M20): the learn waits for the control the user moves
        // AFTER clicking Learn - a message that is already waiting must
        // not steal the learn.
        {
            hub.injectForTest(MidiHub::kTypeCc, 3, 11, 50);
            hub.startLearn(MidiHub::Target::masterGain);
            hub.injectForTest(MidiHub::kTypeCc, 4, 12, 60);
            mc.timerCallback();
            const auto b = hub.binding(MidiHub::Target::masterGain);
            check(b.bound && b.number == 12 && b.channel == 4,
                  "the learn binds the control the user moves after clicking Learn",
                  "CC " + juce::String(b.number) + " channel " + juce::String(b.channel));
        }

        // v1.33.4 (M20): a flood of continuous controls must not lose the
        // newest value - that is the value a bound parameter needs.
        {
            const int flood = 400;
            for (int i = 0; i < flood; ++i)
                hub.injectForTest(MidiHub::kTypeCc, 0, 20, i % 128);
            juce::Array<MidiHub::Activity> seen;
            hub.pollActivities([&seen](const MidiHub::Activity& a) { seen.add(a); });
            const auto newest = seen.size() > 0 ? seen.getLast() : MidiHub::Activity();
            check(newest.valid && newest.number == 20 && newest.value == ((flood - 1) % 128),
                  "the newest CC value survives a flood",
                  "value " + juce::String(newest.value) + " of "
                    + juce::String(seen.size()) + " message(s)");
            check(hub.droppedCount() > 0,
                  "the queue says how many messages it had to drop",
                  juce::String(hub.droppedCount()) + " dropped");
        }

        // v1.33.4 (M20): two producers at once - the MIDI thread and (in
        // a plugin) the audio thread. The queue must not lose a message
        // and must keep each producer's order.
        {
            MidiHub twoProducers;
            constexpr int perThread = 100;
            std::thread t1([&twoProducers, perThread]
            {
                for (int i = 0; i < perThread; ++i)
                    twoProducers.injectForTest(MidiHub::kTypeCc, 0, 1, i);
            });
            std::thread t2([&twoProducers, perThread]
            {
                for (int i = 0; i < perThread; ++i)
                    twoProducers.injectForTest(MidiHub::kTypeCc, 0, 2, i);
            });
            t1.join();
            t2.join();

            int count1 = 0, count2 = 0, next1 = 0, next2 = 0;
            bool ordered = true;
            twoProducers.pollActivities([&](const MidiHub::Activity& a)
            {
                if (a.number == 1) { if (a.value != next1) ordered = false; ++next1; ++count1; }
                else if (a.number == 2) { if (a.value != next2) ordered = false; ++next2; ++count2; }
            });
            check(count1 == perThread && count2 == perThread && ordered
                  && twoProducers.droppedCount() == 0,
                  "two producers at once: every message arrives, in order",
                  juce::String(count1) + " + " + juce::String(count2) + ", "
                    + juce::String(twoProducers.droppedCount()) + " dropped");
        }

        // v1.33.4 (H9): the MIDI input has to be *started* - JUCE drops
        // every message that arrives before start(), so the standalone
        // never saw a byte of MIDI. The device list and the opener are
        // seams, so this can be checked without a MIDI device.
        {
            struct FakeDevice final : MidiHub::Device
            {
                bool started = false, stopped = false;
                void start() override { started = true; }
                void stop()  override { stopped = true; }
            };
            // the hub owns the device it opens, so the fake is shared and
            // the test keeps it alive
            struct SharedFake final : MidiHub::Device
            {
                explicit SharedFake (std::shared_ptr<FakeDevice> f) : fake (std::move (f)) {}
                void start() override { fake->started = true; }
                void stop()  override { fake->stopped = true; }
                std::shared_ptr<FakeDevice> fake;
            };

            juce::Array<juce::MidiDeviceInfo> fakeDevices;
            // two devices with the same name: only the identifier tells
            // them apart
            fakeDevices.add (juce::MidiDeviceInfo ("UPIC Test In", "id-a"));
            fakeDevices.add (juce::MidiDeviceInfo ("UPIC Test In", "id-b"));
            fakeDevices.add (juce::MidiDeviceInfo ("UPIC Other", "id-c"));

            juce::String askedFor;
            auto fake = std::make_shared<FakeDevice>();

            MidiHub testHub;
            testHub.setSeamsForTest (
                [fakeDevices] { return fakeDevices; },
                [&askedFor, fake] (const juce::String& id, juce::MidiInputCallback*)
                {
                    askedFor = id;
                    return std::unique_ptr<MidiHub::Device> (new SharedFake (fake));
                });

            const bool opened = testHub.open ("id-b");
            check(opened && testHub.isOpen() && askedFor == "id-b",
                  "the MIDI hub opens the device by its identifier",
                  askedFor);
            check(fake->started, "the MIDI hub starts the device it opens");
            check(testHub.openDeviceId() == "id-b" && testHub.openDeviceName() == "UPIC Test In",
                  "the MIDI hub remembers the device it opened");

            // a device that is not in the list is not opened
            askedFor.clear();
            check(!testHub.open("id-nope") && askedFor.isEmpty(),
                  "the MIDI hub does not open a device that is not there");

            // closing stops the device
            testHub.open ("id-c");
            testHub.close();
            check(!testHub.isOpen() && fake->stopped,
                  "closing the MIDI hub stops the device");
        }

        // v1.30 (UPIC-53): the LIVE tab has its own granular grain size
        // (the Sample tab's grain control used to be the only one).
        {
            auto* liveTab = dynamic_cast<juce::Button*>(findByName(mc, "Live"));
            auto* lg = dynamic_cast<juce::Slider*>(findByName(mc, "liveGrainSlider"));
            check(liveTab != nullptr && lg != nullptr
                  && (int) lg->getMinimum() == 5 && (int) lg->getMaximum() == 500
                  && (int) lg->getValue() == 40,
                  "the LIVE tab has a 5-500 ms grain control",
                  lg != nullptr ? juce::String(lg->getValue(), 1) : juce::String("missing"));
            if (liveTab != nullptr && lg != nullptr)
            {
                liveTab->onClick();
                check(lg->isVisible(), "the live grain control follows the Live tab");
                lg->setValue(200.0, juce::sendNotificationSync);
                const auto grainVar = mc.pieceToVar().getProperty("liveGrainMs", 0);
                check((int) grainVar == 200,
                      "the live grain size round-trips in the piece",
                      juce::String((int) grainVar) + " / "
                      + juce::String(lg->getValue(), 1));
                lg->setValue(40.0, juce::sendNotificationSync);
            }
        }

        // v1.30 (UPIC-54/56): MIDI and OSC live in their own settings
        // overlay; the audio overlay is the device selector only. The
        // overlay with the MIDI section (for eyeballing) is still captured.
        auto* settings = dynamic_cast<juce::Button*>(findByName(mc, "settingsBtn"));
        check(settings != nullptr, "the MIDI + OSC settings button is present");
        if (settings != nullptr)
        {
            settings->onClick();
            auto* ov = findByName(mc, "settingsOverlay");
            auto* ok = dynamic_cast<juce::Button*>(findByName(mc, "settingsOkBtn"));
            auto* midiInOverlay = ov != nullptr ? findByName(*ov, "midiDevBox") : nullptr;
            check(ov != nullptr && ov->isVisible()
                  && ok != nullptr && ok->isVisible() && ok->getWidth() >= 8
                  && ov->getBounds().contains(ok->getBounds()),
                  "the settings overlay opens with its OK button");
            check(midiInOverlay == dev && dev != nullptr && dev->getParentComponent() == ov,
                  "MIDI lives in the settings overlay, not the audio overlay");
            check(mc.oscPortForTest() == 9000
                  && mc.oscMulticastPortForTest() == 54321
                  && mc.oscMulticastGroupForTest() == "224.0.0.1",
                  "OSC settings keep the old defaults",
                  juce::String(mc.oscPortForTest()) + " / "
                  + juce::String(mc.oscMulticastPortForTest()) + " / "
                  + mc.oscMulticastGroupForTest());
            auto* port = dynamic_cast<juce::Slider*>(findByName(mc, "oscPortSlider"));
            auto* apply = dynamic_cast<juce::Button*>(findByName(mc, "oscApplyBtn"));
            check(port != nullptr && (int) port->getValue() == 9000
                  && (int) port->getMinimum() == 1024
                  && (int) port->getMaximum() == 65535,
                  "the OSC UDP port is editable",
                  port != nullptr ? juce::String(port->getValue(), 0) : juce::String("missing"));
            if (apply != nullptr)
                apply->onClick(); // rebind with the same values
            check(mc.oscIsListening(),
                  "Apply rebinds the OSC receivers from the settings window");

            auto* onBtn = dynamic_cast<juce::ToggleButton*>(findByName(mc, "oscOnBtn"));
            check(onBtn != nullptr, "the OSC on/off button is present");
            if (onBtn != nullptr && apply != nullptr)
            {
                onBtn->setToggleState(false, juce::dontSendNotification);
                apply->onClick();
                check(!mc.oscIsListening() && !mc.oscEnabledForTest(),
                      "OSC can be turned off from settings");
                onBtn->setToggleState(true, juce::dontSendNotification);
                apply->onClick();
                check(mc.oscIsListening(),
                      "OSC can be turned back on from settings");
            }

            const juce::File png = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                       .getChildFile("upic_snapshot_settings.png");
            juce::Image hi = mc.createComponentSnapshot(mc.getLocalBounds(), false);
            if (hi.isValid())
                if (auto os = png.createOutputStream())
                {
                    juce::PNGImageFormat fmt;
                    fmt.writeImageToStream(hi, *os);
                    os->flush();
                }

            if (ok != nullptr) ok->onClick();
            check(ov != nullptr && ! ov->isVisible(),
                  "the settings overlay OK closes it");

            // the audio overlay must not contain the MIDI section any more
            if (auto* ab = dynamic_cast<juce::Button*>(findByName(mc, "Audio")))
            {
                ab->onClick();
                auto* aov = findByName(mc, "audioOverlay");
                auto* aok = dynamic_cast<juce::Button*>(findByName(mc, "OK"));
                check(aov != nullptr && aov->isVisible()
                      && findByName(*aov, "midiDevBox") == nullptr,
                      "the audio overlay no longer contains MIDI");
                if (aok != nullptr) aok->onClick();
            }
        }
    }

}
