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

//--- LinkedSource class ---//

LinkedSource::LinkedSource(obs_data_t *settings, obs_source_t *source, SourceLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      source(source),
      apiClient(_apiClient),
      connected(false),
      uuid(QString::fromUtf8(obs_source_get_uuid(source)))
{
    obs_log(LOG_DEBUG, "LinkedSource created");

    decoderSettings = obs_data_create();

    // Fixed parameters
    obs_data_set_string(decoderSettings, "input_format", "mpegts");
    obs_data_set_bool(decoderSettings, "is_local_file", false);

    // Supplied from UI
    applyDecoderSettings(settings);

    // Decoder source (SRT, RIST, etc.)
    decoderSource = obs_source_create_private("ffmpeg_source", obs_source_get_name(source), decoderSettings);

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

    applyConnection(settings);
}

LinkedSource::~LinkedSource()
{
    if (connected) {
        apiClient->deleteConnection(uuid);
    }

    obs_source_release(decoderSource);
    obs_data_release(decoderSettings);
    obs_log(LOG_DEBUG, "LinkedSource destroyed");
}

void LinkedSource::applyDecoderSettings(obs_data_t *settings)
{
    obs_data_set_string(decoderSettings, "input", obs_data_get_string(settings, "url"));
    obs_data_set_int(decoderSettings, "reconnect_delay_sec", obs_data_get_int(settings, "reconnect_delay_sec"));
    obs_data_set_int(decoderSettings, "buffering_mb", obs_data_get_int(settings, "buffering_mb"));
    obs_data_set_bool(decoderSettings, "hw_decode", obs_data_get_bool(settings, "hw_decode"));
    obs_data_set_bool(decoderSettings, "clear_on_media_end", obs_data_get_bool(settings, "clear_on_media_end"));
}

void LinkedSource::applyConnection(obs_data_t *settings)
{
    // Register connection to server
    auto stageId = obs_data_get_string(settings, "stage_id");
    auto seatName = obs_data_get_string(settings, "seat_name");
    auto sourceName = obs_data_get_string(settings, "source_name");
    if (stageId != nullptr && seatName != nullptr && sourceName != nullptr) {
        apiClient->putConnection(
            uuid,
            QString::fromUtf8(stageId),
            QString::fromUtf8(seatName),
            QString::fromUtf8(sourceName),
            QString::fromUtf8("srt"),
            10001,
            "",
            obs_data_get_int(settings, "max_bitrate"),
            obs_data_get_int(settings, "min_bitrate"),
            obs_data_get_int(settings, "width"),
            obs_data_get_int(settings, "height")
        );
    } else {
        apiClient->deleteConnection(uuid);
    }
}

obs_properties_t *LinkedSource::getProperties()
{
    auto props = obs_properties_create();
    obs_property_t *prop;

    auto stageList =
        obs_properties_add_list(props, "stage", obs_module_text("Stage"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    foreach(auto stage, apiClient->getStages())
    {
        obs_property_list_add_string(
            stageList, stage->getName().toUtf8().constData(), stage->getId().toUtf8().constData()
        );
    }

    auto connectionGroup = obs_properties_create();
    obs_properties_add_group(props, "connection", obs_module_text("Connection"), OBS_GROUP_NORMAL, connectionGroup);

    auto seatList = obs_properties_add_list(
        connectionGroup, "seat", obs_module_text("Seat"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(seatList, obs_module_text("ChooseStageFirst"), "");

    auto sourceList = obs_properties_add_list(
        connectionGroup, "source", obs_module_text("Source"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(sourceList, obs_module_text("ChooseStageFirst"), "");

    obs_property_set_modified_callback2(
        stageList,
        [](void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
            auto apiClient = static_cast<SourceLinkApiClient *>(param);
            auto stageId = obs_data_get_string(settings, "stage");

            auto connectionGroup = obs_property_group_content(obs_properties_get(props, "connection"));
            obs_properties_remove_by_name(connectionGroup, "seat");
            obs_properties_remove_by_name(connectionGroup, "source");

            auto seatList = obs_properties_add_list(
                connectionGroup, "seat", obs_module_text("Seat"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
            );
            auto sourceList = obs_properties_add_list(
                connectionGroup, "seat", obs_module_text("Seat"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
            );

            if (!apiClient->getStages().size()) {
                obs_property_list_add_string(seatList, obs_module_text("ChooseStageFirst"), "");
                obs_property_list_add_string(sourceList, obs_module_text("ChooseStageFirst"), "");
                return true;
            }

            foreach(auto stage, apiClient->getStages())
            {
                if (stage->getId() != stageId) {
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

    obs_properties_add_int(props, "max_bitrate", obs_module_text("MaxBitrate"), 0, 1000000000, 100);
    obs_properties_add_int(props, "min_bitrate", obs_module_text("MinBitrate"), 0, 1000000000, 100);
    obs_properties_add_int(props, "width", obs_module_text("SpecifiedWidth"), 0, 3840, 2);
    obs_properties_add_int(props, "height", obs_module_text("SpecifiedHeight"), 0, 2160, 2);

    obs_properties_add_int_slider(props, "reconnect_delay_sec", obs_module_text("ReconnectDelayTime"), 1, 60, 1);
    prop = obs_properties_add_int_slider(props, "buffering_mb", obs_module_text("BufferingMB"), 0, 16, 1);
    obs_property_int_set_suffix(prop, " MB");
    obs_properties_add_bool(props, "hw_decode", obs_module_text("HardwareDecode"));
    obs_properties_add_bool(props, "clear_on_media_end", obs_module_text("ClearOnMediaEnd"));

    return props;
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
    applyDecoderSettings(settings);
    obs_source_update(decoderSource, decoderSettings);

    applyConnection(settings);
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
    linkedSource->deleteLater();
}

obs_properties_t *getProperties(void *data)
{
    auto linkedSource = static_cast<LinkedSource *>(data);
    return linkedSource->getProperties();
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
    sourceInfo.get_width = getWidth;
    sourceInfo.get_height = getHeight;
    sourceInfo.video_render = videoRender;

    return sourceInfo;
}