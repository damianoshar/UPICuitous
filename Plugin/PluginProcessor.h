#pragma once
#include <JuceHeader.h>

class MainComponent;

//==============================================================================
// VST3 wrapper (v1.11). The whole app UI + engine lives in MainComponent,
// constructed in plugin mode: it never opens an audio device, the host's
// processBlock drives prepareToPlay / getNextAudioBlock, and the Live-tab
// input tap reads the host's input bus instead of the device callback buffer.
// Piece state is the same JSON text as a saved .upic.json file.
//==============================================================================
class UpicuitousAudioProcessor : public juce::AudioProcessor
{
public:
    UpicuitousAudioProcessor();
    ~UpicuitousAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "UPICuitous"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // v1.16: the host's delay compensation. JUCE notifies the host itself
    // when setLatencySamples() changes the value, so the component only has
    // to tell us what the current page needs.
    void setHostLatency(int samples) { setLatencySamples(juce::jmax(0, samples)); }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // The processor owns the one UI/engine instance; the editor displays it.
    MainComponent& getMainComponent() noexcept { return *main; }

private:
    std::unique_ptr<MainComponent> main;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpicuitousAudioProcessor)
};
