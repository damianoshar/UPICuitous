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
// Threading: JUCE calls the MidiInput callback on its own thread, and in a
// plugin the host's MIDI arrives on the audio thread - so there can be two
// producers. Neither of them may block, allocate or touch the app: they
// publish into a bounded, lock-free queue and into a "latest value" slot,
// and the message thread (the 30 Hz timer) picks the messages up. No locks,
// no message-queue flooding.
//
// v1.33.4 (M20): the hub used to keep only the NEWEST message, so a
// note-on and its note-off could vanish before the poll, and the channel
// was packed with "& 0xf" and then shown +1 - MIDI channel 1 read as 2 and
// channel 16 as 1. Now every message is queued (a bounded ring, no locks)
// and the app uses ONE channel convention: the channel INDEX, 0..15 (MIDI
// channel 1 is 0). The change from JUCE's 1..16 happens once, at the
// boundary.
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
        int channel = 0;   // the channel INDEX: 0 = MIDI channel 1 ... 15 = channel 16
        int number = 0;    // the CC number or the note number
        int value = 0;
    };

    struct Binding
    {
        bool bound = false;
        bool note = false;   // a note (on) instead of a CC
        int number = -1;      // the CC or the note number
        int channel = -1;     // -1 = any channel, otherwise the channel index (0..15)
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
    //
    // Every message that arrives is queued, so a note-on and its note-off
    // both get through - they used to collapse into one "newest message"
    // and a pair could vanish before the poll.
    //
    // The messages are handed over in the order they arrived. Each one
    // carries a ticket - the counter that says when it arrived - so the
    // order survives two producers publishing at the same time.
    //
    // A continuous control (a CC) is also kept in a "latest value" slot:
    // when the queue is full the newest CC value still gets through, and
    // that is the value a bound parameter needs.
    void pollActivities (const std::function<void (const Activity&)>& handler)
    {
        uint64_t pending[MessageQueue::kCapacity + 2];
        size_t n = 0;

        uint64_t entry;
        while (n < MessageQueue::kCapacity && queue.pop (entry))
            pending[n++] = entry;

        const auto cc = ccSlot.load (std::memory_order_acquire);
        if (ticketOf (cc) != 0)
            pending[n++] = cc;

        // the order they arrived in (the queue is already nearly sorted)
        for (size_t i = 1; i < n; ++i)
        {
            const auto item = pending[i];
            size_t j = i;
            while (j > 0 && isOlder (ticketOf (item), ticketOf (pending[j - 1])))
            {
                pending[j] = pending[j - 1];
                --j;
            }
            pending[j] = item;
        }

        for (size_t i = 0; i < n; ++i)
        {
            const auto ticket = ticketOf (pending[i]);
            if (! isNewer (ticket, lastDelivered))
                continue;   // already delivered (the queue and the slot hold the same message)
            lastDelivered = ticket;
            handler (unpackMessage ((uint32_t) pending[i]));
        }
    }

    // The newest message since the last poll (empty when nothing arrived).
    // The app uses pollActivities; this is the simple form the tests use.
    Activity pollActivity()
    {
        Activity newest;
        pollActivities ([&newest] (const Activity& a) { newest = a; });
        return newest;
    }

    // The number of messages that arrived (the monitor's "N messages").
    uint32_t messageCount() const
    {
        return ticketCounter.load (std::memory_order_relaxed) - 1;
    }

    // The number of messages the queue had to drop (it was full).
    uint32_t droppedCount() const { return dropped.load (std::memory_order_relaxed); }

    //--- the learn ------------------------------------------------------
    void startLearn (Target t)
    {
        learning = t;
        // a message that is already waiting must not bind the learn - the
        // user wants the control they move AFTER clicking Learn
        clearPending();
    }

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
    // Injects a message as if it had come from the device. The channel is
    // the app's channel index (0..15) - the same convention the binding
    // and the monitor use.
    void injectForTest (int type, int channel, int number, int value)
    {
        publishRaw (type, channel, number, value);
    }

    // Injects a JUCE message the way the device and the plugin host do, so
    // that the channel conversion is the one the app really uses.
    void injectMessage (const juce::MidiMessage& m)
    {
        publish (m);
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
    //--- the message queue ----------------------------------------------
    //
    // A bounded, lock-free queue: several producers (the MIDI thread and,
    // in a plugin, the audio thread) and one consumer (the message thread).
    // Every slot carries a sequence number, so a producer only publishes
    // into a slot the consumer has already released.
    //
    // The ring never allocates and never blocks: when it is full the
    // message is dropped and counted, and the app says so.
    class MessageQueue
    {
    public:
        static constexpr uint32_t kCapacity = 256;   // a power of two

        MessageQueue()
        {
            // every slot starts with its own number: a producer may only
            // publish into the slot whose sequence says "your turn"
            for (uint32_t i = 0; i < kCapacity; ++i)
                slots[i].sequence.store (i, std::memory_order_relaxed);
        }

        bool push (uint64_t entry)
        {
            uint64_t pos = writePos.load (std::memory_order_relaxed);

            for (;;)
            {
                auto& s = slots[pos & (kCapacity - 1)];
                const auto seq = s.sequence.load (std::memory_order_acquire);
                const auto diff = (int64_t) seq - (int64_t) pos;

                if (diff == 0)
                {
                    if (writePos.compare_exchange_weak (pos, pos + 1,
                                                       std::memory_order_relaxed,
                                                       std::memory_order_relaxed))
                    {
                        s.value = entry;
                        s.sequence.store (pos + 1, std::memory_order_release);
                        return true;
                    }
                    // the failed exchange has updated pos - try again
                }
                else if (diff < 0)
                {
                    return false;   // the ring is full
                }
                else
                {
                    pos = writePos.load (std::memory_order_relaxed);
                }
            }
        }

        bool pop (uint64_t& entry)
        {
            uint64_t pos = readPos.load (std::memory_order_relaxed);

            for (;;)
            {
                auto& s = slots[pos & (kCapacity - 1)];
                const auto seq = s.sequence.load (std::memory_order_acquire);
                const auto diff = (int64_t) seq - (int64_t) (pos + 1);

                if (diff == 0)
                {
                    if (readPos.compare_exchange_weak (pos, pos + 1,
                                                      std::memory_order_relaxed,
                                                      std::memory_order_relaxed))
                    {
                        entry = s.value;
                        s.sequence.store (pos + kCapacity, std::memory_order_release);
                        return true;
                    }
                }
                else if (diff < 0)
                {
                    return false;   // nothing waiting
                }
                else
                {
                    pos = readPos.load (std::memory_order_relaxed);
                }
            }
        }

        void clear()
        {
            uint64_t entry;
            while (pop (entry)) {}
        }

    private:
        struct Slot
        {
            std::atomic<uint64_t> sequence { 0 };
            uint64_t value = 0;
        };

        Slot slots[kCapacity];
        std::atomic<uint64_t> writePos { 0 };
        std::atomic<uint64_t> readPos { 0 };
    };

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

    // The messages, the "latest value" slot and the tickets.
    MessageQueue queue;
    std::atomic<uint64_t> ccSlot { 0 };        // the newest CC: ticket + message
    std::atomic<uint32_t> ticketCounter { 1 }; // the next ticket to hand out
    std::atomic<uint32_t> dropped { 0 };       // the messages the queue had to drop
    uint32_t lastDelivered = 0;                // the message thread only

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

    // The packed message: the type (2 bits), the channel index (4), the
    // number (8) and the value (8).
    static uint32_t packMessage (int type, int channel, int number, int value)
    {
        return ((uint32_t) (type & 0x3) << 28)
             | ((uint32_t) (channel & 0xf) << 24)
             | ((uint32_t) (number & 0xff) << 16)
             | ((uint32_t) (value & 0xff) << 8);
    }

    static Activity unpackMessage (uint32_t msg)
    {
        Activity a;
        a.valid = true;
        a.type      = (int) ((msg >> 28) & 0x3);
        a.channel   = (int) ((msg >> 24) & 0xf);
        a.number    = (int) ((msg >> 16) & 0xff);
        a.value     = (int) ((msg >> 8)  & 0xff);
        return a;
    }

    static uint32_t ticketOf (uint64_t entry) { return (uint32_t) (entry >> 32); }

    // The tickets count up and wrap around, so they are compared as signed
    // differences (the usual serial-number arithmetic).
    static bool isNewer (uint32_t a, uint32_t b) { return (int32_t) (a - b) > 0; }
    static bool isOlder (uint32_t a, uint32_t b) { return (int32_t) (a - b) < 0; }

    void publish (const juce::MidiMessage& m)
    {
        int type = kTypeOther, number = 0, value = 0;
        if (m.isController()) { type = kTypeCc; number = m.getControllerNumber(); value = m.getControllerValue(); }
        else if (m.isNoteOn())  { type = kTypeNoteOn; number = m.getNoteNumber(); value = (int) m.getVelocity(); }
        else if (m.isNoteOff()) { type = kTypeNoteOff; number = m.getNoteNumber(); value = 0; }
        // JUCE's channels are 1..16; the app uses the channel index (0..15).
        // This is the one place where the two conventions meet.
        publishRaw (type, m.getChannel() - 1, number, value);
    }

    void publishRaw (int type, int channel, int number, int value)
    {
        uint32_t ticket = ticketCounter.fetch_add (1, std::memory_order_relaxed);
        if (ticket == 0)
            ticket = ticketCounter.fetch_add (1, std::memory_order_relaxed);   // 0 means "none"

        const auto msg = packMessage (type, channel, number, value);
        const uint64_t entry = ((uint64_t) ticket << 32) | (uint64_t) msg;

        if (! queue.push (entry))
        {
            dropped.fetch_add (1, std::memory_order_relaxed);
        }

        // a continuous control keeps its newest value even when the queue is
        // full - that is the value a bound parameter needs
        if (type == kTypeCc)
        {
            auto old = ccSlot.load (std::memory_order_relaxed);
            for (;;)
            {
                if (! isNewer (ticket, ticketOf (old)))
                    return;   // a newer CC is already in the slot
                if (ccSlot.compare_exchange_weak (old, entry,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed))
                    return;
            }
        }
    }

    // Throws away everything that is waiting - the learn starts here, and
    // only a message that arrives after it may bind.
    void clearPending()
    {
        queue.clear();
        ccSlot.store (0, std::memory_order_release);
        lastDelivered = ticketCounter.load (std::memory_order_relaxed) - 1;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiHub)
};
