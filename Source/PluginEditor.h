#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class NeonLookAndFeel;
class RootView;

//==============================================================================
// Editor: one fixed-layout view (kBaseWidth x kBaseHeight) scaled to the window size.
class AtmosBridgeEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit AtmosBridgeEditor (AtmosBridgeProcessor&);
    ~AtmosBridgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AtmosBridgeProcessor& proc;
    std::unique_ptr<NeonLookAndFeel> lnf;
    std::unique_ptr<RootView> root;
    juce::TooltipWindow tooltips { this, 500 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosBridgeEditor)
};
