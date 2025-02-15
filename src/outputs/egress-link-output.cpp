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
#include <util/dstr.h>
#include <util/platform.h>

#include <QUrlQuery>
#include <QString>

#include "egress-link-output.hpp"
#include "audio-source.hpp"

#define OUTPUT_MAX_RETRIES 0
#define OUTPUT_RETRY_DELAY_SECS 1
#define OUTPUT_JSON_NAME "output.json"
#define OUTPUT_MONITORING_INTERVAL_MSECS 1000
#define OUTPUT_RETRY_TIMEOUT_MSECS 3500
#define OUTPUT_START_DELAY_MSECS 1000
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
    aoi.input_callback = [](void *, uint64_t startTsIn, uint64_t, uint64_t *outTs, uint32_t, audio_output_data *) {
        *outTs = startTsIn;
        return true;
    };

    audio_t *audio = nullptr;
    audio_output_open(&audio, &aoi);
    return audio;
}

// Imitate obs-studio/UI/window-basic-settings.cpp
inline QString makeFormatToolTip()
{
    static const char *format_list[][2] = {
        {"1", "FilenameFormatting.TT.1"},       {"CCYY", "FilenameFormatting.TT.CCYY"},
        {"YY", "FilenameFormatting.TT.YY"},     {"MM", "FilenameFormatting.TT.MM"},
        {"DD", "FilenameFormatting.TT.DD"},     {"hh", "FilenameFormatting.TT.hh"},
        {"mm", "FilenameFormatting.TT.mm"},     {"ss", "FilenameFormatting.TT.ss"},
        {"%", "FilenameFormatting.TT.Percent"}, {"a", "FilenameFormatting.TT.a"},
        {"A", "FilenameFormatting.TT.A"},       {"b", "FilenameFormatting.TT.b"},
        {"B", "FilenameFormatting.TT.B"},       {"d", "FilenameFormatting.TT.d"},
        {"H", "FilenameFormatting.TT.H"},       {"I", "FilenameFormatting.TT.I"},
        {"m", "FilenameFormatting.TT.m"},       {"M", "FilenameFormatting.TT.M"},
        {"p", "FilenameFormatting.TT.p"},       {"s", "FilenameFormatting.TT.s"},
        {"S", "FilenameFormatting.TT.S"},       {"y", "FilenameFormatting.TT.y"},
        {"Y", "FilenameFormatting.TT.Y"},       {"z", "FilenameFormatting.TT.z"},
        {"Z", "FilenameFormatting.TT.Z"},       {"FPS", "FilenameFormatting.TT.FPS"},
        {"CRES", "FilenameFormatting.TT.CRES"}, {"ORES", "FilenameFormatting.TT.ORES"},
        {"VF", "FilenameFormatting.TT.VF"},
    };

    QString html = "<table>";

    for (auto f : format_list) {
        html += "<tr><th align='left'>%";
        html += f[0];
        html += "</th><td>";
        html += QTStr(f[1]);
        html += "</td></tr>";
    }

    html += "</table>";
    return html;
}

//--- EgressLinkOutput class ---//

EgressLinkOutput::EgressLinkOutput(const QString &_name, SRCLinkApiClient *_apiClient)
    : QObject(_apiClient),
      name(_name),
      apiClient(_apiClient),
      streamingOutput(nullptr),
      recordingOutput(nullptr),
      service(nullptr),
      videoEncoder(nullptr),
      audioEncoder(nullptr),
      audioSource(nullptr),
      sourceView(nullptr),
      source(nullptr),
      settings(nullptr),
      storedSettingsRev(0),
      activeSettingsRev(0),
      status(EGRESS_LINK_OUTPUT_STATUS_INACTIVE),
      recordingStatus(RECORDING_OUTPUT_STATUS_INACTIVE)
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
    connect(apiClient, &SRCLinkApiClient::egressRefreshNeeded, this, [this]() { refresh(); });

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
    auto output = static_cast<EgressLinkOutput *>(param);
    // Force stop on shutdown
    switch (event) {
    case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
    case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
        output->stop();
        break;
    default:
        // Nothing to do
        break;
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
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *_settings) {
            auto _output = static_cast<EgressLinkOutput *>(param);
            auto _audioSource = obs_data_get_string(_settings, "audio_source");
            obs_property_set_enabled(
                obs_properties_get(_props, "audio_track"),
                (!strlen(_audioSource) && _output->getSourceUuid() == "program") ||
                    !strcmp(_audioSource, "master_track")
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

        auto _name = obs_encoder_get_display_name(encoderId);

        if (obs_get_encoder_type(encoderId) == OBS_ENCODER_VIDEO) {
            obs_property_list_add_string(videoEncoderList, _name, encoderId);
        } else if (obs_get_encoder_type(encoderId) == OBS_ENCODER_AUDIO) {
            obs_property_list_add_string(audioEncoderList, _name, encoderId);
        }
    }

    obs_property_set_modified_callback2(
        audioEncoderList,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *_settings) {
            auto _output = static_cast<EgressLinkOutput *>(param);
            obs_log(LOG_DEBUG, "%s: Audio encoder chainging", qUtf8Printable(_output->getName()));

            const auto _encoderId = obs_data_get_string(_settings, "audio_encoder");
            const OBSProperties encoderProps = obs_get_encoder_properties(_encoderId);
            const auto encoderBitrateProp = obs_properties_get(encoderProps, "bitrate");

            auto _audioEncoderGroup = obs_property_group_content(obs_properties_get(_props, "audio_encoder_group"));
            auto audioBitrateProp = obs_properties_get(_audioEncoderGroup, "audio_bitrate");

            obs_property_list_clear(audioBitrateProp);

            const auto type = obs_property_get_type(encoderBitrateProp);
            auto result = true;
            switch (type) {
            case OBS_PROPERTY_INT: {
                const auto max_value = obs_property_int_max(encoderBitrateProp);
                const auto step_value = obs_property_int_step(encoderBitrateProp);

                for (int j = obs_property_int_min(encoderBitrateProp); j <= max_value; j += step_value) {
                    char bitrateTitle[6];
                    snprintf(bitrateTitle, sizeof(bitrateTitle), "%d", j);
                    obs_property_list_add_int(audioBitrateProp, bitrateTitle, j);
                }

                break;
            }

            case OBS_PROPERTY_LIST: {
                const auto format = obs_property_list_format(encoderBitrateProp);
                if (format != OBS_COMBO_FORMAT_INT) {
                    obs_log(
                        LOG_ERROR, "%s: Invalid bitrate property given by encoder: %s",
                        qUtf8Printable(_output->getName()), _encoderId
                    );
                    result = false;
                    break;
                }

                const auto count = obs_property_list_item_count(encoderBitrateProp);
                for (size_t j = 0; j < count; j++) {
                    if (obs_property_list_item_disabled(encoderBitrateProp, j)) {
                        continue;
                    }
                    const auto bitrate = obs_property_list_item_int(encoderBitrateProp, j);
                    char bitrateTitle[6];
                    snprintf(bitrateTitle, sizeof(bitrateTitle), "%lld", bitrate);
                    obs_property_list_add_int(audioBitrateProp, bitrateTitle, bitrate);
                }
                break;
            }

            default:
                break;
            }

            obs_log(LOG_DEBUG, "%s: Audio encoder changed", qUtf8Printable(_output->getName()));
            return result;
        },
        this
    );

    obs_property_set_modified_callback2(
        videoEncoderList,
        [](void *param, obs_properties_t *_props, obs_property_t *, obs_data_t *_settings) {
            auto _output = static_cast<EgressLinkOutput *>(param);
            obs_log(LOG_DEBUG, "%s: Video encoder chainging", qUtf8Printable(_output->getName()));

            auto _videoEncoderGroup = obs_property_group_content(obs_properties_get(_props, "video_encoder_group"));
            auto _encoderId = obs_data_get_string(_settings, "video_encoder");

            // Apply encoder's defaults
            OBSDataAutoRelease encoderDefaults = obs_encoder_defaults(_encoderId);
            applyDefaults(_settings, encoderDefaults);

            obs_properties_remove_by_name(_videoEncoderGroup, "video_encoder_settings_group");

            auto encoderProps = obs_get_encoder_properties(_encoderId);
            if (encoderProps) {
                obs_properties_add_group(
                    _videoEncoderGroup, "video_encoder_settings_group", obs_encoder_get_display_name(_encoderId),
                    OBS_GROUP_NORMAL, encoderProps
                );

                // Do not apply to _videoEncoderGroup because it will cause memoryleak.
                obs_properties_apply_settings(encoderProps, _settings);
            }

            obs_log(LOG_DEBUG, "%s: Video encoder changed", qUtf8Printable(_output->getName()));
            return true;
        },
        this
    );

    //--- Recording group ---//
    auto recordingGroup = obs_properties_create();

    auto recordingChangeHandler = [](void *, obs_properties_t *_props, obs_property_t *, obs_data_t *_settings) {
        auto splitFile = obs_data_get_string(_settings, "split_file");
        obs_property_set_visible(obs_properties_get(_props, "split_file_time_mins"), !strcmp(splitFile, "by_time"));
        obs_property_set_visible(obs_properties_get(_props, "split_file_size_mb"), !strcmp(splitFile, "by_size"));
        return true;
    };

    obs_properties_add_path(recordingGroup, "path", obs_module_text("Path"), OBS_PATH_DIRECTORY, nullptr, nullptr);
    auto filenameFormatting = obs_properties_add_text(
        recordingGroup, "filename_formatting", obs_module_text("FilenameFormatting"), OBS_TEXT_DEFAULT
    );
    obs_property_set_long_description(filenameFormatting, qUtf8Printable(makeFormatToolTip()));

    // Only support limited formats
    auto fileFormatList = obs_properties_add_list(
        recordingGroup, "rec_format", obs_module_text("VideoFormat"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(fileFormatList, obs_module_text("MKV"), "mkv");
    obs_property_list_add_string(fileFormatList, obs_module_text("hMP4"), "hybrid_mp4"); // beta
    obs_property_list_add_string(fileFormatList, obs_module_text("MP4"), "mp4");
    obs_property_list_add_string(fileFormatList, obs_module_text("MOV"), "mov");
    obs_property_list_add_string(fileFormatList, obs_module_text("TS"), "mpegts");

    auto splitFileList = obs_properties_add_list(
        recordingGroup, "split_file", obs_module_text("SplitFile"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.NoSplit"), "");
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.ByTime"), "by_time");
    obs_property_list_add_string(splitFileList, obs_module_text("SplitFile.BySize"), "by_size");

    obs_property_set_modified_callback2(splitFileList, recordingChangeHandler, nullptr);

    obs_properties_add_int(recordingGroup, "split_file_time_mins", obs_module_text("SplitFile.Time"), 1, 525600, 1);
    obs_properties_add_int(recordingGroup, "split_file_size_mb", obs_module_text("SplitFile.Size"), 1, 1073741824, 1);

    obs_properties_add_group(props, "recording", obs_module_text("Recording"), OBS_GROUP_CHECKABLE, recordingGroup);

    obs_log(LOG_DEBUG, "%s: Properties created", qUtf8Printable(name));
    return props;
}

void EgressLinkOutput::getDefaults(obs_data_t *defaults)
{
    obs_log(LOG_DEBUG, "%s: Default settings applying", qUtf8Printable(name));

    auto config = obs_frontend_get_profile_config();
    auto mode = config_get_string(config, "Output", "Mode");
    bool advanced_out = !strcmp(mode, "Advanced") || !strcmp(mode, "advanced");

    const char *videoEncoderId;
    uint64_t videoBitrate;
    const char *audioEncoderId;
    uint64_t audioBitrate;
    const char *recFormat;
    bool recSplitFile = false;
    const char *recSplitFileType = "Time";
    uint64_t recSplitFileTimeMins = 15;
    uint64_t recSplitFileSizeMb = 2048;
    const char *path;

    if (advanced_out) {
        videoEncoderId = config_get_string(config, "AdvOut", "Encoder");
        videoBitrate = config_get_uint(config, "AdvOut", "FFVBitrate");
        audioEncoderId = config_get_string(config, "AdvOut", "AudioEncoder");
        audioBitrate = config_get_uint(config, "AdvOut", "FFABitrate");
        recFormat = config_get_string(config, "AdvOut", "RecFormat2");
        recSplitFile = config_get_bool(config, "AdvOut", "RecSplitFile");
        recSplitFileTimeMins = config_get_uint(config, "AdvOut", "RecSplitFileTime");
        recSplitFileSizeMb = config_get_uint(config, "AdvOut", "RecSplitFileSize");

        const char *recType = config_get_string(config, "AdvOut", "RecType");
        bool ffmpegRecording = !astrcmpi(recType, "ffmpeg") && config_get_bool(config, "AdvOut", "FFOutputToFile");
        path = config_get_string(config, "AdvOut", ffmpegRecording ? "FFFilePath" : "RecFilePath");
    } else {
        videoEncoderId = getSimpleVideoEncoder(config_get_string(config, "SimpleOutput", "StreamEncoder"));
        videoBitrate = config_get_uint(config, "SimpleOutput", "VBitrate");
        audioEncoderId = getSimpleAudioEncoder(config_get_string(config, "SimpleOutput", "StreamAudioEncoder"));
        audioBitrate = config_get_uint(config, "SimpleOutput", "ABitrate");
        recFormat = config_get_string(config, "SimpleOutput", "RecFormat2");
        path = config_get_string(config, "SimpleOutput", "FilePath");
    }

    obs_data_set_default_string(defaults, "video_encoder", videoEncoderId);
    obs_data_set_default_int(defaults, "bitrate", videoBitrate);
    obs_data_set_default_string(defaults, "audio_encoder", audioEncoderId);
    obs_data_set_default_int(defaults, "audio_bitrate", audioBitrate);
    obs_data_set_default_string(defaults, "audio_source", "");
    obs_data_set_default_bool(defaults, "visible", true);
    obs_data_set_default_string(defaults, "path", path);
    obs_data_set_default_string(defaults, "rec_format", recFormat);

    const char *splitFileValue = "";
    if (recSplitFile && strcmp(recSplitFileType, "Manual")) {
        if (!strcmp(recSplitFileType, "Size")) {
            splitFileValue = "by_size";
        } else {
            splitFileValue = "by_time";
        }
    }
    obs_data_set_default_string(defaults, "split_file", splitFileValue);
    obs_data_set_default_int(defaults, "split_file_time_mins", recSplitFileTimeMins);
    obs_data_set_default_int(defaults, "split_file_size_mb", recSplitFileSizeMb);

    QString filenameFormatting = QString("%1_") + QString(config_get_string(config, "Output", "FilenameFormatting"));
    obs_data_set_default_string(defaults, "filename_formatting", qUtf8Printable(filenameFormatting));

    // Apply encoder's defaults
    OBSDataAutoRelease encoderDefaults = obs_encoder_defaults(videoEncoderId);
    applyDefaults(defaults, encoderDefaults);

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

    // Initialize defaults
    getDefaults(settings);
    // Apply default first
    OBSDataAutoRelease defaults = obs_data_get_defaults(settings);
    obs_data_apply(settings, defaults);

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

obs_data_t *EgressLinkOutput::createEgressSettings(const StageConnection &_connection)
{
    obs_data_t *egressSettings = obs_data_create();
    obs_data_apply(egressSettings, settings);

    if (_connection.getProtocol() == "srt") {
        auto address = _connection.getServer();
        auto uplink = apiClient->getUplink();
        if (address == uplink.getPublicAddress() || uplink.getAllocation().getLan()) {
            // Seems guest lives in the same network -> switch to lan server.
            address = _connection.getLanServer();
        }

        QUrl server;
        server.setScheme("srt");
        server.setHost(address);
        server.setPort(_connection.getPort());

        QUrlQuery parameters(_connection.getParameters());

        if (_connection.getLatency()) {
            // Override latency with participant's settings
            parameters.removeQueryItem("latency");
            parameters.addQueryItem(
                "latency",
                // Convert to microseconds
                QString::number(_connection.getLatency() * 1000)
            );
        }

        if (_connection.getRelay()) {
            // FIXME: Currently encryption not supported !
            parameters.addQueryItem("mode", "caller");
            parameters.addQueryItem(
                "streamid", QString("publish/%1/%2").arg(_connection.getStreamId()).arg(_connection.getPassphrase())
            );
        } else {
            parameters.addQueryItem("mode", "caller");
            if (!_connection.getStreamId().isEmpty()) {
                parameters.addQueryItem("streamid", _connection.getStreamId());
            }
            if (!_connection.getPassphrase().isEmpty()) {
                parameters.addQueryItem("passphrase", _connection.getPassphrase());
            }
        }

        server.setQuery(parameters);

        obs_log(LOG_DEBUG, "%s: SRT server is %s", qUtf8Printable(name), qUtf8Printable(server.toString()));
        obs_data_set_string(egressSettings, "server", qUtf8Printable(server.toString()));
    } else {
        return nullptr;
    }

    // Limit bitrate
    auto bitrate = obs_data_get_int(egressSettings, "bitrate");
    if (bitrate > _connection.getMaxBitrate()) {
        obs_data_set_int(egressSettings, "bitrate", _connection.getMaxBitrate());
    } else if (bitrate < _connection.getMinBitrate()) {
        obs_data_set_int(egressSettings, "bitrate", _connection.getMinBitrate());
    }

    return egressSettings;
}

obs_data_t *EgressLinkOutput::createRecordingSettings(obs_data_t *egressSettings)
{
    obs_data_t *recordingSettings = obs_data_create();
    auto config = obs_frontend_get_profile_config();
    QString filenameFormat = obs_data_get_string(egressSettings, "filename_formatting");
    if (filenameFormat.isEmpty()) {
        filenameFormat = QString("%1_") + QString(config_get_string(config, "Output", "FilenameFormatting"));
    }

    // Sanitize filename
#ifdef __APPLE__
    filenameFormat.replace(QRegularExpression("[:]"), "");
#elif defined(_WIN32)
    filenameFormat.replace(QRegularExpression("[<>:\"\\|\\?\\*]"), "");
#else
    // TODO: Add filtering for other platforms
#endif

    auto path = obs_data_get_string(egressSettings, "path");
    auto recFormat = obs_data_get_string(egressSettings, "rec_format");

    // Add filter name to filename format
    QString sourceName = qUtf8Printable(name);
    filenameFormat = filenameFormat.arg(sourceName.replace(QRegularExpression("[\\s/\\\\.:;*?\"<>|&$,]"), "-"));
    auto compositePath = getOutputFilename(path, recFormat, true, false, qUtf8Printable(filenameFormat));

    obs_data_set_string(recordingSettings, "path", qUtf8Printable(compositePath));

    auto splitFile = obs_data_get_string(egressSettings, "split_file");
    if (strlen(splitFile) > 0) {
        obs_data_set_string(recordingSettings, "directory", path);
        obs_data_set_string(recordingSettings, "format", qUtf8Printable(filenameFormat));
        obs_data_set_string(recordingSettings, "extension", qUtf8Printable(getFormatExt(recFormat)));
        obs_data_set_bool(recordingSettings, "allow_spaces", false);
        obs_data_set_bool(recordingSettings, "allow_overwrite", false);
        obs_data_set_bool(recordingSettings, "split_file", true);

        auto maxTimeSec = !strcmp(splitFile, "by_time") ? obs_data_get_int(egressSettings, "split_file_time_mins") * 60
                                                        : 0;
        obs_data_set_int(recordingSettings, "max_time_sec", maxTimeSec);

        auto maxSizeMb = !strcmp(splitFile, "by_size") ? obs_data_get_int(egressSettings, "split_file_size_mb") : 0;
        obs_data_set_int(recordingSettings, "max_size_mb", maxSizeMb);
    }

    return recordingSettings;
}

// Modifies state of members: connection
void EgressLinkOutput::retrieveConnection()
{
    // Find connection specified by name
    connection = StageConnection();
    foreach (const auto &c, apiClient->getUplink().getConnections().values()) {
        if (c.getSourceName() == name) {
            connection = c;
            break;
        }
    }
}

// Add reference of source which specified by sourceUuid
// Modifies state of members: source
bool EgressLinkOutput::createSource(QString sourceUuid)
{
    // Get reference for specific source
    source = obs_get_source_by_uuid(qUtf8Printable(sourceUuid));
    if (!source || !isSourceAvailable(source) || !isSourceVisible(source)) {
        obs_log(LOG_ERROR, "%s: Source not found: %s", qUtf8Printable(name), qUtf8Printable(sourceUuid));
        source = nullptr;
        return false;
    }
    // DO NOT use obs_source_inc_active() because source's audio will be mixed in main output unexpectedly.
    obs_source_inc_showing(source);

    return true;
}

// Modifies state of members: sourceView
video_t *EgressLinkOutput::createVideo(obs_video_info *vi)
{
    auto video = obs_get_video();

    if (source) {
        // Video setup
        obs_log(LOG_DEBUG, "%s: Video source is %s", qUtf8Printable(name), qUtf8Printable(obs_source_get_name(source)));

        sourceView = obs_view_create();
        obs_view_set_source(sourceView, 0, source);

        // Force dot by dot at this stage
        auto ovi = *vi;
        ovi.output_width = ovi.base_width = obs_source_get_width(source);
        ovi.output_height = ovi.base_height = obs_source_get_height(source);

        if (ovi.base_width == 0 || ovi.base_height == 0 || ovi.output_width == 0 || ovi.output_height == 0) {
            obs_log(LOG_ERROR, "%s: Invalid video spec", qUtf8Printable(name));
            return nullptr;
        }

        video = obs_view_add2(sourceView, &ovi);
        if (!video) {
            obs_log(LOG_ERROR, "%s: Failed to create source video", qUtf8Printable(name));
            return nullptr;
        }
    }

    return video;
}

// Modifies state of members: audioSource, audioSilence
audio_t *EgressLinkOutput::createAudio(QString audioSourceUuid)
{
    auto audio = obs_get_audio();

    if (audioSourceUuid == "no_audio") {
        // Silence
        obs_log(LOG_DEBUG, "%s: Audio source: silence", qUtf8Printable(name));
        audioSilence = createSilenceAudio();
        if (!audioSilence) {
            obs_log(LOG_ERROR, "%s: Failed to create silence audio", qUtf8Printable(name));
            return nullptr;
        }
        audio = audioSilence;

    } else if (audioSourceUuid != "program" && audioSourceUuid != "master_track") {
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
                return nullptr;
            }

            audioSource = new OutputAudioSource(customSource, ai.samples_per_sec, ai.speakers, this);
            audio = audioSource->getAudio();
            if (!audio) {
                obs_log(LOG_ERROR, "%s: Failed to create audio source", qUtf8Printable(name));
                delete audioSource;
                audioSource = nullptr;
                return nullptr;
            }
        }
    }

    return audio;
}

// Modify state of members: service, streamingOutput
bool EgressLinkOutput::createStreamingOutput(obs_data_t *egressSettings)
{
    // Service : always use rtmp_custom
    service =
        obs_service_create("rtmp_custom", qUtf8Printable(QString("%1.Service").arg(name)), egressSettings, nullptr);
    if (!service) {
        obs_log(LOG_ERROR, "%s: Failed to create service", qUtf8Printable(name));
        return false;
    }

    // Output : always use ffmpeg_mpegts_muxer
    streamingOutput = obs_output_create(
        "ffmpeg_mpegts_muxer", qUtf8Printable(QString("%1.Streaming").arg(name)), egressSettings, nullptr
    );
    if (!streamingOutput) {
        obs_log(LOG_ERROR, "%s: Failed to create streaming output", qUtf8Printable(name));
        return false;
    }

    obs_output_set_reconnect_settings(streamingOutput, OUTPUT_MAX_RETRIES, OUTPUT_RETRY_DELAY_SECS);
    obs_output_set_service(streamingOutput, service);

    return true;
}

// Modifies state of members: recordingOutput
bool EgressLinkOutput::createRecordingOutput(obs_data_t *egressSettings)
{
    auto recFormat = obs_data_get_string(egressSettings, "rec_format");
    const char *outputId = !strcmp(recFormat, "hybrid_mp4") ? "mp4_output" : "ffmpeg_muxer";

    // Ensure base path exists
    auto path = obs_data_get_string(egressSettings, "path");
    os_mkdirs(path);

    // No abort happen even if failed to create output
    OBSDataAutoRelease recordingSettings = createRecordingSettings(egressSettings);
    recordingOutput =
        obs_output_create(outputId, qUtf8Printable(QString("%1.Recording").arg(name)), recordingSettings, nullptr);
    if (!recordingOutput) {
        obs_log(LOG_ERROR, "%s: Failed to create recording output", qUtf8Printable(name));
        return false;
    }

    return true;
}

// Modifies state of members: videoEncoder, width, height
bool EgressLinkOutput::createVideoEncoder(obs_data_t *egressSettings, video_t *video, int _width, int _height)
{
    auto videoEncoderId = obs_data_get_string(egressSettings, "video_encoder");
    if (!videoEncoderId) {
        obs_log(LOG_ERROR, "%s: Video encoder did't set", qUtf8Printable(name));
        return false;
    }
    obs_log(LOG_DEBUG, "%s: Video encoder: %s", qUtf8Printable(name), videoEncoderId);
    videoEncoder = obs_video_encoder_create(
        videoEncoderId, qUtf8Printable(QString("%1.VideoEncoder").arg(name)), egressSettings, nullptr
    );
    if (!videoEncoder) {
        obs_log(LOG_ERROR, "%s: Failed to create video encoder: %s", qUtf8Printable(name), videoEncoderId);
        return false;
    }

    width = _width;
    height = _height;

    // Scale to connection's resolution
    // TODO: Keep aspect ratio?
    obs_encoder_set_scaled_size(videoEncoder, width, height);
    obs_encoder_set_gpu_scale_type(videoEncoder, OBS_SCALE_LANCZOS);
    obs_encoder_set_video(videoEncoder, video);

    return true;
}

// Modifies state of members: audioEncoder
bool EgressLinkOutput::createAudioEncoder(obs_data_t *egressSettings, QString audioSourceUuid, audio_t *audio)
{
    auto audioEncoderId = obs_data_get_string(egressSettings, "audio_encoder");
    if (!audioEncoderId) {
        obs_log(LOG_ERROR, "%s: Audio encoder did't set", qUtf8Printable(name));
        return false;
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

    audioEncoder = obs_audio_encoder_create(
        audioEncoderId, qUtf8Printable(QString("%1.AudioEncoder").arg(name)), audioEncoderSettings, audioTrack, nullptr
    );
    if (!audioEncoder) {
        obs_log(LOG_ERROR, "%s: Failed to create audio encoder: %s", qUtf8Printable(name), audioEncoderId);
        return false;
    }

    obs_encoder_set_audio(audioEncoder, audio);

    return true;
}

void EgressLinkOutput::start()
{
    QMutexLocker locker(&outputMutex);
    [&]() {
        //--- Determine source ---//
        QString sourceUuid = obs_data_get_string(settings, "source_uuid");
        bool visible = obs_data_get_bool(settings, "visible");
        if (sourceUuid.isEmpty() || !visible) {
            // Ensure resources are released
            destroyPipeline();

            setStatus(EGRESS_LINK_OUTPUT_STATUS_DISABLED);
            return;
        }

        //--- Retrieve connection ---//
        retrieveConnection();

        // No connection -> Go stand-by state
        bool goStandBy = connection.isEmpty() && status != EGRESS_LINK_OUTPUT_STATUS_STAND_BY;
        // Connection available -> Go active state
        bool goActive = !connection.isEmpty() && status != EGRESS_LINK_OUTPUT_STATUS_ACTIVE;
        // Determine demand of pipline reconstruction
        bool reconstructPipeline = sourceUuid != activeSourceUuid || goStandBy || goActive ||
                                   activeSettingsRev != storedSettingsRev;

        if (reconstructPipeline) {
            // Ensure resources are released
            destroyPipeline();

            // Do this before returning for screenshot
            if (sourceUuid != "program") {
                if (!createSource(sourceUuid)) {
                    setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                    return;
                }
            }

            activeSourceUuid = sourceUuid;
            activeSettingsRev = storedSettingsRev;
        }

        //--- Gather parameters ---//
        auto streaming = !connection.isEmpty();
        auto recording = obs_data_get_bool(settings, "recording");

        if (!streaming && reconstructPipeline) {
            setStatus(EGRESS_LINK_OUTPUT_STATUS_STAND_BY);
            apiClient->incrementStandByOutputs();
        }

        if (!streaming && !recording) {
            // Both of output and recording are not enabled
            return;
        }

        OBSDataAutoRelease egressSettings = nullptr;
        int encoderWidth;
        int encoderHeight;

        obs_video_info vi = {0};
        if (!obs_get_video_info(&vi)) {
            // Abort when no video situation
            obs_log(LOG_ERROR, "%s: Failed to get video info", qUtf8Printable(name));
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            return;
        }

        if (streaming) {
            // Uplink connection is available
            // Capture connection's specified resolution
            encoderWidth = connection.getWidth();
            encoderHeight = connection.getHeight();

            egressSettings = createEgressSettings(connection);
        } else {
            // No uplink connections
            // Use output resolution
            encoderWidth = vi.output_width;
            encoderHeight = vi.output_height;

            egressSettings = obs_data_create();
            obs_data_apply(egressSettings, settings);
        }

        //--- Create encoders ---//
        if (!videoEncoder) {
            // Determine video source
            auto video = createVideo(&vi);
            if (!video) {
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }

            // Create video encoder
            if (!createVideoEncoder(egressSettings, video, encoderWidth, encoderHeight)) {
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }
        }

        if (!audioEncoder) {
            // Determine audio source
            QString audioSourceUuid = obs_data_get_string(settings, "audio_source");
            if (audioSourceUuid.isEmpty()) {
                // Use source embeded audio
                audioSourceUuid = activeSourceUuid;
            }

            auto audio = createAudio(audioSourceUuid);
            if (!audio) {
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }

            // Create audio encoder
            if (!createAudioEncoder(egressSettings, audioSourceUuid, audio)) {
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
                return;
            }
        }

        //--- Create outputs ---//
        if (!streamingOutput && streaming) {
            // Uplink connection is available
            // No abort happen even if failed to create output
            if (!createStreamingOutput(egressSettings)) {
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            }
        }

        if (!recordingOutput && recording) {
            // Recording is enabled
            if (!createRecordingOutput(egressSettings)) {
                setRecordingStatus(RECORDING_OUTPUT_STATUS_ERROR);
            }
        }

        if (!streamingOutput && !recordingOutput) {
            // Both of output and recording are not ready
            return;
        }

        //--- Start recording output ---//
        if (reconstructPipeline && recordingOutput) {
            // Starts recording output later
            setRecordingStatus(RECORDING_OUTPUT_STATUS_ACTIVATING);
        }

        //--- Start streaming output ---//
        if (reconstructPipeline && streamingOutput) {
            // Save current timestamp to reduce reconnection with timeout
            connectionAttemptingAt = QDateTime().currentMSecsSinceEpoch();
            // Starts streaming output later
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ACTIVATING);
        }
    }();
    apiClient->syncUplinkStatus();
    locker.unlock();
}

void EgressLinkOutput::startStreaming()
{
    QMutexLocker locker(&outputMutex);
    [&]() {
        if (streamingOutput) {
            // Save current timestamp to reduce reconnection with timeout
            connectionAttemptingAt = QDateTime().currentMSecsSinceEpoch();

            obs_output_set_video_encoder(streamingOutput, videoEncoder);
            obs_output_set_audio_encoder(streamingOutput, audioEncoder, 0); // Don't support multiple audio outputs

            if (!obs_output_start(streamingOutput)) {
                obs_log(LOG_ERROR, "%s: Failed to start streaming output", qUtf8Printable(name));
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ERROR);
            } else {
                if (source) {
                    obs_source_inc_showing(source);
                }
                obs_log(LOG_INFO, "%s: Activated streaming output", qUtf8Printable(name));
                setStatus(EGRESS_LINK_OUTPUT_STATUS_ACTIVE);
                apiClient->incrementActiveOutputs();
            }
        }
    }();
    apiClient->syncUplinkStatus();
    locker.unlock();
}

void EgressLinkOutput::startRecording()
{
    QMutexLocker locker(&outputMutex);
    [&]() {
        if (recordingOutput) {
            obs_output_set_video_encoder(recordingOutput, videoEncoder);
            obs_output_set_audio_encoder(recordingOutput, audioEncoder, 0); // Don't support multiple audio outputs

            if (!obs_output_start(recordingOutput)) {
                obs_log(LOG_ERROR, "%s: Failed to start recording output", qUtf8Printable(name));
                setRecordingStatus(RECORDING_OUTPUT_STATUS_ERROR);
            } else {
                if (source) {
                    obs_source_inc_showing(source);
                }
                obs_log(LOG_INFO, "%s: Activated recording output", qUtf8Printable(name));
                setRecordingStatus(RECORDING_OUTPUT_STATUS_ACTIVE);
            }
        }
    }();
    locker.unlock();
}

// Modifies state of members:
//   source, activeSourceUuid, streamingOutput, recordingOutput, service, videoEncoder, audioEncoder, sourceView,
//   audioSource, audioSilence
void EgressLinkOutput::destroyPipeline()
{
    if (recordingOutput) {
        if (recordingStatus == RECORDING_OUTPUT_STATUS_ACTIVE) {
            if (source) {
                obs_source_dec_showing(source);
            }
            obs_output_stop(recordingOutput);
        }
    }
    recordingOutput = nullptr;

    if (streamingOutput) {
        if (status == EGRESS_LINK_OUTPUT_STATUS_ACTIVE || status == EGRESS_LINK_OUTPUT_STATUS_RECONNECTING) {
            if (source) {
                obs_source_dec_showing(source);
            }
            obs_output_stop(streamingOutput);
        }
    }
    streamingOutput = nullptr;

    service = nullptr;
    audioEncoder = nullptr;
    videoEncoder = nullptr;

    if (sourceView) {
        obs_view_set_source(sourceView, 0, nullptr);
        obs_view_remove(sourceView);
    }
    sourceView = nullptr;

    if (source) {
        obs_source_dec_showing(source);
    }
    source = nullptr;
    activeSourceUuid = QString();

    if (audioSource) {
        delete audioSource;
        audioSource = nullptr;
    }
    audioSilence = nullptr;

    if (status == EGRESS_LINK_OUTPUT_STATUS_STAND_BY) {
        apiClient->decrementStandByOutputs();
    } else if (status == EGRESS_LINK_OUTPUT_STATUS_ACTIVE || status == EGRESS_LINK_OUTPUT_STATUS_RECONNECTING) {
        apiClient->decrementActiveOutputs();
    }

    setStatus(EGRESS_LINK_OUTPUT_STATUS_INACTIVE);
    setRecordingStatus(RECORDING_OUTPUT_STATUS_INACTIVE);
}

void EgressLinkOutput::stop()
{
    QMutexLocker locker(&outputMutex);
    {
        auto stopStatus = status != EGRESS_LINK_OUTPUT_STATUS_INACTIVE;

        destroyPipeline();

        if (stopStatus) {
            obs_log(LOG_INFO, "%s: Inactivated output", qUtf8Printable(name));
        }
    }
    apiClient->syncUplinkStatus();
    locker.unlock();
}

void EgressLinkOutput::restartStreaming()
{
    QMutexLocker locker(&outputMutex);
    {
        connectionAttemptingAt = QDateTime().currentMSecsSinceEpoch();

        obs_output_force_stop(streamingOutput);

        if (!obs_output_start(streamingOutput)) {
            obs_log(LOG_ERROR, "%s: Failed to restart streaming output", qUtf8Printable(name));
        } else {
            setStatus(EGRESS_LINK_OUTPUT_STATUS_RECONNECTING);
        }
    }
    locker.unlock();
}

void EgressLinkOutput::restartRecording()
{
    QMutexLocker locker(&outputMutex);
    {
        obs_output_force_stop(recordingOutput);

        if (!obs_output_start(recordingOutput)) {
            obs_log(LOG_ERROR, "%s: Failed to restart recording output", qUtf8Printable(name));
        }
    }
    locker.unlock();
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
    auto interlockType = apiClient->getSettings()->value("interlock_type", DEFAULT_INTERLOCK_TYPE);

    auto activateStreaming = status == EGRESS_LINK_OUTPUT_STATUS_ACTIVATING;
    auto activateRecording = recordingStatus == RECORDING_OUTPUT_STATUS_ACTIVATING;
    auto streamingInactiveStatus = status != EGRESS_LINK_OUTPUT_STATUS_ACTIVE &&
                                   status != EGRESS_LINK_OUTPUT_STATUS_STAND_BY &&
                                   status != EGRESS_LINK_OUTPUT_STATUS_RECONNECTING;

    if (activateStreaming || activateRecording) {
        // Prioritize activating output

        if (QDateTime().currentMSecsSinceEpoch() - connectionAttemptingAt > OUTPUT_START_DELAY_MSECS) {
            // Delaying at least OUTPUT_START_DELAY_MSECS after attempting activate
            if (activateStreaming) {
                startStreaming();
            }
            if (activateRecording) {
                startRecording();
            }
        }

    } else if (streamingInactiveStatus) {
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

    } else if (QDateTime().currentMSecsSinceEpoch() - connectionAttemptingAt > OUTPUT_RETRY_TIMEOUT_MSECS) {
        // Delaying at least OUTPUT_RETRY_TIMEOUT_MSECS after attempting start
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

        auto streamingAlive = streamingOutput && obs_output_active(streamingOutput);
        if (!streamingAlive && status == EGRESS_LINK_OUTPUT_STATUS_STAND_BY) {
            // Periodic check the uplink connection is available and activate
            start();
            return;
        }

        if (activeSettingsRev < storedSettingsRev && !obs_output_reconnecting(streamingOutput)) {
            obs_log(LOG_DEBUG, "%s: Attempting change settings", qUtf8Printable(name));
            start();
            return;
        }

        auto recordingAlive = recordingOutput && obs_output_active(recordingOutput);
        if (recordingStatus == RECORDING_OUTPUT_STATUS_ACTIVE && !recordingAlive) {
            obs_log(LOG_DEBUG, "%s: Attempting restart recording", qUtf8Printable(name));
            restartRecording();
            // Do not end turn here
        }

        if (!streamingAlive &&
            (status == EGRESS_LINK_OUTPUT_STATUS_ACTIVE || status == EGRESS_LINK_OUTPUT_STATUS_RECONNECTING)) {
            // Reconnect
            obs_log(LOG_DEBUG, "%s: Attempting restart output", qUtf8Printable(name));
            restartStreaming();
            return;
        }

        if (streamingAlive && status == EGRESS_LINK_OUTPUT_STATUS_RECONNECTING) {
            // Reconnected
            setStatus(EGRESS_LINK_OUTPUT_STATUS_ACTIVE);
        }

        if (source && (!isSourceAvailable(source) || !isSourceAvailable(source))) {
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

void EgressLinkOutput::setRecordingStatus(RecordingOutputStatus value)
{
    if (recordingStatus != value) {
        recordingStatus = value;
        emit recordingStatusChanged(recordingStatus);
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
    StageConnection incomingConnection =
        uplink.getConnections().find([this](const StageConnection &c) { return c.getSourceName() == name; });

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
