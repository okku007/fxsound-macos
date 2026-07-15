#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "VisualizerTap.h"

namespace {
// Fills a stereo buffer so that left = frame index, right = -(frame index).
// Makes it trivial to assert that frames come back in order and channels aren't swapped.
juce::AudioBuffer<float> makeRamp(int startFrame, int numFrames)
{
    juce::AudioBuffer<float> buf(2, numFrames);
    for (int i = 0; i < numFrames; ++i)
    {
        buf.setSample(0, i,  static_cast<float>(startFrame + i));
        buf.setSample(1, i, -static_cast<float>(startFrame + i));
    }
    return buf;
}
} // namespace

struct VisualizerTapTest : juce::UnitTest
{
    VisualizerTapTest() : juce::UnitTest("VisualizerTap") {}

    void runTest() override
    {
        beginTest("inactive tap accepts no data");
        {
            VisualizerTap tap;
            const auto block = makeRamp(0, 128);
            tap.pushDry(block);
            tap.pushWet(block);
            expectEquals(tap.getNumDryFramesReady(), 0);
            expectEquals(tap.getNumWetFramesReady(), 0);
        }

        beginTest("push/read round-trips interleaved stereo in order");
        {
            VisualizerTap tap;
            tap.setActive(true);
            tap.pushDry(makeRamp(0, 4));

            expectEquals(tap.getNumDryFramesReady(), 4);

            std::vector<float> dest(2 * 4, 0.0f);
            expectEquals(tap.readDry(dest.data(), 4), 4);

            for (int i = 0; i < 4; ++i)
            {
                expectWithinAbsoluteError(dest[2 * i],      static_cast<float>(i), 0.0001f);
                expectWithinAbsoluteError(dest[2 * i + 1], -static_cast<float>(i), 0.0001f);
            }

            // Fully drained.
            expectEquals(tap.getNumDryFramesReady(), 0);
        }

        beginTest("dry and wet rings are independent");
        {
            VisualizerTap tap;
            tap.setActive(true);
            tap.pushDry(makeRamp(0, 16));
            expectEquals(tap.getNumDryFramesReady(), 16);
            expectEquals(tap.getNumWetFramesReady(), 0);

            tap.pushWet(makeRamp(100, 8));
            expectEquals(tap.getNumWetFramesReady(), 8);

            std::vector<float> dest(2 * 8, 0.0f);
            expectEquals(tap.readWet(dest.data(), 8), 8);
            expectWithinAbsoluteError(dest[0], 100.0f, 0.0001f);
            // Reading wet must not disturb dry.
            expectEquals(tap.getNumDryFramesReady(), 16);
        }

        beginTest("ring wraps around across many pushes");
        {
            VisualizerTap tap;
            tap.setActive(true);

            // Push and fully drain 3x the ring capacity, forcing wraparound.
            const int blockFrames = 512;
            const int numBlocks   = (3 * VisualizerTap::kCapacityFrames) / blockFrames;
            std::vector<float> dest(2 * blockFrames, 0.0f);

            for (int b = 0; b < numBlocks; ++b)
            {
                tap.pushDry(makeRamp(b * blockFrames, blockFrames));
                expectEquals(tap.readDry(dest.data(), blockFrames), blockFrames);
                // First frame of block b must be b*blockFrames.
                expectWithinAbsoluteError(dest[0], static_cast<float>(b * blockFrames), 0.0001f);
            }
        }

        beginTest("overrun drops the incoming block instead of blocking");
        {
            VisualizerTap tap;
            tap.setActive(true);

            // Fill the ring exactly, without reading.
            const int blockFrames = 1024;
            const int blocksToFill = VisualizerTap::kCapacityFrames / blockFrames;
            for (int b = 0; b < blocksToFill; ++b)
                tap.pushDry(makeRamp(b * blockFrames, blockFrames));

            const int readyWhenFull = tap.getNumDryFramesReady();
            expect(readyWhenFull > 0);

            // One more push must not block, must not grow the ring, and must not
            // corrupt what is already queued.
            tap.pushDry(makeRamp(999999, blockFrames));
            expectEquals(tap.getNumDryFramesReady(), readyWhenFull);

            // The oldest data is still intact: first frame is still frame 0.
            std::vector<float> dest(2 * blockFrames, 0.0f);
            expectEquals(tap.readDry(dest.data(), blockFrames), blockFrames);
            expectWithinAbsoluteError(dest[0], 0.0f, 0.0001f);
        }

        beginTest("reading more than is available returns only what is there");
        {
            VisualizerTap tap;
            tap.setActive(true);
            tap.pushWet(makeRamp(0, 10));

            std::vector<float> dest(2 * 64, 0.0f);
            expectEquals(tap.readWet(dest.data(), 64), 10);
        }

        beginTest("deactivating clears queued data");
        {
            VisualizerTap tap;
            tap.setActive(true);
            tap.pushDry(makeRamp(0, 64));
            expectEquals(tap.getNumDryFramesReady(), 64);

            tap.setActive(false);
            expectEquals(tap.getNumDryFramesReady(), 0);

            // Reactivating starts clean.
            tap.setActive(true);
            expectEquals(tap.getNumDryFramesReady(), 0);
        }
    }
};

static VisualizerTapTest visualizerTapTest;
