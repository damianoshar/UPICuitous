#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MainComponent.h"

//==============================================================================
// v1.33.4 (M18): the VST3 wrapper only declares a MIDI input bus when the
// build asked for one (NEEDS_MIDI_INPUT in Plugin/CMakeLists.txt). The
// processor consumes the host's MIDI and says acceptsMidi() == true, so the
// two have to agree - otherwise the host never sends a note and the MIDI
// scrub transport (UPIC-28) can never be reached.
#if defined(JucePlugin_Build_VST3) && JucePlugin_Build_VST3
 #if ! defined(JucePlugin_WantsMidiInput) || ! JucePlugin_WantsMidiInput
  #error "the VST3 declares no MIDI input bus but the processor consumes MIDI: set NEEDS_MIDI_INPUT TRUE in Plugin/CMakeLists.txt"
 #endif
#endif

//==============================================================================
UpicuitousAudioProcessor::UpicuitousAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // plugin mode: the host supplies the audio - no device is opened, and the
    // Live tab reads the input bus (see MainComponent::setPluginInputChannels)
    main = std::make_unique<MainComponent>(true);

    // v1.16: the component knows what latency the current page needs (the
    // live path is delayed, the score paths are not) - it publishes it from
    // the message thread, and JUCE tells the host when the value changes.
    // v1.33.4 (H10): the callback checks the alive flag, so a timer tick
    // that lands while the host is tearing the processor down cannot call
    // into a dying object.
    main->onLatencyChanged = [this](int samples)
    {
        if (alive.load (std::memory_order_relaxed))
            setHostLatency (samples);
    };
}

UpicuitousAudioProcessor::~UpicuitousAudioProcessor()
{
    alive.store (false, std::memory_order_relaxed);
}

//==============================================================================
void UpicuitousAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    main->prepareToPlay(samplesPerBlock, sampleRate);
    main->setPluginInputChannelCount(juce::jmin(2, getTotalNumInputChannels()));
}

void UpicuitousAudioProcessor::releaseResources()
{
    main->releaseResources();
}

bool UpicuitousAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    const auto& in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::disabled()
        || in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

void UpicuitousAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // The host buffer follows the in-place convention: channel k carries input k
    // on entry and takes the render on exit - exactly like the device callback
    // buffer of the stand-alone app. So the component's input tap (monitor,
    // capture, mode-2 arcs) and the render run unchanged; only the channel
    // counts have to come from the host's bus layout.

    // v1.25 (wave 22): feed host MIDI into the same handler the stand-alone
    // app uses. Parameter binding (master gain / intensity) follows the latest
    // message; scrub CCs drive the signed playhead.
    for (const auto m : midiMessages)
        main->injectMidiMessage(m.getMessage());

    // v1.33.4 (M24): the component gets the host buffer's MAIN BUS channels -
    // the input channels AND the output channels. The old code passed only
    // the output bus's view, so in a stereo-in / mono-out layout the output
    // bus was one channel wide and the input tap never saw the right input
    // channel (the live arcs and the capture heard the left channel only).
    auto inBus  = getBusBuffer (buffer, true, 0);
    auto outBus = getBusBuffer (buffer, false, 0);
    const int numChannels = juce::jmax (inBus.getNumChannels(), outBus.getNumChannels());
    const int n = juce::jmin (buffer.getNumSamples(), outBus.getNumSamples());
    if (n <= 0 || numChannels <= 0)
        return;

    // the input count comes from the host's main input bus (the tap reads at
    // most two channels) ...
    main->setPluginInputChannelCount (juce::jmin (2, inBus.getNumChannels()));
    // ... and the output count tells the component which channels it may
    // clear and write (the others may be read-only host memory).
    main->setPluginOutputChannelCount (outBus.getNumChannels());

    // a view over the first numChannels channels of the host buffer: JUCE's
    // convention is that the input channels come first and the output
    // channels overlap them.
    std::vector<float*> chans;
    chans.reserve ((size_t) numChannels);
    for (int c = 0; c < numChannels; ++c)
        chans.push_back (buffer.getWritePointer (c));
    juce::AudioBuffer<float> view (chans.data(), numChannels, buffer.getNumSamples());

    juce::AudioSourceChannelInfo info(&view, 0, n);
    main->getNextAudioBlock(info);
}

//==============================================================================
juce::AudioProcessorEditor* UpicuitousAudioProcessor::createEditor()
{
    return new UpicuitousAudioProcessorEditor(*this);
}

void UpicuitousAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto text = main->pieceStateText();
    destData.replaceAll(text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void UpicuitousAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // SECURITY (S-03): the host state blob is untrusted input. A null
    // pointer, a negative size or a blob far bigger than any real piece is
    // rejected before it is parsed.
    if (data == nullptr || sizeInBytes <= 0)
        return;
    if ((size_t) sizeInBytes > upic::limits::kMaxPieceBytes)
    {
        juce::Logger::writeToLog ("UPICuitous: the host state is too large - ignored");
        return;
    }

    // The piece is parsed here, on the thread the host called us on, so a
    // malformed blob cannot take the plugin down with a bad_alloc or a
    // JSON parse error. The component itself clamps every count it reads.
    juce::var v;
    try
    {
        const auto text = juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes);
        v = juce::JSON::parse (text);
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog (juce::String ("UPICuitous: the host state could not be parsed - ")
                                  + e.what());
        return;
    }
    catch (...)
    {
        juce::Logger::writeToLog ("UPICuitous: the host state could not be parsed - ignored");
        return;
    }

    if (v.isVoid())
        return;

    // loadFromVar() touches the UI components, so it belongs on the message
    // thread. JUCE calls setStateInformation there already; if some host
    // calls it from elsewhere, the load is marshalled instead of touching
    // the UI from a foreign thread.
    //
    // v1.33.4 (H10): the marshalled load used to capture a raw pointer to
    // the component. If the host destroyed the processor before the message
    // was delivered, the callback dereferenced freed memory. The handle is
    // created while the component is alive and is nulled when it dies, so a
    // late state blob is skipped instead of crashing.
    juce::Component::SafePointer<MainComponent> safe (main.get());

    if (juce::MessageManager::getInstance() != nullptr
        && ! juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        juce::MessageManager::getInstance()->callAsync ([safe, v]
        {
            applyState (safe, v);
        });
        return;
    }

    applyState (safe, v);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UpicuitousAudioProcessor();
}
