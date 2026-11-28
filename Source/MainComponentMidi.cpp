#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iterator>

//==============================================================================
// The MIDI and the OSC: the input, the parameters, the jog, the scrub and the
// host transport.

//==============================================================================
// v1.23 (wave 20): the MIDI input foundation - the learn and the parameters
// the MIDI drives. Everything here runs on the message thread (the MIDI
// callback only publishes the message).
//
// v1.33.4 (M20): the hub queues every message, so a note-on and its note-off
// both arrive - they used to collapse into one "newest message" and a pair
// could vanish before the poll. The messages are handled one after the
// other, in the order they arrived.
void MainComponent::handleMidi()
{
    bool any = false;
    midi.pollActivities ([this, &any] (const MidiHub::Activity& a)
    {
        any = true;
        handleMidiActivity (a);
    });

    if (any)
        updateMidiLabels();
}

void MainComponent::handleMidiActivity (const MidiHub::Activity& a)
{
    // the activity monitor
    juce::String what;
    if (a.type == MidiHub::kTypeCc)        what = "CC " + juce::String (a.number);
    else if (a.type == MidiHub::kTypeNoteOn)  what = "note " + juce::String (a.number) + " on";
    else if (a.type == MidiHub::kTypeNoteOff) what = "note " + juce::String (a.number) + " off";
    else                                   what = "MIDI";
    midiActLab.setText (what + " = " + juce::String (a.value)
                        + "   ch " + juce::String (a.channel + 1)
                        + (midi.isOpen() ? "   " + midi.openDeviceName() : juce::String())
                        + (midi.droppedCount() > 0
                              ? "   " + juce::String (midi.droppedCount()) + " dropped" : juce::String()),
                        juce::dontSendNotification);

    // the learn: the next control message binds to the target
    if (midi.isLearning())
    {
        const bool isCc = (a.type == MidiHub::kTypeCc);
        const bool isNote = (a.type == MidiHub::kTypeNoteOn);
        if (isCc || isNote)
        {
            const auto target = midi.learningTarget();
            MidiHub::Binding b;
            b.bound = true;
            b.note = isNote;
            b.number = a.number;
            b.channel = a.channel;
            midi.setBinding (target, b);
            midi.cancelLearn();
            flash ("Learned " + juce::String (isNote ? "note " : "CC ")
                   + juce::String (a.number) + " -> "
                   + (target == MidiHub::Target::masterGain ? "Master gain" : target == MidiHub::Target::drawIntensity ? "Draw intensity" : target == MidiHub::Target::scrub ? "Scrub" : "Transport"));
        }
        // anything else (clock, pitch bend, ...) is ignored: the learn waits
        // for a CC or a note, and the button cancels it
    }

    // the bound parameters follow the MIDI (the MIDI wins while it is bound)
    const auto bg = midi.binding (MidiHub::Target::masterGain);
    if (midi.matches (bg, a))
    {
        const float v = MidiHub::normalised (a);
        masterGain.store (v);
        masterSlider.setValue (v, juce::dontSendNotification);
    }
    const auto bi = midi.binding (MidiHub::Target::drawIntensity);
    if (midi.matches (bi, a))
    {
        const float v = MidiHub::normalised (a);
        drawIntensity = v;
        intenSlider.setValue (v, juce::dontSendNotification);
        pushVoice();
    }
    const auto bs = midi.binding (MidiHub::Target::scrub);
    if (midi.matches (bs, a))
    {
        // v1.33.4 (H4, UPIC-28): the jog. A CC uses its value, a note uses
        // its velocity, and both read it as an offset from the centre (64):
        // above = forward, below = backward, the distance = the speed (up to
        // +/- 4x). Forward produces the pitches as they are, backward plays
        // the material in reverse - the playhead is signed.
        int v = a.value - 64;   // the CC's value, the note's velocity
        double vel = (double) v / 16.0; // 64 -> 4.0
        if (vel > 4.0) vel = 4.0;
        if (vel < -4.0) vel = -4.0;
        scrubTarget = vel;
        scrubActive = true;
    }
    else if (bs.bound && bs.note && a.type == MidiHub::kTypeNoteOff
             && a.number == bs.number)
    {
        // the note's release ends the jog (the transport stops with it)
        scrubTarget = 0.0;
    }
    // transport binding: a learned note/CC toggles play/stop
    const auto bt = midi.binding (MidiHub::Target::transport);
    if (midi.matches (bt, a))
    {
        if (a.type == MidiHub::kTypeNoteOn || a.value > 63)
        {
            if (transportActive()) stopPlayback();
            else startPlayback();
        }
    }
    else if (! bt.bound)
    {
        // no transport binding: fall back to any note (the original behaviour)
        //
        // v1.33.4 (H4): a note that is bound to the scrub target is NOT a
        // play button - it moves the playhead. It must not also start (or
        // stop) the transport.
        const bool isScrubNote = bs.bound && bs.note
                                 && a.type == MidiHub::kTypeNoteOn
                                 && midi.matches (bs, a);
        if (a.type == MidiHub::kTypeNoteOn)
        {
            if (! isScrubNote && ! transportActive()) startPlayback();
        }
        else if (a.type == MidiHub::kTypeNoteOff)
        {
            // the release of a scrub note does not stop the transport
            const bool scrubNoteOff = bs.bound && bs.note && a.number == bs.number;
            if (! scrubNoteOff && transportActive()) stopPlayback();
        }
    }
}

//==============================================================================
// v1.29: OSC scrub. A phone running MobMuPlat sends /upic/scrub with a float
// 0..1; the receiver calls back on the message thread, so the playhead moves
// through the same seek + preview path as a canvas drag. When the messages
// stop, timerCallback() ends the gesture (and never starts playback).
void MainComponent::oscScrubReceived (double normalised)
{
    handleOscScrub (normalised);
}

void MainComponent::handleOscScrub (double normalised)
{
    const double dur = juce::jmax (1.0e-6, curPage().durationSec);
    const double t = juce::jlimit (0.0, dur, normalised * dur);
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    if (! externalScrubActive)
    {
        externalScrubActive = true;
        externalScrubFrozen = false;
        score.externalScrubStart (t);
        if (! oscAnnounced)
        {
            oscAnnounced = true;
            flash ("OSC scrub on UDP " + juce::String (osc.getPort()));
        }
    }
    else
    {
        if (externalScrubFrozen)
        {
            externalScrubFrozen = false;
            if (score.onScrubFreezeCancel != nullptr)
                score.onScrubFreezeCancel();
        }
        score.externalScrubTo (t);
    }

    externalScrubLastT = t;
    lastExternalScrubMs = nowMs;
}

void MainComponent::updateExternalScrub (double nowMs)
{
    if (! externalScrubActive) return;
    const double dt = nowMs - lastExternalScrubMs;
    if (! externalScrubFrozen && dt > 100.0)
    {
        externalScrubFrozen = true;
        if (score.onScrubFreezeStart != nullptr)
            score.onScrubFreezeStart (externalScrubLastT);
    }
    if (dt > 2000.0)
    {
        externalScrubFrozen = false;
        externalScrubActive = false;
        score.externalScrubEnd();
    }
}

void MainComponent::injectMidiMessage (const juce::MidiMessage& m)
{
    // v1.33.4 (M20): the hub does the work - it turns JUCE's channel (1..16)
    // into the app's channel index (0..15) and queues the message, so a
    // note-on and its note-off both arrive.
    midi.injectMessage (m);
}

void MainComponent::updateMidiLabels()
{
    // R5.10: the words come from the edge (Source/MidiSettings.h).
    namespace ms = upic::midi_settings;
    auto describe = [this] (MidiHub::Target t) -> juce::String
    {
        if (midi.isLearning() && midi.learningTarget() == t)
            return ms::learningText();
        const auto b = midi.binding (t);
        if (! b.bound) return ms::unboundText();
        return juce::String (ms::bindingText (b.note, b.number, b.channel).c_str());
    };
    midiBindGainLab.setText (describe (MidiHub::Target::masterGain), juce::dontSendNotification);
    midiBindIntLab.setText (describe (MidiHub::Target::drawIntensity), juce::dontSendNotification);
    midiBindScrubLab.setText (describe (MidiHub::Target::scrub), juce::dontSendNotification);
    midiBindTransportLab.setText (describe (MidiHub::Target::transport), juce::dontSendNotification);
    auto learnText = [this] (MidiHub::Target t) -> juce::String
    {
        return midi.isLearning() && midi.learningTarget() == t ? ms::cancelText() : ms::learnText();
    };
    midiLearnGain.setButtonText (learnText (MidiHub::Target::masterGain));
    midiLearnInt.setButtonText (learnText (MidiHub::Target::drawIntensity));
    midiLearnScrub.setButtonText (learnText (MidiHub::Target::scrub));
    midiLearnTransport.setButtonText (learnText (MidiHub::Target::transport));
}

void MainComponent::refreshMidiDevices()
{
    // the list is rebuilt when the overlay opens, so a device plugged in
    // after the app started shows up; the open device stays selected
    // R5.10: the item ids come from the edge (Source/MidiSettings.h).
    namespace ms = upic::midi_settings;
    const juce::String current = midi.openDeviceId();
    midiDevBox.clear (juce::dontSendNotification);
    midiDevBox.addItem (ms::noneItemName(), ms::kNoneItemId);
    midiDeviceIds.clear();
    const auto infos = MidiHub::deviceInfos();
    int selected = ms::kNoneItemId;
    for (int i = 0; i < infos.size(); ++i)
    {
        midiDevBox.addItem (infos[i].name, ms::itemIdForDeviceIndex (i));
        midiDeviceIds.add (infos[i].identifier);
        // v1.33.4 (H9): the selection follows the identifier, not the name
        if (infos[i].identifier == current) selected = ms::itemIdForDeviceIndex (i);
    }
    midiDevBox.setSelectedId (selected, juce::dontSendNotification);
    if (midi.isOpen() && ms::isNoneItem (selected))
        midi.close();   // the device disappeared
}

//==============================================================================
// v1.33.4 (H3): the host transport follow.
//
// The plugin pushes the host's transport into the atomics (the audio thread
// reads the host's playhead), and the app follows it when the mode is on:
// the host's transport starts and stops the app, and the host's position is
// where the app's playhead is.
//
// The host's time is the page's time: the page loops under the host's
// transport (the host's clock is the master, so the drawing repeats for as
// long as the host plays).
void MainComponent::setHostTransport (bool known, bool playing, double posSec)
{
    // the audio thread: plain numbers only, no UI, no locks
    hostTransportKnown.store (known, std::memory_order_relaxed);
    hostTransportPlaying.store (playing, std::memory_order_relaxed);
    hostTransportPosSec.store (posSec, std::memory_order_relaxed);
}

void MainComponent::setFollowHostTransport (bool on)
{
    followHostTransport.store (on, std::memory_order_relaxed);
    if (on)
        flash ("The transport now follows the host");
    else
        flash ("The transport is the app's own again");
}

void MainComponent::applyHostTransport()
{
    // off (the app's own clock), or the host has no playhead (many hosts do
    // not offer one) - then nothing happens
    if (! followHostTransport.load (std::memory_order_relaxed)) return;
    if (! hostTransportKnown.load (std::memory_order_relaxed)) return;

    // v1.33.4 (H4, UPIC-28): the MIDI jog has the transport while it runs -
    // the host's transport must not fight it over the playhead (the design
    // note for the scrub transport: the two need a mode switch, and this is
    // it: the user's own hand wins over the host).
    if (scrubActive) return;

    const bool hostPlaying = hostTransportPlaying.load (std::memory_order_relaxed);
    const bool appPlaying = transportActive();

    // the host's position, inside the current page (the page loops under the
    // host's transport)
    const double dur = curPage().durationSec;
    const double hostPos = hostTransportPosSec.load (std::memory_order_relaxed);
    double target = hostPos;
    if (dur > 0.0)
    {
        target = std::fmod (hostPos, dur);
        if (target < 0.0) target += dur;
    }

    if (hostPlaying && ! appPlaying)
    {
        // the host starts: the app starts where the host is
        if (dur > 0.0) seekPlayhead (target);
        startPlayback();
        return;
    }

    if (! hostPlaying && appPlaying)
    {
        // the host stops: the app stops with it (the fade-out, as usual)
        stopPlayback();
        return;
    }

    // both are running: the app follows the host's position. A small
    // difference is the timer's own jitter - the app's playhead runs on its
    // own clock between the seeks, so it is only pulled back when the host
    // really moved (a locate in the DAW).
    if (hostPlaying && dur > 0.0 && std::abs (target - displayPlayhead()) > 0.25)
        seekPlayhead (target);
}

//==============================================================================
// v1.33.4 (H4, UPIC-28): the MIDI jog. A CC bound to "Scrub" is a signed
// jog: the value around the centre (64) sets the speed (up to +/- 4x) and
// the target decays, so the jog winds down when the control comes back to
// the centre. The smoothed speed drives the transport's own speed - the
// playhead moves forwards or backwards, and the material is read in the
// matching direction (the sample slots play backwards at a negative rate).
//
// The jog takes the transport's speed over while it runs, so it has to put
// it back when it ends: otherwise the next playback would still run at the
// jog's speed.
void MainComponent::updateMidiScrub()
{
    scrubVelocity = scrubVelocity * kScrubSmoothOld + scrubTarget * kScrubSmoothNew;
    scrubTarget *= kScrubTargetDecay;
    if (std::abs(scrubVelocity) < kScrubHysteresis) scrubVelocity = 0.0;
    if (std::abs(scrubTarget)  < (kScrubHysteresis * 0.4)) scrubTarget  = 0.0;
    if (! scrubActive) return;

    const double v = juce::jlimit (-4.0, 4.0, scrubVelocity);
    if (std::abs(v) > 0.05)
    {
        if (! transportActive())
        {
            prevPlayRate.store (playRate.load (std::memory_order_relaxed), std::memory_order_relaxed);
            startPlayback();
        }
        if (v != playRate.load (std::memory_order_relaxed))
        {
            prevPlayRate.store (playRate.load (std::memory_order_relaxed), std::memory_order_relaxed);
            playRate.store (v, std::memory_order_relaxed);
            dirChangeSerial.fetch_add (1, std::memory_order_relaxed);
        }
    }
    else
    {
        // The jog is over: the speed goes back to the app's own (the Rev
        // toggle's speed), so the next playback is normal. Clear the jog
        // even if the transport has already stopped on its own; otherwise
        // a stale jog can remain armed indefinitely.
        scrubActive = false;
        const double base = basePlayRate();
        if (playRate.load (std::memory_order_relaxed) != base)
        {
            prevPlayRate.store (playRate.load (std::memory_order_relaxed), std::memory_order_relaxed);
            playRate.store (base, std::memory_order_relaxed);
            dirChangeSerial.fetch_add (1, std::memory_order_relaxed);
        }
        if (transportActive())
            stopPlayback();
    }
}

//==============================================================================
// v1.30 (UPIC-54/56): MIDI + OSC settings. The audio overlay no longer owns
// the MIDI section; this overlay is the one place for the controller setup.
void MainComponent::showSettings()
{
    closeAudioSettings();
    refreshMidiDevices();
    // show the applied values, not abandoned edits from a previous visit
    oscPortSlider.setValue((double) osc.getPort(), juce::dontSendNotification);
    oscMcPortSlider.setValue((double) osc.getMulticastPort(), juce::dontSendNotification);
    oscGroupEdit.setText(osc.getMulticastGroup(), juce::dontSendNotification);
    oscMcBtn.setToggleState(osc.isMulticastEnabled(), juce::dontSendNotification);
    oscOnBtn.setToggleState(osc.isEnabled(), juce::dontSendNotification);
    updateOscStatus();
    settingsOverlay.setBounds(getLocalBounds());
    settingsOverlay.setVisible(true);
    settingsOverlay.toFront(false);
    resized();
}

void MainComponent::closeSettings()
{
    settingsOverlay.setVisible(false);
}

void MainComponent::startOscReceiver()
{
    osc.start();
    oscAnnounced = false;
    updateOscStatus();
}

void MainComponent::stopOscReceiver()
{
    osc.stop();
}

void MainComponent::rebindOscReceiver()
{
    osc.start();
    oscAnnounced = false;
    updateOscStatus();
}

void MainComponent::applyOscSettings()
{
    osc.setEnabled (oscOnBtn.getToggleState());
    osc.setPort (juce::jlimit (1024, 65535, (int) oscPortSlider.getValue()));
    osc.setMulticastPort (juce::jlimit (1024, 65535, (int) oscMcPortSlider.getValue()));
    const auto group = oscGroupEdit.getText().trim();
    if (group.isNotEmpty())
        osc.setMulticastGroup (group);
    osc.setMulticastEnabled (oscMcBtn.getToggleState());

    rebindOscReceiver();

    if (osc.isListening())
        flash("OSC scrub on UDP " + juce::String (osc.getPort())
              + (osc.isMulticastListening() ? " + multicast " + osc.getMulticastGroup() + ":"
                                                   + juce::String (osc.getMulticastPort())
                                             : juce::String()));
    else
        flash("OSC unavailable: UDP port " + juce::String (osc.getPort()) + " busy");
}


void MainComponent::updateOscStatus()
{
    oscStatusLab.setText (osc.statusText(), juce::dontSendNotification);

    if (! osc.isEnabled())
    {
        statusLab.setTooltip ("OSC scrub disabled in MIDI + OSC settings");
        return;
    }

    statusLab.setTooltip ("OSC scrub: send /upic/scrub <0..1> to UDP port "
                          + juce::String (osc.getPort())
                          + " (or multicast " + osc.getMulticastGroup() + ":"
                          + juce::String (osc.getMulticastPort()) + ")");
}
