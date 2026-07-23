// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: shows and edits a MidiClip resolved from a ClipAddress, draws the transport playhead

#include "ui/PianoRoll.h"

#include "ui/Theme.h"

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

// The pitch class name for a MIDI key, sharps, no octave
juce::String noteName(int key) {
    static const char* const names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    return names[((key % 12) + 12) % 12];
}

} // namespace

// Stores references, the clip address, and the snap division provider, starts the playhead timer
PianoRoll::PianoRoll(model::Arrangement& arrangement, model::Session& session, model::PatternBank& patterns,
                      model::ClipAddress address, model::CommandStack& commandStack, engine::Transport& transport,
                      double sampleRate, std::function<model::SnapDivision()> snapProvider)
    : m_arrangement(arrangement)
    , m_session(session)
    , m_patterns(patterns)
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

// Resolves the addressed clip fresh, nullptr when it no longer resolves
model::MidiClip* PianoRoll::resolveClip() const {
    return model::resolveClip(m_arrangement, m_session, &m_patterns, m_address);
}

// Height of the key grid, the component's height less the velocity lane
float PianoRoll::keyGridHeight() const {
    return static_cast<float>(getHeight() - m_velocityLaneHeight);
}

// Row height for one key, never smaller than the height that fits every key into the grid so
// the grid always fills, and larger when the vertical zoom is raised
float PianoRoll::rowHeight() const {
    const float fit = keyGridHeight() / static_cast<float>(kNumKeys);
    return juce::jmax(fit, kBaseKeyRowHeight * m_verticalZoom);
}

// Content width of the grid in pixels at the current horizontal zoom
float PianoRoll::contentWidth() const {
    return static_cast<float>(getWidth() - kKeyboardWidth) * m_horizontalZoom;
}

// Clamps the horizontal scroll into range, applies it, repaints on change
void PianoRoll::applyHorizontalScroll(int requested) {
    const int maxScroll = juce::jmax(0,
        static_cast<int>(contentWidth() - static_cast<float>(getWidth() - kKeyboardWidth)));
    const int clamped = juce::jlimit(0, maxScroll, requested);

    if (clamped != m_horizontalScroll) {
        m_horizontalScroll = clamped;
        repaint();
    }
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

// Converts a pixel x position to a tick, clamped to the visible span, unsnapped; the grid
// starts after the piano key gutter and follows the horizontal zoom and scroll
int64_t PianoRoll::xToTick(int x, int64_t clipLengthTicks) const {
    const int64_t span = visibleTickSpan(clipLengthTicks);
    const float content = contentWidth();
    const float pixelInContent = static_cast<float>(x - kKeyboardWidth + m_horizontalScroll);
    const float ratio = juce::jlimit(0.0f, 1.0f, pixelInContent / content);
    return static_cast<int64_t>(ratio * static_cast<float>(span));
}

// Converts a pixel y position to a MIDI key, clamped to the visible range
int PianoRoll::yToKey(int y) const {
    const float scrolledY = static_cast<float>(y + m_verticalScroll);
    const int rowIndex = juce::jlimit(0, kNumKeys - 1, static_cast<int>(scrolledY / rowHeight()));
    return kHighestKey - rowIndex;
}

// Converts a tick to a pixel x position, past the piano key gutter, following the zoom and scroll
float PianoRoll::tickToX(int64_t tick, int64_t clipLengthTicks) const {
    const int64_t span = visibleTickSpan(clipLengthTicks);
    return static_cast<float>(kKeyboardWidth)
        + static_cast<float>(tick) / static_cast<float>(span) * contentWidth()
        - static_cast<float>(m_horizontalScroll);
}

// Converts a MIDI key to the y position of the top of its row, in the current scroll
float PianoRoll::keyToY(int key) const {
    const int rowIndex = kHighestKey - key;
    return static_cast<float>(rowIndex) * rowHeight() - static_cast<float>(m_verticalScroll);
}

// Clamps requested into the valid vertical scroll range, applies it, repaints on change
void PianoRoll::applyVerticalScroll(int requested) {
    const float content = static_cast<float>(kNumKeys) * rowHeight();
    const int maxScroll = juce::jmax(0, static_cast<int>(content - keyGridHeight()));
    const int clamped = juce::jlimit(0, maxScroll, requested);

    if (clamped != m_verticalScroll) {
        m_verticalScroll = clamped;
        repaint();
    }
}

// Centers the view on the clip's notes (C5 when empty) the first time a size arrives,
// and re-clamps the vertical scroll on any later resize
void PianoRoll::resized() {
    if (!m_scrollCentered && getHeight() > 0) {
        m_scrollCentered = true;

        model::MidiClip* clip = resolveClip();
        int centerKey = 60; // labeled C5
        if (clip != nullptr && !clip->notes().empty()) {
            int keySum = 0;
            for (const model::Note& note : clip->notes()) {
                keySum += note.key;
            }
            centerKey = keySum / static_cast<int>(clip->notes().size());
        }

        const float rowTop = static_cast<float>(kHighestKey - juce::jlimit(kLowestKey, kHighestKey, centerKey))
            * rowHeight();
        applyVerticalScroll(static_cast<int>(rowTop + rowHeight() / 2.0f - keyGridHeight() / 2.0f));
        return;
    }

    m_velocityLaneHeight = juce::jlimit(kMinVelocityLaneHeight, juce::jmax(kMinVelocityLaneHeight, getHeight() / 2),
        m_velocityLaneHeight);
    applyVerticalScroll(m_verticalScroll);
    applyHorizontalScroll(m_horizontalScroll);
}

// Plain wheel scrolls keys, Ctrl wheel zooms time around the cursor, Shift wheel scrolls time,
// Alt wheel zooms the key height
void PianoRoll::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (event.mods.isCtrlDown()) {
        // Zoom time around the cursor, keeping the content point under it fixed
        const float gridLeft = static_cast<float>(kKeyboardWidth);
        const float cursorInContent = static_cast<float>(event.x) - gridLeft + static_cast<float>(m_horizontalScroll);
        const float ratio = contentWidth() > 0.0f ? cursorInContent / contentWidth() : 0.0f;

        const float factor = wheel.deltaY > 0.0f ? 1.15f : 1.0f / 1.15f;
        m_horizontalZoom = juce::jlimit(kMinHorizontalZoom, kMaxHorizontalZoom, m_horizontalZoom * factor);

        const int target = static_cast<int>(ratio * contentWidth() - (static_cast<float>(event.x) - gridLeft));
        applyHorizontalScroll(target);
        repaint();
        return;
    }

    if (event.mods.isShiftDown()) {
        applyHorizontalScroll(m_horizontalScroll - static_cast<int>(wheel.deltaY * 120.0f));
        return;
    }

    if (event.mods.isAltDown()) {
        const float factor = wheel.deltaY > 0.0f ? 1.1f : 1.0f / 1.1f;
        m_verticalZoom = juce::jlimit(kMinVerticalZoom, kMaxVerticalZoom, m_verticalZoom * factor);
        applyVerticalScroll(m_verticalScroll);
        repaint();
        return;
    }

    applyVerticalScroll(m_verticalScroll - static_cast<int>(wheel.deltaY * rowHeight() * 4.0f));
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

// Draws the piano key gutter, key grid, notes, playhead, and the velocity lane, an empty
// grid and lane when the address does not resolve
void PianoRoll::paint(juce::Graphics& g) {
    g.fillAll(theme::kWindowBg);

    const float gridHeight = keyGridHeight();
    const float rowH = rowHeight();
    const float keyboardWidth = static_cast<float>(kKeyboardWidth);

    // Shade black-key rows across the grid, starting after the key gutter
    g.setColour(theme::kWindowBg.brighter(0.1f));
    for (int key = kLowestKey; key <= kHighestKey; ++key) {
        const int pitchClass = key % 12;
        const bool isBlackKey = pitchClass == 1 || pitchClass == 3 || pitchClass == 6
                              || pitchClass == 8 || pitchClass == 10;
        if (isBlackKey) {
            g.fillRect(keyboardWidth, keyToY(key), static_cast<float>(getWidth()) - keyboardWidth, rowH);
        }
    }

    model::MidiClip* clip = resolveClip();
    const int64_t clipLength = clip != nullptr ? clip->lengthTicks() : 0;

    // Beat grid lines, spanning the key grid and the velocity lane so the columns line up
    const int64_t span = visibleTickSpan(clipLength);
    const int numBeats = static_cast<int>(span / model::kTicksPerQuarter);
    g.setColour(theme::kBorder.withAlpha(0.4f));
    for (int beat = 0; beat <= numBeats; ++beat) {
        const float x = tickToX(static_cast<int64_t>(beat) * model::kTicksPerQuarter, clipLength);
        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(getHeight()));
    }

    // The owning channel's color tints notes so the roll matches the rack and arrange clips
    juce::Colour noteColor = theme::kAccent;
    if (m_address.trackIndex < m_arrangement.numTracks()) {
        noteColor = juce::Colour(m_arrangement.track(m_address.trackIndex).color);
    }

    // Notes, fill alpha scales with velocity so loud notes read solid, selected notes get a border
    if (clip != nullptr) {
        for (const model::Note& note : clip->notes()) {
            if (note.key < kLowestKey || note.key > kHighestKey) {
                continue;
            }
            const float x = tickToX(note.startTick, clipLength);
            const float width = tickToX(note.startTick + note.lengthTicks, clipLength) - x;
            const auto bounds = juce::Rectangle<float> { x, keyToY(note.key), juce::jmax(2.0f, width), rowH };

            g.setColour(noteColor.withAlpha(0.4f + 0.6f * note.velocity));
            g.fillRect(bounds);

            // The note name on the block, when it is wide and tall enough to hold the text
            if (bounds.getWidth() >= 16.0f && rowH >= 8.0f) {
                g.setColour(noteColor.contrasting());
                g.setFont(juce::jmin(rowH - 2.0f, static_cast<float>(theme::kFontSizeSmall)));
                g.drawText(noteName(note.key), bounds.reduced(3.0f, 0.0f),
                    juce::Justification::centredLeft, false);
            }

            if (isSelected(note)) {
                g.setColour(theme::kSelection);
                g.drawRect(bounds, 1.5f);
            }
        }
    }

    // Playhead
    g.setColour(theme::kPlayhead);
    const float playheadX = tickToX(static_cast<int64_t>(playheadTick()), clipLength);
    g.drawVerticalLine(static_cast<int>(playheadX), 0.0f, static_cast<float>(getHeight()));

    // Marquee, a translucent rectangle over the region being swept
    if (m_marqueeActive) {
        const auto marqueeBounds = juce::Rectangle<int>(m_marqueeStart, m_marqueeCurrent);
        g.setColour(theme::kSelection.withAlpha(0.2f));
        g.fillRect(marqueeBounds);
        g.setColour(theme::kSelection.withAlpha(0.6f));
        g.drawRect(marqueeBounds, 1.0f);
    }

    // Velocity lane: one bar per note, height proportional to velocity
    g.setColour(theme::kPanelBg);
    g.fillRect(0.0f, gridHeight, static_cast<float>(getWidth()), static_cast<float>(m_velocityLaneHeight));

    // The draggable divider at the top edge of the lane
    g.setColour(theme::kBorder);
    g.fillRect(0.0f, gridHeight - 1.0f, static_cast<float>(getWidth()), 2.0f);

    if (clip != nullptr) {
        g.setColour(noteColor);
        for (const model::Note& note : clip->notes()) {
            const float barX = tickToX(note.startTick, clipLength);
            const float barHeight = note.velocity * static_cast<float>(m_velocityLaneHeight);
            g.fillRect(barX - static_cast<float>(kVelocityBarWidth) / 2.0f,
                gridHeight + static_cast<float>(m_velocityLaneHeight) - barHeight,
                static_cast<float>(kVelocityBarWidth), barHeight);
        }
    }

    // Piano key gutter, drawn last so nothing in the grid paints over it. Every row starts as a
    // white key, a black key is a shorter dark key over the left of the row so it reads as a real
    // keyboard, and each C carries its octave label. Clipped to the grid so scrolled keys never
    // paint over the velocity lane
    g.saveState();
    g.reduceClipRegion(0, 0, getWidth(), static_cast<int>(gridHeight));
    const float blackKeyWidth = keyboardWidth * 0.62f;
    for (int key = kLowestKey; key <= kHighestKey; ++key) {
        const int pitchClass = key % 12;
        const bool isBlackKey = pitchClass == 1 || pitchClass == 3 || pitchClass == 6
                              || pitchClass == 8 || pitchClass == 10;
        const float y = keyToY(key);

        g.setColour(theme::kTextPrimary);
        g.fillRect(0.0f, y, keyboardWidth, rowH);
        if (isBlackKey) {
            g.setColour(theme::kWindowBg);
            g.fillRect(0.0f, y, blackKeyWidth, rowH);
        }
        g.setColour(theme::kBorder);
        g.drawHorizontalLine(static_cast<int>(y), 0.0f, keyboardWidth);

        if (pitchClass == 0 && rowH >= 7.0f) {
            g.setColour(theme::kWindowBg);
            g.setFont(theme::kFontSizeSmall);
            g.drawText("C" + juce::String(key / 12),
                juce::Rectangle<float> { 0.0f, y, keyboardWidth, rowH }.reduced(5.0f, 0.0f),
                juce::Justification::centredRight, false);
        }
    }

    // Hairline separating the keys from the grid
    g.setColour(theme::kBorder);
    g.drawVerticalLine(kKeyboardWidth, 0.0f, gridHeight);
    g.restoreState();
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

    // Grab the velocity lane divider when the click lands within a few pixels of its top edge
    if (std::abs(static_cast<float>(event.y) - keyGridHeight()) <= static_cast<float>(kVelocityDividerZone)) {
        m_draggingVelocityDivider = true;
        m_dragMode = DragMode::None;
        m_dragNoteIndex = -1;
        return;
    }

    // Clicks on the piano key gutter do nothing, keys are not a preview surface in v1
    if (event.x < kKeyboardWidth && static_cast<float>(event.y) < keyGridHeight()) {
        m_dragMode = DragMode::None;
        m_dragNoteIndex = -1;
        return;
    }

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
                    m_arrangement, m_session, &m_patterns, m_address, target, splitTick));
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
            m_arrangement, m_session, &m_patterns, m_address, note));

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
    if (m_draggingVelocityDivider) {
        const int maxLaneHeight = juce::jmax(kMinVelocityLaneHeight, getHeight() / 2);
        m_velocityLaneHeight = juce::jlimit(kMinVelocityLaneHeight, maxLaneHeight, getHeight() - event.y);
        applyVerticalScroll(m_verticalScroll);
        repaint();
        return;
    }

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

        model::ReplaceNotesCommand preview(m_arrangement, m_session, &m_patterns, m_address, m_dragCurrentNotes, candidates);
        preview.execute();
        m_dragCurrentNotes = std::move(candidates);

        if (maxEndTick > clip->lengthTicks()) {
            clip->setLengthTicks(maxEndTick);
        }
    } else {
        model::Note note = clip->notes()[static_cast<std::size_t>(m_dragNoteIndex)];

        if (m_dragMode == DragMode::Velocity) {
            const float relativeY = static_cast<float>(event.y) - keyGridHeight();
            const float ratio = relativeY / static_cast<float>(m_velocityLaneHeight);
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

// Shows a resize cursor over the velocity divider, an I-beam while Alt is held over a note
void PianoRoll::mouseMove(const juce::MouseEvent& event) {
    if (std::abs(static_cast<float>(event.y) - keyGridHeight()) <= static_cast<float>(kVelocityDividerZone)) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    model::MidiClip* clip = resolveClip();
    if (clip == nullptr || !event.mods.isAltDown() || event.x < kKeyboardWidth
        || static_cast<float>(event.y) >= keyGridHeight()) {
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
    if (m_draggingVelocityDivider) {
        m_draggingVelocityDivider = false;
        return;
    }

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
                m_arrangement, m_session, &m_patterns, m_address, m_dragOriginalSelection, m_dragCurrentNotes));
            m_selection = m_dragCurrentNotes;
        } else {
            const model::Note finalNote = clip->notes()[static_cast<std::size_t>(m_dragNoteIndex)];
            m_commandStack.perform(std::make_unique<model::ReplaceNotesCommand>(m_arrangement, m_session, &m_patterns,
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
            m_arrangement, m_session, &m_patterns, m_address, m_selection, std::vector<model::Note> {}));
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
            m_arrangement, m_session, &m_patterns, m_address, std::vector<model::Note> {}, copies));

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
            m_arrangement, m_session, &m_patterns, m_address, m_selection, nudged));
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
            m_arrangement, m_session, &m_patterns, m_address, m_selection, transposed));
        m_selection = std::move(transposed);
        repaint();
        return true;
    }

    return false;
}

} // namespace howl::ui
