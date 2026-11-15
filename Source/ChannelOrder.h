#pragma once
// Host (VST3) channel order for a JUCE bus layout.
//
// For named layouts JUCE reorders the host's VST3 channels into JUCE's own channel order. The user
// thinks in host channel numbers ("host channel 5"), so we map host index -> JUCE buffer index
// using the same VST3 orders JUCE uses (see juce_VST3Common.h, detail::layoutTable).

#include <JuceHeader.h>
#include <vector>

namespace ab
{
inline std::vector<juce::AudioChannelSet::ChannelType> hostChannelOrder (const juce::AudioChannelSet& set)
{
    using X = juce::AudioChannelSet;
    using V = std::vector<X::ChannelType>;
    static const std::vector<V> table {
        { X::centre },
        { X::left, X::right },
        { X::left, X::right, X::centre },
        { X::left, X::right, X::centre, X::LFE, X::leftSurround, X::rightSurround },                                    // 5.1
        { X::left, X::right, X::centre, X::LFE, X::leftSurroundRear, X::rightSurroundRear, X::leftSurroundSide, X::rightSurroundSide }, // 7.1
        { X::left, X::right, X::centre, X::LFE, X::leftSurround, X::rightSurround, X::topFrontLeft, X::topFrontRight, X::topRearLeft, X::topRearRight }, // 5.1.4
        { X::left, X::right, X::centre, X::LFE, X::leftSurroundRear, X::rightSurroundRear, X::leftSurroundSide, X::rightSurroundSide, X::topSideLeft, X::topSideRight }, // 7.1.2
        { X::left, X::right, X::centre, X::LFE, X::leftSurroundRear, X::rightSurroundRear, X::leftSurroundSide, X::rightSurroundSide,
          X::topFrontLeft, X::topFrontRight, X::topRearLeft, X::topRearRight },                                                    // 7.1.4
        { X::left, X::right, X::centre, X::LFE, X::leftSurroundRear, X::rightSurroundRear, X::leftSurroundSide, X::rightSurroundSide,
          X::topFrontLeft, X::topFrontRight, X::topRearLeft, X::topRearRight, X::topSideLeft, X::topSideRight },                  // 7.1.6
        { X::left, X::right, X::centre, X::LFE, X::leftSurroundRear, X::rightSurroundRear, X::leftSurroundSide, X::rightSurroundSide,
          X::topFrontLeft, X::topFrontRight, X::topRearLeft, X::topRearRight, X::wideLeft, X::wideRight },                        // 9.1.4
        { X::left, X::right, X::centre, X::LFE, X::leftSurroundRear, X::rightSurroundRear, X::leftSurroundSide, X::rightSurroundSide,
          X::topFrontLeft, X::topFrontRight, X::topRearLeft, X::topRearRight, X::topSideLeft, X::topSideRight, X::wideLeft, X::wideRight }, // 9.1.6
    };

    for (const auto& order : table)
    {
        if ((int) order.size() != set.size()) continue;
        juce::Array<X::ChannelType> arr;
        for (auto t : order) arr.add (t);
        if (X::channelSetWithChannels (arr) == set)
            return order;
    }

    const auto types = set.getChannelTypes(); // discrete / other layouts: JUCE order == host order
    return V (types.begin(), types.end());
}
} // namespace ab
