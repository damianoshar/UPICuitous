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

    // v1.33.4 (H10): the lifetime-safe state load.
    //
    // A host can call setStateInformation from any thread, and it can destroy
    // the processor before the message that carries the load has been
    // delivered. The load therefore travels with a handle that is nulled when
    // the component dies - never with a raw pointer.
    //
    // `handle` is a juce::Component::SafePointer<MainComponent>. The template
    // keeps this header independent of MainComponent's definition, and both
    // the direct load and the marshalled one go through the same code.
    template <typename SafeHandle>
    static void applyState (const SafeHandle& handle, const juce::var& v)
    {
        if (handle == nullptr)
        {
            juce::Logger::writeToLog ("UPICuitous: the host state arrived after the plugin was closed - ignored");
            return;
        }

        try
        {
            // v1.33.4 (M25): a state that is not a piece is refused, and the
            // current state is kept (the host gets a log line, not a wiped
            // plugin).
            if (! handle->loadFromVar (v))
                juce::Logger::writeToLog ("UPICuitous: the host state was not a piece - the current state was kept");
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog (juce::String ("UPICuitous: the host state was rejected - ") + e.what());
        }
        catch (...)
        {
            juce::Logger::writeToLog ("UPICuitous: the host state was rejected - ignored");
        }
    }

    // The processor owns the one UI/engine instance; the editor displays it.
    MainComponent& getMainComponent() noexcept { return *main; }

private:
    std::unique_ptr<MainComponent> main;

    // v1.33.4 (H10): the component publishes the host latency from its status
    // timer. If the host destroys the processor on another thread, that timer
    // can fire while the processor is being torn down - the flag stops the
    // callback from touching a dying object.
    std::atomic<bool> alive { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpicuitousAudioProcessor)
};
