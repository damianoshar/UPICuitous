#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MainComponent.h"

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
    main->onLatencyChanged = [this](int samples) { setHostLatency(samples); };
}

UpicuitousAudioProcessor::~UpicuitousAudioProcessor() = default;

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
    // capture, mode-2 arcs) and the render run unchanged; only the input channel
    // count has to come from the host's bus layout.
    main->setPluginInputChannelCount(juce::jmin(2, getTotalNumInputChannels()));

    // v1.25 (wave 22): feed host MIDI into the same handler the stand-alone
    // app uses. Parameter binding (master gain / intensity) follows the latest
    // message; scrub CCs drive the signed playhead.
    for (const auto m : midiMessages)
        main->injectMidiMessage(m.getMessage());

    // limit the view to the main output bus: any extra input-only channels must
    // not be written to
    auto outBus = getBusBuffer(buffer, false, 0);
    const int n = juce::jmin(buffer.getNumSamples(), outBus.getNumSamples());
    if (n <= 0 || outBus.getNumChannels() <= 0)
        return;

    juce::AudioSourceChannelInfo info(&outBus, 0, n);
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
    const auto text = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    const auto v = juce::JSON::parse(text);
    if (! v.isVoid())
        main->loadFromVar(v);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UpicuitousAudioProcessor();
}
