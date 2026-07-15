#include "VisualizerTap.h"

VisualizerTap::Ring::Ring()
{
    data.calloc(2 * static_cast<size_t>(kCapacityFrames));
}

int VisualizerTap::Ring::numReady() const noexcept
{
    return fifo.getNumReady();
}

void VisualizerTap::Ring::reset() noexcept
{
    fifo.reset();
}

void VisualizerTap::Ring::push(const juce::AudioBuffer<float>& buffer) noexcept
{
    const int numFrames = buffer.getNumSamples();
    if (numFrames <= 0 || buffer.getNumChannels() <= 0)
        return;

    // Overrun: drop the incoming block rather than touching the reader's index.
    if (fifo.getFreeSpace() < numFrames)
        return;

    const float* left  = buffer.getReadPointer(0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : left;

    int start1, size1, start2, size2;
    fifo.prepareToWrite(numFrames, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
    {
        data[2 * (start1 + i)]     = left[i];
        data[2 * (start1 + i) + 1] = right[i];
    }

    for (int i = 0; i < size2; ++i)
    {
        data[2 * (start2 + i)]     = left[size1 + i];
        data[2 * (start2 + i) + 1] = right[size1 + i];
    }

    fifo.finishedWrite(size1 + size2);
}

int VisualizerTap::Ring::read(float* dest, int numFrames) noexcept
{
    const int available = juce::jmin(numFrames, fifo.getNumReady());
    if (available <= 0)
        return 0;

    int start1, size1, start2, size2;
    fifo.prepareToRead(available, start1, size1, start2, size2);

    if (size1 > 0)
        std::memcpy(dest, data + 2 * start1, sizeof(float) * 2 * static_cast<size_t>(size1));

    if (size2 > 0)
        std::memcpy(dest + 2 * size1, data + 2 * start2, sizeof(float) * 2 * static_cast<size_t>(size2));

    fifo.finishedRead(size1 + size2);
    return size1 + size2;
}

VisualizerTap::VisualizerTap() = default;

void VisualizerTap::setActive(bool shouldBeActive)
{
    if (! shouldBeActive)
    {
        active.store(false, std::memory_order_relaxed);
        // The audio thread has stopped pushing (it checks the flag first), so it is
        // safe to drop the backlog here on the message thread.
        dry.reset();
        wet.reset();
        return;
    }

    dry.reset();
    wet.reset();
    active.store(true, std::memory_order_relaxed);
}

void VisualizerTap::pushDry(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (! isActive()) return;
    dry.push(buffer);
}

void VisualizerTap::pushWet(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (! isActive()) return;
    wet.push(buffer);
}

int VisualizerTap::readDry(float* dest, int numFrames) noexcept { return dry.read(dest, numFrames); }
int VisualizerTap::readWet(float* dest, int numFrames) noexcept { return wet.read(dest, numFrames); }

int VisualizerTap::getNumDryFramesReady() const noexcept { return dry.numReady(); }
int VisualizerTap::getNumWetFramesReady() const noexcept { return wet.numReady(); }
