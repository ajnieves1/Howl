// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows and edits a MidiClip resolved from a ClipAddress, draws the transport playhead

#include "ui/PianoRoll.h"

#include <cmath>
#include <memory>
#include <vector>

namespace howl::ui {

// Stores references, the clip address, and the snap division provider, starts the playhead timer
PianoRoll::PianoRoll(model::Arrangement& arrangement, model::Session& session, model::ClipAddress address,
                      model::CommandStack& commandStack, engine::Transport& transport, double sampleRate,
                      std::function<model::SnapDivision()> snapProvider)
    : m_arrangement(arrangement)
    , m_session(session)
    , m_address(address)
    , m_commandStack(commandStack)
    , m_transport(transport)
    , m_sampleRate(sampleRate)
    , m_snapProvider(std::move(snapProvider))
{
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

// Stops the playhead timer
PianoRoll::~PianoRoll() {
    stopTimer();
}

// Repaints so the playhead position stays current during playback
void PianoRoll::timerCallback() {
    repaint();
}

// Resolves the addressed clip fresh, nullptr when it no longer resolves. patterns is always
// nullptr, a Pattern-sourced address never resolves until a later phase introduces PatternBank
model::MidiClip* PianoRoll::resolveClip() const {
    return model::resolveClip(m_arrangement, m_session, nullptr, m_address);
}

// Height of the key grid, the component's height less the velocity lane
float PianoRoll::keyGridHeight() const {
    return static_cast<float>(getHeight() - kVelocityLaneHeight);
}

// Returns the current global snap division, Step when no provider is set
model::SnapDivision PianoRoll::snapDivision() const {
    return m_snapProvider ? m_snapProvider() : model::SnapDivision::Step;
}

// Ticks spanned by the visible grid, at least 4 beats even for an empty or unresolved clip
int64_t PianoRoll::visibleTickSpan(int64_t clipLengthTicks) const {
    const int64_t minimumSpan = kDefaultNoteLengthTicks * 4;
    return juce::jmax(clipLengthTicks, minimumSpan);
}

// Converts a pixel x position to a tick, clamped to the visible span, unsnapped
int64_t PianoRoll::xToTick(int x, int64_t clipLengthTicks) const {
    const int64_t span = visibleTickSpan(clipLengthTicks);
    const float ratio = juce::jlimit(0.0f, 1.0f, static_cast<float>(x) / static_cast<float>(getWidth()));
    return static_cast<int64_t>(ratio * static_cast<float>(span));
}

// Converts a pixel y position to a MIDI key, clamped to the visible range
int PianoRoll::yToKey(int y) const {
    const float rowHeight = keyGridHeight() / static_cast<float>(kNumKeys);
    const int rowIndex = juce::jlimit(0, kNumKeys - 1, static_cast<int>(static_cast<float>(y) / rowHeight));
    return kHighestKey - rowIndex;
}

// Converts a tick to a pixel x position
float PianoRoll::tickToX(int64_t tick, int64_t clipLengthTicks) const {
    const int64_t span = visibleTickSpan(clipLengthTicks);
    return static_cast<float>(tick) / static_cast<float>(span) * static_cast<float>(getWidth());
}

// Converts a MIDI key to the y position of the top of its row
float PianoRoll::keyToY(int key) const {
    const float rowHeight = keyGridHeight() / static_cast<float>(kNumKeys);
    const int rowIndex = kHighestKey - key;
    return static_cast<float>(rowIndex) * rowHeight;
}

// Returns the index of the note at (tick, key), or -1 if none, hit-testing is unsnapped
int PianoRoll::hitTestNote(const model::MidiClip& clip, int64_t tick, int key) const {
    const auto& notes = clip.notes();
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const model::Note& note = notes[i];
        if (note.key == key && tick >= note.startTick && tick < note.startTick + note.lengthTicks) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Returns the index of the note whose velocity bar is nearest x within kVelocityHitPixels,
// or -1 if none; notes sharing a start tick (bit-exact integer ticks, no float compare
// needed) break the tie toward the higher key
int PianoRoll::hitTestVelocityBar(const model::MidiClip& clip, int x) const {
    const auto& notes = clip.notes();
    const int64_t clipLength = clip.lengthTicks();

    int bestIndex = -1;
    float bestDistance = 0.0f;
    int64_t bestStartTick = 0;

    for (std::size_t i = 0; i < notes.size(); ++i) {
        const float barX = tickToX(notes[i].startTick, clipLength);
        const float distance = std::abs(static_cast<float>(x) - barX);
        if (distance > static_cast<float>(kVelocityHitPixels)) {
            continue;
        }

        if (bestIndex < 0 || distance < bestDistance) {
            bestIndex = static_cast<int>(i);
            bestDistance = distance;
            bestStartTick = notes[i].startTick;
        } else if (notes[i].startTick == bestStartTick
                   && notes[i].key > notes[static_cast<std::size_t>(bestIndex)].key) {
            bestIndex = static_cast<int>(i);
        }
    }

    return bestIndex;
}

// Returns the current transport position converted to ticks
double PianoRoll::playheadTick() const {
    const double tempo = m_transport.tempo();
    const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(model::kTicksPerQuarter);
    return static_cast<double>(m_transport.position()) / samplesPerTick;
}

// Draws the key grid, notes, playhead, and the velocity lane, an empty grid and lane
// when the address does not resolve
void PianoRoll::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    const float gridHeight = keyGridHeight();
    const float rowHeight = gridHeight / static_cast<float>(kNumKeys);

    // Shade black-key rows
    g.setColour(juce::Colours::black.brighter(0.1f));
    for (int key = kLowestKey; key <= kHighestKey; ++key) {
        const int pitchClass = key % 12;
        const bool isBlackKey = pitchClass == 1 || pitchClass == 3 || pitchClass == 6
                              || pitchClass == 8 || pitchClass == 10;
        if (isBlackKey) {
            g.fillRect(0.0f, keyToY(key), static_cast<float>(getWidth()), rowHeight);
        }
    }

    model::MidiClip* clip = resolveClip();
    const int64_t clipLength = clip != nullptr ? clip->lengthTicks() : 0;

    // Beat grid lines, spanning the key grid and the velocity lane so the columns line up
    const int64_t span = visibleTickSpan(clipLength);
    const int numBeats = static_cast<int>(span / model::kTicksPerQuarter);
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    for (int beat = 0; beat <= numBeats; ++beat) {
        const float x = tickToX(static_cast<int64_t>(beat) * model::kTicksPerQuarter, clipLength);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(getHeight()));
    }

    // Notes, fill alpha scales with velocity so loud notes read solid
    if (clip != nullptr) {
        for (const model::Note& note : clip->notes()) {
            if (note.key < kLowestKey || note.key > kHighestKey) {
                continue;
            }
            const float x = tickToX(note.startTick, clipLength);
            const float width = tickToX(note.startTick + note.lengthTicks, clipLength) - x;
            g.setColour(juce::Colours::orange.withAlpha(0.4f + 0.6f * note.velocity));
            g.fillRect(x, keyToY(note.key), juce::jmax(2.0f, width), rowHeight);
        }
    }

    // Playhead
    g.setColour(juce::Colours::white);
    const float playheadX = tickToX(static_cast<int64_t>(playheadTick()), clipLength);
    g.drawVerticalLine(static_cast<int>(playheadX), 0.0f, static_cast<float>(getHeight()));

    // Velocity lane: one bar per note, height proportional to velocity
    g.setColour(juce::Colours::darkgrey.darker());
    g.fillRect(0.0f, gridHeight, static_cast<float>(getWidth()), static_cast<float>(kVelocityLaneHeight));

    if (clip != nullptr) {
        g.setColour(juce::Colours::orange);
        for (const model::Note& note : clip->notes()) {
            const float barX = tickToX(note.startTick, clipLength);
            const float barHeight = note.velocity * static_cast<float>(kVelocityLaneHeight);
            g.fillRect(barX - static_cast<float>(kVelocityBarWidth) / 2.0f,
                gridHeight + static_cast<float>(kVelocityLaneHeight) - barHeight,
                static_cast<float>(kVelocityBarWidth), barHeight);
        }
    }
}

// Begins adding a note (performs AddNoteCommand), begins a move or resize drag, or grabs
// a velocity bar when the click lands inside the velocity lane
void PianoRoll::mouseDown(const juce::MouseEvent& event) {
    model::MidiClip* clip = resolveClip();
    if (clip == nullptr) {
        return;
    }

    m_mouseDownPosition = event.getPosition();
    m_hasDraggedBeyondThreshold = false;

    if (static_cast<float>(event.y) >= keyGridHeight()) {
        const int velocityIndex = hitTestVelocityBar(*clip, event.x);
        if (velocityIndex < 0) {
            m_dragMode = DragMode::None;
            m_dragNoteIndex = -1;
            return;
        }

        m_dragNoteIndex = velocityIndex;
        m_dragOriginalNote = clip->notes()[static_cast<std::size_t>(velocityIndex)];
        m_dragMode = DragMode::Velocity;
        return;
    }

    const int64_t tick = xToTick(event.x, clip->lengthTicks());
    const int key = yToKey(event.y);
    const int index = hitTestNote(*clip, tick, key);

    if (index < 0) {
        const model::SnapDivision division = snapDivision();
        const int64_t startTick = model::snapTickFloor(tick, division);
        const int64_t lengthTicks = division == model::SnapDivision::Off
            ? model::kTicksPerQuarter : model::snapUnitTicks(division);
        const model::Note note { key, 1.0f, startTick, lengthTicks };

        m_commandStack.perform(std::make_unique<model::AddNoteCommand>(
            m_arrangement, m_session, nullptr, m_address, note));

        // Growing the clip to fit a note placed past its current end is not undoable,
        // matching the pre-existing behavior this task carries forward unchanged
        model::MidiClip* afterAdd = resolveClip();
        if (afterAdd != nullptr && startTick + lengthTicks > afterAdd->lengthTicks()) {
            afterAdd->setLengthTicks(startTick + lengthTicks);
        }

        m_dragMode = DragMode::None;
        m_dragNoteIndex = -1;
        repaint();
        return;
    }

    const model::Note& note = clip->notes()[static_cast<std::size_t>(index)];
    m_dragOriginalNote = note;
    const float rightEdgeX = tickToX(note.startTick + note.lengthTicks, clip->lengthTicks());

    m_dragNoteIndex = index;
    if (std::abs(static_cast<float>(event.x) - rightEdgeX) <= static_cast<float>(kResizeHandlePixels)) {
        m_dragMode = DragMode::Resize;
    } else {
        m_dragMode = DragMode::Move;
        m_dragTickOffset = tick - note.startTick;
    }
}

// Continues a move or resize drag once the mouse has moved past a small threshold. Mutates
// the resolved clip directly for live feedback, the gesture rule; mouseUp turns the net
// change into one ReplaceNotesCommand
void PianoRoll::mouseDrag(const juce::MouseEvent& event) {
    if (m_dragMode == DragMode::None || m_dragNoteIndex < 0) {
        return;
    }

    model::MidiClip* clip = resolveClip();
    if (clip == nullptr) {
        return;
    }

    if (event.getPosition().getDistanceFrom(m_mouseDownPosition) > kDragThresholdPixels) {
        m_hasDraggedBeyondThreshold = true;
    }
    if (!m_hasDraggedBeyondThreshold) {
        return;
    }

    model::Note note = clip->notes()[static_cast<std::size_t>(m_dragNoteIndex)];

    if (m_dragMode == DragMode::Velocity) {
        const float relativeY = static_cast<float>(event.y) - keyGridHeight();
        const float ratio = relativeY / static_cast<float>(kVelocityLaneHeight);
        note.velocity = juce::jlimit(0.05f, 1.0f, 1.0f - ratio);
    } else {
        const model::SnapDivision division = snapDivision();

        if (m_dragMode == DragMode::Move) {
            const int64_t rawTick = xToTick(event.x, clip->lengthTicks()) - m_dragTickOffset;
            note.startTick = model::snapTick(juce::jmax<int64_t>(0, rawTick), division);
            note.key = yToKey(event.y);
        } else {
            const int64_t rawEndTick = xToTick(event.x, clip->lengthTicks());
            const int64_t snappedEndTick = model::snapTick(rawEndTick, division);
            note.lengthTicks = juce::jmax<int64_t>(1, snappedEndTick - note.startTick);
        }
    }

    m_dragNoteIndex = static_cast<int>(clip->replaceNoteAt(static_cast<std::size_t>(m_dragNoteIndex), note));

    if (note.startTick + note.lengthTicks > clip->lengthTicks()) {
        clip->setLengthTicks(note.startTick + note.lengthTicks);
    }

    repaint();
}

// Performs ReplaceNotesCommand for a completed drag (the model already reflects the after
// state from the live mutation, execute() is a harmless no-op, undo restores the before
// values), or RemoveNoteCommand for a plain key grid click that never moved. A plain click
// on a velocity bar with no drag does nothing, it is a grab gesture, not a delete gesture
void PianoRoll::mouseUp(const juce::MouseEvent&) {
    if (m_dragMode == DragMode::None || m_dragNoteIndex < 0) {
        return;
    }

    model::MidiClip* clip = resolveClip();
    if (clip != nullptr) {
        if (m_hasDraggedBeyondThreshold) {
            const model::Note finalNote = clip->notes()[static_cast<std::size_t>(m_dragNoteIndex)];
            m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(m_arrangement, m_session, nullptr,
                m_address, std::vector<model::Note> { m_dragOriginalNote }, std::vector<model::Note> { finalNote }));
        } else if (m_dragMode != DragMode::Velocity) {
            m_commandStack.perform(std::make_unique<model::RemoveNoteCommand>(
                m_arrangement, m_session, nullptr, m_address, m_dragOriginalNote));
        }
        repaint();
    }

    m_dragMode = DragMode::None;
    m_dragNoteIndex = -1;
}

// Toggles the transport between play and stop when space is pressed
bool PianoRoll::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey) {
        if (m_transport.isPlaying()) {
            m_transport.stop();
        } else {
            m_transport.play();
        }
        return true;
    }
    return false;
}

} // namespace howl::ui
