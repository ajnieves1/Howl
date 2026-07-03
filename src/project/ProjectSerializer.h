// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: saves and loads a full session to/from .howl JSON text

#pragma once

#include "engine/EffectFactory.h"
#include "model/Arrangement.h"
#include "model/Mixer.h"
#include "model/Session.h"
#include "plugins/IPluginInstance.h"

#include <juce_core/juce_core.h>

namespace howl::project {

// Audio clips are saved as sourcePath + startTick only, never samples (project stays free
// of an io dependency, keeping the model->project->ui->app layering clean). load() fills
// audio clip placements with EMPTY model::AudioClip objects carrying only sourcePath() and
// startTick; the caller (app layer) must re-read each sourcePath through its own audio
// import path and replace the clip before playback.
class ProjectSerializer {
public:
    // Serializes the session to .howl JSON text; instruments is a juce::var array, one entry
    // per track (in track order), built by the app: null for audio tracks,
    // { "kind": "subtractive", "params": [...] } or
    // { "kind": "plugin", "name", "format", "path", "state": "<Base64>" } for MIDI tracks.
    // Session slots and audio clips also carry originalBpm/warpEnabled, additive fields under
    // the same "version": 1
    static juce::String save(const model::Arrangement& arrangement, model::Mixer& mixer,
                             const model::Session& session, const juce::var& instruments, double tempo);

    // Parses json and rebuilds into arrangement/mixer/session (mixer is reset() then rebuilt
    // in place, ArrangementNode owns it and cannot be replaced; session is replaced outright).
    // Built-in effects are created via factory; plugin effects are instantiated via pluginHost
    // and skipped (with a juce::Logger line) when pluginHost is null or the plugin cannot be
    // found. Returns false only on JSON parse failure. instrumentsOut/tempoOut are filled for
    // the app to rebuild instruments the same way save()'s instruments parameter was built.
    // An absent "session" key (older files) loads as an empty grid sized to the track count
    static bool load(const juce::String& json, model::Arrangement& arrangement,
                     model::Mixer& mixer, model::Session& session, engine::IEffectFactory& factory,
                     plugins::IPluginHost* pluginHost, juce::var& instrumentsOut,
                     double& tempoOut);
};

} // namespace howl::project
