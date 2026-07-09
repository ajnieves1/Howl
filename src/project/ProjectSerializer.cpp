// SPDX-License-Identifier: GPL-3.0-or-later
// Howl DAW: saves and loads a full session to/from .howl JSON text

#include "project/ProjectSerializer.h"

#include "model/AudioClip.h"
#include "model/MidiClip.h"
#include "model/Note.h"
#include "model/Session.h"
#include "plugins/PluginEffect.h"

#include <cstdint>
#include <memory>

namespace howl::project {

namespace {

// Writes one effect's JSON entry: kind, identity (builtin type or plugin name/format/path/state), params
juce::var effectToVar(engine::Effect& effect) {
    auto* obj = new juce::DynamicObject();

    if (auto* pluginEffect = dynamic_cast<plugins::PluginEffect*>(&effect)) {
        const plugins::StateBlob blob = pluginEffect->instance().saveState();
        obj->setProperty("kind", "plugin");
        obj->setProperty("name", juce::String(pluginEffect->displayName()));
        obj->setProperty("format", juce::String(pluginEffect->pluginFormat()));
        obj->setProperty("path", juce::String(pluginEffect->pluginPath()));
        obj->setProperty("state", juce::Base64::toBase64(blob.data(), blob.size()));
    } else {
        obj->setProperty("kind", "builtin");
        obj->setProperty("type", juce::String(effect.displayName()));
    }

    juce::Array<juce::var> params;
    for (int i = 0; i < effect.numParameters(); ++i) {
        params.add(static_cast<double>(effect.getParameter(i)));
    }
    obj->setProperty("params", params);

    return juce::var(obj);
}

// Writes one strip's JSON entry: gain, pan, mute, solo, and its effect chain
juce::var stripToVar(model::ChannelStrip& strip) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("gainDb", static_cast<double>(strip.gainDb()));
    obj->setProperty("pan", static_cast<double>(strip.pan()));
    obj->setProperty("muted", strip.muted());
    obj->setProperty("soloed", strip.soloed());

    juce::Array<juce::var> effects;
    engine::EffectChain& chain = strip.effects();
    for (std::size_t i = 0; i < chain.size(); ++i) {
        effects.add(effectToVar(chain.at(i)));
    }
    obj->setProperty("effects", effects);

    return juce::var(obj);
}

// Writes one automation lane's JSON entry: the parameter it targets and its points
juce::var automationLaneToVar(const model::AutomationLaneSlot& slot) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("param", slot.paramIndex);

    juce::Array<juce::var> points;
    for (const auto& point : slot.lane.points()) {
        auto* pointObj = new juce::DynamicObject();
        pointObj->setProperty("tick", static_cast<juce::int64>(point.tick));
        pointObj->setProperty("value", static_cast<double>(point.value));
        points.add(juce::var(pointObj));
    }
    obj->setProperty("points", points);

    return juce::var(obj);
}

// Writes one send's JSON entry
juce::var sendToVar(const model::Send& send) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("bus", static_cast<int>(send.busIndex));
    obj->setProperty("level", static_cast<double>(send.level));
    obj->setProperty("preFader", send.preFader);
    return juce::var(obj);
}

// Writes one note's JSON entry
juce::var noteToVar(const model::Note& note) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("key", note.key);
    obj->setProperty("velocity", static_cast<double>(note.velocity));
    obj->setProperty("startTick", static_cast<juce::int64>(note.startTick));
    obj->setProperty("lengthTicks", static_cast<juce::int64>(note.lengthTicks));
    return juce::var(obj);
}

// Writes one MIDI clip placement's JSON entry, including its notes
juce::var midiClipToVar(const model::MidiClipPlacement& placement) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("startTick", static_cast<juce::int64>(placement.startTick));
    obj->setProperty("lengthTicks", static_cast<juce::int64>(placement.clip.lengthTicks()));
    obj->setProperty("muted", placement.muted);

    juce::Array<juce::var> notes;
    for (const auto& note : placement.clip.notes()) {
        notes.add(noteToVar(note));
    }
    obj->setProperty("notes", notes);

    return juce::var(obj);
}

// Writes one audio clip placement's JSON entry, sourcePath and warp fields, never samples
juce::var audioClipToVar(const model::AudioClipPlacement& placement) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("startTick", static_cast<juce::int64>(placement.startTick));
    obj->setProperty("sourcePath", juce::String(placement.clip.sourcePath()));
    obj->setProperty("originalBpm", placement.clip.originalBpm());
    obj->setProperty("warpEnabled", placement.clip.warpEnabled());
    obj->setProperty("muted", placement.muted);
    return juce::var(obj);
}

// Writes one session slot's JSON entry, null when empty
juce::var slotToVar(const model::ClipSlot& slot) {
    if (slot.content == model::SlotContent::Empty) {
        return juce::var();
    }

    auto* obj = new juce::DynamicObject();

    if (slot.content == model::SlotContent::Midi) {
        obj->setProperty("kind", "midi");
        obj->setProperty("lengthTicks", static_cast<juce::int64>(slot.midiClip.lengthTicks()));

        juce::Array<juce::var> notes;
        for (const auto& note : slot.midiClip.notes()) {
            notes.add(noteToVar(note));
        }
        obj->setProperty("notes", notes);
    } else {
        obj->setProperty("kind", "audio");
        obj->setProperty("sourcePath", juce::String(slot.audioClip.sourcePath()));
        obj->setProperty("originalBpm", slot.audioClip.originalBpm());
        obj->setProperty("warpEnabled", slot.audioClip.warpEnabled());
    }

    return juce::var(obj);
}

// Writes the session grid's JSON entry: scene count and one column array per track
juce::var sessionToVar(const model::Session& session) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("numScenes", static_cast<int>(session.numScenes()));

    juce::Array<juce::var> tracks;
    for (std::size_t t = 0; t < session.numTracks(); ++t) {
        juce::Array<juce::var> column;
        for (std::size_t s = 0; s < session.numScenes(); ++s) {
            column.add(slotToVar(session.slot(t, s)));
        }
        tracks.add(column);
    }
    obj->setProperty("tracks", tracks);

    return juce::var(obj);
}

// Writes one track's JSON entry: identity, clips, strip, output routing, sends
juce::var trackToVar(const model::Track& track, model::Mixer& mixer, std::size_t trackIndex) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("name", juce::String(track.name));
    obj->setProperty("kind", track.kind == model::TrackKind::Midi ? "midi" : "audio");

    juce::Array<juce::var> midiClips;
    for (const auto& placement : track.midiClips) {
        midiClips.add(midiClipToVar(placement));
    }
    obj->setProperty("midiClips", midiClips);

    juce::Array<juce::var> audioClips;
    for (const auto& placement : track.audioClips) {
        audioClips.add(audioClipToVar(placement));
    }
    obj->setProperty("audioClips", audioClips);

    juce::Array<juce::var> automationLanes;
    for (const auto& slot : track.automation) {
        automationLanes.add(automationLaneToVar(slot));
    }
    obj->setProperty("automation", automationLanes);

    obj->setProperty("strip", stripToVar(mixer.trackStrip(trackIndex)));

    const std::size_t output = mixer.trackOutput(trackIndex);
    obj->setProperty("output", output == model::Mixer::kMaster ? -1 : static_cast<int>(output));

    juce::Array<juce::var> sends;
    for (const auto& send : mixer.sends(trackIndex)) {
        sends.add(sendToVar(send));
    }
    obj->setProperty("sends", sends);

    return juce::var(obj);
}

// Applies a saved param array onto a freshly created effect, index for index
void applyParams(const juce::var& effectVar, engine::Effect& effect) {
    if (const auto* paramsArray = effectVar.getProperty("params", juce::var()).getArray()) {
        for (int i = 0; i < paramsArray->size() && i < effect.numParameters(); ++i) {
            effect.setParameter(i, static_cast<float>(static_cast<double>((*paramsArray)[i])));
        }
    }
}

// Recreates one effect from its JSON entry, built-ins via factory, plugins via pluginHost.
// Returns nullptr (logged) when the type/plugin cannot be resolved, never throws
std::unique_ptr<engine::Effect> effectFromVar(const juce::var& effectVar, engine::IEffectFactory& factory,
                                              plugins::IPluginHost* pluginHost) {
    const juce::String kind = effectVar.getProperty("kind", juce::var()).toString();

    if (kind == "builtin") {
        const juce::String type = effectVar.getProperty("type", juce::var()).toString();

        for (engine::EffectType candidate : factory.availableTypes()) {
            if (juce::String(factory.displayName(candidate)) == type) {
                std::unique_ptr<engine::Effect> effect = factory.create(candidate);
                applyParams(effectVar, *effect);
                return effect;
            }
        }

        juce::Logger::writeToLog("Howl: unknown built-in effect type '" + type + "', skipped on load");
        return nullptr;
    }

    if (kind == "plugin") {
        if (pluginHost == nullptr) {
            juce::Logger::writeToLog("Howl: no plugin host, skipped a plugin effect on load");
            return nullptr;
        }

        const juce::String name = effectVar.getProperty("name", juce::var()).toString();
        const juce::String format = effectVar.getProperty("format", juce::var()).toString();
        const juce::String path = effectVar.getProperty("path", juce::var()).toString();

        plugins::PluginDescriptor found;
        bool foundAny = false;
        for (const auto& descriptor : pluginHost->list()) {
            if (descriptor.name == name.toStdString() && descriptor.format == format.toStdString()
                && descriptor.path == path.toStdString()) {
                found = descriptor;
                foundAny = true;
                break;
            }
        }

        if (!foundAny) {
            juce::Logger::writeToLog("Howl: plugin '" + name + "' not found, skipped an effect on load");
            return nullptr;
        }

        auto instance = pluginHost->instantiate(found);
        if (instance == nullptr) {
            juce::Logger::writeToLog("Howl: failed to instantiate plugin '" + name + "', skipped an effect on load");
            return nullptr;
        }

        // Base64::toBase64() (used to write "state" above) pairs with
        // Base64::convertFromBase64(), not MemoryBlock::fromBase64Encoding(), which expects
        // its own "byteCount.base64" format and otherwise just silently fails to an empty block
        juce::MemoryOutputStream decodedState;
        juce::Base64::convertFromBase64(decodedState, effectVar.getProperty("state", juce::var()).toString());
        const auto* stateBytes = static_cast<const uint8_t*>(decodedState.getData());
        plugins::StateBlob blob(stateBytes, stateBytes + decodedState.getDataSize());
        instance->loadState(blob);

        auto effect = std::make_unique<plugins::PluginEffect>(std::move(instance), found);
        applyParams(effectVar, *effect);
        return effect;
    }

    return nullptr;
}

// Applies a saved strip entry (gain/pan/mute/solo/effects) onto a live ChannelStrip
void applyStripVar(const juce::var& stripVar, model::ChannelStrip& strip, engine::IEffectFactory& factory,
                   plugins::IPluginHost* pluginHost) {
    strip.setGainDb(static_cast<float>(static_cast<double>(stripVar.getProperty("gainDb", 0.0))));
    strip.setPan(static_cast<float>(static_cast<double>(stripVar.getProperty("pan", 0.0))));
    strip.setMuted(static_cast<bool>(stripVar.getProperty("muted", false)));
    strip.setSoloed(static_cast<bool>(stripVar.getProperty("soloed", false)));

    if (const auto* effectsArray = stripVar.getProperty("effects", juce::var()).getArray()) {
        for (const auto& effectVar : *effectsArray) {
            std::unique_ptr<engine::Effect> effect = effectFromVar(effectVar, factory, pluginHost);
            if (effect != nullptr) {
                strip.effects().add(std::move(effect));
            }
        }
    }
}

} // namespace

// Serializes the session to .howl JSON text; instruments is a juce::var array, one entry
// per track (in track order), built by the app
juce::String ProjectSerializer::save(const model::Arrangement& arrangement, model::Mixer& mixer,
                                     const model::Session& session, const juce::var& instruments, double tempo,
                                     const juce::var& midiMappings) {
    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("tempo", tempo);

    juce::Array<juce::var> tracks;
    for (std::size_t i = 0; i < arrangement.numTracks(); ++i) {
        tracks.add(trackToVar(arrangement.track(i), mixer, i));
    }
    root->setProperty("tracks", tracks);

    juce::Array<juce::var> buses;
    for (std::size_t i = 0; i < mixer.numBuses(); ++i) {
        auto* busObj = new juce::DynamicObject();
        busObj->setProperty("name", juce::String(mixer.busName(i)));
        busObj->setProperty("strip", stripToVar(mixer.busStrip(i)));
        buses.add(juce::var(busObj));
    }
    root->setProperty("buses", buses);

    root->setProperty("masterStrip", stripToVar(mixer.masterStrip()));
    root->setProperty("instruments", instruments);
    root->setProperty("session", sessionToVar(session));
    root->setProperty("midiMappings", midiMappings);

    return juce::JSON::toString(juce::var(root));
}

// Parses json and rebuilds into arrangement/mixer (mixer is reset() then rebuilt in place).
// Built-in effects come from factory; plugin effects come from pluginHost and are skipped
// (with a log line) when pluginHost is null or the plugin cannot be found. Returns false
// only on JSON parse failure
bool ProjectSerializer::load(const juce::String& json, model::Arrangement& arrangement,
                             model::Mixer& mixer, model::Session& session, engine::IEffectFactory& factory,
                             plugins::IPluginHost* pluginHost, juce::var& instrumentsOut,
                             double& tempoOut, juce::var& midiMappingsOut) {
    juce::var parsed;
    const juce::Result parseResult = juce::JSON::parse(json, parsed);
    if (parseResult.failed() || !parsed.isObject()) {
        return false;
    }

    tempoOut = static_cast<double>(parsed.getProperty("tempo", 120.0));
    instrumentsOut = parsed.getProperty("instruments", juce::var());
    midiMappingsOut = parsed.getProperty("midiMappings", juce::var());

    arrangement = model::Arrangement();

    if (const auto* tracksArray = parsed.getProperty("tracks", juce::var()).getArray()) {
        for (const auto& trackVar : *tracksArray) {
            const juce::String kindStr = trackVar.getProperty("kind", "midi").toString();
            const model::TrackKind kind = kindStr == "audio" ? model::TrackKind::Audio : model::TrackKind::Midi;
            const std::string name = trackVar.getProperty("name", juce::var()).toString().toStdString();

            const std::size_t trackIndex = arrangement.addTrack(name, kind);

            if (const auto* midiClipsArray = trackVar.getProperty("midiClips", juce::var()).getArray()) {
                for (const auto& clipVar : *midiClipsArray) {
                    model::MidiClip clip;
                    clip.setLengthTicks(static_cast<int64_t>(
                        static_cast<juce::int64>(clipVar.getProperty("lengthTicks", 0))));

                    if (const auto* notesArray = clipVar.getProperty("notes", juce::var()).getArray()) {
                        for (const auto& noteVar : *notesArray) {
                            model::Note note {
                                static_cast<int>(noteVar.getProperty("key", 60)),
                                static_cast<float>(static_cast<double>(noteVar.getProperty("velocity", 1.0))),
                                static_cast<int64_t>(static_cast<juce::int64>(noteVar.getProperty("startTick", 0))),
                                static_cast<int64_t>(static_cast<juce::int64>(noteVar.getProperty("lengthTicks", 0)))
                            };
                            clip.addNote(note);
                        }
                    }

                    const int64_t startTick = static_cast<int64_t>(
                        static_cast<juce::int64>(clipVar.getProperty("startTick", 0)));
                    const bool muted = static_cast<bool>(clipVar.getProperty("muted", false));
                    arrangement.addMidiClipPlacement(trackIndex, model::MidiClipPlacement { startTick, clip, muted });
                }
            }

            if (const auto* audioClipsArray = trackVar.getProperty("audioClips", juce::var()).getArray()) {
                for (const auto& clipVar : *audioClipsArray) {
                    const int64_t startTick = static_cast<int64_t>(
                        static_cast<juce::int64>(clipVar.getProperty("startTick", 0)));
                    const std::string sourcePath = clipVar.getProperty("sourcePath", juce::var()).toString().toStdString();

                    model::AudioClip clip;
                    clip.setSourcePath(sourcePath);
                    clip.setOriginalBpm(static_cast<double>(clipVar.getProperty("originalBpm", 0.0)));
                    clip.setWarpEnabled(static_cast<bool>(clipVar.getProperty("warpEnabled", false)));
                    const bool muted = static_cast<bool>(clipVar.getProperty("muted", false));
                    arrangement.addAudioClipPlacement(trackIndex, model::AudioClipPlacement { startTick, clip, muted });
                }
            }

            if (const auto* automationArray = trackVar.getProperty("automation", juce::var()).getArray()) {
                for (const auto& laneVar : *automationArray) {
                    model::AutomationLaneSlot laneSlot;
                    laneSlot.paramIndex = static_cast<int>(laneVar.getProperty("param", 0));

                    if (const auto* pointsArray = laneVar.getProperty("points", juce::var()).getArray()) {
                        for (const auto& pointVar : *pointsArray) {
                            laneSlot.lane.addPoint(model::AutomationPoint {
                                static_cast<int64_t>(static_cast<juce::int64>(pointVar.getProperty("tick", 0))),
                                static_cast<float>(static_cast<double>(pointVar.getProperty("value", 0.0)))
                            });
                        }
                    }

                    arrangement.track(trackIndex).automation.push_back(std::move(laneSlot));
                }
            }
        }
    }

    session = model::Session();

    const juce::var sessionVar = parsed.getProperty("session", juce::var());
    const int numScenes = static_cast<int>(sessionVar.getProperty("numScenes", 0));

    for (int s = 0; s < numScenes; ++s) {
        session.addScene();
    }

    const auto* sessionTracksArray = sessionVar.getProperty("tracks", juce::var()).getArray();

    for (std::size_t t = 0; t < arrangement.numTracks(); ++t) {
        session.addTrackColumn();

        if (sessionTracksArray == nullptr || static_cast<int>(t) >= sessionTracksArray->size()) {
            continue;
        }

        const auto* columnArray = (*sessionTracksArray)[static_cast<int>(t)].getArray();
        if (columnArray == nullptr) {
            continue;
        }

        for (std::size_t s = 0; s < session.numScenes() && static_cast<int>(s) < columnArray->size(); ++s) {
            const juce::var& slotVar = (*columnArray)[static_cast<int>(s)];
            if (slotVar.isVoid() || slotVar.isUndefined()) {
                continue;
            }

            const juce::String kind = slotVar.getProperty("kind", juce::var()).toString();
            model::ClipSlot& slot = session.slot(t, s);

            if (kind == "midi") {
                model::MidiClip clip;
                clip.setLengthTicks(static_cast<int64_t>(
                    static_cast<juce::int64>(slotVar.getProperty("lengthTicks", 0))));

                if (const auto* notesArray = slotVar.getProperty("notes", juce::var()).getArray()) {
                    for (const auto& noteVar : *notesArray) {
                        model::Note note {
                            static_cast<int>(noteVar.getProperty("key", 60)),
                            static_cast<float>(static_cast<double>(noteVar.getProperty("velocity", 1.0))),
                            static_cast<int64_t>(static_cast<juce::int64>(noteVar.getProperty("startTick", 0))),
                            static_cast<int64_t>(static_cast<juce::int64>(noteVar.getProperty("lengthTicks", 0)))
                        };
                        clip.addNote(note);
                    }
                }

                slot.content = model::SlotContent::Midi;
                slot.midiClip = clip;
            } else if (kind == "audio") {
                model::AudioClip clip;
                clip.setSourcePath(slotVar.getProperty("sourcePath", juce::var()).toString().toStdString());
                clip.setOriginalBpm(static_cast<double>(slotVar.getProperty("originalBpm", 0.0)));
                clip.setWarpEnabled(static_cast<bool>(slotVar.getProperty("warpEnabled", false)));

                slot.content = model::SlotContent::Audio;
                slot.audioClip = clip;
            }
        }
    }

    mixer.reset();
    mixer.prepare(arrangement.numTracks());

    if (const auto* busesArray = parsed.getProperty("buses", juce::var()).getArray()) {
        for (const auto& busVar : *busesArray) {
            const std::size_t busIndex = mixer.addBus(busVar.getProperty("name", juce::var()).toString().toStdString());
            applyStripVar(busVar.getProperty("strip", juce::var()), mixer.busStrip(busIndex), factory, pluginHost);
        }
    }

    if (const auto* tracksArray = parsed.getProperty("tracks", juce::var()).getArray()) {
        for (int i = 0; i < tracksArray->size(); ++i) {
            const juce::var& trackVar = (*tracksArray)[i];
            const auto trackIndex = static_cast<std::size_t>(i);

            applyStripVar(trackVar.getProperty("strip", juce::var()), mixer.trackStrip(trackIndex), factory, pluginHost);

            const int output = static_cast<int>(trackVar.getProperty("output", -1));
            mixer.setTrackOutput(trackIndex, output < 0 ? model::Mixer::kMaster : static_cast<std::size_t>(output));

            if (const auto* sendsArray = trackVar.getProperty("sends", juce::var()).getArray()) {
                for (const auto& sendVar : *sendsArray) {
                    model::Send send {
                        static_cast<std::size_t>(static_cast<int>(sendVar.getProperty("bus", 0))),
                        static_cast<float>(static_cast<double>(sendVar.getProperty("level", 1.0))),
                        static_cast<bool>(sendVar.getProperty("preFader", false))
                    };
                    mixer.addSend(trackIndex, send);
                }
            }
        }
    }

    applyStripVar(parsed.getProperty("masterStrip", juce::var()), mixer.masterStrip(), factory, pluginHost);

    mixer.updateLatencies();

    return true;
}

} // namespace howl::project
