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

#include <QUrlQuery>

#include "../plugin-support.h"
#include "../utils.hpp"
#include "ingress-link-source.hpp"

#define POLLING_INTERVAL_MSECS 10000

//--- IngressLinkSource class ---//

IngressLinkSource::IngressLinkSource(
    obs_data_t *settings, obs_source_t *_source, SourceLinkApiClient *_apiClient, QObject *parent
)
    : QObject(parent),
      weakSource(obs_source_get_weak_source(_source)),
      apiClient(_apiClient),
      connected(false),
      uuid(obs_source_get_uuid(_source)),
      port(0),
      revision(0),
      audioThread(nullptr),
      intervalTimer(nullptr)
{
    name = obs_source_get_name(_source);
    obs_log(LOG_DEBUG, "%s: Source creating", qPrintable(name));

    // Allocate port
    port = apiClient->getFreePort();

    // Capture source's settings first
    captureSettings(settings);

    // Register connection to server
    handleConnection();

    // Create decoder private source (SRT, RIST, etc.)
    OBSDataAutoRelease decoderSettings = createDecoderSettings();
    QString decoderName = QString("%1 (decoder)").arg(obs_source_get_name(_source));
    decoderSource = obs_source_create_private("ffmpeg_source", qPrintable(decoderName), decoderSettings);
    obs_source_inc_active(decoderSource);

    obs_audio_info ai = {0};
    obs_get_audio_info(&ai);
    speakers = ai.speakers;
    samplesPerSec = ai.samples_per_sec;

    // Audio handling
    audioThread = new SourceAudioThread(this);
    audioThread->start();

    // API server polling
    intervalTimer = new QTimer(this);
    intervalTimer->setInterval(POLLING_INTERVAL_MSECS);
    intervalTimer->start();

    connect(intervalTimer, SIGNAL(timeout()), this, SLOT(onIntervalTimerTimeout()));
    connect(
        apiClient, SIGNAL(connectionPutSucceeded(const StageConnection &)), this,
        SLOT(onConnectionPutSucceeded(const StageConnection &))
    );
    connect(apiClient, SIGNAL(connectionPutFailed()), this, SLOT(onConnectionPutFailed()));
    connect(
        apiClient, SIGNAL(connectionDeleteSucceeded(const QString &)), this,
        SLOT(onConnectionDeleteSucceeded(const QString &))
    );
    connect(apiClient, &SourceLinkApiClient::ingressRestartNeeded, [this]() { reconnect(); });

    renameSignal.Connect(
        obs_source_get_signal_handler(_source), "rename",
        [](void *data, calldata_t *cd) {
            auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
            ingressLinkSource->name = calldata_string(cd, "new_name");
        },
        this
    );

    obs_log(LOG_INFO, "%s: Source created", qPrintable(name));
}

IngressLinkSource::~IngressLinkSource()
{
    obs_log(LOG_DEBUG, "%s: Source destroying", qPrintable(name));

    renameSignal.Disconnect();

    disconnect(this);

    apiClient->deleteConnection(uuid, true);
    apiClient->releasePort(port);

    // Destroy decoder private source
    audioThread->requestInterruption();
    audioThread->wait();
    delete audioThread;

    obs_source_dec_active(decoderSource);

    obs_log(LOG_INFO, "%s: Source destroyed", qPrintable(name));
}

QString IngressLinkSource::compositeParameters(obs_data_t *settings, bool remote)
{
    auto apiSettings = apiClient->getSettings();
    QString parameters;

    if (protocol == "srt") {
        // Generate SRT parameters
        auto mode = apiSettings->getIngressSrtMode();
        if (remote) {
            // Invert mode for remote
            if (mode == "listener") {
                mode = "caller";
            } else if (mode == "caller") {
                mode = "listener";
            }
        }

        auto latency = apiSettings->getIngressSrtLatency();
        if (obs_data_get_bool(settings, "advanced_settings")) {
            latency = obs_data_get_int(settings, "srt_latency");
        }

        parameters = QString("mode=%1&latency=%2&pbkeylen=%3&passphrase=%4&streamid=%5")
                         .arg(mode)
                         .arg(latency * 1000) // Convert to microseconds
                         .arg(apiSettings->getIngressSrtPbkeylen())
                         .arg(passphrase)
                         .arg(streamId);
    }

    return parameters;
}

void IngressLinkSource::captureSettings(obs_data_t *settings)
{
    protocol = apiClient->getSettings()->getIngressProtocol();

    stageId = obs_data_get_string(settings, "stage_id");
    seatName = obs_data_get_string(settings, "seat_name");
    sourceName = obs_data_get_string(settings, "source_name");
    maxBitrate = obs_data_get_int(settings, "max_bitrate");
    minBitrate = obs_data_get_int(settings, "min_bitrate");
    width = obs_data_get_int(settings, "width");
    height = obs_data_get_int(settings, "height");
    hwDecode = obs_data_get_bool(settings, "hw_decode");
    clearOnMediaEnd = obs_data_get_bool(settings, "clear_on_media_end");

    if (obs_data_get_bool(settings, "advanced_settings")) {
        reconnectDelaySec = obs_data_get_int(settings, "reconnect_delay_sec");
        bufferingMb = obs_data_get_int(settings, "buffering_mb");
    } else {
        reconnectDelaySec = apiClient->getSettings()->getIngressReconnectDelayTime();
        bufferingMb = apiClient->getSettings()->getIngressNetworkBufferSize();
    }

    // Generate new passphrase and stream ID here
    passphrase = generatePassword(16, "");
    streamId = generatePassword(32, "");

    localParameters = compositeParameters(settings);
    remoteParameters = compositeParameters(settings, true);

    // Increment connection revision
    revision++;
}

obs_data_t *IngressLinkSource::createDecoderSettings()
{
    auto decoderSettings = obs_data_create();

    if (protocol == "srt" && port > 0) {
        auto input = QString("srt://0.0.0.0:%1?%2").arg(port).arg(localParameters);
        obs_data_set_string(decoderSettings, "input", qPrintable(input));
    } else {
        obs_data_set_string(decoderSettings, "input", "");
    }

    obs_data_set_int(decoderSettings, "reconnect_delay_sec", reconnectDelaySec);
    obs_data_set_int(decoderSettings, "buffering_mb", bufferingMb);
    obs_data_set_bool(decoderSettings, "hw_decode", hwDecode);
    obs_data_set_bool(decoderSettings, "clear_on_media_end", clearOnMediaEnd);

    // Static parameters
    obs_data_set_string(decoderSettings, "input_format", "mpegts");
    obs_data_set_bool(decoderSettings, "is_local_file", false);

    return decoderSettings;
}

void IngressLinkSource::handleConnection()
{
    if (!stageId.isEmpty() && !seatName.isEmpty() && !sourceName.isEmpty()) {
        // Register connection to server
        // Note: apiClient instance might live in a different thread
        QMetaObject::invokeMethod(
            apiClient, "putConnection", Q_ARG(QString, uuid), Q_ARG(QString, stageId), Q_ARG(QString, seatName),
            Q_ARG(QString, sourceName), Q_ARG(QString, protocol), Q_ARG(int, port), Q_ARG(QString, remoteParameters),
            Q_ARG(int, maxBitrate), Q_ARG(int, minBitrate), Q_ARG(int, width), Q_ARG(int, height), Q_ARG(int, revision)
        );
    } else {
        // Unregister connection if no stage/seat/source selected.
        // Note: apiClient instance might live in a different thread
        QMetaObject::invokeMethod(apiClient, "deleteConnection", Q_ARG(QString, uuid));
    }
}

obs_properties_t *IngressLinkSource::getProperties()
{
    obs_log(LOG_DEBUG, "%s: Properties creating", qPrintable(name));
    auto props = obs_properties_create();
    obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

    // Connection group
    auto connectionGroup = obs_properties_create();
    obs_properties_add_group(props, "connection", obs_module_text("Connection"), OBS_GROUP_NORMAL, connectionGroup);

    // Connection group -> Stage list
    auto stageList = obs_properties_add_list(
        connectionGroup, "stage_id", obs_module_text("Stage"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING
    );
    foreach (auto &stage, apiClient->getStages()) {
        obs_property_list_add_string(stageList, qPrintable(stage.getName()), qPrintable(stage.getId()));
    }

    // Connection group -> Create button
    obs_properties_add_button2(
        connectionGroup, "create_stage", obs_module_text("CreateNewStage"),
        [](obs_properties_t *props, obs_property_t *property, void *param) { 
            auto ingressLinkSource = static_cast<IngressLinkSource *>(param);
            ingressLinkSource->apiClient->openStageCreationForm();
            return true;
        },
        this
    );

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
            QString stageId = obs_data_get_string(settings, "stage_id");

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

            foreach (auto &stage, apiClient->getStages()) {
                if (stageId != stage.getId()) {
                    continue;
                }

                if (!stage.getSeats().size()) {
                    obs_property_list_add_string(seatList, obs_module_text("ChooseStageFirst"), "");
                }
                if (!stage.getSources().size()) {
                    obs_property_list_add_string(sourceList, obs_module_text("ChooseStageFirst"), "");
                }

                foreach (auto &seat, stage.getSeats()) {
                    obs_property_list_add_string(
                        seatList, qPrintable(seat.getDisplayName()), qPrintable(seat.getName())
                    );
                }
                foreach (auto &source, stage.getSources()) {
                    obs_property_list_add_string(
                        sourceList, qPrintable(source.getDisplayName()), qPrintable(source.getName())
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
    obs_properties_add_bool(props, "hw_decode", obs_module_text("HardwareDecode"));
    obs_properties_add_bool(props, "clear_on_media_end", obs_module_text("ClearOnMediaEnd"));

    auto advancedSettings = obs_properties_add_bool(props, "advanced_settings", obs_module_text("AdvancedSettings"));
    obs_property_set_modified_callback2(
        advancedSettings,
        [](void *param, obs_properties_t *props, obs_property_t *, obs_data_t *settings) {
            IngressLinkSource *ingressLinkSource = static_cast<IngressLinkSource *>(param);
            auto apiSettings = ingressLinkSource->apiClient->getSettings();
            auto advanced = obs_data_get_bool(settings, "advanced_settings");

            obs_property_set_visible(obs_properties_get(props, "reconnect_delay_sec"), advanced);
            obs_property_set_visible(obs_properties_get(props, "buffering_mb"), advanced);
            obs_property_set_visible(
                obs_properties_get(props, "srt_latency"), advanced && apiSettings->getIngressProtocol() == "srt"
            );

            return true;
        },
        this
    );

    // Advanced settings
    auto reconnectDelay =
        obs_properties_add_int_slider(props, "reconnect_delay_sec", obs_module_text("ReconnectDelayTime"), 1, 60, 1);
    obs_property_int_set_suffix(reconnectDelay, " secs");

    auto buffering = obs_properties_add_int_slider(props, "buffering_mb", obs_module_text("BufferingMB"), 0, 16, 1);
    obs_property_int_set_suffix(buffering, " MB");

    auto srtLatency = obs_properties_add_int(props, "srt_latency", obs_module_text("LatencyMsecs"), 0, 60000, 1);
    obs_property_int_set_suffix(srtLatency, " ms");
    obs_property_set_visible(srtLatency, apiClient->getSettings()->getIngressProtocol() == "srt");

    obs_log(LOG_DEBUG, "%s: Properties created", qPrintable(name));
    return props;
}

void IngressLinkSource::getDefaults(obs_data_t *settings, SourceLinkApiClient *apiClient)
{
    obs_log(LOG_DEBUG, "Default settings applying.");

    obs_data_set_default_bool(settings, "hw_decode", false);
    obs_data_set_default_bool(settings, "clear_on_media_end", true);
    obs_data_set_default_int(settings, "max_bitrate", 8000);
    obs_data_set_default_int(settings, "min_bitrate", 4000);
    obs_data_set_default_bool(settings, "advanced_settings", false);
    obs_data_set_default_int(settings, "srt_latency", apiClient->getSettings()->getIngressSrtLatency());
    obs_data_set_default_int(settings, "reconnect_delay_sec", apiClient->getSettings()->getIngressReconnectDelayTime());
    obs_data_set_default_int(settings, "buffering_mb", apiClient->getSettings()->getIngressNetworkBufferSize());

    obs_video_info ovi = {0};
    if (obs_get_video_info(&ovi)) {
        obs_data_set_default_int(settings, "width", ovi.base_width);
        obs_data_set_default_int(settings, "height", ovi.base_height);
    }

    obs_log(LOG_DEBUG, "Default settings applied.");
}

void IngressLinkSource::videoRenderCallback()
{
    // Just pass through the video
    obs_source_video_render(decoderSource);
}

void IngressLinkSource::update(obs_data_t *settings)
{
    obs_log(LOG_DEBUG, "%s: Source updating", qPrintable(name));

    captureSettings(settings);
    handleConnection();

    OBSDataAutoRelease decoderSettings = createDecoderSettings();
    obs_source_update(decoderSource, decoderSettings);

    obs_log(LOG_INFO, "%s: Source updated", qPrintable(name));
}

void IngressLinkSource::onConnectionPutSucceeded(const StageConnection &connection)
{
    connected = true;
}

void IngressLinkSource::onConnectionPutFailed()
{
    connected = false;
}

void IngressLinkSource::onConnectionDeleteSucceeded(const QString &)
{
    connected = false;
}

void IngressLinkSource::onStageConnectionReady(const StageConnection &connection)
{
    if (connection.getId() != uuid) {
        return;
    }

    if (revision != connection.getRevision()) {
        revision = connection.getRevision();

        // Reconnect occurres
        reconnect();
    }
}

void IngressLinkSource::onIntervalTimerTimeout()
{
    if (!stageId.isEmpty() && !seatName.isEmpty() && !sourceName.isEmpty()) {
        apiClient->requestStageConnection(uuid);
    }
}

void IngressLinkSource::reconnect()
{
    OBSSourceAutoRelease source = obs_weak_source_get_source(weakSource);
    OBSDataAutoRelease settings = obs_source_get_settings(source);
    update(settings);
}

//--- SourceLinkAudioThread class ---//

SourceAudioThread::SourceAudioThread(IngressLinkSource *_linkedSource)
    : ingressLinkSource(_linkedSource),
      QThread(_linkedSource),
      SourceAudioCapture(
          _linkedSource->decoderSource, _linkedSource->samplesPerSec, _linkedSource->speakers, _linkedSource
      )
{
    obs_log(LOG_DEBUG, "%s: Audio thread creating.", qPrintable(ingressLinkSource->name));
}

SourceAudioThread::~SourceAudioThread()
{
    if (isRunning()) {
        requestInterruption();
        wait();
    }

    obs_log(LOG_DEBUG, "%s: Audio thread destroyed.", qPrintable(ingressLinkSource->name));
}

void SourceAudioThread::run()
{
    obs_log(LOG_DEBUG, "%s: Audio thread started.", qPrintable(ingressLinkSource->name));
    active = true;

    while (!isInterruptionRequested()) {
        QMutexLocker locker(&audioBufferMutex);
        {
            OBSSourceAutoRelease source = obs_weak_source_get_source(ingressLinkSource->weakSource);
            if (!source) {
                break;
            }
            if (!audioBufferFrames) {
                // No data at this time
                locker.unlock();
                msleep(10);
                continue;
            }

            // Peek header of first chunk
            deque_peek_front(&audioBuffer, audioConvBuffer, sizeof(AudioBufferHeader));
            auto header = (AudioBufferHeader *)audioConvBuffer;
            size_t dataSize = sizeof(AudioBufferHeader) + header->speakers * header->frames * 4;

            // Read chunk data
            deque_pop_front(&audioBuffer, audioConvBuffer, dataSize);

            // Create audio data to send source output
            obs_source_audio audioData = {0};
            audioData.frames = header->frames;
            audioData.timestamp = header->timestamp;
            audioData.speakers = header->speakers;
            audioData.format = header->format;
            audioData.samples_per_sec = header->samples_per_sec;

            for (int i = 0; i < header->speakers; i++) {
                if (!header->data_idx[i]) {
                    continue;
                }
                audioData.data[i] = audioConvBuffer + header->data_idx[i];
            }

            // Send data to source output
            obs_source_output_audio(source, &audioData);

            audioBufferFrames -= header->frames;
        }
        locker.unlock();
    }

    active = false;
    obs_log(LOG_DEBUG, "%s: Audio thread stopped.", qPrintable(ingressLinkSource->name));
}

//--- Source registration ---//

extern SourceLinkApiClient *apiClient;

void *createSource(obs_data_t *settings, obs_source_t *source)
{
    return new IngressLinkSource(settings, source, apiClient);
}

void destroySource(void *data)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    // Delete with thread-safe way
    ingressLinkSource->deleteLater();
}

obs_properties_t *getProperties(void *data)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    return ingressLinkSource->getProperties();
}

void getDefaults(obs_data_t *settings)
{
    IngressLinkSource::getDefaults(settings, apiClient);
}

uint32_t getWidth(void *data)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    return ingressLinkSource->getWidth();
}

uint32_t getHeight(void *data)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    return ingressLinkSource->getHeight();
}

void videoRender(void *data, gs_effect_t *)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    ingressLinkSource->videoRenderCallback();
}

void update(void *data, obs_data_t *settings)
{
    auto ingressLinkSource = static_cast<IngressLinkSource *>(data);
    ingressLinkSource->update(settings);
}

obs_source_info createLinkedSourceInfo()
{
    obs_source_info sourceInfo = {0};

    sourceInfo.id = "ingress_link_source";
    sourceInfo.type = OBS_SOURCE_TYPE_INPUT;
    sourceInfo.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;

    sourceInfo.get_name = [](void *) {
        return "Ingress Link";
    };
    sourceInfo.create = createSource;
    sourceInfo.destroy = destroySource;
    sourceInfo.get_properties = getProperties;
    sourceInfo.get_defaults = getDefaults;
    sourceInfo.get_width = getWidth;
    sourceInfo.get_height = getHeight;
    sourceInfo.video_render = videoRender;
    sourceInfo.update = update;

    return sourceInfo;
}