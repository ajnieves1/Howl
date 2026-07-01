// SPDX-License-Identifier: GPL-3.0-or-later
// Hearth DAW: shows and edits a MidiClip, draws the transport playhead

#include "ui/PianoRoll.h"

#include <cmath>

namespace hearth::ui {

// Stores references to the clip and transport, starts the playhead timer
PianoRoll::PianoRoll(model::MidiClip& clip, engine::Transport& transport, double sampleRate)
    : m_clip(clip)
    , m_transport(transport)
    , m_sampleRate(sampleRate)
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

// Ticks spanned by the visible grid, at least 4 beats even for an empty clip
int64_t PianoRoll::visibleTickSpan() const {
    const int64_t minimumSpan = kDefaultNoteLengthTicks * 4;
    return juce::jmax(m_clip.lengthTicks(), minimumSpan);
}

// Converts a pixel x position to a tick, clamped to the visible span
int64_t PianoRoll::xToTick(int x) const {
    const int64_t span = visibleTickSpan();
    const float ratio = juce::jlimit(0.0f, 1.0f, static_cast<float>(x) / static_cast<float>(getWidth()));
    return static_cast<int64_t>(ratio * static_cast<float>(span));
}

// Converts a pixel y position to a MIDI key, clamped to the visible range
int PianoRoll::yToKey(int y) const {
    const float rowHeight = static_cast<float>(getHeight()) / static_cast<float>(kNumKeys);
    const int rowIndex = juce::jlimit(0, kNumKeys - 1, static_cast<int>(static_cast<float>(y) / rowHeight));
    return kHighestKey - rowIndex;
}

// Converts a tick to a pixel x position
float PianoRoll::tickToX(int64_t tick) const {
    const int64_t span = visibleTickSpan();
    return static_cast<float>(tick) / static_cast<float>(span) * static_cast<float>(getWidth());
}

// Converts a MIDI key to the y position of the top of its row
float PianoRoll::keyToY(int key) const {
    const float rowHeight = static_cast<float>(getHeight()) / static_cast<float>(kNumKeys);
    const int rowIndex = kHighestKey - key;
    return static_cast<float>(rowIndex) * rowHeight;
}

// Returns the index of the note at (tick, key), or -1 if none
int PianoRoll::hitTestNote(int64_t tick, int key) const {
    const auto& notes = m_clip.notes();
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const model::Note& note = notes[i];
        if (note.key == key && tick >= note.startTick && tick < note.startTick + note.lengthTicks) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Returns the current transport position converted to ticks
double PianoRoll::playheadTick() const {
    const double tempo = m_transport.tempo();
    const double samplesPerTick = (60.0 / tempo) * m_sampleRate / static_cast<double>(model::kTicksPerQuarter);
    return static_cast<double>(m_transport.position()) / samplesPerTick;
}

// Draws the key grid, notes, and playhead
void PianoRoll::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);

    const float rowHeight = static_cast<float>(getHeight()) / static_cast<float>(kNumKeys);

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

    // Beat grid lines
    const int64_t span = visibleTickSpan();
    const int numBeats = static_cast<int>(span / model::kTicksPerQuarter);
    g.setColour(juce::Colours::grey.withAlpha(0.4f));
    for (int beat = 0; beat <= numBeats; ++beat) {
        const float x = tickToX(static_cast<int64_t>(beat) * model::kTicksPerQuarter);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(getHeight()));
    }

    // Notes
    g.setColour(juce::Colours::orange);
    for (const model::Note& note : m_clip.notes()) {
        if (note.key < kLowestKey || note.key > kHighestKey) {
            continue;
        }
        const float x = tickToX(note.startTick);
        const float width = tickToX(note.startTick + note.lengthTicks) - x;
        g.fillRect(x, keyToY(note.key), juce::jmax(2.0f, width), rowHeight);
    }

    // Playhead
    g.setColour(juce::Colours::white);
    const float playheadX = tickToX(static_cast<int64_t>(playheadTick()));
    g.drawVerticalLine(static_cast<int>(playheadX), 0.0f, static_cast<float>(getHeight()));
}

// Begins adding a note, or begins a move or resize drag on an existing note
void PianoRoll::mouseDown(const juce::MouseEvent& event) {
    m_mouseDownPosition = event.getPosition();
    m_hasDraggedBeyondThreshold = false;

    const int64_t tick = xToTick(event.x);
    const int key = yToKey(event.y);
    const int index = hitTestNote(tick, key);

    if (index < 0) {
        model::Note note { key, 1.0f, tick, kDefaultNoteLengthTicks };
        m_clip.addNote(note);
        if (note.startTick + note.lengthTicks > m_clip.lengthTicks()) {
            m_clip.setLengthTicks(note.startTick + note.lengthTicks);
        }
        m_dragMode = DragMode::None;
        m_dragNoteIndex = -1;
        repaint();
        return;
    }

    const auto& notes = m_clip.notes();
    const model::Note& note = notes[static_cast<std::size_t>(index)];
    const float rightEdgeX = tickToX(note.startTick + note.lengthTicks);

    m_dragNoteIndex = index;
    if (std::abs(static_cast<float>(event.x) - rightEdgeX) <= static_cast<float>(kResizeHandlePixels)) {
        m_dragMode = DragMode::Resize;
    } else {
        m_dragMode = DragMode::Move;
        m_dragTickOffset = tick - note.startTick;
    }
}

// Continues a move or resize drag once the mouse has moved past a small threshold
void PianoRoll::mouseDrag(const juce::MouseEvent& event) {
    if (m_dragMode == DragMode::None || m_dragNoteIndex < 0) {
        return;
    }

    if (event.getPosition().getDistanceFrom(m_mouseDownPosition) > kDragThresholdPixels) {
        m_hasDraggedBeyondThreshold = true;
    }
    if (!m_hasDraggedBeyondThreshold) {
        return;
    }

    const auto& notes = m_clip.notes();
    model::Note note = notes[static_cast<std::size_t>(m_dragNoteIndex)];

    if (m_dragMode == DragMode::Move) {
        const int64_t newTick = xToTick(event.x) - m_dragTickOffset;
        note.startTick = juce::jmax<int64_t>(0, newTick);
        note.key = yToKey(event.y);
    } else {
        const int64_t newEndTick = xToTick(event.x);
        note.lengthTicks = juce::jmax<int64_t>(1, newEndTick - note.startTick);
    }

    m_dragNoteIndex = static_cast<int>(m_clip.replaceNoteAt(static_cast<std::size_t>(m_dragNoteIndex), note));

    if (note.startTick + note.lengthTicks > m_clip.lengthTicks()) {
        m_clip.setLengthTicks(note.startTick + note.lengthTicks);
    }

    repaint();
}

// Deletes the note if mouseDown started a drag that never moved
void PianoRoll::mouseUp(const juce::MouseEvent&) {
    if (m_dragMode != DragMode::None && m_dragNoteIndex >= 0 && !m_hasDraggedBeyondThreshold) {
        m_clip.removeNoteAt(static_cast<std::size_t>(m_dragNoteIndex));
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

} // namespace hearth::ui
