#include "OscScrubReceiver.h"

//==============================================================================
juce::String OscScrubReceiver::statusText() const
{
    if (! oscEnabled)
        return "OSC off";

    juce::String s;
    s << (oscListening ? "UDP " + juce::String (oscPort) + " listening"
                       : "UDP " + juce::String (oscPort) + " unavailable");
    if (oscMulticastEnabled)
        s << "  |  " << (multicastListening
                        ? "multicast " + oscMulticastGroup + ":" + juce::String (oscMulticastPort)
                        : "multicast unavailable");
    else
        s << "  |  multicast off";
    return s;
}

//==============================================================================
void OscScrubReceiver::start()
{
    stop();

    if (! oscEnabled)
        return;

    // v1.29: the phone sends /upic/scrub to UDP 9000 ...
    oscReceiver.addListener (this);
    oscListening = oscReceiver.connect (oscPort);

    // ... and MobMuPlat's default output is multicast 224.0.0.1:54321, which
    // works even when the phone's network settings cannot be changed.
    if (oscMulticastEnabled)
    {
        multicastSocket = std::make_unique<juce::DatagramSocket> (false);
        multicastSocket->setEnablePortReuse (true);
        if (multicastSocket->bindToPort (oscMulticastPort))
        {
            if (multicastSocket->joinMulticast (oscMulticastGroup))
            {
                multicastReceiver.addListener (this);
                multicastListening = multicastReceiver.connectToSocket (*multicastSocket);
            }
        }
    }
}

//==============================================================================
void OscScrubReceiver::stop()
{
    if (multicastListening)
    {
        multicastReceiver.removeListener (this);
        multicastReceiver.disconnect();
        multicastListening = false;
    }
    if (multicastSocket != nullptr)
    {
        multicastSocket->leaveMulticast (oscMulticastGroup);
        multicastSocket->shutdown();
        multicastSocket.reset();
    }
    if (oscListening)
    {
        oscReceiver.removeListener (this);
        oscReceiver.disconnect();
        oscListening = false;
    }
}

//==============================================================================
void OscScrubReceiver::oscMessageReceived (const juce::OSCMessage& message)
{
    handleMessage (message);
}

void OscScrubReceiver::oscBundleReceived (const juce::OSCBundle& bundle)
{
    for (int i = 0; i < bundle.size(); ++i)
    {
        const auto& element = bundle[i];
        if (element.isMessage())
            handleMessage (element.getMessage());
        else if (element.isBundle())
            oscBundleReceived (element.getBundle());
    }
}

void OscScrubReceiver::handleMessage (const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();
    if (address != "/upic/scrub" && address != "/upic/scrub/pos")
        return;
    if (message.size() < 1)
        return;

    float value = 0.0f;
    if (message[0].isFloat32())      value = message[0].getFloat32();
    else if (message[0].isInt32())
    {
        // 0/1 for a binary position, otherwise a 7-bit fader
        const int v = message[0].getInt32();
        value = v <= 1 ? (float) v : (float) v / 127.0f;
    }
    else return;

    owner.oscScrubReceived (juce::jlimit (0.0, 1.0, (double) value));
}
