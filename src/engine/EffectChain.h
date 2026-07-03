// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: an ordered list of effects that runs them in series, in place

#pragma once

#include "core/Types.h"
#include "engine/Effect.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace howl::engine {

class EffectChain {
public:
    // Appends an effect, takes ownership, off the audio thread
    void add(std::unique_ptr<Effect> effect);

    // Removes the effect at index, off the audio thread
    void removeAt(std::size_t index);

    // Removes and returns the effect at index, off the audio thread
    std::unique_ptr<Effect> takeAt(std::size_t index);

    // Inserts an effect at index, off the audio thread
    void insertAt(std::size_t index, std::unique_ptr<Effect> effect);

    // Returns how many effects are in the chain
    std::size_t size() const;

    // Returns the effect at index
    Effect& at(std::size_t index);

    // Returns the effect at index
    const Effect& at(std::size_t index) const;

    // Prepares every effect in the chain
    void prepare(double sampleRate, int maxBlockSize);

    // [RT] Runs every effect in order, in place
    void process(AudioBlock& audio) noexcept;

    // Sum of every effect's latencySamples(), for PDC
    int latencySamples() const noexcept;

private:
    std::vector<std::unique_ptr<Effect>> m_effects;
};

} // namespace howl::engine
