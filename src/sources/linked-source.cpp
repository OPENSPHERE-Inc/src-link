/*
Source Link
Copyright (C) 2024 OPENSPHERE Inc. info@opensphere.co.jp

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>

#include "../plugin-support.h"
#include "linked-source.hpp"
#include "../utils.hpp"

//--- LinkedSource class ---//

LinkedSource::LinkedSource(obs_data_t *settings, obs_source_t *source, SourceLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      source(source),
      apiClient(_apiClient),
      connected(false),
      uuid(QString(obs_source_get_uuid(source))),
      port(0)
{
    obs_log(LOG_DEBUG, "%s: Source creating", obs_source_get_name(source));

    // Allocate port
    port = apiClient->getFreePort();

    // Register connection to server
    handleConnection(settings);

    // Composite decoder's settings
    auto decoderSettings = createDecoderSettings(settings);

    // Decoder source (SRT, RIST, etc.)
    decoderSource = obs_source_create_private("ffmpeg_source", obs_source_get_name(source), decoderSettings);
    obs_data_release(decoderSettings);

    // Audio handling
    obs_source_add_audio_capture_callback(
        decoderSource,
        // Just pass through the audio
        [](void *data, obs_source_t *, const audio_data *audioData, bool muted) {
            auto linkedSource = static_cast<LinkedSource *>(data);

            struct obs_source_audio destData;
            memcpy(destData.data, audioData->data, sizeof(audio_data::data));
            destData.frames = audioData->frames;
            destData.timestamp = audioData->timestamp;

            obs_source_output_audio(linkedSource->source, &destData);
        },
        this
    );

    connect(
        apiClient, SIGNAL(connectionPutSucceeded(StageConnection *)), this,
        SLOT(onConnectionPutSucceeded(StageConnection *))
    );
    connect(apiClient, SIGNAL(connectionPutFailed()), this, SLOT(onConnectionPutFailed()));

    obs_log(LOG_INFO, "%s: Source created", obs_source_get_name(source));
}

LinkedSource::~LinkedSource()
{
    obs_log(LOG_INFO, "%s: Source destroying", obs_source_get_name(source));

    apiClient->deleteConnection(uuid);
    apiClient->releasePort(port);

    obs_source_release(decoderSource);

    obs_log(LOG_INFO, "%s: Source destroyed", obs_source_get_name(source));
}

QString LinkedSource::compositeParameters(obs_data_t *settings, bool client)
{
    auto protocol = QString(obs_data_get_string(settings, "protocol"));
    QString parameters;

    if (protocol == QString("srt")) {
        // Generate SRT parameters
        auto mode = QString(obs_data_get_string(settings, "mode"));
        if (client) {
            // Invert mode for client
            if (mode == QString("listener")) {
                mode = QString("caller");
            } else if (mode == QString("caller")) {
                mode = QString("listener");
            }
        }

        parameters = QString("mode=%1&latency=%2&pbkeylen=%3&passphrase=%4&stream_id=%5")
                         .arg(mode)
                         .arg(obs_data_get_int(settings, "latency") * 1000) // Convert to microseconds
                         .arg(obs_data_get_int(settings, "pbkeylen"))
                         .arg(generatePassword(10, ""))
                         .arg(generatePassword(16, ""));
    }

    return parameters;
}

obs_data_t *LinkedSource::createDecoderSettings(obs_data_t *settings)
{
    auto protocol = QString(obs_data_get_string(settings, "protocol"));
    auto decoderSettings = obs_data_create();

    if (protocol == QString("srt") && port > 0) {
        QString parameters = compositeParameters(settings);
        auto input = QString("srt://0.0.0.0:%1?%2").arg(port).arg(parameters).toUtf8().constData();
        obs_data_set_string(decoderSettings, "input", input);
    } else {
        obs_data_set_string(decoderSettings, "input", "");
    }

    obs_data_set_int(decoderSettings, "reconnect_delay_sec", obs_data_get_int(settings, "reconnect_delay_sec"));
    obs_data_set_int(decoderSettings, "buffering_mb", obs_data_get_int(settings, "buffering_mb"));
    obs_data_set_bool(decoderSettings, "hw_decode", obs_data_get_bool(settings, "hw_decode"));
    obs_data_set_bool(decoderSettings, "clear_on_media_end", obs_data_get_bool(settings, "clear_on_media_end"));

    // Fixed parameters
    obs_data_set_string(decoderSettings, "input_format", "mpegts");
    obs_data_set_bool(decoderSettings, "is_local_file", false);

    return decoderSettings;
}

void LinkedSource::handleConnection(obs_data_t *settings)
{
    auto stageId = QString(obs_data_get_string(settings, "stage_id"));
    auto seatName = QString(obs_data_get_string(settings, "seat_name"));
    auto sourceName = QString(obs_data_get_string(settings, "source_name"));

    if (!stageId.isEmpty() && !seatName.isEmpty() && !sourceName.isEmpty()) {
        // Register connection to server
        auto protocol = QString(obs_data_get_string(settings, "protocol"));
        QString parameters = compositeParameters(settings, true);

        apiClient->putConnection(
            uuid, stageId, seatName, sourceName, protocol, port, parameters, obs_data_get_int(settings, "max_bitrate"),
            obs_data_get_int(settings, "min_bitrate"), obs_data_get_int(settings, "width"),
            obs_data_get_int(settings, "height")
        );
    } else {
        // Unregister connection if no stage/seat/source selected.
        apiClient->deleteConnection(uuid);
    }
}

obs_properties_t *LinkedSource::getProperties()
{
    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    obs_property_t *prop;

    // Stage list
    auto stageList = obs_properties_add_list(
        props, "stage_id", obs_module_text("Stage"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    foreach(auto stage, apiClient->getStages())
    {
        obs_property_list_add_string(
            stageList, stage->getName().toUtf8().constData(), stage->getId().toUtf8().constData()
        );
    }

    // Connection group
    auto connectionGroup = obs_properties_create();
    obs_properties_add_group(props, "connection", obs_module_text("Connection"), OBS_GROUP_NORMAL, connectionGroup);

    // Connection group -> Seat list
    auto seatList = obs_properties_add_list(
        connectionGroup, "seat_name", obs_module_text("Seat"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(seatList, obs_module_text("ChooseStageFirst"), "");

    // Connection group -> Source list
    auto sourceList = obs_properties_add_list(
        connectionGroup, "source_name", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(sourceList, obs_module_text("ChooseStageFirst"), "");

    // Stage change event -> Update connection group
    obs_property_set_modified_callback2(
        stageList,
        [](void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
            obs_log(LOG_DEBUG, "Stage has been changed.");

            auto apiClient = static_cast<SourceLinkApiClient *>(param);
            auto stageId = QString(obs_data_get_string(settings, "stage_id"));

            auto connectionGroup = obs_property_group_content(obs_properties_get(props, "connection"));
            obs_properties_remove_by_name(connectionGroup, "seat_name");
            obs_properties_remove_by_name(connectionGroup, "source_name");

            auto seatList = obs_properties_add_list(
                connectionGroup, "seat_name", obs_module_text("Seat"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
            );
            auto sourceList = obs_properties_add_list(
                connectionGroup, "source_name", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
            );

            if (!apiClient->getStages().size()) {
                obs_property_list_add_string(seatList, obs_module_text("ChooseStageFirst"), "");
                obs_property_list_add_string(sourceList, obs_module_text("ChooseStageFirst"), "");
                return true;
            }

            foreach(auto stage, apiClient->getStages())
            {
                if (stageId != stage->getId()) {
                    continue;
                }

                if (!stage->getSeats().size()) {
                    obs_property_list_add_string(seatList, obs_module_text("ChooseStageFirst"), "");
                }
                if (!stage->getSources().size()) {
                    obs_property_list_add_string(sourceList, obs_module_text("ChooseStageFirst"), "");
                }

                foreach(auto seat, stage->getSeats())
                {
                    obs_property_list_add_string(
                        seatList, seat.displayName.toUtf8().constData(), seat.name.toUtf8().constData()
                    );
                }
                foreach(auto source, stage->getSources())
                {
                    obs_property_list_add_string(
                        sourceList, source.displayName.toUtf8().constData(), source.name.toUtf8().constData()
                    );
                }

                break;
            }

            return true;
        },
        apiClient
    );

    // Other stage settings
    obs_properties_add_int(props, "max_bitrate", obs_module_text("MaxBitrate"), 0, 1000000000, 100);
    obs_properties_add_int(props, "min_bitrate", obs_module_text("MinBitrate"), 0, 1000000000, 100);
    obs_properties_add_int(props, "width", obs_module_text("SpecifiedWidth"), 0, 3840, 2);
    obs_properties_add_int(props, "height", obs_module_text("SpecifiedHeight"), 0, 2160, 2);

    // Private source settings
    obs_properties_add_int_slider(props, "reconnect_delay_sec", obs_module_text("ReconnectDelayTime"), 1, 60, 1);
    prop = obs_properties_add_int_slider(props, "buffering_mb", obs_module_text("BufferingMB"), 0, 16, 1);
    obs_property_int_set_suffix(prop, " MB");
    obs_properties_add_bool(props, "hw_decode", obs_module_text("HardwareDecode"));
    obs_properties_add_bool(props, "clear_on_media_end", obs_module_text("ClearOnMediaEnd"));

    // Protocol list
    auto protocolList = obs_properties_add_list(
        props, "protocol", obs_module_text("Protocol"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(protocolList, obs_module_text("SRT"), "srt");

    // Protocol setting group
    auto protocolSettingGroup = obs_properties_create();
    obs_properties_add_group(
        props, "protocol_settings", obs_module_text("ProtocolSettings"), OBS_GROUP_NORMAL, protocolSettingGroup
    );

    // Protocol change event -> Update protocol settings group
    obs_property_set_modified_callback2(
        protocolList,
        [](void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
            obs_log(LOG_DEBUG, "Protocol has been changed.");
            auto apiClient = static_cast<SourceLinkApiClient *>(param);
            auto protocol = QString(obs_data_get_string(settings, "protocol"));

            // Remove group's properties
            auto protocolSettingsGroup = obs_property_group_content(obs_properties_get(props, "protocol_settings"));
            for (auto prop = obs_properties_first(protocolSettingsGroup); prop; obs_property_next(&prop)) {
                obs_properties_remove_by_name(protocolSettingsGroup, obs_property_name(prop));
            }

            if (protocol == QString("srt")) {
                // mode list
                auto modeList = obs_properties_add_list(
                    protocolSettingsGroup, "mode", obs_module_text("Mode"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
                );
                obs_property_list_add_string(modeList, "listener", "listener");
                obs_property_list_add_string(modeList, "caller", "caller");
                obs_property_list_add_string(modeList, "rendezvous", "rendezvous");

                // Latency
                obs_properties_add_int(protocolSettingsGroup, "latency", obs_module_text("LatencyMsecs"), 0, 60000, 1);

                // pbkeylen list
                auto pbkeylenList = obs_properties_add_list(
                    protocolSettingsGroup, "pbkeylen", obs_module_text("PBKeyLen"), OBS_COMBO_TYPE_LIST,
                    OBS_COMBO_FORMAT_INT
                );
                obs_property_list_add_int(pbkeylenList, "16", 16);
                obs_property_list_add_int(pbkeylenList, "24", 24);
                obs_property_list_add_int(pbkeylenList, "32", 32);

                obs_properties_add_text(
                    protocolSettingsGroup, "passphrase", obs_module_text("PassphraseWillBeGenerated"), OBS_TEXT_INFO
                );
                obs_properties_add_text(
                    protocolSettingsGroup, "stream_id", obs_module_text("StreamIdWillBeGenerated"), OBS_TEXT_INFO
                );
            }

            return true;
        },
        apiClient
    );

    return props;
}

void LinkedSource::getDefault(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "Default settings applying.");

    obs_data_set_default_int(settings, "reconnect_delay_sec", 1);
    obs_data_set_default_int(settings, "buffering_mb", 1);
    obs_data_set_default_bool(settings, "hw_decode", true);
    obs_data_set_default_bool(settings, "clear_on_media_end", true);
    obs_data_set_default_int(settings, "max_bitrate", 8000);
    obs_data_set_default_int(settings, "min_bitrate", 4000);

    obs_data_set_default_string(settings, "protocol", "srt");
    obs_data_set_default_string(settings, "mode", "listener");
    obs_data_set_default_int(settings, "latency", 200);
    obs_data_set_default_int(settings, "pbkeylen", 16);

    obs_video_info ovi = {0};
    if (obs_get_video_info(&ovi)) {
        obs_data_set_default_int(settings, "width", ovi.base_width);
        obs_data_set_default_int(settings, "height", ovi.base_height);
    }

    obs_log(LOG_INFO, "Default settings applied.");
}

uint32_t LinkedSource::getWidth()
{
    return obs_source_get_width(decoderSource);
}

uint32_t LinkedSource::getHeight()
{
    return obs_source_get_height(decoderSource);
}

void LinkedSource::videoRenderCallback()
{
    // Just pass through the video
    obs_source_video_render(decoderSource);
}

void LinkedSource::update(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "%s: Source updating", obs_source_get_name(source));

    handleConnection(settings);

    auto decoderSettings = createDecoderSettings(settings);
    obs_source_update(decoderSource, decoderSettings);
    obs_data_release(decoderSettings);

    obs_log(LOG_INFO, "%s: Source updated", obs_source_get_name(source));
}

void LinkedSource::onConnectionPutSucceeded(StageConnection *connection)
{
    connected = true;
}

void LinkedSource::onConnectionPutFailed()
{
    connected = false;
}

//--- Source registration ---//

extern SourceLinkApiClient *apiClient;

void *createSource(obs_data_t *settings, obs_source_t *source)
{
    return new LinkedSource(settings, source, apiClient);
}

void destroySource(void *data)
{
    auto linkedSource = static_cast<LinkedSource *>(data);
    delete linkedSource;
}

obs_properties_t *getProperties(void *data)
{
    auto linkedSource = static_cast<LinkedSource *>(data);
    return linkedSource->getProperties();
}

void getDefault(void *data, obs_data_t *settings)
{
    auto linkedSource = static_cast<LinkedSource *>(data);
    linkedSource->getDefault(settings);
}

uint32_t getWidth(void *data)
{
    auto linkedSource = static_cast<LinkedSource *>(data);
    return linkedSource->getWidth();
}

uint32_t getHeight(void *data)
{
    auto linkedSource = static_cast<LinkedSource *>(data);
    return linkedSource->getHeight();
}

void videoRender(void *data, gs_effect_t *)
{
    auto linkedSource = static_cast<LinkedSource *>(data);
    linkedSource->videoRenderCallback();
}

void update(void *data, obs_data_t *settings)
{
    auto linkedSource = static_cast<LinkedSource *>(data);
    linkedSource->update(settings);
}

obs_source_info createLinkedSourceInfo()
{
    obs_source_info sourceInfo = {0};

    sourceInfo.id = "linked_source";
    sourceInfo.type = OBS_SOURCE_TYPE_INPUT;
    sourceInfo.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO;

    sourceInfo.get_name = [](void *unused) {
        return "Source Linked Source";
    };
    sourceInfo.create = createSource;
    sourceInfo.destroy = destroySource;
    sourceInfo.get_properties = getProperties;
    sourceInfo.get_defaults2 = getDefault;
    sourceInfo.get_width = getWidth;
    sourceInfo.get_height = getHeight;
    sourceInfo.video_render = videoRender;
    sourceInfo.update = update;

    return sourceInfo;
}