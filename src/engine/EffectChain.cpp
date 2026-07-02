// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: an ordered list of effects that runs them in series, in place

#include "engine/EffectChain.h"

namespace howl::engine {

// Appends an effect, takes ownership, off the audio thread
void EffectChain::add(std::unique_ptr<Effect> effect) {
    m_effects.push_back(std::move(effect));
}

// Removes the effect at index, off the audio thread
void EffectChain::removeAt(std::size_t index) {
    if (index >= m_effects.size()) {
        return;
    }

    m_effects.erase(m_effects.begin() + static_cast<std::ptrdiff_t>(index));
}

// Removes and returns the effect at index, off the audio thread
std::unique_ptr<Effect> EffectChain::takeAt(std::size_t index) {
    std::unique_ptr<Effect> taken = std::move(m_effects[index]);
    m_effects.erase(m_effects.begin() + static_cast<std::ptrdiff_t>(index));
    return taken;
}

// Inserts an effect at index, off the audio thread
void EffectChain::insertAt(std::size_t index, std::unique_ptr<Effect> effect) {
    m_effects.insert(m_effects.begin() + static_cast<std::ptrdiff_t>(index), std::move(effect));
}

// Returns how many effects are in the chain
std::size_t EffectChain::size() const {
    return m_effects.size();
}

// Returns the effect at index
Effect& EffectChain::at(std::size_t index) {
    return *m_effects[index];
}

// Prepares every effect in the chain
void EffectChain::prepare(double sampleRate, int maxBlockSize) {
    for (auto& effect : m_effects) {
        effect->prepare(sampleRate, maxBlockSize);
    }
}

// [RT] Runs every effect in order, in place
void EffectChain::process(AudioBlock& audio) noexcept {
    for (auto& effect : m_effects) {
        effect->process(audio);
    }
}

// Sum of every effect's latencySamples(), for PDC
int EffectChain::latencySamples() const noexcept {
    int total = 0;
    for (const auto& effect : m_effects) {
        total += effect->latencySamples();
    }

    return total;
}

} // namespace howl::engine
