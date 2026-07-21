// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: MIDI file import maps note events to a clip at the project tick resolution

#include "model/MidiFileImport.h"

#include "model/MidiClip.h"
#include "model/Note.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <catch2/catch_test_macros.hpp>

using howl::model::importMidiFileAsClip;
using howl::model::kTicksPerQuarter;

namespace {

// Writes a two note MIDI file at the given PPQ to a temp file and returns it
juce::File writeTwoNoteMidiFile(int ticksPerQuarter) {
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0.0);
    track.addEvent(juce::MidiMessage::noteOff(1, 60), static_cast<double>(ticksPerQuarter));
    track.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(80)),
        static_cast<double>(ticksPerQuarter * 2));
    track.addEvent(juce::MidiMessage::noteOff(1, 64), static_cast<double>(ticksPerQuarter * 3));
    track.updateMatchedPairs();

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(ticksPerQuarter);
    midiFile.addTrack(track);

    const juce::File temp = juce::File::createTempFile(".mid");
    juce::FileOutputStream out(temp);
    midiFile.writeTo(out);
    out.flush();
    return temp;
}

} // namespace

TEST_CASE("importMidiFileAsClip rescales note events to kTicksPerQuarter", "[model]") {
    // 480 PPQ against the project's 960 doubles every tick value
    const juce::File file = writeTwoNoteMidiFile(480);
    const auto clip = importMidiFileAsClip(file);
    file.deleteFile();

    REQUIRE(clip.has_value());
    REQUIRE(clip->notes().size() == 2);

    REQUIRE(clip->notes()[0].startTick == 0);
    REQUIRE(clip->notes()[0].lengthTicks == kTicksPerQuarter);
    REQUIRE(clip->notes()[1].startTick == kTicksPerQuarter * 2);
    REQUIRE(clip->notes()[1].lengthTicks == kTicksPerQuarter);

    // Last note ends at 3 quarters, rounded up to a whole 4 quarter bar
    REQUIRE(clip->lengthTicks() == kTicksPerQuarter * 4);
}

TEST_CASE("importMidiFileAsClip returns nullopt for a file that does not exist", "[model]") {
    const auto clip = importMidiFileAsClip(juce::File("/no/such/file.mid"));
    REQUIRE_FALSE(clip.has_value());
}
