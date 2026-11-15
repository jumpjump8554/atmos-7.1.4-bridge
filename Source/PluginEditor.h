#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class NeonLookAndFeel;
class RootView;
struct UiCtx;

//==============================================================================
// Editor: one fixed-layout view (kBaseWidth x kBaseHeight) scaled to the window size.
// Three theme presets (Cyberpunk / White Modern / Cool Beaches) are drawn by the same components;
// the active theme comes from the processor (which also remembers it for the whole PC user).
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
    void applyTheme();

    AtmosBridgeProcessor& proc;
    std::unique_ptr<UiCtx> ctx;
    std::unique_ptr<NeonLookAndFeel> lnf;
    std::unique_ptr<RootView> root;
    juce::TooltipWindow tooltips { this, 500 };
    ab::ThemeId shownTheme = ab::ThemeId::Cyberpunk;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtmosBridgeEditor)
};
