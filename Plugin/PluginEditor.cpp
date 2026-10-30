#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "MainComponent.h"

//==============================================================================
UpicuitousAudioProcessorEditor::UpicuitousAudioProcessorEditor(UpicuitousAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processor(p), main(p.getMainComponent())
{
    juce::ignoreUnused(processor);
    addAndMakeVisible(main);
    setResizable(true, true);
    setResizeLimits(640, 400, 4096, 2400);
    setSize(1280, 800);
}

UpicuitousAudioProcessorEditor::~UpicuitousAudioProcessorEditor()
{
    // detach the processor-owned UI so it survives this editor (the host may
    // destroy the editor while audio keeps running)
    removeChildComponent(&main);
}

//==============================================================================
void UpicuitousAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0b0b0e));
}

void UpicuitousAudioProcessorEditor::resized()
{
    main.setBounds(getLocalBounds());
}
