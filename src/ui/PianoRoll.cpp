// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows and edits a MidiClip resolved from a ClipAddress, draws the transport playhead

#include "ui/PianoRoll.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace howl::ui {

namespace {

// Bit-exact match for a stored velocity value, structured to avoid -Wfloat-equal on ==
bool sameVelocity(float a, float b) {
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

// Exact field match between two notes, velocity compared bit-exact like sameVelocity
bool sameNote(const model::Note& a, const model::Note& b) {
    return a.key == b.key && a.startTick == b.startTick && a.lengthTicks == b.lengthTicks
        && sameVelocity(a.velocity, b.velocity);
}

} // namespace

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

// The division's own unit, Beat's unit when the division is Off (Off has no unit of its own)
int64_t PianoRoll::effectiveUnitTicks(model::SnapDivision division) const {
    return division == model::SnapDivision::Off
        ? model::snapUnitTicks(model::SnapDivision::Beat) : model::snapUnitTicks(division);
}

// True when note matches an entry in m_selection by exact field value
bool PianoRoll::isSelected(const model::Note& note) const {
    return std::any_of(m_selection.begin(), m_selection.end(), [&](const model::Note& selected) {
        return sameNote(selected, note);
    });
}

// Adds note to the selection if absent, removes it if present
void PianoRoll::toggleSelection(const model::Note& note) {
    const auto it = std::find_if(m_selection.begin(), m_selection.end(), [&](const model::Note& selected) {
        return sameNote(selected, note);
    });

    if (it == m_selection.end()) {
        m_selection.push_back(note);
    } else {
        m_selection.erase(it);
    }
}

// Selects every note intersecting the marquee rectangle, replacing or adding to the
// current selection depending on whether Shift was held when the marquee began
void PianoRoll::finalizeMarquee() {
    model::MidiClip* clip = resolveClip();
    m_marqueeActive = false;

    if (clip == nullptr) {
        return;
    }

    const int64_t clipLength = clip->lengthTicks();
    const int64_t tickA = xToTick(m_marqueeStart.x, clipLength);
    const int64_t tickB = xToTick(m_marqueeCurrent.x, clipLength);
    const int64_t minTick = juce::jmin(tickA, tickB);
    const int64_t maxTick = juce::jmax(tickA, tickB);
    const int keyA = yToKey(m_marqueeStart.y);
    const int keyB = yToKey(m_marqueeCurrent.y);
    const int minKey = juce::jmin(keyA, keyB);
    const int maxKey = juce::jmax(keyA, keyB);

    std::vector<model::Note> caught;
    for (const model::Note& note : clip->notes()) {
        const bool tickOverlaps = note.startTick < maxTick && note.startTick + note.lengthTicks > minTick;
        const bool keyOverlaps = note.key >= minKey && note.key <= maxKey;
        if (tickOverlaps && keyOverlaps) {
            caught.push_back(note);
        }
    }

    if (m_marqueeAdditive) {
        for (const model::Note& note : caught) {
            if (!isSelected(note)) {
                m_selection.push_back(note);
            }
        }
    } else {
        m_selection = std::move(caught);
    }
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

    // Notes, fill alpha scales with velocity so loud notes read solid, selected notes get a border
    if (clip != nullptr) {
        for (const model::Note& note : clip->notes()) {
            if (note.key < kLowestKey || note.key > kHighestKey) {
                continue;
            }
            const float x = tickToX(note.startTick, clipLength);
            const float width = tickToX(note.startTick + note.lengthTicks, clipLength) - x;
            const auto bounds = juce::Rectangle<float> { x, keyToY(note.key), juce::jmax(2.0f, width), rowHeight };

            g.setColour(juce::Colours::orange.withAlpha(0.4f + 0.6f * note.velocity));
            g.fillRect(bounds);

            if (isSelected(note)) {
                g.setColour(juce::Colours::white);
                g.drawRect(bounds, 1.5f);
            }
        }
    }

    // Playhead
    g.setColour(juce::Colours::white);
    const float playheadX = tickToX(static_cast<int64_t>(playheadTick()), clipLength);
    g.drawVerticalLine(static_cast<int>(playheadX), 0.0f, static_cast<float>(getHeight()));

    // Marquee, a translucent rectangle over the region being swept
    if (m_marqueeActive) {
        const auto marqueeBounds = juce::Rectangle<int>(m_marqueeStart, m_marqueeCurrent);
        g.setColour(juce::Colours::lightblue.withAlpha(0.2f));
        g.fillRect(marqueeBounds);
        g.setColour(juce::Colours::lightblue.withAlpha(0.6f));
        g.drawRect(marqueeBounds, 1.0f);
    }

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

// Begins adding a note (performs AddNoteCommand), begins a move/resize/velocity drag, begins
// a marquee (Ctrl+drag on empty grid), or updates the selection per the click rules: Shift
// toggles the clicked note, a plain click on an already selected note keeps the selection
// (so a group drag can start), a plain click on an unselected note selects only that note,
// and a plain click on empty grid clears the selection (as well as adding a note, as today)
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

    if (event.mods.isAltDown()) {
        if (index >= 0) {
            const model::Note target = clip->notes()[static_cast<std::size_t>(index)];
            const int64_t splitTick = model::snapTick(tick, snapDivision());
            if (splitTick > target.startTick && splitTick < target.startTick + target.lengthTicks) {
                m_commandStack.perform(std::make_unique<model::SplitNoteCommand>(
                    m_arrangement, m_session, nullptr, m_address, target, splitTick));
                repaint();
            }
            // A split point that snaps onto either edge is a no-op, per the Steps rule
        }

        m_dragMode = DragMode::None;
        m_dragNoteIndex = -1;
        return;
    }

    if (index < 0) {
        if (event.mods.isCommandDown()) {
            m_marqueeActive = true;
            m_marqueeAdditive = event.mods.isShiftDown();
            m_marqueeStart = event.getPosition();
            m_marqueeCurrent = m_marqueeStart;
            m_dragMode = DragMode::None;
            m_dragNoteIndex = -1;
            return;
        }

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

        m_selection.clear();
        m_dragMode = DragMode::None;
        m_dragNoteIndex = -1;
        repaint();
        return;
    }

    const model::Note note = clip->notes()[static_cast<std::size_t>(index)];

    if (event.mods.isShiftDown() && !event.mods.isCommandDown()) {
        toggleSelection(note);
    } else if (!isSelected(note)) {
        m_selection = { note };
    }
    // A plain click on an already selected note leaves the selection exactly as it was

    m_dragOriginalNote = note;
    const float rightEdgeX = tickToX(note.startTick + note.lengthTicks, clip->lengthTicks());

    m_dragNoteIndex = index;
    if (std::abs(static_cast<float>(event.x) - rightEdgeX) <= static_cast<float>(kResizeHandlePixels)) {
        m_dragMode = DragMode::Resize;
    } else {
        m_dragMode = DragMode::Move;
        m_dragTickOffset = tick - note.startTick;
        // A note the shift-click just removed from the selection has no group to drag,
        // it falls back to moving on its own
        m_dragOriginalSelection = isSelected(note) ? m_selection : std::vector<model::Note> { note };
        m_dragCurrentNotes = m_dragOriginalSelection;
    }

    repaint();
}

// Continues a marquee sweep, or a move/resize/velocity drag once the mouse has moved past a
// small threshold. A move drags the whole selection: every selected note gets the same tick
// and key delta the grabbed note itself moved by. Mutates the resolved clip directly for live
// feedback, the gesture rule; mouseUp turns the net change into one ReplaceNotesCommand. The
// live group move reuses ReplaceNotesCommand::execute() itself as a throwaway mutator (never
// pushed to the command stack) so every frame's swap is the same presence checked, harmless
// to repeat operation the final committed command relies on
void PianoRoll::mouseDrag(const juce::MouseEvent& event) {
    if (m_marqueeActive) {
        m_marqueeCurrent = event.getPosition();
        repaint();
        return;
    }

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

    if (m_dragMode == DragMode::Move) {
        const model::SnapDivision division = snapDivision();
        const int64_t rawTick = xToTick(event.x, clip->lengthTicks()) - m_dragTickOffset;
        const int64_t newStartTick = model::snapTick(juce::jmax<int64_t>(0, rawTick), division);
        const int64_t tickDelta = newStartTick - m_dragOriginalNote.startTick;
        const int keyDelta = yToKey(event.y) - m_dragOriginalNote.key;

        std::vector<model::Note> candidates;
        candidates.reserve(m_dragOriginalSelection.size());
        int64_t maxEndTick = 0;
        for (const model::Note& original : m_dragOriginalSelection) {
            const model::Note moved {
                juce::jlimit(kLowestKey, kHighestKey, original.key + keyDelta),
                original.velocity,
                juce::jmax<int64_t>(0, original.startTick + tickDelta),
                original.lengthTicks
            };
            candidates.push_back(moved);
            maxEndTick = juce::jmax(maxEndTick, moved.startTick + moved.lengthTicks);
        }

        model::ReplaceNotesCommand preview(m_arrangement, m_session, nullptr, m_address, m_dragCurrentNotes, candidates);
        preview.execute();
        m_dragCurrentNotes = std::move(candidates);

        if (maxEndTick > clip->lengthTicks()) {
            clip->setLengthTicks(maxEndTick);
        }
    } else {
        model::Note note = clip->notes()[static_cast<std::size_t>(m_dragNoteIndex)];

        if (m_dragMode == DragMode::Velocity) {
            const float relativeY = static_cast<float>(event.y) - keyGridHeight();
            const float ratio = relativeY / static_cast<float>(kVelocityLaneHeight);
            note.velocity = juce::jlimit(0.05f, 1.0f, 1.0f - ratio);
        } else {
            const model::SnapDivision division = snapDivision();
            const int64_t rawEndTick = xToTick(event.x, clip->lengthTicks());
            const int64_t snappedEndTick = model::snapTick(rawEndTick, division);
            note.lengthTicks = juce::jmax<int64_t>(1, snappedEndTick - note.startTick);
        }

        m_dragNoteIndex = static_cast<int>(clip->replaceNoteAt(static_cast<std::size_t>(m_dragNoteIndex), note));

        if (note.startTick + note.lengthTicks > clip->lengthTicks()) {
            clip->setLengthTicks(note.startTick + note.lengthTicks);
        }
    }

    repaint();
}

// Shows an I-beam cursor while Alt is held over a note, the slice gesture's cue
void PianoRoll::mouseMove(const juce::MouseEvent& event) {
    model::MidiClip* clip = resolveClip();
    if (clip == nullptr || !event.mods.isAltDown() || static_cast<float>(event.y) >= keyGridHeight()) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    const int64_t tick = xToTick(event.x, clip->lengthTicks());
    const int key = yToKey(event.y);
    const bool overNote = hitTestNote(*clip, tick, key) >= 0;

    setMouseCursor(overNote ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
}

// Finalizes a marquee selection, or performs one ReplaceNotesCommand for a completed drag (the
// model already reflects the after state from the live mutation, execute() is a harmless
// no-op, undo restores the before values). A plain click with no drag only ever changed the
// selection (done already in mouseDown), notes are no longer deleted by clicking them alone,
// that is now Delete/Backspace's job so a selection can survive a click that keeps it
void PianoRoll::mouseUp(const juce::MouseEvent&) {
    if (m_marqueeActive) {
        finalizeMarquee();
        repaint();
        return;
    }

    if (m_dragMode == DragMode::None) {
        return;
    }

    model::MidiClip* clip = resolveClip();
    if (clip != nullptr && m_hasDraggedBeyondThreshold) {
        if (m_dragMode == DragMode::Move) {
            m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(
                m_arrangement, m_session, nullptr, m_address, m_dragOriginalSelection, m_dragCurrentNotes));
            m_selection = m_dragCurrentNotes;
        } else {
            const model::Note finalNote = clip->notes()[static_cast<std::size_t>(m_dragNoteIndex)];
            m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(m_arrangement, m_session, nullptr,
                m_address, std::vector<model::Note> { m_dragOriginalNote }, std::vector<model::Note> { finalNote }));
        }
        repaint();
    }

    m_dragMode = DragMode::None;
    m_dragNoteIndex = -1;
}

// Space toggles play/stop; with a non-empty selection: Delete/Backspace removes it, arrow
// keys nudge (left/right) or transpose (up/down, Shift for an octave) it, Ctrl+D duplicates
// it. Every command here applies to the whole selection at once, one undo step per press
bool PianoRoll::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey) {
        if (m_transport.isPlaying()) {
            m_transport.stop();
        } else {
            m_transport.play();
        }
        return true;
    }

    if (m_selection.empty()) {
        return false;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(
            m_arrangement, m_session, nullptr, m_address, m_selection, std::vector<model::Note> {}));
        m_selection.clear();
        repaint();
        return true;
    }

    if (key == juce::KeyPress('D', juce::ModifierKeys::commandModifier, 0)) {
        int64_t minStart = m_selection.front().startTick;
        int64_t maxEnd = m_selection.front().startTick + m_selection.front().lengthTicks;
        for (const model::Note& note : m_selection) {
            minStart = juce::jmin(minStart, note.startTick);
            maxEnd = juce::jmax(maxEnd, note.startTick + note.lengthTicks);
        }

        const int64_t unit = effectiveUnitTicks(snapDivision());
        const int64_t span = maxEnd - minStart;
        const int64_t offset = juce::jmax(unit, ((span + unit - 1) / unit) * unit);

        std::vector<model::Note> copies;
        copies.reserve(m_selection.size());
        int64_t maxClipEnd = 0;
        for (const model::Note& note : m_selection) {
            const model::Note copy { note.key, note.velocity, note.startTick + offset, note.lengthTicks };
            copies.push_back(copy);
            maxClipEnd = juce::jmax(maxClipEnd, copy.startTick + copy.lengthTicks);
        }

        m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(
            m_arrangement, m_session, nullptr, m_address, std::vector<model::Note> {}, copies));

        // Growing the clip to fit the duplicated notes is not undoable, matching AddNoteCommand
        model::MidiClip* clip = resolveClip();
        if (clip != nullptr && maxClipEnd > clip->lengthTicks()) {
            clip->setLengthTicks(maxClipEnd);
        }

        m_selection = std::move(copies);
        repaint();
        return true;
    }

    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) {
        const int64_t unit = effectiveUnitTicks(snapDivision());
        const int64_t delta = key == juce::KeyPress::leftKey ? -unit : unit;

        std::vector<model::Note> nudged;
        nudged.reserve(m_selection.size());
        for (const model::Note& note : m_selection) {
            nudged.push_back(model::Note { note.key, note.velocity,
                juce::jmax<int64_t>(0, note.startTick + delta), note.lengthTicks });
        }

        m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(
            m_arrangement, m_session, nullptr, m_address, m_selection, nudged));
        m_selection = std::move(nudged);
        repaint();
        return true;
    }

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
        const int semitones = key.getModifiers().isShiftDown() ? 12 : 1;
        const int delta = key == juce::KeyPress::upKey ? semitones : -semitones;

        std::vector<model::Note> transposed;
        transposed.reserve(m_selection.size());
        for (const model::Note& note : m_selection) {
            transposed.push_back(model::Note { juce::jlimit(kLowestKey, kHighestKey, note.key + delta),
                note.velocity, note.startTick, note.lengthTicks });
        }

        m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(
            m_arrangement, m_session, nullptr, m_address, m_selection, transposed));
        m_selection = std::move(transposed);
        repaint();
        return true;
    }

    return false;
}

} // namespace howl::ui
