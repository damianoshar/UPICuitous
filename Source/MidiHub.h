#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>

//==============================================================================
// The MIDI input foundation (wave 20).
//
// Three things, and nothing more:
//   - the **device list**: which MIDI input the app listens to;
//   - the **activity monitor**: what came in last;
//   - the **learn**: bind a MIDI control to a parameter.
//
// The MIDI scrub transport (the thing that will *use* this) is a later wave
// (UPIC-28). This is the plumbing it needs.
//
// Threading: JUCE calls the MidiInput callback on its own thread, so the
// callback must not touch the app. It only publishes the last message into
// one atomic (the message packed into the low half, a counter in the high
// half - one read, so the pair is always consistent) and the message thread
// (the 30 Hz timer) picks it up. No locks, no message-queue flooding.
class MidiHub
{
public:
    // What a MIDI control can drive. The scrub transport will add its own
    // (an action, not a parameter) later.
    enum class Target { masterGain, drawIntensity, scrub, transport, none };

    struct Activity
    {
        bool valid = false;
        int type = 0;      // 0 = CC, 1 = note on, 2 = note off, 3 = other
        int channel = 0;
        int number = 0;    // the CC number or the note number
        int value = 0;
    };

    struct Binding
    {
        bool bound = false;
        bool note = false;   // a note (on) instead of a CC
        int number = -1;      // the CC or the note number
        int channel = -1;     // -1 = any channel
    };

    MidiHub() = default;
    ~MidiHub() { close(); }

    //--- the device handle ----------------------------------------------
    // A thin wrapper over the platform MIDI input.
    //
    // JUCE only delivers messages to an input that was *started*: the
    // MidiInput returned by openDevice() is inactive until start() is
    // called, and every message that arrives before that is dropped. The
    // wrapper exists so that "the hub starts what it opens" is one visible
    // step, and so that the selftest can hand in a fake and watch it.
    class Device
    {
    public:
        virtual ~Device() = default;
        virtual void start() = 0;
        virtual void stop() = 0;
    };

    // The real thing: a juce::MidiInput.
    class RealDevice final : public Device
    {
    public:
        explicit RealDevice (std::unique_ptr<juce::MidiInput> d) : input (std::move (d)) {}
        void start() override { if (input != nullptr) input->start(); }
        void stop()  override { if (input != nullptr) input->stop(); }
    private:
        std::unique_ptr<juce::MidiInput> input;
    };

    //--- the devices ----------------------------------------------------
    // The devices the app can listen to (name and identifier). The
    // identifier is what identifies a device - two devices can share a
    // name, and a name can change.
    static juce::Array<juce::MidiDeviceInfo> deviceInfos()
    {
        return juce::MidiInput::getAvailableDevices();
    }

    // Opens a device by its identifier ("" = close). Returns false when the
    // device could not be opened.
    bool open (const juce::String& deviceId)
    {
        close();
        if (deviceId.isEmpty()) return true;

        for (const auto& d : currentDevices())
        {
            if (d.identifier != deviceId) continue;

            callback = std::make_unique<Callback> (*this);
            auto dev = openDevice (deviceId, callback.get());
            if (dev != nullptr)
            {
                // v1.33.4 (H9): JUCE drops every message until the input is
                // started - openDevice() alone never delivers anything.
                dev->start();
                device = std::move (dev);
                openId = deviceId;
                openName = d.name;
                return true;
            }
            callback.reset();
            return false;
        }
        return false;
    }

    void close()
    {
        if (device != nullptr) device->stop();
        device.reset();   // closes the device
        callback.reset();
        openId.clear();
        openName.clear();
    }

    bool isOpen() const { return device != nullptr; }
    juce::String openDeviceName() const { return openName; }
    juce::String openDeviceId() const { return openId; }

    //--- the activity (the message thread polls it) ----------------------
    // Returns the newest message (and clears it). An empty one means
    // nothing arrived.
    Activity pollActivity()
    {
        Activity a;
        const auto packed = slot.load (std::memory_order_acquire);
        const auto count = (uint32_t) (packed >> 32);
        if (count == seenCount)
            return a;
        seenCount = count;

        const uint32_t msg = (uint32_t) (packed & 0xffffffffu);
        a.valid = true;
        a.type      = (int) ((msg >> 28) & 0x3);
        a.channel   = (int) ((msg >> 24) & 0xf);
        a.number    = (int) ((msg >> 16) & 0xff);
        a.value     = (int) ((msg >> 8)  & 0xff);
        return a;
    }

    // The number of messages that arrived (the monitor's "N messages").
    uint32_t messageCount() const { return (uint32_t) (slot.load (std::memory_order_relaxed) >> 32); }

    //--- the learn ------------------------------------------------------
    void startLearn (Target t) { learning = t; }
    void cancelLearn() { learning = Target::none; }
    bool isLearning() const { return learning != Target::none; }
    Target learningTarget() const { return learning; }

    Binding binding (Target t) const
    {
        const auto i = indexOf (t);
        return i < 0 ? Binding() : bindings[(size_t) i];
    }

    void setBinding (Target t, const Binding& b)
    {
        const auto i = indexOf (t);
        if (i >= 0) bindings[(size_t) i] = b;
    }

    void clearBinding (Target t)
    {
        const auto i = indexOf (t);
        if (i >= 0) bindings[(size_t) i] = Binding();
    }

    // True when the message is the one the target is bound to.
    bool matches (const Binding& b, const Activity& a) const
    {
        if (! b.bound || ! a.valid) return false;
        if (b.channel >= 0 && b.channel != a.channel) return false;
        if (b.note) return a.type == 1 && a.number == b.number;
        return a.type == 0 && a.number == b.number;
    }

    // The value a bound target should take (0..1) from the message.
    static float normalised (const Activity& a)
    {
        return juce::jlimit (0.0f, 1.0f, (float) a.value / 127.0f);
    }

    //--- the test hook --------------------------------------------------
    // Injects a message as if it had come from the device.
    void injectForTest (int type, int channel, int number, int value)
    {
        publishRaw (type, channel, number, value);
    }

    // The selftest replaces the device list and the opener, so that "the hub
    // opens the device it was asked for, by identifier, and starts it" can
    // be checked without a MIDI device.
    using DeviceList = std::function<juce::Array<juce::MidiDeviceInfo>()>;
    using Opener = std::function<std::unique_ptr<Device>(const juce::String& identifier,
                                                           juce::MidiInputCallback* callback)>;
    void setSeamsForTest (DeviceList l, Opener o)
    {
        deviceList = std::move (l);
        opener = std::move (o);
    }

    static constexpr int kTypeCc = 0, kTypeNoteOn = 1, kTypeNoteOff = 2, kTypeOther = 3;

private:
    // JUCE 8 calls this on a high-priority thread: it must do nothing but
    // publish the message.
    struct Callback : public juce::MidiInputCallback
    {
        explicit Callback (MidiHub& owner) : hub (owner) {}
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            hub.publish (m);
        }
        MidiHub& hub;
    };

    std::unique_ptr<Device> device;
    std::unique_ptr<Callback> callback;
    juce::String openName;
    juce::String openId;

    // The device list and the opener (the seams the selftest replaces).
    DeviceList deviceList = [] { return juce::MidiInput::getAvailableDevices(); };
    Opener opener = [] (const juce::String& identifier, juce::MidiInputCallback* cb)
        -> std::unique_ptr<Device>
    {
        if (auto in = juce::MidiInput::openDevice (identifier, cb))
            return std::unique_ptr<Device> (new RealDevice (std::move (in)));
        return nullptr;
    };

    juce::Array<juce::MidiDeviceInfo> currentDevices() const { return deviceList(); }
    std::unique_ptr<Device> openDevice (const juce::String& id, juce::MidiInputCallback* cb)
    {
        return opener (id, cb);
    }

    // the packed message + a counter, in one atomic (so a reader always sees
    // a consistent pair)
    std::atomic<uint64_t> slot { 0 };
    uint32_t seenCount = 0;

    Target learning = Target::none;
    Binding bindings[4] = { Binding(), Binding(), Binding(), Binding() };

    static int indexOf (Target t)
    {
        if (t == Target::masterGain) return 0;
        if (t == Target::drawIntensity) return 1;
        if (t == Target::scrub)          return 2;
        if (t == Target::transport)     return 3;
        return -1;
    }

    void publish (const juce::MidiMessage& m)
    {
        int type = kTypeOther, number = 0, value = 0;
        if (m.isController()) { type = kTypeCc; number = m.getControllerNumber(); value = m.getControllerValue(); }
        else if (m.isNoteOn())  { type = kTypeNoteOn; number = m.getNoteNumber(); value = (int) m.getVelocity(); }
        else if (m.isNoteOff()) { type = kTypeNoteOff; number = m.getNoteNumber(); value = 0; }
        publishRaw (type, m.getChannel(), number, value);
    }

    void publishRaw (int type, int channel, int number, int value)
    {
        const uint32_t msg = (uint32_t) ((type & 0x3) << 28)
                           | (uint32_t) ((channel & 0xf) << 24)
                           | (uint32_t) ((number & 0xff) << 16)
                           | (uint32_t) ((value & 0xff) << 8);
        const auto count = (uint32_t) (slot.load (std::memory_order_relaxed) >> 32) + 1;
        slot.store (((uint64_t) count << 32) | (uint64_t) msg, std::memory_order_release);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiHub)
};
