#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

// Lock-free hand-off of pre-DSP ("dry") and post-DSP ("wet") samples from the audio
// thread to the visualizer. The audio thread only memcpys; every bit of analysis
// (FFT, meters, clip counting) happens on the consumer side.
//
// Samples are stored interleaved stereo: dest[2*i] = left, dest[2*i+1] = right.
//
// When inactive, push* returns immediately and the ring stays empty, so a closed
// visualizer window costs the audio thread nothing beyond one relaxed atomic load.
//
// Single producer (audio thread) / single consumer (message thread). On overrun the
// INCOMING block is dropped: dropping the oldest instead would mean the producer
// advancing the consumer's read index, which is a data race. The audio thread never
// blocks either way.
class VisualizerTap
{
public:
    static constexpr int kCapacityFrames = 8192;

    VisualizerTap();

    // Message thread. Deactivating also drops whatever is queued, so a reopened
    // window starts on live audio rather than a stale backlog.
    void setActive(bool shouldBeActive);
    bool isActive() const noexcept { return active.load(std::memory_order_relaxed); }

    // Audio thread. No locks, no allocation. Mono or >2ch input is handled:
    // channel 0 is used for left, and channel 1 if present, else channel 0 again.
    void pushDry(const juce::AudioBuffer<float>& buffer) noexcept;
    void pushWet(const juce::AudioBuffer<float>& buffer) noexcept;

    // Consumer thread. Reads up to numFrames into dest (which must hold 2*numFrames
    // floats). Returns the number of frames actually read.
    int readDry(float* dest, int numFrames) noexcept;
    int readWet(float* dest, int numFrames) noexcept;

    int getNumDryFramesReady() const noexcept;
    int getNumWetFramesReady() const noexcept;

private:
    struct Ring
    {
        Ring();

        void push(const juce::AudioBuffer<float>& buffer) noexcept;
        int  read(float* dest, int numFrames) noexcept;
        void reset() noexcept;
        int  numReady() const noexcept;

        juce::AbstractFifo   fifo { kCapacityFrames };
        juce::HeapBlock<float> data;   // 2 * kCapacityFrames, interleaved
    };

    Ring dry, wet;
    std::atomic<bool> active { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualizerTap)
};
