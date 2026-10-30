#pragma once
#include <JuceHeader.h>

class UpicuitousAudioProcessor;
class MainComponent;

//==============================================================================
// Hosts the processor-owned MainComponent (v1.11). The app layout is fully
// responsive, so the editor is resizable (640x400 up to 4096x2400, the sizes
// verified for the stand-alone app).
//==============================================================================
class UpicuitousAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit UpicuitousAudioProcessorEditor(UpicuitousAudioProcessor&);
    ~UpicuitousAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    UpicuitousAudioProcessor& processor;
    MainComponent& main;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpicuitousAudioProcessorEditor)
};
