/*
SRC-Link
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

#include "egress-link-output.hpp"
#include "audio-source.hpp"

#define OUTPUT_MAX_RETRIES 3
#define OUTPUT_RETRY_DELAY_SECS 1
#define OUTPUT_JSON_NAME "output.json"
#define OUTPUT_MONITORING_INTERVAL_MSECS 1000
#define OUTPUT_RETRY_TIMEOUT_MSECS 5000
#define OUTPUT_SCREENSHOT_HEIGHT 720

inline audio_t *createSilenceAudio()
{
    obs_audio_info ai = {0};
    if (!obs_get_audio_info(&ai)) {
        return nullptr;
    }

    audio_output_info aoi = {0};
    aoi.name = "Silence";
    aoi.samples_per_sec = ai.samples_per_sec;
    aoi.speakers = ai.speakers;
    aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
    aoi.input_param = nullptr;
    aoi.input_callback = [](void *param, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t mixers,
                            audio_output_data *mixes) {
        *outTs = startTsIn;
        return true;
    };

    audio_t *audio = nullptr;
    audio_output_open(&audio, &aoi);
    return audio;
}

inline bool isSourceAvailable(obs_source_t *source)
{
    auto width = obs_source_get_width(source);
    auto height = obs_source_get_height(source);
    if (width == 0 || height == 0) {
        return false;
    }

    auto found = !!obs_scene_from_source(source);
    if (found) {
        return true;
    }

    obs_frontend_source_list scenes = {0};
    obs_frontend_get_scenes(&scenes);

    for (size_t i = 0; i < scenes.sources.num && !found; i++) {
        obs_scene_t *scene = obs_scene_from_source(scenes.sources.array[i]);
        found = !!obs_scene_find_source_recursive(scene, obs_source_get_name(source));
    }

    obs_frontend_source_list_free(&scenes);

    return found;
}

//--- EgressLinkOutput class ---//

EgressLinkOutput::EgressLinkOutput(const QString &_name, SRLinkApiClient *_apiClient)
    : QObject(_apiClient),
      name(_name),
      apiClient(_apiClient),
      output(nullptr),
      service(nullptr),
      videoEncoder(nullptr),
      audioEncoder(nullptr),
      audioSource(nullptr),
      sourceView(nullptr),
      sourceVideo(nullptr),
      source(nullptr),
      settings(nullptr),
      storedSettingsRev(0),
      activeSettingsRev(0),
      status(EGRESS_LINK_OUTPUT_STATUS_INACTIVE)
{
    obs_log(LOG_DEBUG, "%s: Output creating", qUtf8Printable(name));

    loadSettings();

    snapshotTimer = new QTimer(this);
    snapshotTimer->setInterval(apiClient->getSettings()->getEgressScreenshotInterval() * 1000);
    snapshotTimer->start();
    connect(snapshotTimer, SIGNAL(timeout()), this, SLOT(onSnapshotTimerTimeout()));

    monitoringTimer = new QTimer(this);
    monitoringTimer->setInterval(OUTPUT_MONITORING_INTERVAL_MSECS);
    monitoringTimer->start();
    connect(monitoringTimer, SIGNAL(timeout()), this, SLOT(onMonitoringTimerTimeout()));

    connect(apiClient, SIGNAL(uplinkReady(const UplinkInfo &)), this, SLOT(onUplinkReady(const UplinkInfo &)));
    connect(apiClient, &SRLinkApiClient::egressRefreshNeeded, this, [this]() { refresh(); });

    obs_frontend_add_event_callback(onOBSFrontendEvent, this);

    obs_log(LOG_INFO, "%s: Output created", qUtf8Printable(name));
}

EgressLinkOutput::~EgressLinkOutput()
{
    obs_log(LOG_DEBUG, "%s: Output destroying", qUtf8Printable(name));

    disconnect(this);

    stop();

    obs_frontend_remove_event_callback(onOBSFrontendEvent, this);

    obs_log(LOG_INFO, "%s: Output destroyed", qUtf8Printable(name));
}

void EgressLinkOutput::onOBSFrontendEvent(enum obs_frontend_event event, void *param)
{
    auto output = (EgressLinkOutput *)param;
    // Force stop on shutdown
    if (event == OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN) {
        output->stop();
    }
}

void EgressLinkOutput::refresh()
{
    snapshotTimer->setInterval(apiClient->getSettings()->getEgressScreenshotInterval() * 1000);
}

obs_properties_t *EgressLinkOutput::getProperties()
{
    obs_log(LOG_DEBUG, "%s: Properties creating", qUtf8Printable(name));

    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    //--- "Audio Encoder" group ---//
    auto audioEncoderGroup = obs_properties_create();

    // Custom audio source
    auto audioSourceList = obs_properties_add_list(
        audioEncoderGroup, "audio_source", obs_module_text("AudioSource"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(audioSourceList, obs_module_text("NoAudio"), "no_audio");
    obs_property_list_add_string(audioSourceList, obs_module_text("DefaultAudio"), "");
    obs_property_list_add_string(audioSourceList, obs_module_text("MasterTrack"), "master_track");

    obs_enum_sources(
        [](void *param, obs_source_t *_source) {
            auto prop = (obs_property_t *)param;
            const auto flags = obs_source_get_output_flags(_source);
            if (flags & OBS_SOURCE_AUDIO) {
                obs_property_list_add_string(prop, obs_source_get_name(_source), obs_source_get_uuid(_source));
            }
            return true;
        },
        audioSourceList
    );
    obs_property_set_modified_callback2(
        audioSourceList,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *settings) {
            auto output = static_cast<EgressLinkOutput *>(param);
            auto audioSource = obs_data_get_string(settings, "audio_source");
            obs_property_set_enabled(
                obs_properties_get(_props, "audio_track"),
                (!strlen(audioSource) && output->getSourceUuid().isEmpty()) || !strcmp(audioSource, "master_track")
            );
            return true;
        },
        this
    );

    // Audio tracks have separate combo box
    auto audioTrackList = obs_properties_add_list(
        audioEncoderGroup, "audio_track", obs_module_text("Track"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT
    );
    for (int i = 1; i <= MAX_AUDIO_MIXES; i++) {
        char trackNo[] = "Track1";
        snprintf(trackNo, sizeof(trackNo), "Track%d", i);
        obs_property_list_add_int(audioTrackList, obs_module_text(trackNo), i);
    }
    obs_property_set_enabled(audioTrackList, false); // Initially disabled

    // Audio encoder list
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

    //--- "Video Encoder" group ---//
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
            auto output = static_cast<EgressLinkOutput *>(param);
            obs_log(LOG_DEBUG, "%s: Audio encoder chainging", qUtf8Printable(output->getName()));

            const auto encoderId = obs_data_get_string(settings, "audio_encoder");
            const OBSProperties encoderProps = obs_get_encoder_properties(encoderId);
            const auto encoderBitrateProp = obs_properties_get(encoderProps, "bitrate");

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
                        qUtf8Printable(output->getName()), encoderId
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

            obs_log(LOG_DEBUG, "%s: Audio encoder changed", qUtf8Printable(output->getName()));
            return result;
        },
        this
    );

    obs_property_set_modified_callback2(
        videoEncoderList,
        [](void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
            auto output = static_cast<EgressLinkOutput *>(param);
            obs_log(LOG_DEBUG, "%s: Video encoder chainging", qUtf8Printable(output->getName()));

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
            OBSDataAutoRelease encoderDefaults = obs_encoder_defaults(encoderId);
            applyDefaults(settings, encoderDefaults);

            obs_log(LOG_DEBUG, "%s: Video encoder changed", qUtf8Printable(output->getName()));
            return true;
        },
        this
    );

    obs_log(LOG_DEBUG, "%s: Properties created", qUtf8Printable(name));
    return props;
}

void EgressLinkOutput::getDefaults(obs_data_t *defaults)
{
    obs_log(LOG_DEBUG, "%s: Default settings applying", qUtf8Printable(name));

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
    obs_data_set_default_int(defaults, "bitrate", videoBitrate);
    obs_data_set_default_string(defaults, "audio_encoder", audioEncoderId);
    obs_data_set_default_int(defaults, "audio_bitrate", audioBitrate);

    obs_data_set_default_string(defaults, "audio_source", "");
    obs_data_set_default_bool(defaults, "visible", true);

    obs_log(LOG_DEBUG, "%s: Default settings applied", qUtf8Printable(name));
}

void EgressLinkOutput::update(obs_data_t *newSettings)
{
    obs_log(LOG_DEBUG, "%s: Output updating", qUtf8Printable(name));

    // Apply default first
    OBSDataAutoRelease defaults = obs_data_get_defaults(newSettings);
    obs_data_apply(settings, defaults);
    // Apply new settings
    obs_data_apply(settings, newSettings);
    // Save settings to permanent storage
    saveSettings();

    // Increment revision to restart output
    storedSettingsRev++;

    obs_log(LOG_INFO, "%s: Output updated", qUtf8Printable(name));
}

void EgressLinkOutput::setSourceUuid(const QString &value)
{
    if (value == obs_data_get_string(settings, "source_uuid")) {
        return;
    }

    obs_log(LOG_INFO, "%s: Source changed: %s", qUtf8Printable(name), qUtf8Printable(value));

    obs_data_set_string(settings, "source_uuid", qUtf8Printable(value));
    saveSettings();

    // Increment revision to restart output
    storedSettingsRev++;
}

void EgressLinkOutput::loadSettings()
{
    settings = obs_data_create();

    // Apply defaults
    getDefaults(settings);

    // Load settings from json
    OBSString path = obs_module_get_config_path(obs_current_module(), qUtf8Printable(QString("%1.json").arg(name)));
    OBSDataAutoRelease data = obs_data_create_from_json_file(path);

    if (data) {
        obs_data_apply(settings, data);

        // Increment revision to restart output
        storedSettingsRev++;
    }
}

void EgressLinkOutput::saveSettings()
{
    // Save settings to json file
    OBSString path = obs_module_get_config_path(obs_current_module(), qUtf8Printable(QString("%1.json").arg(name)));
    obs_data_save_json_safe(settings, path, "tmp", "bak");
}

obs_data_t *EgressLinkOutput::createEgressSettings(const StageConnection &connection)
{
    obs_data_t *egressSettings = obs_data_create();
    obs_data_apply(egressSettings, settings);

    if (connection.getProtocol() == "srt") {
        QString server;
        if (connection.getRelay()) {
            // FIXME: Currently encryption not supported !
            server = QString("srt://%1:%2?mode=caller&%3"
                             "streamid=publish/%4/%5");
        } else {
            server = QString("srt://%1:%2?mode=caller&%3"
                             "streamid=%4&passphrase=%5");
        }
        server = server.arg(connection.getServer())
                     .arg(connection.getPort())
                     .arg(connection.getParameters() + (connection.getParameters().isEmpty() ? "" : "&"))
                     .arg(connection.getStreamId())
                     .arg(connection.getPassphrase());

        obs_log(LOG_DEBUG, "%s: SRT server is %s", qUtf8Printable(name), qUtf8Printable(server));
        obs_data_set_string(egressSettings, "server", qUtf8Printable(server));
    } else {
        return nullptr;
    }

    // Limit bitrate
    auto bitrate = obs_data_get_int(egressSettings, "bitrate");
    if (bitrate > connection.getMaxBitrate()) {
        obs_data_set_int(egressSettings, "bitrate", connection.getMaxBitrate());
    } else if (bitrate < connection.getMinBitrate()) {
        obs_data_set_int(egressSettings, "bitrate", connection.getMinBitrate());
    }

    return egressSettings;
}

void EgressLinkOutput::start()
{
    // Force free resources
    releaseResources();

    QMutexLocker locker(&outputMutex);
    auto innerFunc = [&]() {
        if (status != EGRESS_LINK_OUTPUT_STATUS_INACTIVE) {
            return;
        }

        activeSettingsRev = storedSettingsRev;
        activeSourceUuid = obs_data_get_string(settings, "source_uuid");
        if (activeSourceUuid == "disabled" || !obs_data_get_bool(settings, "visible")) {
            setStatus(EGRESS_LINK_OUTPUT_STATUS_DISABLED);
            return;
        }

        if (!activeSourceUuid.isEmpty()) {
            // Get reference for specific source
            source = obs_get_source_by_uuid(qUtf8Printable(activeSourceUuid));
            if (!source) {
                obs_log(LOG_ERROR, "%s: Source not found: %s", qUtf8Printable(name), qUtf8Printable(activeSourceUuid));
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }
            // DO NOT use obs_source_inc_active() because source's audio will be mixed in main output unexpectedly.
            obs_source_inc_showing(source);

            obs_log(
                LOG_DEBUG, "%s: Video source is %s", qUtf8Printable(name), qUtf8Printable(obs_source_get_name(source))
            );
        }

        // Retrieve connection
        // Find connection specified by name
        connection = StageConnection();
        foreach (const auto &c, apiClient->getUplink().getConnections().values()) {
            if (c.getSourceName() == name) {
                connection = c;
                break;
            }
        }

        if (connection.isEmpty()) {
            setStatus(EGRESS_LINK_OUTPUT_STATUS_STAND_BY);
            apiClient->incrementStandByOutputs();
            return;
        }

        // Reduce reconnect with timeout
        connectionAttemptingAt = QDateTime().currentMSecsSinceEpoch();

        width = connection.getWidth();
        height = connection.getHeight();

        OBSDataAutoRelease egressSettings = createEgressSettings(connection);
        if (!egressSettings) {
            obs_log(LOG_ERROR, "%s: Unsupported connection for", qUtf8Printable(name));
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }

        // Service : always use rtmp_custom
        service = obs_service_create("rtmp_custom", qUtf8Printable(name), egressSettings, nullptr);
        if (!service) {
            obs_log(LOG_ERROR, "%s: Failed to create service", qUtf8Printable(name));
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }

        // Output : always use ffmpeg_mpegts_muxer
        output = obs_output_create("ffmpeg_mpegts_muxer", qUtf8Printable(name), egressSettings, nullptr);
        if (!output) {
            obs_log(LOG_ERROR, "%s: Failed to create output", qUtf8Printable(name));
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }

        obs_output_set_reconnect_settings(output, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
        obs_output_set_service(output, service);

        // Determine video source
        auto video = obs_get_video();
        auto audio = obs_get_audio();

        if (source) {
            // Video setup
            sourceView = obs_view_create();
            obs_view_set_source(sourceView, 0, source);

            obs_video_info ovi = {0};
            if (!obs_get_video_info(&ovi)) {
                // Abort when no video situation
                obs_log(LOG_ERROR, "%s: Failed to get video info", qUtf8Printable(name));
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }

            ovi.base_width = obs_source_get_width(source);
            ovi.base_height = obs_source_get_height(source);
            ovi.output_width = width;
            ovi.output_height = height;

            if (ovi.base_width == 0 || ovi.base_height == 0 || ovi.output_width == 0 || ovi.output_height == 0) {
                obs_log(LOG_ERROR, "%s: Invalid video spec", qUtf8Printable(name));
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }

            sourceVideo = obs_view_add2(sourceView, &ovi);
            if (!sourceVideo) {
                obs_log(LOG_ERROR, "%s: Failed to create source video", qUtf8Printable(name));
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }
            video = sourceVideo;
        }

        // Determine audio source
        QString audioSourceUuid = obs_data_get_string(egressSettings, "audio_source");
        if (audioSourceUuid.isEmpty()) {
            // Use source embeded audio
            audioSourceUuid = activeSourceUuid;
        }
        if (audioSourceUuid == "no_audio") {
            // Silence
            obs_log(LOG_DEBUG, "%s: Audio source: silence", qUtf8Printable(name));
            audioSilence = createSilenceAudio();
            if (!audioSilence) {
                obs_log(LOG_ERROR, "%s: Failed to create silence audio", qUtf8Printable(name));
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }
            audio = audioSilence;
        } else if (!audioSourceUuid.isEmpty() && audioSourceUuid != "master_track") {
            // Not master audio
            OBSSourceAutoRelease customSource = obs_get_source_by_uuid(qUtf8Printable(audioSourceUuid));
            if (customSource) {
                obs_log(
                    LOG_DEBUG, "%s: Audio source: %s", qUtf8Printable(name),
                    qUtf8Printable(obs_source_get_name(customSource))
                );
                obs_audio_info ai = {0};
                if (!obs_get_audio_info(&ai)) {
                    obs_log(LOG_ERROR, "%s: Failed to get audio info", qUtf8Printable(name));
                    setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                    return;
                }

                audioSource = new OutputAudioSource(customSource, ai.samples_per_sec, ai.speakers, this);
                audio = audioSource->getAudio();
                if (!audio) {
                    obs_log(LOG_ERROR, "%s: Failed to create audio source", qUtf8Printable(name));
                    delete audioSource;
                    audioSource = nullptr;
                    setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                    return;
                }
            }
        }

        // Setup video encoder
        auto videoEncoderId = obs_data_get_string(egressSettings, "video_encoder");
        if (!videoEncoderId) {
            obs_log(LOG_ERROR, "%s: Video encoder did't set", qUtf8Printable(name));
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }
        obs_log(LOG_DEBUG, "%s: Video encoder: %s", qUtf8Printable(name), videoEncoderId);
        videoEncoder = obs_video_encoder_create(videoEncoderId, qUtf8Printable(name), egressSettings, nullptr);
        if (!videoEncoder) {
            obs_log(LOG_ERROR, "%s: Failed to create video encoder: %s", qUtf8Printable(name), videoEncoderId);
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }

        // Scale to connection's resolution
        // TODO: Keep aspect ratio?
        obs_encoder_set_scaled_size(videoEncoder, width, height);
        obs_encoder_set_gpu_scale_type(videoEncoder, OBS_SCALE_LANCZOS);
        obs_encoder_set_video(videoEncoder, video);
        obs_output_set_video_encoder(output, videoEncoder);

        // Setup audio encoder
        auto audioEncoderId = obs_data_get_string(egressSettings, "audio_encoder");
        if (!audioEncoderId) {
            obs_log(LOG_ERROR, "%s: Audio encoder did't set", qUtf8Printable(name));
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }
        obs_log(LOG_DEBUG, "%s: Audio encoder: %s", qUtf8Printable(name), audioEncoderId);
        auto audioBitrate = obs_data_get_int(egressSettings, "audio_bitrate");
        OBSDataAutoRelease audioEncoderSettings = obs_encoder_defaults(audioEncoderId);
        obs_data_set_int(audioEncoderSettings, "bitrate", audioBitrate);

        // Determine audio track
        size_t audioTrack = 0;
        if (audioSourceUuid == "master_track") {
            size_t value = obs_data_get_int(egressSettings, "audio_track");
            audioTrack = value - 1;
            obs_log(LOG_DEBUG, "%s: Audio source: Master track %d", qUtf8Printable(name), value);
        }

        audioEncoder =
            obs_audio_encoder_create(audioEncoderId, qUtf8Printable(name), audioEncoderSettings, audioTrack, nullptr);
        if (!audioEncoder) {
            obs_log(LOG_ERROR, "%s: Failed to create audio encoder: %s", qUtf8Printable(name), audioEncoderId);
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }

        obs_encoder_set_audio(audioEncoder, audio);
        obs_output_set_audio_encoder(output, audioEncoder, 0); // Don't support multiple audio outputs

        // Start output
        if (!obs_output_start(output)) {
            obs_log(LOG_ERROR, "%s: Failed to start output", qUtf8Printable(name));
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }

        obs_log(LOG_INFO, "%s: Activated output", qUtf8Printable(name));
        setStatus(EGRESS_LINK_OUTPUT_STATUS_ACTIVE);
        apiClient->incrementActiveOutputs();
    };
    innerFunc();
    apiClient->syncUplinkStatus();
    locker.unlock();
}

void EgressLinkOutput::releaseResources(bool stopStatus)
{
    QMutexLocker locker(&outputMutex);
    {
        if (source) {
            obs_source_dec_showing(source);
        }

        source = nullptr;

        if (output) {
            if (status == EGRESS_LINK_OUTPUT_STATUS_ACTIVE) {
                obs_output_stop(output);
            }
        }

        output = nullptr;
        service = nullptr;
        audioEncoder = nullptr;
        videoEncoder = nullptr;

        if (audioSource) {
            delete audioSource;
            audioSource = nullptr;
        }

        audioSilence = nullptr;

        if (sourceView) {
            obs_view_set_source(sourceView, 0, nullptr);
            obs_view_remove(sourceView);
        }

        sourceView = nullptr;

        if (stopStatus && status != EGRESS_LINK_OUTPUT_STATUS_INACTIVE) {
            obs_log(LOG_INFO, "%s: Inactivated output", qUtf8Printable(name));
        }

        if (status == EGRESS_LINK_OUTPUT_STATUS_STAND_BY) {
            apiClient->decrementStandByOutputs();
        } else if (status == EGRESS_LINK_OUTPUT_STATUS_ACTIVE) {
            apiClient->decrementActiveOutputs();
        }

        setStatus(EGRESS_LINK_OUTPUT_STATUS_INACTIVE);
    }
    locker.unlock();
}

void EgressLinkOutput::stop()
{
    releaseResources(true);
    apiClient->syncUplinkStatus();
}

// Called every OUTPUT_SNAPSHOT_INTERVAL_MSECS
void EgressLinkOutput::onSnapshotTimerTimeout()
{
    if (status != EGRESS_LINK_OUTPUT_STATUS_ACTIVE && status != EGRESS_LINK_OUTPUT_STATUS_STAND_BY) {
        return;
    }

    // Upload screenshot during output is active
    OBSSourceAutoRelease ssSource = obs_frontend_get_current_scene();
    bool success = false;
    // Keep source's aspect ratio
    auto screenshot = source ? takeSourceScreenshot(source, success, 0, OUTPUT_SCREENSHOT_HEIGHT)
                             : takeSourceScreenshot(ssSource, success, 0, OUTPUT_SCREENSHOT_HEIGHT);

    if (success) {
        apiClient->putScreenshot(name, screenshot);
    }
}

// Called every OUTPUT_MONITORING_INTERVAL_MSECS
void EgressLinkOutput::onMonitoringTimerTimeout()
{
    auto interlockType = apiClient->getSettings()->value("interlock_type");
    if (status != EGRESS_LINK_OUTPUT_STATUS_ACTIVE && status != EGRESS_LINK_OUTPUT_STATUS_STAND_BY) {
        if (interlockType == "always_on") {
            start();
        } else if (interlockType == "streaming") {
            if (obs_frontend_streaming_active()) {
                start();
            }
        } else if (interlockType == "recording") {
            if (obs_frontend_recording_active()) {
                start();
            }
        } else if (interlockType == "streaming_recording") {
            if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
                start();
            }
        } else if (interlockType == "virtual_cam") {
            if (obs_frontend_virtualcam_active()) {
                start();
            }
        }
        return;
    }

    if (QDateTime().currentMSecsSinceEpoch() - connectionAttemptingAt > OUTPUT_RETRY_TIMEOUT_MSECS) {
        if (interlockType.isEmpty()) {
            // Always off
            stop();
            return;
        } else if (interlockType == "streaming") {
            if (!obs_frontend_streaming_active()) {
                stop();
                return;
            }
        } else if (interlockType == "recording") {
            if (!obs_frontend_recording_active()) {
                stop();
                return;
            }
        } else if (interlockType == "streaming_recording") {
            if (!obs_frontend_streaming_active() && !obs_frontend_recording_active()) {
                stop();
                return;
            }
        } else if (interlockType == "virtual_cam") {
            if (!obs_frontend_virtualcam_active()) {
                stop();
                return;
            }
        }

        auto outputOnLive = output && obs_output_active(output);
        if (!outputOnLive) {
            if (status != EGRESS_LINK_OUTPUT_STATUS_STAND_BY) {
                obs_log(LOG_DEBUG, "%s: Attempting reactivate output", qUtf8Printable(name));
            }
            start();
            return;
        }

        if (activeSettingsRev != storedSettingsRev) {
            obs_log(LOG_DEBUG, "%s: Attempting change settings", qUtf8Printable(name));
            start();
            return;
        }

        if (source && !isSourceAvailable(source)) {
            obs_log(LOG_DEBUG, "%s: Source removed or inactive", qUtf8Printable(name));
            stop();
            return;
        }
    }
}

void EgressLinkOutput::setStatus(EgressLinkOutputStatus value)
{
    if (status != value) {
        status = value;
        emit statusChanged(status);
    }
}

void EgressLinkOutput::setVisible(bool value)
{
    obs_log(LOG_DEBUG, "%s: Visibility changing: %s", qUtf8Printable(name), value ? "true" : "false");

    obs_data_set_bool(settings, "visible", value);
    saveSettings();

    // Increment revision to restart output
    storedSettingsRev++;
}

void EgressLinkOutput::onUplinkReady(const UplinkInfo &uplink)
{
    StageConnection incomingConnection = apiClient->getUplink().getConnections().find([this](const StageConnection &c) {
        return c.getSourceName() == name;
    });

    if (connection.getId() != incomingConnection.getId() ||
        connection.getRevision() < incomingConnection.getRevision()) {
        // Restart output
        obs_log(LOG_DEBUG, "%s: The connection has been changed", qUtf8Printable(name));
        // The connection will be retrieved in start() again.
        connection = incomingConnection;
        // Increment revision to restart output
        storedSettingsRev++;
    }
}
