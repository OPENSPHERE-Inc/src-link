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
#include "../UI/common.hpp"

#define OUTPUT_MAX_RETRIES 7
#define OUTPUT_RETRY_DELAY_SECS 1
#define OUTPUT_JSON_NAME "output.json"
#define OUTPUT_POLLING_INTERVAL_MSECS 10000
#define OUTPUT_MONITORING_INTERVAL_MSECS 1000
#define OUTPUT_RETRY_TIMEOUT_MSECS 15000

//--- LinkedOutput class ---//

LinkedOutput::LinkedOutput(const QString &_name, SourceLinkApiClient *_apiClient, QObject *parent)
    : QObject(parent),
      name(_name),
      apiClient(_apiClient),
      output(nullptr),
      service(nullptr),
      videoEncoder(nullptr),
      audioEncoder(nullptr),
      audioSource(nullptr),
      sourceView(nullptr),
      sourceVideo(nullptr),
      weakSource(nullptr),
      settings(nullptr),
      outputActive(false)
{
    obs_log(LOG_DEBUG, "%s: Output creating", qPrintable(name));

    loadSettings();
    apiClient->putSeatAllocation();

    pollingTimer = new QTimer(this);
    pollingTimer->setInterval(OUTPUT_POLLING_INTERVAL_MSECS);
    pollingTimer->start();
    connect(pollingTimer, SIGNAL(timeout()), this, SLOT(onPollingTimerTimeout()));

    monitoringTimer = new QTimer(this);
    monitoringTimer->setInterval(OUTPUT_MONITORING_INTERVAL_MSECS);
    monitoringTimer->start();
    connect(monitoringTimer, SIGNAL(timeout()), this, SLOT(onMonitoringTimerTimeout()));

    obs_log(LOG_INFO, "%s: Output created", qPrintable(name));
}

LinkedOutput::~LinkedOutput()
{
    obs_log(LOG_DEBUG, "%s: Output destroying", qPrintable(name));

    disconnect(this);

    stopOutput();

    obs_log(LOG_INFO, "%s: Output destroyed", qPrintable(name));
}

obs_properties_t *LinkedOutput::getProperties()
{
    obs_log(LOG_DEBUG, "%s: Properties creating", qPrintable(name));

    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    // "Connection" group
    auto connectionGroup = obs_properties_create();
    auto triggerList = obs_properties_add_list(
        connectionGroup, "trigger", obs_module_text("Start trigger"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(triggerList, obs_module_text("Disabled"), "disabled");
    obs_property_list_add_string(triggerList, obs_module_text("Always"), "always");
    obs_property_list_add_string(triggerList, obs_module_text("DuringStreaming"), "streaming");
    obs_property_list_add_string(triggerList, obs_module_text("DuringRecording"), "recording");
    obs_property_list_add_string(triggerList, obs_module_text("DuringStreamingOrRecording"), "streaming_recording");
    obs_property_list_add_string(triggerList, obs_module_text("DuringVertualCam"), "virtual_cam");

    auto sourceNameList = obs_properties_add_list(
        connectionGroup, "source_name", obs_module_text("Type"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    if (!apiClient->getSeat().isEmpty() && !apiClient->getSeat().getStage().isEmpty()) {
        foreach(auto &source, apiClient->getSeat().getStage().getSources())
        {
            obs_property_list_add_string(sourceNameList, qPrintable(source.getDisplayName()), qPrintable(source.getName()));
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
    obs_properties_add_group(
        props, "connection_group", obs_module_text("Connection"), OBS_GROUP_NORMAL, connectionGroup
    );

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

void LinkedOutput::getDefault(obs_data_t *defaults)
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

void LinkedOutput::update(obs_data_t *newSettings)
{
    obs_log(LOG_DEBUG, "%s: Output updating", qPrintable(name));

    // Apply default first
    OBSDataAutoRelease defaults = obs_data_get_defaults(newSettings);
    obs_data_apply(settings, defaults);
    // Apply new settings
    obs_data_apply(settings, newSettings);
    // Save settings to permanent storage
    saveSettings();

    obs_log(LOG_INFO, "%s: Output updated", qPrintable(name));
}

void LinkedOutput::loadSettings()
{
    settings = obs_data_create();

    // Apply defaults
    getDefault(settings);

    // Load settings from json
    OBSString path = obs_module_get_config_path(obs_current_module(), qPrintable(QString("%1.json").arg(name)));
    OBSDataAutoRelease data = obs_data_create_from_json_file(path);

    if (data) {
        obs_data_apply(settings, data);
    }
}

void LinkedOutput::saveSettings()
{
    // Save settings to json file
    OBSString path = obs_module_get_config_path(obs_current_module(), qPrintable(QString("%1.json").arg(name)));
    obs_data_save_json_safe(settings, path, "tmp", "bak");
}

obs_data_t *LinkedOutput::createEgressSettings(const StageConnection &connection)
{
    obs_data_t *egressSettings = obs_data_create();
    obs_data_apply(egressSettings, settings);

    if (connection.getProtocol() == QString("srt")) {
        auto server = QString("srt://%1:%2?%3")
                          .arg(connection.getServer())
                          .arg(connection.getPort())
                          .arg(connection.getParameters());
        obs_data_set_string(egressSettings, "server", qPrintable(server));
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

void LinkedOutput::startOutput()
{
    // Force free resources
    stopOutput();

    // Retrieve connection
    sourceName = obs_data_get_string(settings, "source_name");
    if (sourceName.isEmpty()) {
        return;
    }

    // Find connection specified by source_name
    StageConnection connection;
    foreach(const auto &c, apiClient->getSeat().getConnections())
    {
        if (c.getSourceName() == sourceName) {
            connection = c;
            break;
        }
    }

    if (connection.isEmpty()) {
        obs_log(LOG_WARNING, "No active connection for %s (source=%s)", qPrintable(name), qPrintable(sourceName));
        return;
    }

    width = connection.getWidth();
    height = connection.getHeight();

    OBSDataAutoRelease egressSettings = createEgressSettings(connection);
    if (!egressSettings) {
        obs_log(LOG_ERROR, "Unsupported connection for %s", qPrintable(name));
        return;
    }

    // Service : always use rtmp_custom
    service = obs_service_create("rtmp_custom", qPrintable(name), egressSettings, nullptr);
    if (!service) {
        obs_log(LOG_ERROR, "Failed to create service %s", qPrintable(name));
        return;
    }

    // Output : always use ffmpeg_mpegts_muxer
    output = obs_output_create("ffmpeg_mpegts_muxer", qPrintable(name), egressSettings, nullptr);
    if (!output) {
        obs_log(LOG_ERROR, "Failed to create output %s", qPrintable(name));
        return;
    }

    obs_output_set_reconnect_settings(output, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
    obs_output_set_service(output, service);
    // Reduce reconnect with timeout
    connectionAttemptingAt = QDateTime().currentMSecsSinceEpoch();

    // Determine capture source
    auto video = obs_get_video();
    auto audio = obs_get_audio();

    if (!sourceUuid.isEmpty()) {
        // Specific source
        OBSSourceAutoRelease source = obs_get_source_by_uuid(qPrintable(sourceUuid));
        if (!source) {
            obs_log(LOG_ERROR, "%s: Source not found: %s", qPrintable(name), qPrintable(sourceUuid));
            return;
        }
        weakSource = obs_source_get_weak_source(source);
        obs_source_inc_showing(source);

        // Video setup
        sourceView = obs_view_create();
        obs_view_set_source(sourceView, 0, source);

        obs_video_info ovi = {0};
        if (!obs_get_video_info(&ovi)) {
            // Abort when no video situation
            obs_log(LOG_ERROR, "%s: Failed to get video info", qPrintable(name));
            return;
        }

        ovi.base_width = width;
        ovi.base_height = height;
        ovi.output_width = width;
        ovi.output_height = height;

        sourceVideo = obs_view_add2(sourceView, &ovi);
        if (!sourceVideo) {
            obs_log(LOG_ERROR, "%s: Failed to create source video", qPrintable(name));
            return;
        }
        video = sourceVideo;

        // Audio setup
        obs_audio_info ai = {0};
        if (!obs_get_audio_info(&ai)) {
            obs_log(LOG_ERROR, "%s: Failed to get audio info", qPrintable(name));
            return;
        }

        audioSource = new LinkedOutputAudioSource(source, ai.samples_per_sec, ai.speakers, this);
        audio = audioSource->getAudio();
        if (!audio) {
            obs_log(LOG_ERROR, "%s: Failed to create audio source", qPrintable(name));
            delete audioSource;
            audioSource = nullptr;
            return;
        }
    }

    // Setup video encoder
    auto videoEncoderId = obs_data_get_string(egressSettings, "video_encoder");
    obs_log(LOG_DEBUG, "Video encoder: %s", videoEncoderId);
    videoEncoder = obs_video_encoder_create(videoEncoderId, qPrintable(name), egressSettings, nullptr);
    if (!videoEncoder) {
        obs_log(LOG_ERROR, "Failed to create video encoder %s for %s", videoEncoderId, qPrintable(name));
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
    obs_log(LOG_DEBUG, "Audio encoder: %s", audioEncoderId);
    auto audioBitrate = obs_data_get_int(egressSettings, "audio_bitrate");
    auto audioTrack = obs_data_get_int(egressSettings, "audio_track");
    OBSDataAutoRelease audioEncoderSettings = obs_encoder_defaults(audioEncoderId);
    obs_data_set_int(audioEncoderSettings, "bitrate", audioBitrate);

    audioEncoder =
        obs_audio_encoder_create(audioEncoderId, qPrintable(name), audioEncoderSettings, audioTrack, nullptr);
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
    if (weakSource) {
        OBSSourceAutoRelease source = obs_weak_source_get_source(weakSource);
        if (source) {
            obs_source_dec_showing(source);
        }
        weakSource = nullptr;
    }

    if (output) {
        if (outputActive) {
            obs_output_stop(output);
        }
        output = nullptr;
    }

    if (service) {
        service = nullptr;
    }

    if (audioEncoder) {
        audioEncoder = nullptr;
    }

    if (videoEncoder) {
        videoEncoder = nullptr;
    }

    if (audioSource) {
        delete audioSource;
        audioSource = nullptr;
    }

    if (sourceView) {
        obs_view_set_source(sourceView, 0, nullptr);
        obs_view_remove(sourceView);
        sourceView = nullptr;
    }

    if (outputActive) {
        outputActive = false;
        obs_log(LOG_INFO, "Stopped output %s", qPrintable(name));
    }
}

// Called every OUTPUT_POLLING_INTERVAL_MSECS
void LinkedOutput::onPollingTimerTimeout()
{
    if (!outputActive) {
        return;
    }

    // Upload screenshot during output is active
    OBSSourceAutoRelease source = obs_frontend_get_current_scene();
    bool success = false;
    auto screenshot = TakeSourceScreenshot(source, success, width, height);

    if (success) {
        apiClient->putScreenshot(sourceName, screenshot);
    }
}

// Called every OUTPUT_MONITORING_INTERVAL_MSECS
void LinkedOutput::onMonitoringTimerTimeout()
{
    QString trigger = obs_data_get_string(settings, "trigger");
    if (!outputActive) {
        if (trigger == "always") {
            startOutput();
        } else if (trigger == "streaming" || trigger == "streaming_recording") {
            if (obs_frontend_streaming_active()) {
                startOutput();
            }
        } else if (trigger == "recording" || trigger == "streaming_recording") {
            if (obs_frontend_recording_active()) {
                startOutput();
            }
        } else if (trigger == "virtual_cam") {
            if (obs_frontend_virtualcam_active()) {
                startOutput();
            }
        }
        return;
    }

    if (QDateTime().currentMSecsSinceEpoch() - connectionAttemptingAt > OUTPUT_RETRY_TIMEOUT_MSECS) {
        if (trigger == "disabled") {
            stopOutput();
            return;
        } else if (trigger == "streaming" || trigger == "streaming_recording") {
            if (!obs_frontend_streaming_active()) {
                stopOutput();
                return;
            }
        } else if (trigger == "recording" || trigger == "streaming_recording") {
            if (!obs_frontend_recording_active()) {
                stopOutput();
                return;
            }
        } else if (trigger == "virtual_cam") {
            if (!obs_frontend_virtualcam_active()) {
                stopOutput();
                return;
            }
        }

        auto outputOnLive = obs_output_active(output);
        if (!outputOnLive) {
            obs_log(LOG_INFO, "%s: Attempting reactivate output", qPrintable(name));
            startOutput();
        }
    }
}

//--- LinkedOutputAudioSource class ---//

LinkedOutputAudioSource::LinkedOutputAudioSource(
    obs_source_t *source, uint32_t _samplesPerSec, speaker_layout _speakers, QObject *parent
)
    : SourceAudioCapture(source, _samplesPerSec, _speakers, parent),
      audio(nullptr)
{
    audio_output_info aoi = {0};
    aoi.name = obs_source_get_name(source);
    aoi.samples_per_sec = _samplesPerSec;
    aoi.speakers = _speakers;
    aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
    aoi.input_param = this;
    aoi.input_callback = LinkedOutputAudioSource::onOutputAudio;

    if (audio_output_open(&audio, &aoi) < 0) {
        audio = nullptr;
        return;
    }

    active = true;
}

LinkedOutputAudioSource::~LinkedOutputAudioSource()
{
    active = false;

    if (audio) {
        audio_output_close(audio);
    }
}

uint64_t LinkedOutputAudioSource::popAudio(uint64_t startTsIn, uint32_t mixers, audio_output_data *audioData)
{
    audioBufferMutex.lock();
    {
        if (audioBufferFrames < AUDIO_OUTPUT_FRAMES) {
            // Wait until enough frames are receved.
            audioBufferMutex.unlock();

            // DO NOT stall audio output pipeline
            return startTsIn;
        }

        size_t maxFrames = AUDIO_OUTPUT_FRAMES;

        while (maxFrames > 0 && audioBufferFrames) {
            // Peek header of first chunk
            deque_peek_front(&audioBuffer, audioConvBuffer, sizeof(AudioBufferHeader));
            auto header = (AudioBufferHeader *)audioConvBuffer;
            auto dataSize = sizeof(AudioBufferHeader) + header->speakers * header->frames * 4;

            // Read chunk data
            deque_peek_front(&audioBuffer, audioConvBuffer, dataSize);

            auto chunkFrames = header->frames - header->offset;
            auto frames = (chunkFrames > maxFrames) ? maxFrames : chunkFrames;
            auto outOffset = AUDIO_OUTPUT_FRAMES - maxFrames;

            for (auto tr = 0; tr < MAX_AUDIO_MIXES; tr++) {
                if ((mixers & (1 << tr)) == 0) {
                    continue;
                }
                for (auto ch = 0; ch < header->speakers; ch++) {
                    auto out = audioData[tr].data[ch] + outOffset;
                    if (!header->data_idx[ch]) {
                        continue;
                    }
                    auto in = (float *)(audioConvBuffer + header->data_idx[ch]) + header->offset;

                    for (auto i = 0; i < frames; i++) {
                        *out += *(in++);
                        if (*out > 1.0f) {
                            *out = 1.0f;
                        } else if (*out < -1.0f) {
                            *out = -1.0f;
                        }
                        out++;
                    }
                }
            }

            if (frames == chunkFrames) {
                // Remove fulfilled chunk from buffer
                deque_pop_front(&audioBuffer, NULL, dataSize);
            } else {
                // Chunk frames are remaining -> modify chunk header
                header->offset += frames;
                deque_place(&audioBuffer, 0, header, sizeof(AudioBufferHeader));
            }

            maxFrames -= frames;

            // Decrement buffer usage
            audioBufferFrames -= frames;
        }
    }
    audioBufferMutex.unlock();

    return startTsIn;
}

// Callback from audio_output_open
bool LinkedOutputAudioSource::onOutputAudio(
    void *param, uint64_t start_ts_in, uint64_t, uint64_t *out_ts, uint32_t mixers, audio_output_data *mixes
)
{
    auto *audioSource = static_cast<LinkedOutputAudioSource *>(param);
    *out_ts = audioSource->popAudio(start_ts_in, mixers, mixes);
    return true;
}
