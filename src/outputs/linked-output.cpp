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
#include <obs-frontend-api.h>
#include <util/config-file.h>

#include "linked-output.hpp"
#include "../utils.hpp"

//--- LinkedOutput class ---//

LinkedOutput::LinkedOutput(const QString &_name, SourceLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      name(_name),
      apiClient(_apiClient),
      output(nullptr),
      service(nullptr),
      videoEncoder(nullptr),
      audioEncoder(nullptr),
      outputActive(false)
{
    obs_log(LOG_DEBUG, "%s: Output creating", qPrintable(name));

    loadSettings();
    apiClient->putSeatAllocation();

    obs_log(LOG_INFO, "%s: Output created", qPrintable(name));
}

LinkedOutput::~LinkedOutput()
{
    obs_log(LOG_DEBUG, "%s: Output destroying", qPrintable(name));

    obs_log(LOG_INFO, "%s: Output destroyed", qPrintable(name));
}

obs_properties_t *LinkedOutput::getProperties()
{
    obs_log(LOG_DEBUG, "%s: Properties creating", qPrintable(name));

    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    // "Connection" group
    auto connectionGroup = obs_properties_create();
    auto sourceNameList = obs_properties_add_list(
        connectionGroup, "source_name", obs_module_text("Type"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    if (apiClient->getSeat() && apiClient->getSeat()->getStage()) {
        foreach(auto source, apiClient->getSeat()->getStage()->getSources())
        {
            obs_property_list_add_string(sourceNameList, qPrintable(source.displayName), qPrintable(source.name));
        }
    }
    obs_properties_add_button2(
        connectionGroup, "reload", obs_module_text("Reload"),
        [](obs_properties_t *, obs_property_t *, void *param) {
            LinkedOutput *linkedOutput = static_cast<LinkedOutput *>(param);
            linkedOutput->apiClient->requestSeatAllocation();
            return true;
        },
        this
    );
    obs_properties_add_group(props, "connection_group", obs_module_text("Connection"), OBS_GROUP_NORMAL, connectionGroup);

    // "Audio Encoder" group
    auto audioEncoderGroup = obs_properties_create();
    auto audioEncoderList = obs_properties_add_list(
        audioEncoderGroup, "audio_encoder", obs_module_text("AudioEncoder"), OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );
    // The bitrate list is empty initially.
    obs_properties_add_list(
        audioEncoderGroup, "audio_bitrate", obs_module_text("AudioBitrate"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT
    );
    obs_properties_add_group(
        props, "audio_encoder_group", obs_module_text("AudioEncoder"), OBS_GROUP_NORMAL, audioEncoderGroup
    );

    // "Video Encoder" group
    auto videoEncoderGroup = obs_properties_create();
    auto videoEncoderList = obs_properties_add_list(
        videoEncoderGroup, "video_encoder", obs_module_text("VideoEncoder"), OBS_COMBO_TYPE_LIST,
        OBS_COMBO_FORMAT_STRING
    );
    obs_properties_add_group(
        props, "video_encoder_group", obs_module_text("VideoEncoder"), OBS_GROUP_NORMAL, videoEncoderGroup
    );

    // Enum audio and video encoders
    const char *encoderId = nullptr;
    size_t i = 0;
    while (obs_enum_encoder_types(i++, &encoderId)) {
        auto caps = obs_get_encoder_caps(encoderId);
        if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL)) {
            // Ignore deprecated and internal
            continue;
        }

        auto name = obs_encoder_get_display_name(encoderId);

        if (obs_get_encoder_type(encoderId) == OBS_ENCODER_VIDEO) {
            obs_property_list_add_string(videoEncoderList, name, encoderId);
        } else if (obs_get_encoder_type(encoderId) == OBS_ENCODER_AUDIO) {
            obs_property_list_add_string(audioEncoderList, name, encoderId);
        }
    }

    obs_property_set_modified_callback2(
        audioEncoderList,
        [](void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
            auto linkedOutput = static_cast<LinkedOutput *>(param);
            obs_log(LOG_DEBUG, "%s: Audio encoder chainging", qPrintable(linkedOutput->getName()));

            const auto encoderId = obs_data_get_string(settings, "audio_encoder");
            const auto encoderProps = obs_get_encoder_properties(encoderId);
            const auto encoderBitrateProp = obs_properties_get(encoderProps, "bitrate");
            obs_properties_destroy(encoderProps);

            auto audioEncoderGroup = obs_property_group_content(obs_properties_get(props, "audio_encoder_group"));
            auto audioBitrateProp = obs_properties_get(audioEncoderGroup, "audio_bitrate");

            obs_property_list_clear(audioBitrateProp);

            const auto type = obs_property_get_type(encoderBitrateProp);
            auto result = true;
            switch (type) {
            case OBS_PROPERTY_INT: {
                const auto max_value = obs_property_int_max(encoderBitrateProp);
                const auto step_value = obs_property_int_step(encoderBitrateProp);

                for (int i = obs_property_int_min(encoderBitrateProp); i <= max_value; i += step_value) {
                    char bitrateTitle[6];
                    snprintf(bitrateTitle, sizeof(bitrateTitle), "%d", i);
                    obs_property_list_add_int(audioBitrateProp, bitrateTitle, i);
                }

                break;
            }

            case OBS_PROPERTY_LIST: {
                const auto format = obs_property_list_format(encoderBitrateProp);
                if (format != OBS_COMBO_FORMAT_INT) {
                    obs_log(
                        LOG_ERROR, "%s: Invalid bitrate property given by encoder: %s",
                        qPrintable(linkedOutput->getName()), encoderId
                    );
                    result = false;
                    break;
                }

                const auto count = obs_property_list_item_count(encoderBitrateProp);
                for (size_t i = 0; i < count; i++) {
                    if (obs_property_list_item_disabled(encoderBitrateProp, i)) {
                        continue;
                    }
                    const auto bitrate = obs_property_list_item_int(encoderBitrateProp, i);
                    char bitrateTitle[6];
                    snprintf(bitrateTitle, sizeof(bitrateTitle), "%lld", bitrate);
                    obs_property_list_add_int(audioBitrateProp, bitrateTitle, bitrate);
                }
                break;
            }

            default:
                break;
            }

            obs_log(LOG_INFO, "%s: Audio encoder changed", qPrintable(linkedOutput->getName()));
            return result;
        },
        this
    );

    obs_property_set_modified_callback2(
        videoEncoderList,
        [](void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
            auto linkedOutput = static_cast<LinkedOutput *>(param);
            obs_log(LOG_DEBUG, "%s: Video encoder chainging", qPrintable(linkedOutput->getName()));

            auto videoEncoderGroup = obs_property_group_content(obs_properties_get(props, "video_encoder_group"));
            auto encoderId = obs_data_get_string(settings, "video_encoder");

            obs_properties_remove_by_name(videoEncoderGroup, "video_encoder_settings_group");

            auto encoderProps = obs_get_encoder_properties(encoderId);
            if (encoderProps) {
                obs_properties_add_group(
                    videoEncoderGroup, "video_encoder_settings_group", obs_encoder_get_display_name(encoderId),
                    OBS_GROUP_NORMAL, encoderProps
                );
            }

            // Apply encoder's defaults
            auto encoderDefaults = obs_encoder_defaults(encoderId);
            applyDefaults(settings, encoderDefaults);
            obs_data_release(encoderDefaults);

            obs_log(LOG_INFO, "%s: Video encoder changed", qPrintable(linkedOutput->getName()));
            return true;
        },
        this
    );

    obs_log(LOG_INFO, "%s: Properties created", qPrintable(name));
    return props;
}

void LinkedOutput::getDefault(OBSData defaults)
{
    obs_log(LOG_DEBUG, "%s: Default settings applying", qPrintable(name));

    auto config = obs_frontend_get_profile_config();
    auto mode = config_get_string(config, "Output", "Mode");
    bool advanced_out = strcmp(mode, "Advanced") == 0 || strcmp(mode, "advanced");

    const char *videoEncoderId;
    uint64_t videoBitrate;
    const char *audioEncoderId;
    uint64_t audioBitrate;
    if (advanced_out) {
        videoEncoderId = config_get_string(config, "AdvOut", "Encoder");
        videoBitrate = config_get_uint(config, "AdvOut", "FFVBitrate");
        audioEncoderId = config_get_string(config, "AdvOut", "AudioEncoder");
        audioBitrate = config_get_uint(config, "AdvOut", "FFABitrate");
    } else {
        videoEncoderId = getSimpleVideoEncoder(config_get_string(config, "SimpleOutput", "StreamEncoder"));
        videoBitrate = config_get_uint(config, "SimpleOutput", "VBitrate");
        audioEncoderId = getSimpleAudioEncoder(config_get_string(config, "SimpleOutput", "StreamAudioEncoder"));
        audioBitrate = config_get_uint(config, "SimpleOutput", "ABitrate");
    }
    obs_data_set_default_string(defaults, "video_encoder", videoEncoderId);
    obs_data_set_default_int(defaults, "video_bitrate", videoBitrate);
    obs_data_set_default_string(defaults, "audio_encoder", audioEncoderId);
    obs_data_set_default_int(defaults, "audio_bitrate", audioBitrate);

    obs_log(LOG_INFO, "%s: Default settings applied", qPrintable(name));
}

void LinkedOutput::update(OBSData newSettings)
{
    obs_log(LOG_DEBUG, "%s: Output updating", qPrintable(name));

    // Apply default first
    obs_data_apply(settings, obs_data_get_defaults(newSettings));
    // Apply new settings
    obs_data_apply(settings, newSettings);
    // Save settings to permanent storage
    saveSettings();

    obs_log(LOG_INFO, "%s: Output updated", qPrintable(name));
}

void LinkedOutput::loadSettings()
{
    auto data = obs_data_create();
    settings = OBSData(data);
    obs_data_release(data);

    // Apply defaults
    getDefault(settings);

    // Load settings from json
    auto path = obs_module_get_config_path(obs_current_module(), qPrintable(QString("%1.json").arg(name)));
    data = obs_data_create_from_json_file(path);
    bfree(path);

    if (data) {
        obs_data_apply(settings, data);
        obs_data_release(data);
    } 
}

void LinkedOutput::saveSettings()
{
    // Save settings to json file
    auto path = obs_module_get_config_path(obs_current_module(), qPrintable(QString("%1.json").arg(name)));
    obs_data_save_json_safe(settings, path, "tmp", "bak");
    bfree(path);
}

OBSData LinkedOutput::createEgressSettings()
{
    if (!connection) {
        return nullptr;
    }

    auto data = obs_data_create();
    auto egressSettings = OBSData(data);
    obs_data_release(data);

    obs_data_apply(egressSettings, settings);

    if (connection->getProtocol() == QString("srt")) {
        auto server = QString("srt://%1:%2?%3")
                          .arg(connection->getServer())
                          .arg(connection->getPort())
                          .arg(connection->getParameters());
        obs_data_set_string(egressSettings, "server", qPrintable(server));
    } else {
        return nullptr;
    }

    // Limit bitrate
    auto bitrate = obs_data_get_int(egressSettings, "bitrate");
    if (bitrate > connection->getMaxBitrate()) {
        obs_data_set_int(egressSettings, "bitrate", connection->getMaxBitrate());
    } else if (bitrate < connection->getMinBitrate()) {
        obs_data_set_int(egressSettings, "bitrate", connection->getMinBitrate());
    }

    return egressSettings;
}

void LinkedOutput::startOutput(video_t *video, audio_t *audio)
{
    // Force free resources
    stopOutput();

    // Retrieve connection
    connection = nullptr;
    auto sourceName = QString(obs_data_get_string(settings, "source_name"));
    if (sourceName.isEmpty()) {
        return;
    }

    // Find connection specified by source_name
    foreach(const auto c, apiClient->getSeat()->getConnections())
    {
        if (c->getSourceName() == sourceName) {
            connection = c;
            break;
        }
    }

    if (!connection) {
        obs_log(LOG_WARNING, "No active connection for %s (source=%s)", qPrintable(name), qPrintable(sourceName));
        return;
    }

    auto egressSettings = createEgressSettings();
    if (!egressSettings) {
        obs_log(LOG_ERROR, "Unsupported connection for %s", qPrintable(name));
        return;
    }

    // Service : always use rtmp_custom
    service = obs_service_create("rtmp_custom", qPrintable(name), egressSettings, nullptr);
    if (!service) {
        obs_log(LOG_ERROR, "Failed to create service %s", qPrintable(name));
        obs_data_release(egressSettings);
        return;
    }

    // Output : always use ffmpeg_mpegts_muxer
    output = obs_output_create("ffmpeg_mpegts_muxer", qPrintable(name), egressSettings, nullptr);
    if (!output) {
        obs_log(LOG_ERROR, "Failed to create output %s", qPrintable(name));
        obs_data_release(egressSettings);
        return;
    }

    obs_output_set_reconnect_settings(output, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
    obs_output_set_service(output, service);

    // Setup video encoder
    auto videoEncoderId = obs_data_get_string(egressSettings, "video_encoder");
    obs_log(LOG_DEBUG, "Video encoder: %s", videoEncoderId);
    videoEncoder = obs_video_encoder_create(videoEncoderId, qPrintable(name), egressSettings, nullptr);
    if (!videoEncoder) {
        obs_log(LOG_ERROR, "Failed to create video encoder %s for %s", videoEncoderId, qPrintable(name));
        obs_data_release(egressSettings);
        return;
    }

    // Scale to connection's resolution
    // TODO: Keep aspect ratio?
    obs_encoder_set_scaled_size(videoEncoder, connection->getWidth(), connection->getHeight());
    obs_encoder_set_gpu_scale_type(videoEncoder, OBS_SCALE_LANCZOS);
    obs_encoder_set_video(videoEncoder, video);
    obs_output_set_video_encoder(output, videoEncoder);

    // Setup audio encoder
    auto audioEncoderId = obs_data_get_string(egressSettings, "audio_encoder");
    obs_log(LOG_DEBUG, "Audio encoder: %s", audioEncoderId);
    auto audioBitrate = obs_data_get_int(egressSettings, "audio_bitrate");
    auto audioTrack = obs_data_get_int(egressSettings, "audio_track");
    auto audioEncoderSettings = obs_encoder_defaults(audioEncoderId);
    obs_data_set_int(audioEncoderSettings, "bitrate", audioBitrate);

    audioEncoder =
        obs_audio_encoder_create(audioEncoderId, qPrintable(name), audioEncoderSettings, audioTrack, nullptr);
    obs_data_release(audioEncoderSettings);
    obs_data_release(egressSettings);
    if (!audioEncoder) {
        obs_log(LOG_ERROR, "Failed to create audio encoder %s for %s", audioEncoderId, qPrintable(name));
        return;
    }

    obs_encoder_set_audio(audioEncoder, audio);
    obs_output_set_audio_encoder(output, audioEncoder, audioTrack);

    // Start output
    if (!obs_output_start(output)) {
        obs_log(LOG_ERROR, "Failed to start output %s", qPrintable(name));
        return;
    }

    outputActive = true;
    obs_log(LOG_INFO, "Started output %s", qPrintable(name));
}

void LinkedOutput::stopOutput()
{
    if (output) {
        if (outputActive) {
            obs_output_stop(output);
        }

        obs_output_release(output);
        output = nullptr;
    }

    if (service) {
        obs_service_release(service);
        service = nullptr;
    }

    if (audioEncoder) {
        obs_encoder_release(audioEncoder);
        audioEncoder = nullptr;
    }

    if (videoEncoder) {
        obs_encoder_release(videoEncoder);
        videoEncoder = nullptr;
    }

    if (outputActive) {
        outputActive = false;
        obs_log(LOG_INFO, "Stopped output %s", qPrintable(name));
    }
}
