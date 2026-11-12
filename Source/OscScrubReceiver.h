#pragma once

//==============================================================================
// The OSC scrub receiver - the phone/MobMuPlat controller.
//
// MobMuPlat (or any OSC sender) moves the playhead with
//     /upic/scrub <0..1>
// over UDP. The receiver listens on two endpoints:
//   - a plain UDP port (9000 by default), and
//   - MobMuPlat's default multicast output (224.0.0.1:54321), so the
//     controller works even when the phone cannot save a custom output IP.
//
// Both endpoints are configurable in the MIDI + OSC settings overlay; Apply
// rebinds them. The values are session-only (the piece format does not
// carry them).
//
// v1.30 (5.3 refactor): the receiver, the endpoints and the rebinding moved
// out of MainComponent - they are one self-contained concern.
//==============================================================================
#include <JuceHeader.h>

class OscScrubReceiver : public juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    // The owner of the receiver. The receiver itself only turns OSC
    // messages into scrub values; what a scrub means is the app's business.
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void oscScrubReceived (double normalised) = 0;
    };

    static constexpr int kDefaultPort = 9000;
    static constexpr int kDefaultMulticastPort = 54321;
    static constexpr auto kDefaultMulticastGroup = "224.0.0.1";

    explicit OscScrubReceiver (Listener& owner) : owner (owner) {}
    ~OscScrubReceiver() override { stop(); }

    // --- the endpoints ---

    void setEnabled (bool on)         { oscEnabled = on; }
    bool isEnabled() const            { return oscEnabled; }
    void setMulticastEnabled (bool on){ oscMulticastEnabled = on; }
    bool isMulticastEnabled() const   { return oscMulticastEnabled; }
    void setPort (int p)              { oscPort = p; }
    int  getPort() const              { return oscPort; }
    void setMulticastPort (int p)     { oscMulticastPort = p; }
    int  getMulticastPort() const     { return oscMulticastPort; }
    void setMulticastGroup (const juce::String& g) { oscMulticastGroup = g; }
    juce::String getMulticastGroup() const { return oscMulticastGroup; }

    bool isListening() const          { return oscListening || multicastListening; }
    bool isMulticastListening() const { return multicastListening; }

    // The one-line status for the settings overlay.
    juce::String statusText() const;

    // --- the receiver ---

    // Bind the endpoints. Call this after changing them (that is what
    // Apply does): the old sockets are closed and the new ones opened.
    void start();
    void stop();

    // --- the message-loop callbacks (the listener base) ---

    void oscMessageReceived (const juce::OSCMessage& message) override;
    void oscBundleReceived (const juce::OSCBundle& bundle) override;

    // For the selftest: run a message through the same path as a real one.
    void handleMessageForTest (const juce::OSCMessage& m) { handleMessage (m); }

private:
    void handleMessage (const juce::OSCMessage& message);

    Listener& owner;
    juce::OSCReceiver oscReceiver;
    juce::OSCReceiver multicastReceiver;
    std::unique_ptr<juce::DatagramSocket> multicastSocket;
    bool oscListening = false;
    bool multicastListening = false;
    bool oscEnabled = true;
    bool oscMulticastEnabled = true;
    int oscPort = kDefaultPort;
    int oscMulticastPort = kDefaultMulticastPort;
    juce::String oscMulticastGroup { kDefaultMulticastGroup };
};
